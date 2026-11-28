#include "test_settings_sandbox.h"
#include <QPainter>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QToolTip>
#include "shared/theme/AppTheme.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "geo/GeoTypes.h"
#include "map3d/Map3DWindow.h"
#include "map3d/Map3DLoadTracker.h"
#include "map3d/MapResourceDialog.h"
#include "map3d/Map3DRuntime.h"
#include "map3d/EarthProjectionProfile.h"
#include "map3d/GroundDetailEllipse.h"
#include "map3d/TiandituImageLayer.h"
#include "map3d/CesiumTerrain.h"
#include "map3d/OpenAerialMapImageLayer.h"
#include "map3d/EoxSentinel2ImageLayer.h"
#include "map3d/OnlineOsmLayer.h"
#include "map3d/MapLayerRemoval.h"
#include <osgEarth/Map>
#include <osgEarth/TerrainTileModelFactory>
#include <osgDB/ReadFile>
#include <QImage>
#include <osg/Matrixd>
#include "shared/theme/SingleLevelPopupComboBox.h"
#include "shared/theme/SingleLevelPopupMenu.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QPointer>
#include <QTableWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QProgressBar>
#include <QProgressDialog>
#include <QSettings>
#include <QSemaphore>
#include <QRunnable>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QWidgetAction>
#include <QWindow>
#include <QtCore/qtestsupport_core.h>

#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <future>
#include <vector>

namespace
{

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class DeleteSiblingOnTranslation final : public QObject
{
public:
    QPointer<QObject> victim;
    bool fired = false;

    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::ToolTipChange && victim) {
            fired = true;
            delete victim.data();
        }
        return false;
    }
};

class TranslationVictim final : public QObject
{
public:
    using QObject::QObject;
    // Keep storage until the test ends, but make stale virtual calls fail
    // deterministically instead of depending on allocator reuse.
    static void operator delete(void* storage) noexcept
    {
        std::memset(storage, 0, sizeof(void*));
    }
};

void testLanguageObjectLifetime()
{
    const QVariant originalEnglish = qApp->property("vaporViewEnglish");
    qApp->setProperty("vaporViewEnglish", false);
    {
        VaporView::Map3D::Map3DWindow window;
        auto* trigger = new QWidget(&window);
        trigger->setToolTip(QStringLiteral("关闭"));
        DeleteSiblingOnTranslation deletion;
        auto* victim = new TranslationVictim(&window);
        std::unique_ptr<void, void(*)(void*)> storage(victim, [](void* p) { ::operator delete(p); });
        deletion.victim = victim;
        trigger->installEventFilter(&deletion);
        qApp->setProperty("vaporViewEnglish", true);
        require(deletion.fired && deletion.victim.isNull(),
                "translation can synchronously destroy a later child in the snapshot");
        require(trigger->toolTip() == QStringLiteral("Close"),
                "translation completes after a child is destroyed");
        qApp->setProperty("vaporViewEnglish", false);
        require(trigger->toolTip() == QStringLiteral("关闭"),
                "translation remains usable after child destruction");
    }
    qApp->setProperty("vaporViewEnglish", originalEnglish);
}

void testRemovedImageryLifetime()
{
    osg::ref_ptr<osgEarth::Map> map = new osgEarth::Map;
    osg::ref_ptr<osgEarth::XYZImageLayer> layer = new osgEarth::XYZImageLayer;
    layer->setName("lifetime probe");
    layer->setURL(osgEarth::URI("https://example.invalid/{z}/{x}/{y}.png"));
    layer->setProfile(osgEarth::Profile::create(osgEarth::Profile::SPHERICAL_MERCATOR));
    map->addLayer(layer.get());
    require(layer->isOpen(), "in-memory imagery profile opens without a network request");
    layer->setDataExtents({osgEarth::DataExtent(layer->getProfile()->getExtent(), 0u, 5u)});
    osg::observer_ptr<osgEarth::XYZImageLayer> observer(layer.get());
    layer = nullptr;

    std::promise<void> acquired, resume;
    auto resumeFuture = resume.get_future();
    auto worker = std::async(std::launch::async, [&]() {
        // TerrainTileModelFactory also retains this reference-counted snapshot.
        osgEarth::LayerVector snapshot;
        map->getLayers(snapshot);
        auto* tileLayer = dynamic_cast<osgEarth::TileLayer*>(snapshot.front().get());
        const auto& extent = tileLayer->getDataExtentsUnion();
        const auto* srs = extent.getSRS();
        acquired.set_value();
        resumeFuture.wait();
        return srs && extent.getSRS() == srs && tileLayer->isOpen()
            && extent.intersects(osgEarth::Profile::create(osgEarth::Profile::GLOBAL_GEODETIC)->getExtent());
    });
    require(acquired.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready,
            "terrain reader acquires the imagery extent within a bounded wait");
    VaporView::Map3D::Detail::removeLayerByName(map.get(), "lifetime probe");
    const bool removed = map->getNumLayers() == 0;
    resume.set_value();
    require(worker.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
            "detached terrain reader finishes within a bounded wait");
    const bool usable = worker.get();
    require(removed, "switched imagery is immediately absent from the map");
    require(usable, "removing imagery preserves the extent and driver until its terrain reader finishes");
    require(!observer.valid(), "detached imagery is destroyed after the last terrain reader finishes");
}

bool titleIconHasVisibleSize(QToolButton* button)
{
    const QImage image = button->parentWidget()->grab(button->geometry()).toImage();
    const qreal ratio = button->devicePixelRatioF();
    const int inset = qRound(4 * ratio);
    if (image.width() <= 2 * inset || image.height() <= 2 * inset) return false;
    const QColor background = image.pixelColor(inset, inset);
    int left = image.width(), right = -1, top = image.height(), bottom = -1;
    for (int y = inset; y < image.height() - inset; ++y)
        for (int x = inset; x < image.width() - inset; ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (qMax(qMax(qAbs(pixel.red() - background.red()), qAbs(pixel.green() - background.green())),
                     qAbs(pixel.blue() - background.blue())) < 60) continue;
            left = qMin(left, x); right = qMax(right, x);
            top = qMin(top, y); bottom = qMax(bottom, y);
        }
    return right - left + 1 >= qRound(10 * ratio) && bottom - top + 1 >= qRound(10 * ratio);
}

void requireComboPopupStyled(QComboBox *combo, const char *message)
{
    require(combo != nullptr, message);
    require(combo->property("vaporViewComboPopupStyled").toBool(), message);
    require(combo->view() != nullptr, message);
    require(combo->view()->property("vaporViewComboPopupStyled").toBool(), message);
    require(!combo->view()->property("vaporViewComboPopupRoundedMaskEnabled").isValid(), message);
    require(!combo->view()->property("vaporViewComboPopupViewportMargin").isValid(), message);
    require(!combo->view()->property("vaporViewComboPopupShadowEnabled").toBool(), message);
    require(!combo->view()->property("floatingPanelChrome").toBool(), message);
    require(combo->view()->objectName() == QStringLiteral("vaporViewComboPopupView"), message);
    require(combo->view()->findChild<QWidget *>(QStringLiteral("vaporViewComboPopupBorderOverlay")) == nullptr,
            message);
    require(combo->view()->hasMouseTracking() &&
                combo->view()->testAttribute(Qt::WA_Hover) &&
                combo->view()->viewport() &&
                combo->view()->viewport()->hasMouseTracking() &&
                combo->view()->viewport()->testAttribute(Qt::WA_Hover),
            message);
    const QString popupStyle = combo->view()->styleSheet();
    const QString hoverColor = VaporView::appThemeColorName(VaporView::AppThemeColor::MenuHover,
                                                            VaporView::isDarkThemeEnabled());
    require(popupStyle.contains(QStringLiteral("border: none")) &&
                !popupStyle.contains(QStringLiteral("border: 1px solid")) &&
                !popupStyle.contains(QStringLiteral("border-bottom: 1px solid")) &&
                popupStyle.contains(QStringLiteral("border-radius: 0px")) &&
                popupStyle.contains(QStringLiteral("padding: 12px 0px")) &&
                popupStyle.contains(QStringLiteral("padding: 7px 14px")) &&
                popupStyle.contains(QStringLiteral("min-height: 30px")) &&
                popupStyle.contains(QStringLiteral("::item:hover,")) &&
                popupStyle.contains(QStringLiteral("::item:selected:hover")) &&
                popupStyle.contains(QStringLiteral("::item:selected:active:hover")) &&
                popupStyle.contains(QStringLiteral("::item:selected:!active:hover { background-color: %1").arg(hoverColor)) &&
                !popupStyle.contains(QStringLiteral("padding: 12px 4px")),
            message);
}

void requireSingleLevelComboPopup(QComboBox *combo, const char *message)
{
    require(combo != nullptr, message);
    auto *singleLevelCombo = dynamic_cast<VaporView::SingleLevelPopupComboBox *>(combo);
    require(singleLevelCombo != nullptr, message);
    require(combo->property("usesSingleLevelPopupMenu").toBool(), message);
    VaporView::SingleLevelPopupMenu *popupMenu = singleLevelCombo->popupMenu();
    require(popupMenu != nullptr, message);

    singleLevelCombo->showPopup();
    QCoreApplication::processEvents();
    require(popupMenu->isVisible(), message);
    const QList<VaporView::SingleLevelPopupMenuRow *> rows = popupMenu->rows();
    require(rows.size() == combo->count(), message);

    int previousBottom = -1;
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto *row = rows.at(i);
        require(row != nullptr, message);
        require(row->text() == combo->itemText(i), message);
        require(row->height() >= 32, message);
        if (previousBottom >= 0)
        {
            require(row->y() >= previousBottom, message);
        }
        previousBottom = row->y() + row->height();
    }

    singleLevelCombo->hidePopup();
    singleLevelCombo->showPopup();
    QList<VaporView::SingleLevelPopupMenuRow *> directRows;
    for (QObject *child : popupMenu->children())
    {
        if (auto *row = qobject_cast<VaporView::SingleLevelPopupMenuRow *>(child))
        {
            directRows.push_back(row);
        }
    }
    require(directRows.size() == combo->count(), "rebuilt combo popup removes stale row widgets");
    QCoreApplication::processEvents();
    singleLevelCombo->hidePopup();
    QCoreApplication::processEvents();
    require(!popupMenu->isVisible(), message);
}

void writeSessionTrack(QTemporaryDir& sessionDir)
{
    require(sessionDir.isValid(), "temporary session directory is valid");
    QDir root(sessionDir.path());
    require(root.mkpath(QStringLiteral("sensors")), "create sensors directory");

    QFile devicesCsv(root.filePath(QStringLiteral("sensors/devices.csv")));
    require(devicesCsv.open(QIODevice::WriteOnly | QIODevice::Text), "open devices.csv");

    QTextStream out(&devicesCsv);
    out << "record_timestamp_us,device_timestamp_us,nav_lat_deg,nav_lon_deg,nav_height_m,gnss_satellites,hdop,gnss_fix\n";
    out << "1000000,900000,39.9000000,116.3000000,45.0,12,0.9,RTK_FIXED\n";
    out << "2000000,1900000,39.9000100,116.3000200,46.0,12,0.9,RTK_FIXED\n";
}

bool waitForText(QLabel* label, const QString& text, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents();
        if (label && label->text().contains(text))
        {
            return true;
        }
        QThread::msleep(10);
    }
    return label && label->text().contains(text);
}

QLabel* statusLabel(VaporView::Map3D::Map3DWindow& window)
{
    QLabel* label = window.findChild<QLabel*>(QStringLiteral("map3DStatusLabel"));
    require(label != nullptr, "map3d status label exists");
    return label;
}

QAction* actionByName(VaporView::Map3D::Map3DWindow& window, const QString& objectName)
{
    QAction* action = window.findChild<QAction*>(objectName);
    require(action != nullptr, "map3d action exists");
    return action;
}

void selectMapResourceWithMouse(QToolButton* moreButton, QMenu* resources, QAction* target)
{
    auto* more = moreButton->defaultAction()->menu();
    auto sendMouse = [](QEvent::Type type, QPoint global) {
        auto* popup = QApplication::activePopupWidget();
        require(popup && popup->windowHandle(), "map popup receives mouse input");
        const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        const auto buttons = type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton;
        QMouseEvent event(type, popup->mapFromGlobal(global), global, button, buttons, Qt::NoModifier);
        QApplication::sendEvent(popup->windowHandle(), &event);
    };
    QTimer::singleShot(0, more, [&] {
        const QPoint start = more->mapToGlobal(more->actionGeometry(resources->menuAction()).center());
        sendMouse(QEvent::MouseMove, start - QPoint(10, 0));
        sendMouse(QEvent::MouseMove, start);
        require(QTest::qWaitFor([&] { return resources->isVisible(); }, 2000),
                "hovering the map resources row opens the submenu");
        auto* adjacent = more->actions().at(1);
        sendMouse(QEvent::MouseMove, more->mapToGlobal(more->actionGeometry(adjacent).center()));
        require(more->activeAction() == adjacent, "an open submenu does not delay sibling hover feedback");
        sendMouse(QEvent::MouseMove, start);
        require(QTest::qWaitFor([&] { return resources->isVisible(); }, 2000),
                "moving back to the source row reopens its submenu");
        const QPoint end = resources->mapToGlobal(resources->actionGeometry(target).center());
        for (int step = 1; step <= 40; ++step)
        {
            sendMouse(QEvent::MouseMove, start + (end - start) * step / 40);
            QTest::qWait(12);
            require(resources->isVisible(), "map resources stay open while the pointer crosses into the submenu");
        }
        sendMouse(QEvent::MouseButtonPress, end);
        sendMouse(QEvent::MouseButtonRelease, end);
        require(!more->isVisible(), "selecting the resource closes the menu chain");
    });
    moreButton->showMenu();
}

QSettings map3DTestSettings()
{
    return QSettings(QSettings::defaultFormat(),
                     QSettings::UserScope,
                     QStringLiteral("VaporView"),
                     QStringLiteral("Map3D"));
}

void checkEoxSelectionPersistence()
{
    qputenv("VAPORVIEW_MAP3D_HEADLESS_TEST", "1");
    auto settings = map3DTestSettings();
    settings.remove(QStringLiteral("satelliteImagerySource"));
    VaporView::Map3D::Map3DWindow window;
    auto* action = window.findChild<QAction*>(QStringLiteral("map3DOnlineImagery_eoxSentinel2_2024"));
    require(action && action->isEnabled() && action->isCheckable(),
            "EOX imagery selection action is available in the real menu path");
    action->trigger();
    require(action->isChecked()
                && settings.value(QStringLiteral("satelliteImagerySource")).toString()
                       == QStringLiteral("online:eox_sentinel2_2024"),
            "EOX imagery selection persists through the map source path");
    settings.remove(QStringLiteral("satelliteImagerySource"));
}

bool waitForSessionDirectory(const QString& path, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    const QString expected = QFileInfo(path).absoluteFilePath();
    while (timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents();
        if (map3DTestSettings().value(QStringLiteral("lastSessionDir")).toString() == expected)
        {
            return true;
        }
        QThread::msleep(10);
    }
    return map3DTestSettings().value(QStringLiteral("lastSessionDir")).toString() == expected;
}

void checkOsmOverlays(bool useLiveService)
{
    using namespace osgEarth;
    using VaporView::Map3D::Detail::OnlineOsmLayer;
    VaporView::Map3D::initializeMap3DRuntime();
    QTemporaryDir source;
    QFile tile(source.filePath(QStringLiteral("14/13660/6746.mvt")));
    const QByteArray originalBase = qgetenv("VAPORVIEW_OSM_VECTOR_BASE_URL");
    if (!useLiveService)
    {
        require(QDir().mkpath(QFileInfo(tile).absolutePath()) && tile.open(QIODevice::WriteOnly),
                "create isolated OSM tile source");
        // Shortbread tile containing a horizontal street and a square lake in different quadrants.
        tile.write(QByteArray::fromHex("1a1d0a0773747265657473120d1802220909800480080a8034002880207802"
                                       "1a2b0a0e77617465725f706f6c79676f6e7312141803221009802080201a801000008010ff0f000f2880207802"));
        tile.close();
        qputenv("VAPORVIEW_OSM_VECTOR_BASE_URL", source.path().toUtf8());
    }
    osg::ref_ptr<OnlineOsmLayer> roads = new OnlineOsmLayer(true, nullptr);
    osg::ref_ptr<OnlineOsmLayer> water = new OnlineOsmLayer(false, nullptr);
    require(roads->open().isOK() && water->open().isOK(), "OSM overlays open without local GeoPackages");
    const TileKey westLake(14, 13660, 6746, roads->getProfile());
    const GeoImage roadImage = roads->createImage(westLake);
    require(roadImage.valid(), "road tile loads");
    if (!useLiveService) require(tile.remove(), "disconnect fixture after the first tile download");
    const GeoImage waterImage = water->createImage(westLake);
    require(waterImage.valid(), "water tile reuses the shared vector cache");
    int roadPixels = 0, waterPixels = 0, clearPixels = 0;
    for (int y = 0; y < roadImage.getImage()->t(); ++y)
        for (int x = 0; x < roadImage.getImage()->s(); ++x)
        {
            const auto r = roadImage.getImage()->getColor(x, y);
            const auto w = waterImage.getImage()->getColor(x, y);
            if (r.a() > 0.1f && r.r() > r.b()) ++roadPixels;
            if (w.a() > 0.1f && w.b() > w.r()) ++waterPixels;
            if (r.a() == 0 && w.a() == 0) ++clearPixels;
        }
    require(roadPixels > 0 && waterPixels > 0 && clearPixels > 0,
            "separate road and water pixels leave the underlying imagery visible");
    if (!useLiveService)
    {
        const auto enlarged = water->createImage(westLake.createChildKey(3));
        require(enlarged.valid(), "overzoom uses the cached level 14 vector geometry");
        require(!roads->createImage(TileKey(14, 0, 0, roads->getProfile())).valid(),
                "a missing uncached tile leaves the underlying imagery visible");
        if (originalBase.isNull()) qunsetenv("VAPORVIEW_OSM_VECTOR_BASE_URL");
        else qputenv("VAPORVIEW_OSM_VECTOR_BASE_URL", originalBase);
    }
    std::cout << "OSM overlay pixels: roads " << roadPixels << ", water " << waterPixels
              << ", transparent background " << clearPixels << '\n';
}

void checkImageryFallback()
{
    using namespace osgEarth;
    using VaporView::Map3D::Detail::TiandituImageLayer;
    using VaporView::Map3D::Detail::isTiandituNoImageryTile;
    VaporView::Map3D::initializeMap3DRuntime();
    QTemporaryDir tiles;
    require(tiles.isValid(), "temporary imagery tiles directory");
    QImage warning(256, 256, QImage::Format_RGB32);
    warning.fill(QColor(228, 227, 223));
    {
        QPainter painter(&warning);
        // Deterministic warning ink: independent of Windows fonts and ClearType.
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(182, 181, 179));
        painter.drawPolygon(QPolygon{QPoint(32, 140), QPoint(44, 116), QPoint(56, 140)});
        for (int x = 68; x < 220; x += 8)
        {
            painter.drawRect(x, 116, 4, 8);
            painter.drawRect(x, 136, 4, 8);
        }
    }
    require(QDir(tiles.path()).mkpath(QStringLiteral("3/4")), "create child tile directory");
    const QString missingPath = tiles.filePath(QStringLiteral("3/4/2.png"));
    require(warning.save(missingPath), "write synthetic provider warning tile");
    auto decoded = osgDB::readRefImageFile(missingPath.toStdString());
    require(isTiandituNoImageryTile(decoded), "provider warning tile detected");
    const QString jpegWarningPath = tiles.filePath(QStringLiteral("warning.jpg"));
    require(warning.save(jpegWarningPath, "JPG", 85), "write JPEG-compressed warning tile");
    require(isTiandituNoImageryTile(osgDB::readRefImageFile(jpegWarningPath.toStdString())),
            "warning detection tolerates JPEG artifacts");
    for (const QColor& color : {QColor(228,227,223), QColor(250,250,250), QColor(8,30,75), QColor(180,150,95)})
    {
        QImage valid(256, 256, QImage::Format_RGB32);
        valid.fill(color);
        const QString path = tiles.filePath(QStringLiteral("valid.png"));
        require(valid.save(path), "write uniform valid imagery");
        require(!isTiandituNoImageryTile(osgDB::readRefImageFile(path.toStdString())),
                "uniform snow, sea and desert imagery are not discarded");
    }
    QImage parent(256, 256, QImage::Format_RGB32);
    // Georeferenced color ramp plus crossing roads detects crop/flip/seam errors.
    for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x)
        parent.setPixelColor(x, y, QColor(x, y, 50));
    {
        QPainter painter(&parent);
        painter.setPen(QPen(Qt::white, 6));
        painter.drawLine(64, 0, 64, 255);
        painter.drawLine(0, 64, 255, 64);
    }
    require(QDir(tiles.path()).mkpath(QStringLiteral("2/2")), "create parent tile directory");
    require(parent.save(tiles.filePath(QStringLiteral("2/2/1.png"))), "write available parent tile");
    QImage detail(256, 256, QImage::Format_RGB32);
    detail.fill(Qt::blue);
    require(QDir(tiles.path()).mkpath(QStringLiteral("3/5")), "create detailed sibling directory");
    require(detail.save(tiles.filePath(QStringLiteral("3/5/2.png"))), "write available detailed sibling");
    require(QDir(tiles.path()).mkpath(QStringLiteral("3/6")), "create unrelated region directory");
    require(detail.save(tiles.filePath(QStringLiteral("3/6/2.png"))), "write unrelated detailed tile");
    osg::ref_ptr<TiandituImageLayer> layer = new TiandituImageLayer;
    layer->setProfile(Profile::create(Profile::SPHERICAL_MERCATOR));
    layer->setURL(URI((tiles.path() + QStringLiteral("/{z}/{x}/{y}.png")).toStdString()));
    layer->setCachePolicy(CachePolicy::NO_CACHE);
    layer->options().minLevel() = 1u;
    layer->options().maxDataLevel() = 18u;
    osg::ref_ptr<Map> map = new Map;
    map->setProfile(Profile::create(Profile::SPHERICAL_MERCATOR));
    map->addLayer(layer);
    require(layer->isOpen(), "test imagery layer opens");
    const TileKey child(3, 4, 2, map->getProfile());
    const TileKey sibling(3, 5, 2, map->getProfile());
    require(layer->createImage(sibling).getImage()->getColor(20, 20).b() > 0.9f,
            "available detail is initially displayed");
    require(layer->createImage(child).valid(), "warning tile is replaced with same-source regional imagery");
    require(layer->fallbackPending.exchange(false), "missing imagery requests a GUI notice");
    VaporView::Map3D::Detail::TiandituSourceLayer source(layer->options());
    GeoImage cachedWarning(decoded, child.getExtent());
    source.postCreateImageImplementation(cachedWarning, child, nullptr);
    require(!cachedWarning.valid(), "old cached warning imagery is rejected too");
    const auto revision = layer->getRevision();
    const auto changedRegions = layer->refreshFallbackRegions();
    require(changedRegions.size() == 1
                && std::abs(changedRegions.front().xMin() - child.createParentKey().getExtent().xMin()) < 1e-5
                && std::abs(changedRegions.front().yMin() - child.createParentKey().getExtent().yMin()) < 1e-5,
            "renderer refreshes exactly the affected parent region");
    require(layer->getRevision() > revision, "region fallback invalidates already displayed detailed neighbors");
    require(layer->refreshFallbackRegions().empty(), "unchanged regions do not trigger repeated terrain refreshes");
    const auto parentKey = child.createParentKey();
    GeoImage reference(osgDB::readRefImageFile(tiles.filePath(QStringLiteral("2/2/1.png")).toStdString()),
                       parentKey.getExtent());
    GeoImagePixelReader referenceReader(reference);
    referenceReader.setBilinear(true);
    auto checkPixels = [&](const GeoImage& result) {
        require(result.valid(), "regional image exists");
        const auto* pixels = result.getImage();
        for (int y : {16, 64, 127, 192, 240}) for (int x : {16, 64, 127, 192, 240})
        {
            osg::Vec3d position(result.getExtent().xMin() + (x + 0.5) * result.getExtent().width() / pixels->s(),
                                result.getExtent().yMin() + (y + 0.5) * result.getExtent().height() / pixels->t(), 0.0);
            require(result.getSRS()->transform(position, reference.getSRS(), position), "sample coordinate transforms");
            osg::Vec4f expected;
            referenceReader.readCoordWithoutClamping(expected, position.x(), position.y());
            const auto actual = pixels->getColor(x, y);
            require((expected - actual).length() < 0.06f,
                    "roads and coordinate ramp match the same source across regional tile seams");
        }
    };
    for (unsigned quadrant = 0; quadrant < 4; ++quadrant)
        checkPixels(layer->createImage(parentKey.createChildKey(quadrant)));
    const TileKey grandchild(4, 8, 4, map->getProfile());
    checkPixels(layer->createImage(grandchild));
    // The actual globe uses geodetic terrain with Mercator imagery: exercise reprojection too.
    const auto geodetic = Profile::create(Profile::GLOBAL_GEODETIC);
    checkPixels(layer->createImage(TileKey(4, 18, 3, geodetic)));
    require(layer->createImage(TileKey(3, 6, 2, map->getProfile())).getImage()->getColor(20, 20).b() > 0.9f,
            "unrelated regions retain their available detail");
    const TileKey outside(3, 0, 0, map->getProfile());
    const auto missing = layer->createImage(outside);
    require(missing.valid() && missing.getImage()->getColor(0, 0).a() == 1.0f
                && std::abs(missing.getImage()->getColor(0, 0).r() - 58.0f / 255.0f) < 0.01f,
            "no same-source data produces opaque neutral coverage, never another map source");
    std::array<QColor, 4> rootColors = {QColor(190,30,30), QColor(30,190,30),
                                        QColor(30,30,190), QColor(190,190,30)};
    QTemporaryDir globeTiles;
    require(globeTiles.isValid(), "temporary global source directory");
    const TileKey root(0, 0, 0, map->getProfile());
    for (unsigned quadrant = 0; quadrant < 4; ++quadrant)
    {
        const auto rootChild = root.createChildKey(quadrant);
        const QString folder = QStringLiteral("1/%1").arg(rootChild.getTileX());
        require(QDir(globeTiles.path()).mkpath(folder), "create global source tile directory");
        QImage globalTile(256, 256, QImage::Format_RGB32);
        globalTile.fill(rootColors[quadrant]);
        require(globalTile.save(globeTiles.filePath(folder + QStringLiteral("/%1.png").arg(rootChild.getTileY()))),
                "write level-one source tile");
    }
    require(QDir(globeTiles.path()).mkpath(QStringLiteral("11/0")), "create high-detail source directory");
    QImage highDetail(256, 256, QImage::Format_RGB32);
    highDetail.fill(QColor(220, 80, 120));
    require(highDetail.save(globeTiles.filePath(QStringLiteral("11/0/0.png"))), "write high-detail source tile");
    osg::ref_ptr<TiandituImageLayer> globeLayer = new TiandituImageLayer;
    globeLayer->setProfile(Profile::create(Profile::SPHERICAL_MERCATOR));
    globeLayer->setURL(URI((globeTiles.path() + QStringLiteral("/{z}/{x}/{y}.png")).toStdString()));
    globeLayer->setCachePolicy(CachePolicy::NO_CACHE);
    globeLayer->options().maxLevel() = 18u;
    globeLayer->options().maxDataLevel() = 18u;
    osg::ref_ptr<Map> globeMap = new Map;
    globeMap->setProfile(Profile::create(Profile::SPHERICAL_MERCATOR));
    globeMap->addLayer(globeLayer);
    require(globeLayer->isOpen(), "global source layer opens");
    const auto global = globeLayer->createImage(root);
    require(global.valid(), "global overview assembles from source level one without provider warning level zero");
    GeoImagePixelReader globalReader(global);
    bool allQuadrantsMatch = true;
    for (unsigned quadrant = 0; quadrant < 4; ++quadrant)
    {
        const auto extent = root.createChildKey(quadrant).getExtent();
        osg::Vec4f pixel;
        require(globalReader.readCoordWithoutClamping(pixel,
                    (extent.xMin() + extent.xMax()) / 2.0,
                    (extent.yMin() + extent.yMax()) / 2.0), "sample global quadrant");
        allQuadrantsMatch = allQuadrantsMatch
            && std::abs(pixel.r() - rootColors[quadrant].redF()) < 0.03f
            && std::abs(pixel.g() - rootColors[quadrant].greenF()) < 0.03f
            && std::abs(pixel.b() - rootColors[quadrant].blueF()) < 0.03f;
    }
    require(allQuadrantsMatch, "global overview uses the four same-source quadrants");
    const auto levelEleven = globeLayer->createImage(TileKey(11, 0, 0, globeMap->getProfile()));
    require(levelEleven.valid() && levelEleven.getImage()->getColor(20, 20).r() > 0.8f,
            "source detail beyond XYZ's default level ten stays available");
    require(layer->isKeyInVisualRange(TileKey(19, 0, 0, map->getProfile())),
            "zooming beyond source maximum still displays upsampled imagery");
}

void checkLiveReadings(VaporView::Map3D::Map3DWindow& window)
{
    using namespace VaporView::Map3D;
    using VaporView::Geo::TrajectoryRenderSample;
    auto* panel = window.findChild<QFrame*>(QStringLiteral("map3DLiveDataSidePanel"));
    auto* toggle = window.findChild<QToolButton*>(QStringLiteral("map3DSidePanelToggleButton"));
    auto* section = window.findChild<QToolButton*>(QStringLiteral("map3DProfileSectionToggle"));
    auto* plot = window.findChild<QWidget*>(QStringLiteral("map3DWaterProfilePlot"));
    auto* open = window.findChild<QPushButton*>(QStringLiteral("map3DOpenWaveformPageButton"));
    auto* resolution = window.findChild<QComboBox*>(QStringLiteral("map3DProfileResolution"));
    auto* metric = window.findChild<QComboBox*>(QStringLiteral("map3DHeatMetricCombo"));
    auto* bar = window.findChild<QFrame*>(QStringLiteral("map3DMainHeatLegendBar"));
    auto* legend = window.findChild<QLabel*>(QStringLiteral("map3DMainHeatLegendText"));
    auto* canvas = window.findChild<QWidget*>(QStringLiteral("map3DView"));
    require(panel && toggle && plot && resolution && metric && canvas && legend && bar,
            "live sidebar, profile and main legend controls exist");
    require(panel->isVisible() && panel->autoFillBackground() && !panel->testAttribute(Qt::WA_TranslucentBackground)
            && bar->isVisible() && legend->text().contains(QStringLiteral("n=0")), "opaque sidebar and empty legend are visible");
    require(canvas->parentWidget() == panel->parentWidget() && !canvas->geometry().intersects(panel->geometry()),
            "sidebar and map occupy separate layout rectangles");
    require(plot->isVisible() && !section && !open,
            "the profile starts visible without a fold control or waveform shortcut");
    const int previousMetric = metric->currentIndex();
    for (int i = 0; i < 7; ++i)
        require(metric->itemText(i) == VaporView::Geo::heatMetricName(static_cast<VaporView::Geo::HeatMetric>(i), false),
                "heat metrics retain their persisted indices");

    int revisionReads = 0, snapshots = 0, lastResolution = 0;
    quint64 revision = 1;
    window.setWaterProfileProviders([&] { ++revisionReads; return revision; }, [&](int r0, int r1) {
        ++snapshots;
        require(r0 == r1, "height control applies to both channels");
        lastResolution = r0;
        Map3DWaterProfileSnapshot profile;
        profile.unit = QStringLiteral("mol/mol");
        profile.channels[0].resolutionM = r0;
        profile.channels[0].totalSamples = revision;
        profile.channels[0].layers.push_back({0, 0, r0, r0 * 0.5, 0.000123, 0, revision});
        return profile;
    });
    Map3DLiveTelemetrySnapshot live;
    live.calibrationSelected = live.calibrationValid = true;
    live.calibrationEpoch = 8;
    live.waterUnit = QStringLiteral("mol/mol");
    live.hasColumnWater = live.hasInSituWater = live.hasPosition = true;
    live.columnWater = 0.000123;
    live.inSituWater = 0.000456;
    live.columnAgeMs = 1250;
    live.latDeg = 39.9;
    live.lonDeg = 116.3;
    live.heightM = 87;
    live.fixQualityText = QStringLiteral("RTK");
    live.hasTemperature = live.hasHumidity = live.hasPressure = live.hasLidar = true;
    live.temperatureC = 23.4;
    live.humidityRh = 56.7;
    live.pressureHpa = 1001.2;
    live.lidarDistanceM = 18.9;
    live.temperatureSource = QStringLiteral("SHT");
    live.temperatureStale = true;
    window.setLiveTelemetrySnapshot(live);
    const auto shows = [&](const char* name, const QString& text) {
        const auto* label = panel->findChild<QLabel*>(QString::fromLatin1(name));
        return label && label->text().contains(text);
    };
    require(shows("map3DLiveColumnWater", QStringLiteral("816 nm")) && shows("map3DLiveColumnWater", QStringLiteral("0.000123 mol/mol"))
            && shows("map3DLiveInSituWater", QStringLiteral("1368 nm")) && shows("map3DLiveInSituWater", QStringLiteral("0.000456 mol/mol")),
            "both live water concentrations show their wavelength and calibrated units");
    require(shows("map3DLivePosition", QStringLiteral("RTK")) && shows("map3DLivePosition", QStringLiteral("39.900000"))
            && shows("map3DLiveTemperature", QStringLiteral("23.4 °C")) && shows("map3DLiveTemperature", QStringLiteral("SHT"))
            && shows("map3DLiveTemperature", QStringLiteral("已过期")) && shows("map3DLiveHumidity", QStringLiteral("56.7 %RH"))
            && shows("map3DLivePressure", QStringLiteral("1001.2 hPa")) && shows("map3DLiveLidar", QStringLiteral("18.9 m"))
            && shows("map3DLiveLidar", QStringLiteral("激光测距高度"))
            && !panel->findChild<QLabel*>(QStringLiteral("map3DLiveThermal")),
            "sidebar shows position, ambient readings and laser ranging height without thermal controls");
    require(shows("map3DProfileCaption", QStringLiteral("激光测距高度"))
            && metric->itemText(metric->count() - 1) == QStringLiteral("激光测距高度"),
            "the profile caption and heat metric use the same height label");
    require(revisionReads == 1 && snapshots == 1 && lastResolution == 5
            && plot->accessibleDescription().contains(QStringLiteral("n=1")),
            "the visible profile fetches and displays its data immediately");
    const QImage firstPlot = plot->grab().toImage();
    auto* channel = window.findChild<QComboBox*>(QStringLiteral("map3DProfileChannel"));
    require(channel && channel->count() == 2, "both accumulated profile channels are selectable");
    channel->setCurrentIndex(1);
    require(plot->accessibleDescription().startsWith(QStringLiteral("816 nm")) && snapshots == 1,
            "switching to the empty second channel reuses the cached profile");
    channel->setCurrentIndex(0);
    require(QTest::qWaitFor([&] { return revisionReads >= 2; }, 1500) && snapshots == 1,
            "unchanged revisions do not rebuild profile data");
    ++revision;
    require(QTest::qWaitFor([&] { return snapshots == 2; }, 1500),
            "a visible profile fetches a new revision without a telemetry update");
    resolution->setCurrentIndex(resolution->findData(20));
    require(lastResolution == 20 && snapshots == 3 && plot->grab().toImage() != firstPlot,
            "new height resolution changes the rendered profile without a revision bump");
    toggle->click();
    const int hiddenReads = revisionReads;
    ++revision;
    window.setLiveTelemetrySnapshot(live);
    QTest::qWait(500);
    require(!panel->isVisible() && revisionReads == hiddenReads && snapshots == 3 && bar->isVisible(),
            "hidden sidebar performs no profile work and keeps the main legend visible");
    toggle->click();
    require(panel->isVisible() && plot->isVisible() && snapshots == 4,
            "reopening the panel shows the profile and consumes the pending revision");
    window.setWaterProfileProviders({}, {});

    TrajectoryRenderSample point;
    point.navigation.nedNM = point.navigation.nedEM = point.navigation.nedDM = 0;
    point.navigation.fixQuality = VaporView::Geo::FixQuality::Fixed;
    point.heat.calibrationEpoch = live.calibrationEpoch;
    point.heat.columnWaterConcentration = live.columnWater;
    point.heat.inSituWaterConcentration = live.inSituWater;
    point.heat.temperatureC = live.temperatureC;
    point.heat.humidityRh = live.humidityRh;
    point.heat.pressureHpa = live.pressureHpa;
    point.heat.lidarDistanceM = live.lidarDistanceM;
    auto other = point;
    other.heat.columnWaterConcentration = 0.000567;
    auto previous = point;
    previous.heat.columnWaterConcentration = 999;
    --previous.heat.calibrationEpoch;
    metric->setCurrentIndex(4);
    window.appendRenderSamples({previous, point, other});
    require(legend->text().contains(QStringLiteral("0.000123 – 0.000567 mol/mol")) && legend->text().contains(QStringLiteral("n=2")),
            "main legend preserves small water concentrations and excludes other calibration epochs");
    --live.calibrationEpoch;
    window.setLiveTelemetrySnapshot(live);
    require(legend->text().contains(QStringLiteral("999 – 999")) && legend->text().contains(QStringLiteral("n=1")),
            "live epoch changes refresh the legend even without new track points");

    // Exercise the pick-to-card signal with a real layer pick, without requiring a GL context.
    Trajectory3DLayer layer;
    layer.appendRenderSample(point);
    const auto pick = layer.pickNearestSample(osg::Matrixd::identity(), 0, 0, 1);
    require(pick.has_value(), "frozen sample can be picked");
    OsgEarthViewWidget view(nullptr, true);
    view.setWaterUnitLabel(live.waterUnit);
    view.trajectoryRenderSampleSelected(pick->sampleIndex, pick->renderSample);
    auto* title = view.findChild<QLabel*>(QStringLiteral("map3DTrajectoryInfoTitle"));
    auto* details = view.findChild<QLabel*>(QStringLiteral("map3DTrajectoryInfoDetails"));
    require(title && title->text().contains(QStringLiteral("冻结")) && details, "picked card identifies a frozen sample");
    const QString frozen = details->text();
    for (const auto* text : {"0.000123 mol/mol", "0.000456 mol/mol", "23.4 °C", "56.7 %RH", "1001.2 hPa", "18.9 m"})
        require(frozen.contains(QString::fromUtf8(text)), "card shows each frozen auxiliary and concentration measurement");
    live.columnWater = 0.123;
    live.lidarDistanceM = 999;
    window.setLiveTelemetrySnapshot(live);
    require(shows("map3DLiveLidar", QStringLiteral("999 m")) && details->text() == frozen, "new live readings preserve the picked values");
    metric->setCurrentIndex(previousMetric);
    window.clearTrack();
    window.setLiveTelemetrySnapshot({});
}

// Run explicitly on a desktop with an OpenGL context. Business settings and online layers stay isolated.
void checkFrozenMapClick()
{
    using namespace VaporView::Map3D;
    qunsetenv("VAPORVIEW_MAP3D_HEADLESS_TEST");
    VaporView::setSettingsWritesSuspended(true);
    Map3DWindow window;
    Map3DLiveTelemetrySnapshot live;
    live.calibrationEpoch = 4;
    live.calibrationValid = live.calibrationSelected = live.hasColumnWater = true;
    live.waterUnit = QStringLiteral("mol/mol");
    live.columnWater = 0.000123;
    window.setLiveTelemetrySnapshot(live);
    VaporView::Geo::TrajectoryRenderSample point;
    point.navigation.latDeg = 30.25;
    point.navigation.lonDeg = 120.15;
    point.navigation.heightM = 25;
    point.navigation.fixQuality = VaporView::Geo::FixQuality::Fixed;
    point.navigation.heightReference = VaporView::Geo::HeightReference::Wgs84Ellipsoid;
    point.heat.columnWaterConcentration = live.columnWater;
    point.heat.calibrationEpoch = live.calibrationEpoch;
    window.appendRenderSample(point); // Before deferred GL initialization.
    auto* metric = window.findChild<QComboBox*>(QStringLiteral("map3DHeatMetricCombo"));
    metric->setCurrentIndex(4);
    window.show();
    OsgEarthViewWidget* view = nullptr;
    require(QTest::qWaitFor([&] {
        view = window.findChild<OsgEarthViewWidget*>();
        return view && view->isValid() && view->sampleCount() == 1;
    }, 10000), "deferred renderer restores the buffered measurement");
    require(view->heatCalibrationEpochFilter() == live.calibrationEpoch && view->heatRange().validCount == 1,
            "GL range matches the buffered live epoch");
    require(view->flyToTrack(), "camera centers the buffered point");
    QTest::qWait(250);
    const QPointF center(view->rect().center());
    const QPointF global(view->mapToGlobal(center.toPoint()));
    QMouseEvent press(QEvent::MouseButtonPress, center, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, center, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view, &press);
    QCoreApplication::sendEvent(view, &release);
    auto* card = view->findChild<QFrame*>(QStringLiteral("map3DTrajectoryInfoCard"));
    auto* details = view->findChild<QLabel*>(QStringLiteral("map3DTrajectoryInfoDetails"));
    require(card && details && card->isVisible() && view->rect().contains(card->geometry())
            && details->text().contains(QStringLiteral("0.000123 mol/mol")), "mouse click opens the frozen card within the map");
    const QString frozen = details->text();
    live.columnWater = 0.000456;
    window.setLiveTelemetrySnapshot(live);
    const auto* current = window.findChild<QLabel*>(QStringLiteral("map3DLiveColumnWater"));
    require(current->text().contains(QStringLiteral("0.000456")) && details->text() == frozen,
            "a new live snapshot only changes the live reading in the same window");
    window.close();
    VaporView::setSettingsWritesSuspended(false);
}

void checkGroundDetailEllipse()
{
    using VaporView::Map3D::Detail::GroundDetailEllipse;
    osgEarth::Ellipsoid earth;
    GroundDetailEllipse ellipse;
    const osg::Vec3d eye = earth.geodeticToGeocentric({116.4, 39.9, 3500});
    const osg::Vec3d nearGround = earth.geodeticToGeocentric({116.4, 39.9, 0});
    for (double longitude : {115.82, 116.98})
    {
        const osg::Vec3d target = earth.geodeticToGeocentric({longitude, 39.9, 0});
        const auto pose = osg::Matrixd::inverse(osg::Matrixd::lookAt(eye, target, eye - nearGround));
        const double range = (target - eye).length();
        ellipse.update(earth, pose, range, -4);
        require(ellipse.majorAxisM > 128000 && ellipse.majorAxisM < 131000,
                "Beijing oblique view covers both foci with a 60 km semi-minor axis");
        require(ellipse.intersects({nearGround, 0}) && ellipse.intersects({target, 0}),
                "both the ground under the camera and the distant target receive detail");
        const auto middle = (nearGround + target) * 0.5;
        auto side = (target - nearGround) ^ ellipse.up;
        side.normalize();
        require(ellipse.intersects({middle + side * 59000, 0}) &&
                !ellipse.intersects({middle + side * 61000, 0}) &&
                ellipse.intersects({middle + side * 61000, 2000}),
                "side detail ends at 60 km and tiles overlapping the boundary are retained");
        require(!ellipse.intersects({nearGround - (target - nearGround), 20}),
                "turning the camera leaves detail behind it outside the ellipse");
        require(ellipse.maxAllowedLod({nearGround, 0}, 8u) > 19u &&
                ellipse.maxAllowedLod({middle + side * 68000, 0}, 8u) == 12u &&
                ellipse.maxAllowedLod({middle + side * 88000, 0}, 8u) == 11u &&
                ellipse.maxAllowedLod({middle + side * 115000, 0}, 8u) == 10u &&
                ellipse.maxAllowedLod({middle + side * 150000, 0}, 8u) == 9u &&
                ellipse.maxAllowedLod({middle + side * 180000, 0}, 8u) == 8u,
                "LOD tapers smoothly in rings outside the oblique focus ellipse");
        require(ellipse.requiredFarPlaneM > range, "far plane accommodates the target");
        ellipse.update(earth, pose, range, -90);
        require(ellipse.intersects({middle + side * 180000, 0}) && ellipse.requiredFarPlaneM == 0,
                "top-down views release the detail limit");
    }
}

} // namespace

static void checkOnlineResourceEditor()
{
    using namespace VaporView::Map3D::Detail;
    using Provider = OnlineMapProvider;
    const QByteArray envToken = qgetenv("VAPORVIEW_CESIUM_ION_TOKEN");
    qunsetenv("VAPORVIEW_CESIUM_ION_TOKEN");
    auto settings = map3DTestSettings();
    auto appSettings = tiandituKeySettings();
    const auto tiandituKey = QStringLiteral("TrajectoryViewer/map/tianditu_key");
    const auto esriKey = QStringLiteral("esriApiKey");
    const auto legacyCesiumToken = QStringLiteral("cesium/ion_token");
    const auto editorCesiumToken = QStringLiteral("cesium/editor_token");
    appSettings.remove(tiandituKey);
    for (const auto& key : {esriKey, legacyCesiumToken, editorCesiumToken,
                            requestUrlSetting(Provider::Tianditu), requestUrlSetting(Provider::Esri),
                            requestUrlSetting(Provider::Cesium)})
        settings.remove(key);

    const QString credential = QString::number(42) + QStringLiteral("&+/?");
    settings.setValue(legacyCesiumToken, credential);
    require(configuredOnlineMapResource(Provider::Cesium).credential == credential, "legacy Cesium setting is used");
    qputenv("VAPORVIEW_CESIUM_ION_TOKEN", "fixture-env");
    require(configuredOnlineMapResource(Provider::Cesium).credential == QStringLiteral("fixture-env"),
            "Cesium environment token takes precedence over legacy settings");
    const OnlineMapResource override{defaultRequestUrl(Provider::Cesium), credential};
    saveOnlineMapResource(Provider::Cesium, override);
    require(configuredOnlineMapResource(Provider::Cesium) == override, "explicit Cesium edit can replace the environment default");
    qunsetenv("VAPORVIEW_CESIUM_ION_TOKEN");
    settings.remove(legacyCesiumToken);
    settings.remove(editorCesiumToken);
    settings.remove(requestUrlSetting(Provider::Cesium));

    const auto imagerySetting = QStringLiteral("satelliteImagerySource");
    const auto demSetting = QStringLiteral("demSource");
    const auto previousImagery = QStringLiteral("online:openaerialmap");
    const auto previousDem = QStringLiteral("online:copernicus");
    settings.setValue(imagerySetting, previousImagery);
    settings.setValue(demSetting, previousDem);
    VaporView::Map3D::Map3DWindow window;

    struct Case
    {
        Provider provider;
        QString actionName;
        QString editName;
        QString source;
        QString selectionSetting;
        QString customUrl;
    };
    const Case cases[] = {
        {Provider::Tianditu, QStringLiteral("map3DOnlineImagery_tianditu"), QStringLiteral("map3DTiandituKeyAction"),
         QStringLiteral("online:tianditu"), imagerySetting, QStringLiteral("https://maps.invalid/wmts?z={z}&x={x}&y={y}&tk={key}")},
        {Provider::Esri, QStringLiteral("map3DOnlineImagery_esri"), QStringLiteral("map3DEsriKeyAction"),
         QStringLiteral("online:esri"), imagerySetting, QStringLiteral("https://maps.invalid/tile/{z}/{y}/{x}")},
        {Provider::Cesium, QStringLiteral("map3DDemSource_online_cesium"), QStringLiteral("map3DCesiumTokenAction"),
         QStringLiteral("online:cesium"), demSetting, QStringLiteral("https://maps.invalid/v1/assets/2/endpoint")},
    };
    for (const auto& test : cases)
    {
        auto* select = actionByName(window, test.actionName);
        auto* edit = actionByName(window, test.editName);
        const QString oldSelection = settings.value(test.selectionSetting).toString();
        auto runDialog = [&](QAction* entry, const auto& inspect) {
            bool opened = false;
            QTimer::singleShot(0, &window, [&]() {
                auto* dialog = window.findChild<QDialog*>(QStringLiteral("map3DOnlineResourceDialog"));
                if (!dialog) return;
                opened = true;
                auto* url = dialog->findChild<QLineEdit*>(QStringLiteral("map3DOnlineResourceUrl"));
                auto* secret = dialog->findChild<QLineEdit*>(QStringLiteral("map3DOnlineResourceCredential"));
                auto* error = dialog->findChild<QLabel*>(QStringLiteral("map3DOnlineResourceError"));
                auto* buttons = dialog->findChild<QDialogButtonBox*>();
                require(url && secret && error && buttons && !url->isReadOnly() && !secret->isReadOnly()
                            && secret->echoMode() == QLineEdit::Password,
                        "editor provides an editable URL and masked credential");
                inspect(*url, *secret, *error, *buttons);
            });
            entry->trigger();
            require(opened, "resource action opens the configuration dialog");
        };
        require(select->isEnabled() && edit->isEnabled(), "missing credentials do not disable the provider or its editor");
        runDialog(select, [&](QLineEdit& url, QLineEdit& secret, QLabel&, QDialogButtonBox& buttons) {
            require(url.text() == defaultRequestUrl(test.provider) && secret.text().isEmpty(), "default request URL is prefilled");
            url.setText(test.customUrl);
            secret.setText(credential);
            buttons.button(QDialogButtonBox::Cancel)->click();
        });
        require(!select->isChecked() && settings.value(test.selectionSetting).toString() == oldSelection
                    && configuredOnlineMapResource(test.provider).credential.isEmpty()
                    && !settings.contains(requestUrlSetting(test.provider)),
                "cancelling a new provider leaves the previous selection and settings intact");
        runDialog(select, [&](QLineEdit& url, QLineEdit& secret, QLabel& error, QDialogButtonBox& buttons) {
            url.setText(QStringLiteral("http://maps.invalid/invalid"));
            secret.setText(credential);
            buttons.button(QDialogButtonBox::Save)->click();
            require(error.isVisible() && !settings.contains(requestUrlSetting(test.provider)), "invalid URL cannot be saved");
            url.setText(test.customUrl);
            secret.clear();
            buttons.button(QDialogButtonBox::Save)->click();
            require(error.isVisible() && !settings.contains(requestUrlSetting(test.provider)), "blank credential cannot be saved");
            secret.setText(credential);
            buttons.button(QDialogButtonBox::Save)->click();
        });
        require(select->isChecked() && settings.value(test.selectionSetting).toString() == test.source,
                "saving first-time configuration selects the provider");
        runDialog(edit, [&](QLineEdit& url, QLineEdit& secret, QLabel&, QDialogButtonBox& buttons) {
            require(url.text() == test.customUrl && secret.text() == credential, "editing opens the saved URL and credential");
            url.setText(defaultRequestUrl(test.provider));
            secret.setText(QString::number(84));
            buttons.button(QDialogButtonBox::Cancel)->click();
        });
        const OnlineMapResource persisted{test.customUrl, credential};
        require(select->isChecked() && configuredOnlineMapResource(test.provider) == persisted
                    && settings.value(test.selectionSetting).toString() == test.source,
                "cancelling later edits preserves saved values and active selection");
        if (test.provider == Provider::Tianditu)
        {
            const auto request = credentialedImageryRequest(test.provider, persisted, 4, 5, 6);
            require(request.url().toEncoded() == "https://maps.invalid/wmts?z=4&x=5&y=6&tk=42%26%2B%2F%3F"
                        && !request.attribute(QNetworkRequest::CacheSaveControlAttribute).toBool(),
                    "Tianditu sends the edited URL and one URL-encoded credential");
        }
        else
        {
            const auto request = test.provider == Provider::Esri
                ? credentialedImageryRequest(test.provider, persisted, 4, 5, 6) : cesiumEndpointRequest(persisted);
            const auto expected = test.provider == Provider::Esri ? QStringLiteral("https://maps.invalid/tile/4/6/5") : test.customUrl;
            require(request.url() == QUrl(expected) && request.rawHeader("Authorization") == "Bearer " + credential.toUtf8()
                        && !request.attribute(QNetworkRequest::CacheSaveControlAttribute).toBool(),
                    "Esri and Cesium send the edited URL with a bearer header");
        }
        runDialog(edit, [&](QLineEdit& url, QLineEdit& secret, QLabel&, QDialogButtonBox& buttons) {
            url.setText(defaultRequestUrl(test.provider));
            secret.setText(QString::number(84));
            buttons.button(QDialogButtonBox::Save)->click();
        });
        const OnlineMapResource updated{defaultRequestUrl(test.provider), QString::number(84)};
        require(select->isChecked() && configuredOnlineMapResource(test.provider) == updated,
                "saving later edits updates the provider without switching sources");
    }
    if (!envToken.isNull()) qputenv("VAPORVIEW_CESIUM_ION_TOKEN", envToken);
}

int main(int argc, char** argv)
{
    qputenv("VAPORVIEW_MAP3D_HEADLESS_TEST", "1");
    checkGroundDetailEllipse();

    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory is valid");
    qputenv("OSGEARTH_CACHE_PATH", settingsDir.filePath(QStringLiteral("map-cache")).toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

    // A surface focal point must remain visible throughout the zoom/pitch range.
    for (double range : {50.0, 11000.0, 120000.0, 850000.0, 1000000.0,
                         1000001.0, 10000000.0, 18000000.0, 20000000.0})
    {
        for (double pitch : {-90.0, -35.0, -4.0})
        {
            const auto profile = VaporView::Map3D::earthProjectionProfile(range, pitch);
            const auto projection = osg::Matrixd::perspective(30.0, 4.0 / 3.0,
                VaporView::Map3D::kEarthProjectionNearPlaneM, profile.farPlaneM);
            const osg::Vec3d focalPoint = osg::Vec3d(0.0, 0.0, -range) * projection;
            require(focalPoint.z() > -1.0 && focalPoint.z() < 1.0,
                    "earth focal point stays inside the camera clip volume at every zoom level");
            if (range > 1000000.0)
                require(profile.farPlaneM >= range + 2.0 * 6378137.0,
                        "orbital view includes the far side of the globe");
        }
    }

    QApplication app(argc, argv);
    VaporView::installButtonFocusStyle();
    if (app.arguments().contains(QStringLiteral("--online-resource-editor-only")))
    {
        checkOnlineResourceEditor();
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--eox-selection-only"))) {
        checkEoxSelectionPersistence();
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--osm-live")))
    {
        checkOsmOverlays(true);
        return 0;
    }
    checkOsmOverlays(false);
    testRemovedImageryLifetime();
    if (app.arguments().contains(QStringLiteral("--layer-lifetime-only"))) return 0;
    QCoreApplication::setOrganizationName(QStringLiteral("VaporViewTest"));
    QCoreApplication::setApplicationName(QStringLiteral("map3d_window_smoke_test"));
    if (app.arguments().contains(QStringLiteral("--frozen-map-pick"))) {
        checkFrozenMapClick();
        return 0;
    }
    testLanguageObjectLifetime();
    if (app.arguments().contains(QStringLiteral("--language-lifetime-only"))) return 0;

    const QString expectedProjectData = QDir::cleanPath(
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../data")));
    require(VaporView::Map3D::map3DProjectDataDirectory() == expectedProjectData,
            "session chooser defaults to the project-root data directory");
    require(VaporView::Map3D::map3DProjectDataDirectory()
                != QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("data")),
            "session chooser does not default to the release data directory");

    checkImageryFallback();
    {
        const auto eoxUrl = VaporView::Map3D::Detail::eoxSentinel2TileRequest(
            osgEarth::TileKey(12, 3414, 1686, osgEarth::Profile::create(osgEarth::Profile::SPHERICAL_MERCATOR)));
        require(eoxUrl.url().toString() == QStringLiteral(
                    "https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2024_3857/default/g/12/1686/3414.jpg"),
                "EOX Sentinel-2 uses the WMTS row/column y/x ordering");
        require(VaporView::Map3D::Detail::eoxSentinel2TileRequest(
                    osgEarth::TileKey(19, 0, 0, osgEarth::Profile::create(osgEarth::Profile::SPHERICAL_MERCATOR)))
                    .url().isEmpty(),
                "EOX Sentinel-2 rejects levels above the 2024 service coverage");
        require(QString::fromLatin1(VaporView::Map3D::Detail::kEoxSentinel2Attribution)
                    .contains(QStringLiteral("Contains modified Copernicus Sentinel data 2024"))
                    && QString::fromLatin1(VaporView::Map3D::Detail::kEoxSentinel2UsageUrl)
                           == QStringLiteral("https://cloudless.eox.at/documentation/usage"),
                "EOX Sentinel-2 attribution and usage source are fixed in the layer contract");
    }
    {
        osg::ref_ptr<VaporView::Map3D::Detail::OpenAerialMapImageLayer> layer =
            new VaporView::Map3D::Detail::OpenAerialMapImageLayer;
        require(layer->options().minLevel().get() == 14u,
                "OpenAerialMap does not mistake the low zoom coverage grid for imagery");
        require(QString::fromStdString(layer->getURL().full())
                    == QStringLiteral("https://global.imagery.hotosm.org/{z}/{x}/{y}.png"),
                "OpenAerialMap uses the official global imagery service");
        osg::ref_ptr<osg::Image> empty = new osg::Image;
        empty->allocateImage(256, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
            {
                auto* pixel = empty->data(x, y);
                pixel[0] = pixel[1] = pixel[2] = 0;
                pixel[3] = 255;
            }
        const auto* profile = osgEarth::Profile::create(osgEarth::Profile::SPHERICAL_MERCATOR);
        const osgEarth::TileKey key(14, 0, 0, profile);
        const auto mask = [&key, &empty]() {
            return VaporView::Map3D::Detail::maskOpenAerialMapNoData(osgEarth::GeoImage(empty.get(), key.getExtent()));
        };
        require(!mask().valid(),
                "an all-black no-coverage tile falls through to the basemap");
        empty->data(255, 255)[0] = 1;
        const auto partial = mask();
        require(partial.valid() && partial.getImage()->getColor(0, 0).a() == 0.0f
                    && partial.getImage()->getColor(255, 255).a() == 1.0f
                    && empty->getColor(0, 0).a() == 1.0f,
                "no-data pixels are transparent while dark imagery and the cached input are retained");
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
            {
                auto* pixel = empty->data(x, y);
                pixel[0] = pixel[1] = pixel[2] = static_cast<unsigned char>(98 + x / 64);
                pixel[3] = 255;
            }
        empty->data(255, 255)[0] = empty->data(255, 255)[1] = empty->data(255, 255)[2] = 102;
        require(!mask().valid(), "almost uniform gray OAM tiles fall through to the basemap");
        osgEarth::GeoImage cachedGray(empty.get(), key.getExtent());
        layer->postCreateImageImplementation(cachedGray, key, nullptr);
        require(!cachedGray.valid() && empty->getColor(0, 0).a() == 1.0f,
                "cached gray tiles also fall through without changing the cached image");
        empty->data(255, 255)[0] = empty->data(255, 255)[1] = empty->data(255, 255)[2] = 103;
        require(mask().valid(), "textured grayscale imagery remains visible");
        empty->data(255, 255)[0] = 99;
        empty->data(255, 255)[1] = empty->data(255, 255)[2] = 98;
        require(mask().valid(), "subtle color imagery remains visible");
    }
    {
        auto settings = map3DTestSettings();
        settings.setValue(QStringLiteral("layers/roadNetworkVisible"), true);
        settings.setValue(QStringLiteral("layers/satelliteImageryVisible"), false);
    }
    VaporView::Map3D::Map3DWindow window;
    QCoreApplication::processEvents();

    window.show();
    auto* moreMenu = window.findChild<QMenu*>(QStringLiteral("map3DMoreMenu"));
    auto* imageryMenu = window.findChild<QMenu*>(QStringLiteral("map3DLocalImageryMenu"));
    require(moreMenu && imageryMenu, "nested map menus exist");
    QMenu* resourcesMenu = moreMenu->actions().first()->menu();
    moreMenu->popup(window.mapToGlobal(QPoint(10, 60)));
    moreMenu->setActiveAction(resourcesMenu->menuAction());
    QKeyEvent openSubmenu(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(moreMenu, &openSubmenu);
    QCoreApplication::processEvents();
    resourcesMenu->setActiveAction(imageryMenu->menuAction());
    QApplication::sendEvent(resourcesMenu, &openSubmenu);
    QCoreApplication::processEvents();
    require(resourcesMenu->isVisible() && imageryMenu->isVisible(),
            "keyboard navigation opens second and third level map menus");
    require(imageryMenu->title() == QStringLiteral("卫星影像"), "imagery menu uses the satellite title");
    auto* onlineImagery = window.findChild<QAction*>(QStringLiteral("map3DOnlineImagery_tianditu"));
    auto* openAerialMap = window.findChild<QAction*>(QStringLiteral("map3DOnlineImagery_openaerialmap"));
    auto* esriImagery = window.findChild<QAction*>(QStringLiteral("map3DOnlineImagery_esri"));
    auto* eoxImagery = window.findChild<QAction*>(QStringLiteral("map3DOnlineImagery_eoxSentinel2_2024"));
    require(onlineImagery && onlineImagery->isEnabled()
                && onlineImagery->text().contains(QStringLiteral("在线"))
                && onlineImagery->text().contains(QStringLiteral("未配置 Key")),
            "online imagery is listed with an explanation when no key is configured");
    require(openAerialMap && openAerialMap->isEnabled() && openAerialMap->isCheckable()
                && openAerialMap->text() == QStringLiteral("在线 · OpenAerialMap"),
            "OpenAerialMap is available online without a key");
    require(esriImagery && esriImagery->isEnabled() && esriImagery->isCheckable()
                && esriImagery->text().contains(QStringLiteral("Esri World Imagery"))
                && esriImagery->text().contains(QStringLiteral("未配置 Key")),
            "Esri World Imagery can be configured from its source action");
    require(eoxImagery && eoxImagery->isEnabled() && eoxImagery->isCheckable()
                && eoxImagery->text().contains(QStringLiteral("EOX Sentinel-2 cloudless 2024")),
            "EOX Sentinel-2 cloudless 2024 is available as a no-key imagery source");
    require(window.findChild<QAction*>(QStringLiteral("map3DEsriKeyAction")),
            "Esri API key configuration is available");
    {
        auto settings = map3DTestSettings();
        settings.setValue(QStringLiteral("esriApiKey"), QStringLiteral("test-key-not-sent"));
        settings.sync();
    }
    QMetaObject::invokeMethod(imageryMenu, "aboutToShow", Qt::DirectConnection);
    require(esriImagery->isEnabled() && esriImagery->text() == QStringLiteral("在线 · Esri World Imagery"),
            "configured Esri imagery becomes selectable");
    const auto hasMenuCheckMark = [imageryMenu, openAerialMap]() {
        const bool wasChecked = openAerialMap->isChecked();
        openAerialMap->setChecked(false);
        const QImage unchecked = imageryMenu->grab().toImage();
        openAerialMap->setChecked(true);
        const QImage checked = imageryMenu->grab().toImage();
        openAerialMap->setChecked(wasChecked);
        const QRect item = imageryMenu->actionGeometry(openAerialMap);
        const qreal dpr = imageryMenu->devicePixelRatioF();
        const QRect slot(qRound((item.left() + 8) * dpr), qRound((item.center().y() - 10) * dpr),
                         qRound(20 * dpr), qRound(20 * dpr));
        int changed = 0;
        for (int y = slot.top(); y <= slot.bottom() && y < checked.height(); ++y)
            for (int x = slot.left(); x <= slot.right() && x < checked.width(); ++x)
                if (x >= 0 && y >= 0 && checked.pixel(x, y) != unchecked.pixel(x, y)) ++changed;
        return changed > 8;
    };
    require(hasMenuCheckMark(), "checked imagery source paints a visible check icon");
    for (const QString& key : {QStringLiteral("sentinel2"), QStringLiteral("landsat"),
                               QStringLiteral("openaerialmap")})
    {
        const auto* action = window.findChild<QAction*>(QStringLiteral("map3DLocalImagery_%1").arg(key));
        require(action && action->text().startsWith(QStringLiteral("本地 · ")),
                "all local imagery options have a local prefix");
    }
    require(!resourcesMenu->geometry().intersects(imageryMenu->geometry()),
            "third level menu does not cover its parent menu or arrow");
    require(!imageryMenu->toolTipsVisible(), "imagery menu does not show obstructing tooltips");
    auto* autoImagery = window.findChild<QAction*>(QStringLiteral("map3DAutoImageryAction"));
    require(autoImagery && autoImagery->isCheckable() && autoImagery->isChecked()
                && autoImagery->text() == QStringLiteral("自动（默认）"),
            "imagery menu defaults to automatic selection");

    auto* baseMapMenu = window.findChild<QMenu*>(QStringLiteral("map3DBaseMapSourceMenu"));
    auto* demMenu = window.findChild<QMenu*>(QStringLiteral("map3DDemSourceMenu"));
    auto* osmMenu = window.findChild<QMenu*>(QStringLiteral("map3DOsmSourceMenu"));
    auto* buildingsMenu = window.findChild<QMenu*>(QStringLiteral("map3DBuildingsSourceMenu"));
    require(baseMapMenu && demMenu && osmMenu && buildingsMenu,
            "all map resources expose manual source selector submenus under Map Resources");
    for (QMenu* subMenu : {baseMapMenu, demMenu, osmMenu, buildingsMenu})
    {
        QMetaObject::invokeMethod(subMenu, "aboutToShow", Qt::DirectConnection);
        require(!subMenu->actions().isEmpty() && subMenu->actions().first()->isCheckable()
                    && subMenu->actions().first()->isChecked()
                    && subMenu->actions().first()->text() == QStringLiteral("自动（默认）"),
                "each resource source submenu defaults to Auto");
    }
    auto* demOnlineCopernicus = window.findChild<QAction*>(QStringLiteral("map3DDemSource_online_copernicus"));
    auto* osmOnline = window.findChild<QAction*>(QStringLiteral("map3DOsmSource_online_osm"));
    auto* buildingsOnline = window.findChild<QAction*>(QStringLiteral("map3DBuildingsSource_online_openfreemap"));
    auto* baseGlobe = window.findChild<QAction*>(QStringLiteral("map3DBaseMapSource_builtin_globe"));
    require(demOnlineCopernicus && demOnlineCopernicus->isEnabled()
                && osmOnline && osmOnline->isEnabled()
                && buildingsOnline && buildingsOnline->isEnabled()
                && baseGlobe && baseGlobe->isEnabled(),
            "manual data source options are enabled when available");
    demOnlineCopernicus->trigger();
    osmOnline->trigger();
    buildingsOnline->trigger();
    baseGlobe->trigger();
    require(demOnlineCopernicus->isChecked() && osmOnline->isChecked()
                && buildingsOnline->isChecked() && baseGlobe->isChecked(),
            "manual data source choices can be selected and become checked");
    // Restore automatic defaults and initial layer state so later assertions start clean.
    baseMapMenu->actions().first()->trigger();
    demMenu->actions().first()->trigger();
    osmMenu->actions().first()->trigger();
    buildingsMenu->actions().first()->trigger();
    actionByName(window, QStringLiteral("map3DLayer_buildings3D"))->setChecked(false);
    require(baseMapMenu->actions().first()->isChecked()
                && demMenu->actions().first()->isChecked()
                && osmMenu->actions().first()->isChecked()
                && buildingsMenu->actions().first()->isChecked(),
            "clearing manual overrides restores automatic degradation defaults");

    auto* cornerControls = window.findChild<QWidget*>(QStringLiteral("map3DCornerControls"));
    require(cornerControls
                && !cornerControls->testAttribute(Qt::WA_TranslucentBackground)
                && !cornerControls->testAttribute(Qt::WA_NativeWindow)
                && !cornerControls->autoFillBackground()
                && cornerControls->styleSheet().contains(QStringLiteral("background: transparent")),
            "floating corner controls remain alien transparent overlays over the map");

    auto* mapStatus = statusLabel(window);
    require(mapStatus
                && !mapStatus->text().contains(QStringLiteral("热力"))
                && !mapStatus->text().contains(QStringLiteral("湿度"))
                && !mapStatus->text().contains(QStringLiteral("温度"))
                && !mapStatus->text().contains(QStringLiteral("→")),
            "bottom status bar excludes unrelated telemetry and fallback degradation chains");

    auto* loadingBubble = window.findChild<QWidget*>(QStringLiteral("map3DLoadingProgressBubble"));
    auto* loadingLabel = window.findChild<QLabel*>(QStringLiteral("map3DLoadingProgressLabel"));
    auto* loadingBar = window.findChild<QProgressBar*>(QStringLiteral("map3DLoadingProgressBar"));
    require(loadingBubble && loadingLabel && loadingBar && !loadingBubble->isVisible(),
            "top-right map loading progress bubble starts hidden");
    {
        VaporView::Map3D::ScopedMapLoadTask osmTask(VaporView::Map3D::MapLoadCategory::Osm);
        VaporView::Map3D::ScopedMapLoadTask demTask(VaporView::Map3D::MapLoadCategory::Dem);
        demTask.finish();
        QElapsedTimer waitTimer;
        waitTimer.start();
        while (!loadingBubble->isVisible() && waitTimer.elapsed() < 500)
        {
            QCoreApplication::processEvents();
            QThread::msleep(15);
        }
        require(loadingBubble->isVisible()
                    && loadingLabel->text().contains(QStringLiteral("OSM"))
                    && loadingLabel->text().contains(QStringLiteral("50%"))
                    && loadingBar->value() == 50
                    && loadingBubble->geometry().left() > loadingBubble->parentWidget()->width() / 2
                    && loadingBubble->geometry().top() <= 24,
                "loading bubble displays resource progress in the top-right corner");
        // Interruption / cancellation must hide the bubble immediately.
        osmTask.cancel();
        waitTimer.restart();
        while (loadingBubble->isVisible() && waitTimer.elapsed() < 500)
        {
            QCoreApplication::processEvents();
            QThread::msleep(15);
        }
        require(!loadingBubble->isVisible(),
                "interrupted map resource load disappears immediately");
    }
    {
        VaporView::Map3D::ScopedMapLoadTask demTask(VaporView::Map3D::MapLoadCategory::Dem);
        QElapsedTimer waitTimer;
        waitTimer.start();
        while (!loadingBubble->isVisible() && waitTimer.elapsed() < 500)
        {
            QCoreApplication::processEvents();
            QThread::msleep(15);
        }
        require(loadingBubble->isVisible(), "single active load shows loading bubble");
        demTask.finish();
        waitTimer.restart();
        while (loadingBubble->isVisible() && waitTimer.elapsed() < 500)
        {
            QCoreApplication::processEvents();
            QThread::msleep(15);
        }
        require(!loadingBubble->isVisible(), "completed map resource load disappears immediately");
    }
    for (QAction* action : imageryMenu->actions())
        require(!qobject_cast<QWidgetAction*>(action), "imagery submenu uses native menu rows");
    QHelpEvent tooltipEvent(QEvent::ToolTip, QPoint(20, 20), imageryMenu->mapToGlobal(QPoint(20, 20)));
    QApplication::sendEvent(imageryMenu, &tooltipEvent);
    require(!QToolTip::isVisible(), "hover help cannot cover imagery options");
    QEvent leaveEvent(QEvent::Leave);
    QApplication::sendEvent(imageryMenu, &leaveEvent);
    require(!QToolTip::isVisible(), "leaving the menu keeps tooltips dismissed");
    imageryMenu->hide();
    resourcesMenu->hide();
    moreMenu->hide();

    auto* mapFilesAction = actionByName(window, QStringLiteral("map3DMapFilesAction"));
    require(mapFilesAction && mapFilesAction->text() == QStringLiteral("地图文件"),
            "map files button is available before rendering starts");
    auto* moreButton = window.findChild<QToolButton*>(QStringLiteral("map3DMoreButton"));
    require(moreButton && moreButton->defaultAction()->menu() == moreMenu, "the toolbar opens the More menu");
    selectMapResourceWithMouse(moreButton, resourcesMenu, mapFilesAction);
    QCoreApplication::processEvents();
    auto* filesDialog = window.findChild<QDialog*>(QStringLiteral("map3DMapFilesDialog"));
    auto* filesTable = window.findChild<QTableWidget*>(QStringLiteral("map3DMapFilesTable"));
    require(filesDialog && filesDialog->isVisible() && filesTable && filesTable->rowCount() > 0,
            "map files button opens a populated local file list without rendering");
    for (int row = 0; row < filesTable->rowCount(); ++row)
    {
        require(filesTable->item(row, 0) && filesTable->item(row, 1) && filesTable->item(row, 2),
                "each map file has a name, status and path");
        const QFileInfo file(filesTable->item(row, 2)->text());
        if (!file.exists())
            require(filesTable->item(row, 1)->text() == QStringLiteral("缺失"),
                    "missing map files are marked missing");
        else if (file.isFile() && file.isReadable())
            require(filesTable->item(row, 1)->text() == QStringLiteral("就绪"),
                    "readable local map files are marked ready");
    }
    auto* filesRefresh = filesDialog->findChild<QPushButton*>(QStringLiteral("map3DMapFilesRefreshButton"));
    require(filesRefresh != nullptr, "map files list supports rescanning");
    const int fileCount = filesTable->rowCount();
    filesRefresh->click();
    require(filesTable->rowCount() == fileCount, "rescan does not duplicate file rows");
    filesDialog->hide();
    mapFilesAction->trigger();
    require(window.findChildren<QDialog*>(QStringLiteral("map3DMapFilesDialog")).size() == 1,
            "reopening map files reuses the existing dialog");
    filesDialog->hide();

    require(window.windowFlags().testFlag(Qt::FramelessWindowHint)
                && window.menuWidget()
                && window.menuWidget()->objectName() == QStringLiteral("customTitleBar"),
            "map window uses the shared title bar above its toolbar");
    actionByName(window, QStringLiteral("map3DDisplayAction"))->trigger();
    QCoreApplication::processEvents();
    auto* displayPanel = window.findChild<QDialog*>(QStringLiteral("map3DDisplayPanel"));
    require(displayPanel != nullptr, "display settings panel exists");
    auto* refreshStatus = displayPanel->findChild<QLabel*>(QStringLiteral("map3DDisplayRefreshStatus"));
    require(refreshStatus && refreshStatus->isVisible() &&
                refreshStatus->parentWidget()->objectName() == QStringLiteral("customTitleBar") &&
                refreshStatus->text() == QStringLiteral("实时更新") &&
                refreshStatus->width() >= refreshStatus->sizeHint().width(),
            "display title bar communicates live updates");
    auto* lineCheck = displayPanel->findChild<QCheckBox*>(QStringLiteral("map3DTrackLineVisibleCheckBox"));
    auto* pointCheck = displayPanel->findChild<QCheckBox*>(QStringLiteral("map3DTrackPointsVisibleCheckBox"));
    require(lineCheck && pointCheck && lineCheck->parentWidget() == pointCheck->parentWidget() &&
                lineCheck->geometry().center().y() == pointCheck->geometry().center().y() &&
                lineCheck->layoutDirection() == Qt::RightToLeft && pointCheck->layoutDirection() == Qt::RightToLeft,
            "track visibility checkboxes share one row with indicators on the right");
    auto* lineAction = actionByName(window, QStringLiteral("map3DTrackLineVisibleAction"));
    lineCheck->click();
    require(!lineCheck->isChecked() && !lineAction->isChecked() &&
                refreshStatus->text() == QStringLiteral("已设置"),
            "checkbox immediately updates the action and reports the headless setting change");
    lineAction->setChecked(true);
    require(lineCheck->isChecked(), "external visibility action changes synchronize back to the checkbox");
    const QVariant originalEnglish = qApp->property("vaporViewEnglish");
    qApp->setProperty("vaporViewEnglish", true);
    QCoreApplication::processEvents();
    auto* palettePreview = displayPanel->findChild<QWidget*>(QStringLiteral("map3DHeatPalettePreview"));
    require(lineCheck->text() == QStringLiteral("Track line") && refreshStatus->text() == QStringLiteral("Set") &&
                palettePreview && palettePreview->accessibleName() == QStringLiteral("Palette preview"),
            "visibility controls and refresh status translate to English");
    require(imageryMenu->title() == QStringLiteral("Satellite image sources")
                && onlineImagery->text().startsWith(QStringLiteral("Online · "))
                && openAerialMap->text() == QStringLiteral("Online · OpenAerialMap")
                && esriImagery->text() == QStringLiteral("Online · Esri World Imagery")
                && window.findChild<QAction*>(QStringLiteral("map3DLocalImagery_sentinel2"))
                       ->text().startsWith(QStringLiteral("Local · ")),
            "satellite imagery menu translates its source prefixes");
    {
        auto settings = map3DTestSettings();
        settings.remove(QStringLiteral("esriApiKey"));
        settings.sync();
    }
    qApp->setProperty("vaporViewEnglish", originalEnglish);
    QCoreApplication::processEvents();
    require(lineCheck->text() == QStringLiteral("轨迹线") && refreshStatus->text() == QStringLiteral("已设置"),
            "visibility controls and refresh status translate back to Chinese");
    require(imageryMenu->title() == QStringLiteral("卫星影像")
                && onlineImagery->text().startsWith(QStringLiteral("在线 · ")),
            "satellite imagery menu translates back to Chinese");
    auto* paletteCombo = displayPanel->findChild<QComboBox*>(QStringLiteral("map3DHeatPaletteCombo"));
    require(paletteCombo && palettePreview && palettePreview->isVisible(), "current heat palette has a visible preview");
    for (int index = 0; index < 3; ++index) {
        paletteCombo->setCurrentIndex(index);
        const QImage preview = palettePreview->grab().toImage();
        require(!preview.isNull(), "palette preview renders");
        const auto palette = index == 0 ? VaporView::Geo::HeatPalette::Candy :
                             index == 1 ? VaporView::Geo::HeatPalette::BlueRedFast :
                                          VaporView::Geo::HeatPalette::SpectralReverse;
        for (double position : {0.15, 0.5, 0.85}) {
            const QColor pixel = preview.pixelColor(qRound(position * (preview.width() - 1)), preview.height() / 2);
            const auto expected = VaporView::Geo::heatPaletteColor(position, palette);
            require(std::abs(pixel.redF() - expected.r) < 0.06 &&
                        std::abs(pixel.greenF() - expected.g) < 0.06 &&
                        std::abs(pixel.blueF() - expected.b) < 0.06,
                    "palette preview colors match the trajectory heat palette");
        }
    }
    paletteCombo->setCurrentIndex(0);
    int titleIconCount = 0;
    for (QToolButton* button : displayPanel->findChildren<QToolButton*>()) {
        if (!button->property("customTitleBarButton").toBool()) continue;
        ++titleIconCount;
        require(titleIconHasVisibleSize(button), "display panel title icons retain their visible size");
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(button, &leave);
        const auto background = [button]() {
            const QImage image = button->parentWidget()->grab(button->geometry()).toImage();
            const int inset = qRound(5 * button->devicePixelRatioF());
            return image.pixelColor(inset, inset);
        };
        const QColor idle = background();
        QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), QPointF(button->mapToGlobal(QPoint(5, 5))));
        QApplication::sendEvent(button, &enter);
        require(button->property("titleBarHover").toBool() && background() != idle,
                "transparent title buttons gain a background on hover");
        QApplication::sendEvent(button, &leave);
    }
    require(titleIconCount == 3, "display panel has language, theme and close title icons");
    selectMapResourceWithMouse(moreButton, resourcesMenu, actionByName(window, QStringLiteral("map3DMapResourcesAction")));
    auto* resourceDialog = window.findChild<VaporView::Map3D::MapResourceDialog*>();
    require(resourceDialog && resourceDialog->isVisible(), "mouse selection opens the map resource manager");
    for (QDialog* dialog : window.findChildren<QDialog*>())
    {
        require(dialog->windowFlags().testFlag(Qt::FramelessWindowHint)
                    && dialog->findChild<QWidget*>(QStringLiteral("customTitleBar")),
                "map child dialogs use the shared title bar");
        dialog->hide();
    }
    QLabel* label = statusLabel(window);
    require(label->text().contains(QStringLiteral("采样点：0")), "initial status has zero points");
    require(label->text().contains(QStringLiteral("来源 无")), "initial status reports no track source");
    checkLiveReadings(window);
    auto* statusMessage = window.findChild<QLabel*>(QStringLiteral("map3DStatusMessage"));
    require(statusMessage != nullptr, "status bar has a wrapped temporary message label");
    const QString longMessage = QStringLiteral("Loading C:/") + QString(420, QLatin1Char('a'));
    window.statusBar()->showMessage(longMessage, 5000);
    QCoreApplication::processEvents();
    QString renderedMessage = statusMessage->text();
    require(statusMessage->isVisible() && !label->isVisible() && renderedMessage.contains(QLatin1Char('\n'))
            && renderedMessage.remove(QLatin1Char('\n')) == longMessage
            && statusMessage->height() >= statusMessage->sizeHint().height(),
            "the temporary message replaces the status and has room to render the full path");
    window.statusBar()->clearMessage();
    QCoreApplication::processEvents();
    require(label->isVisible() && !statusMessage->isVisible(), "clearing the message restores the compact status");
    QAction* reloadBestMapAction = actionByName(window, QStringLiteral("map3DReloadBestMapAction"));
    QAction* flyToAircraftAction = actionByName(window, QStringLiteral("map3DFlyToAircraftAction"));
    QAction* flyToTrackAction = actionByName(window, QStringLiteral("map3DFlyToTrackAction"));
    QAction* resetViewAction = actionByName(window, QStringLiteral("map3DResetViewAction"));
    QAction* diagnosticsAction = actionByName(window, QStringLiteral("map3DDiagnosticsAction"));
    QAction* layersAction = actionByName(window, QStringLiteral("map3DLayersAction"));
    QAction* localImageryAction = actionByName(window, QStringLiteral("map3DLocalImageryAction"));
    QAction* local3DTilesAction = actionByName(window, QStringLiteral("map3DLocal3DTilesAction"));
    QAction* clearLocal3DTilesAction = actionByName(window, QStringLiteral("map3DClearLocal3DTilesAction"));
    QAction* loadAircraftModelAction = actionByName(window, QStringLiteral("map3DLoadAircraftModelAction"));
    QAction* resetAircraftModelAction = actionByName(window, QStringLiteral("map3DResetAircraftModelAction"));
    auto* heatMetricCombo = window.findChild<QComboBox*>(QStringLiteral("map3DHeatMetricCombo"));
    auto* heatPaletteCombo = window.findChild<QComboBox*>(QStringLiteral("map3DHeatPaletteCombo"));
    auto* maxVisibleSamplesSpin = window.findChild<QSpinBox*>(QStringLiteral("map3DMaxVisibleSamplesSpin"));
    auto* trackLineWidthSpin = window.findChild<QSpinBox*>(QStringLiteral("map3DTrackLineWidthSpin"));
    auto* trackPointSizeSpin = window.findChild<QSpinBox*>(QStringLiteral("map3DTrackPointSizeSpin"));
    auto* heatLegendLabel = window.findChild<QLabel*>(QStringLiteral("map3DHeatLegendLabel"));
    QAction* trackLineVisibleAction =
        actionByName(window, QStringLiteral("map3DTrackLineVisibleAction"));
    QAction* trackPointsVisibleAction =
        actionByName(window, QStringLiteral("map3DTrackPointsVisibleAction"));
    QWidget* mapView = window.findChild<QWidget*>(QStringLiteral("map3DView"));
    auto* osgView = window.findChild<VaporView::Map3D::OsgEarthViewWidget*>();
    require(mapView != nullptr, "embedded map view exists");
    if (osgView)
    {
        require(osgView->focusPolicy() == Qt::StrongFocus,
                "embedded osgEarth view accepts keyboard focus for native camera navigation");
    }
    require(reloadBestMapAction->isEnabled(), "reload best local map action exists");
    for (auto* menu : moreMenu->findChildren<QMenu*>())
        for (auto* action : menu->actions())
            require(action->icon().isNull(), "More menu rows reserve icons for selection checks");
    require(flyToAircraftAction->isEnabled(), "fly to aircraft action exists");
    require(flyToTrackAction->isEnabled(), "fly to track action exists");
    require(resetViewAction->isEnabled(), "reset view action exists");
    require(diagnosticsAction->isEnabled(), "diagnostics action exists");
    require(!layersAction->icon().isNull(), "layers action uses a visible layers icon");
    require(layersAction->toolTip() == QStringLiteral("地图图层"),
            "layers action has a concise accessible label");
    auto* layersButton = window.findChild<QToolButton*>(QStringLiteral("map3DLayersButton"));
    require(layersButton != nullptr
                && layersButton->popupMode() == QToolButton::InstantPopup
                && layersButton->toolButtonStyle() == Qt::ToolButtonIconOnly,
            "clicking the layers icon opens the layer menu directly");
    require(layersButton->accessibleName() == QStringLiteral("地图图层"),
            "layers icon has an accessibility name");
    auto* layersMenu = qobject_cast<VaporView::SingleLevelPopupMenu*>(layersAction->menu());
    require(layersMenu != nullptr, "layers action uses the shared single-level popup menu");
    require(layersMenu->objectName() == QStringLiteral("map3DLayersMenu"),
            "layers menu has a stable object name");
    const QStringList expectedLayerLabels = {
        QStringLiteral("基础地理底图"),
        QStringLiteral("卫星遥感影像"),
        QStringLiteral("数字高程模型（DEM）"),
        QStringLiteral("水系"),
        QStringLiteral("道路网络"),
        QStringLiteral("三维建筑模型"),
        QStringLiteral("飞行任务要素（轨迹与飞行器）")
    };
    require(layersMenu->actions().size() == expectedLayerLabels.size(),
            "layers menu exposes the seven professional map layer groups");
    for (int index = 0; index < expectedLayerLabels.size(); ++index)
    {
        QAction* layerAction = layersMenu->actions().at(index);
        auto* widgetAction = qobject_cast<QWidgetAction*>(layerAction);
        auto* row = widgetAction
            ? qobject_cast<VaporView::SingleLevelPopupMenuRow*>(widgetAction->defaultWidget())
            : nullptr;
        require(layerAction->text() == expectedLayerLabels.at(index),
                "layer action uses the expected professional name");
        const bool expectedVisible = index == 0 || index == 1 || index == 2 || index == 6;
        require(layerAction->isCheckable() && layerAction->isChecked() == expectedVisible,
                "base map, satellite, DEM and flight elements start enabled");
        require(row && row->isChecked() == expectedVisible && !row->closeOnClick(),
                "layer row mirrors its check state and keeps the menu open for multi-selection");
        for (bool checked : {false, true})
        {
            row->setChecked(checked);
            require(row->property("hasCheckIcon").toBool() == checked &&
                    !row->property("hasUncheckedIcon").toBool() && row->checkLabel()->pixmap().isNull() == !checked,
                    "only selected layers display a check icon");
        }
        row->setChecked(expectedVisible);
    }

    window.resize(760, 520);
    window.show();
    QCoreApplication::processEvents();
    const QRect windowClientRect(window.mapToGlobal(window.rect().topLeft()), window.rect().size());
    auto* more = window.findChild<QMenu*>(QStringLiteral("map3DMoreMenu"));
    const auto originalPalette = qApp->palette();
    const auto originalTheme = qApp->property(VaporView::kAppDarkThemeProperty);
    for (bool dark : {false, true})
    {
        qApp->setProperty(VaporView::kAppDarkThemeProperty, dark);
        qApp->setPalette(VaporView::appThemePalette(dark));
        QCoreApplication::processEvents();
        require(cornerControls->styleSheet().contains(QStringLiteral("QToolButton:hover { background: ") +
                    VaporView::appThemeColorName(VaporView::AppThemeColor::TitleBarHover, dark)),
                "map corner buttons use the neutral hover background");
        more->popup(moreButton->mapToGlobal(QPoint(0, moreButton->height())));
        QCoreApplication::processEvents();
        if (dark)
        {
            imageryMenu->popup(window.mapToGlobal(QPoint(40, 120)));
            QCoreApplication::processEvents();
            require(hasMenuCheckMark(), "checked imagery source remains visible in the dark theme");
            imageryMenu->hide();
        }
        more->hide();
    }
    qApp->setProperty(VaporView::kAppDarkThemeProperty, originalTheme);
    qApp->setPalette(originalPalette);
    auto* locateButton = window.findChild<QToolButton*>(QStringLiteral("map3DLocateButton"));
    require(locateButton && !locateButton->icon().isNull(), "corner locate button has an icon");
    require(mapView->rect().contains(layersButton->parentWidget()->geometry()),
            "corner controls stay inside the map viewport");
    const QRect expectedPopupBounds = windowClientRect.adjusted(8, 8, -8, -8);
    layersMenu->popup(QPoint(windowClientRect.right() - 2, windowClientRect.bottom() - 2));
    QCoreApplication::processEvents();
    require(expectedPopupBounds.contains(layersMenu->geometry()),
            "layers menu stays inside the 3D map window in compact window mode");
    int layerMenuHideCount = 0;
    const auto hideConnection = QObject::connect(layersMenu, &QMenu::aboutToHide,
                                                [&]() { ++layerMenuHideCount; });
    auto* flightWidgetAction = qobject_cast<QWidgetAction*>(
        actionByName(window, QStringLiteral("map3DLayer_flightElements")));
    auto* flightRow = flightWidgetAction
        ? qobject_cast<VaporView::SingleLevelPopupMenuRow*>(flightWidgetAction->defaultWidget()) : nullptr;
    require(flightRow != nullptr, "flight layer row exists");
    const bool originalFlightChecked = flightRow->isChecked();
    const QRect originalMenuGeometry = layersMenu->geometry();
    for (int i = 0; i < 4; ++i)
    {
        flightRow->click();
        QCoreApplication::processEvents();
        require(flightRow->isChecked() == (i % 2 == 0 ? !originalFlightChecked : originalFlightChecked),
                "each layer row click toggles exactly once");
        require(layerMenuHideCount == 0 && layersMenu->isVisible() &&
                    layersMenu->geometry() == originalMenuGeometry,
                "layer clicks never hide or reposition the open popup");
    }
    QObject::disconnect(hideConnection);
    layersMenu->hide();

    QAction* satelliteLayerAction =
        actionByName(window, QStringLiteral("map3DLayer_satelliteImagery"));
    satelliteLayerAction->trigger();
    QCoreApplication::processEvents();
    {
        QSettings settings = map3DTestSettings();
        require(!settings.value(QStringLiteral("layers/satelliteImageryVisible"), true).toBool(),
                "satellite layer visibility is persisted when hidden");
    }
    satelliteLayerAction->trigger();
    QCoreApplication::processEvents();
    require(satelliteLayerAction->isChecked(),
            "satellite layer can be restored without closing the layer menu");
    require(localImageryAction->menu() != nullptr, "local imagery action has a menu");
    require(localImageryAction->menu() == imageryMenu,
            "local imagery action uses the cascading map menu");
    require(localImageryAction->isEnabled(),
            "imagery menu remains available to explain missing imagery sources");
    require(!clearLocal3DTilesAction->isEnabled(), "clear local 3D Tiles action starts disabled before preview load");
    require(loadAircraftModelAction->isEnabled(), "load aircraft model action exists");
    require(resetAircraftModelAction->isEnabled(), "reset aircraft model action exists");
    require(loadAircraftModelAction->toolTip().contains(QStringLiteral("飞机模型")),
            "load aircraft model action explains local model selection");
    require(resetAircraftModelAction->toolTip().contains(QStringLiteral("内置")),
            "reset aircraft model action explains built-in fallback");
    require(local3DTilesAction->toolTip().contains(QStringLiteral("地图诊断"))
                || local3DTilesAction->toolTip().contains(QStringLiteral("3D Tiles"))
                || local3DTilesAction->toolTip().contains(QStringLiteral("建筑瓦片")),
            "local 3D Tiles action explains its available or unavailable state");
    require(heatMetricCombo != nullptr && heatMetricCombo->count() == 7,
            "heat metric combo exposes peak, humidity, temperature, pressure, both water channels and lidar");
    require(heatPaletteCombo != nullptr && heatPaletteCombo->count() == 3
                && heatPaletteCombo->itemText(2) == QStringLiteral("反向光谱"),
            "heat palette combo exposes the three shared palette names");
    requireSingleLevelComboPopup(heatMetricCombo,
                                 "heat metric combo uses the shared single-level popup without overlapping rows");
    requireSingleLevelComboPopup(heatPaletteCombo,
                                 "heat palette combo uses the shared single-level popup without overlapping rows");
    require(trackLineVisibleAction->isCheckable() && trackLineVisibleAction->isChecked(),
            "track line visibility action starts enabled");
    require(trackPointsVisibleAction->isCheckable() && trackPointsVisibleAction->isChecked(),
            "track point visibility action starts enabled");
    require(trackLineWidthSpin != nullptr && trackLineWidthSpin->value() == 5,
            "track line width control starts at the default");
    require(trackPointSizeSpin != nullptr && trackPointSizeSpin->value() == 7,
            "track point size control starts at the default");
    require(trackPointSizeSpin->maximum() == 128, "track point size can be increased to 128 pixels");
    require(heatLegendLabel != nullptr
                && heatLegendLabel->text().contains(QStringLiteral("峰值"))
                && heatLegendLabel->text().contains(QStringLiteral("无有效数据")),
            "heat legend reports no valid data instead of a fake zero range");
    require(maxVisibleSamplesSpin != nullptr, "max visible samples spin box exists");
    require(maxVisibleSamplesSpin->minimum() == 1000, "max visible samples lower bound is 1000");
    require(maxVisibleSamplesSpin->maximum() == 1000000, "max visible samples upper bound is 1000000");

    heatMetricCombo->setCurrentText(QStringLiteral("温度"));
    heatPaletteCombo->setCurrentText(QStringLiteral("蓝红渐变"));
    trackLineVisibleAction->setChecked(false);
    trackPointsVisibleAction->setChecked(false);
    trackLineWidthSpin->setValue(9);
    trackPointSizeSpin->setValue(96);
    QCoreApplication::processEvents();
    {
        QSettings settings = map3DTestSettings();
        require(settings.value(QStringLiteral("heatMetric")).toInt() == 2,
                "heat metric setting is persisted");
        require(settings.value(QStringLiteral("heatPalette")).toInt() == 1,
                "heat palette setting is persisted");
        require(!settings.value(QStringLiteral("trackLineVisible"), true).toBool(),
                "track line visibility setting is persisted");
        require(!settings.value(QStringLiteral("trackPointsVisible"), true).toBool(),
                "track point visibility setting is persisted");
        require(settings.value(QStringLiteral("trackLineWidth")).toInt() == 9,
                "track line width setting is persisted");
        require(settings.value(QStringLiteral("trackPointSize")).toInt() == 96,
                "track point size setting is persisted");
    }
    require(heatLegendLabel->text().contains(QStringLiteral("温度")),
            "heat legend follows the selected metric");

    maxVisibleSamplesSpin->setValue(1000);
    {
        QSettings settings = map3DTestSettings();
        require(settings.value(QStringLiteral("maxVisibleSamples")).toInt() == 1000,
                "max visible sample setting is persisted");
    }

    std::vector<VaporView::Geo::NavSample> manySamples(1100);
    for (int i = 0; i < static_cast<int>(manySamples.size()); ++i)
    {
        manySamples[i].recordTimestampUs = static_cast<qint64>(i + 1) * 1000000;
        manySamples[i].latDeg = 39.9 + static_cast<double>(i) * 0.000001;
        manySamples[i].lonDeg = 116.3;
        manySamples[i].heightM = 45.0;
        manySamples[i].nedNM = static_cast<double>(i);
        manySamples[i].nedEM = 0.0;
        manySamples[i].nedDM = -45.0;
        manySamples[i].fixQuality = VaporView::Geo::FixQuality::Fixed;
        manySamples[i].satellites = 12;
        manySamples[i].hdop = 0.9;
    }
    window.appendSamples(manySamples);
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("采样点：1000/1100")),
            "max visible samples caps the visible status count");
    require(label->text().contains(QStringLiteral("来源 实时")), "live append batch reports live source");
    window.clearTrack();
    QCoreApplication::processEvents();

    VaporView::Geo::NavSample sample;
    sample.recordTimestampUs = 1000000;
    sample.deviceTimestampUs = 900000;
    sample.latDeg = 39.9;
    sample.lonDeg = 116.3;
    sample.heightM = 45.0;
    sample.satellites = 12;
    sample.hdop = 0.9;
    sample.fixQuality = VaporView::Geo::FixQuality::Fixed;
    window.appendSample(sample);
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("采样点：1")), "appendSample updates status");
    require(label->text().contains(QStringLiteral("来源 实时")), "appendSample reports live source");
    require(label->text().contains(QStringLiteral("定位 固定解")),
            "status reports readable GNSS fix quality");
    require(!label->text().contains(QStringLiteral("记录时间")) && !label->text().contains(QStringLiteral("HDOP"))
            && !label->text().contains(QStringLiteral("质量 固定解")) && !label->text().contains(QStringLiteral("视口")),
            "status leaves detailed telemetry and performance to diagnostics");
    const auto requireDiagnostics = [&](const QString& detail, const char* failure) {
        window.showMapDiagnostics();
        auto* text = window.findChild<QPlainTextEdit*>();
        require(text && text->toPlainText().contains(detail), failure);
        text->window()->hide();
    };
    requireDiagnostics(QStringLiteral("最新记录时间戳 us： 1000000"), "diagnostics include latest record timestamp");
    requireDiagnostics(QStringLiteral("最新设备时间戳 us： 900000"), "diagnostics include latest device timestamp");
    requireDiagnostics(QStringLiteral("卫星数 12"), "diagnostics include satellite count");
    requireDiagnostics(QStringLiteral("HDOP 0.90"), "diagnostics include latest sample HDOP");
    requireDiagnostics(QStringLiteral("姿态来源： 无"), "diagnostics report absent attitude source");

    window.clearTrack();
    QCoreApplication::processEvents();

    VaporView::Geo::NavSample eulerSample = sample;
    eulerSample.yawDeg = 91.0;
    window.appendSample(eulerSample);
    QCoreApplication::processEvents();
    requireDiagnostics(QStringLiteral("姿态来源： 欧拉角"), "diagnostics report Euler attitude source");

    window.clearTrack();
    QCoreApplication::processEvents();

    VaporView::Geo::NavSample quaternionSample = sample;
    quaternionSample.quatW = 1.0;
    quaternionSample.quatX = 0.0;
    quaternionSample.quatY = 0.0;
    quaternionSample.quatZ = 0.0;
    window.appendSample(quaternionSample);
    QCoreApplication::processEvents();
    requireDiagnostics(QStringLiteral("姿态来源： 四元数"), "diagnostics report quaternion attitude source");

    window.clearTrack();
    window.appendSample(sample);
    VaporView::Geo::NavSample throttledSample = sample;
    throttledSample.recordTimestampUs = 2000000;
    throttledSample.deviceTimestampUs = 1900000;
    throttledSample.latDeg = 39.91;
    window.appendSample(throttledSample);
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("采样点：1/1")),
            "live status updates are throttled below 5 Hz");
    QThread::msleep(220);
    QCoreApplication::processEvents();
    throttledSample.recordTimestampUs = 3000000;
    throttledSample.deviceTimestampUs = 2900000;
    throttledSample.latDeg = 39.92;
    window.appendSample(throttledSample);
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("采样点：3/3")),
            "live status updates refresh after the 5 Hz throttle interval");

    window.clearTrack();
    QCoreApplication::processEvents();

    require(label->text().contains(QStringLiteral("采样点：0")), "clearTrack resets status");
    require(label->text().contains(QStringLiteral("来源 无")), "clearTrack resets track source");

    QTemporaryDir sessionDir;
    writeSessionTrack(sessionDir);
    auto* loadingPool = QThreadPool::globalInstance();
    require(loadingPool->waitForDone(5000), "previous map workers have finished");
    const int previousPoolSize = loadingPool->maxThreadCount();
    loadingPool->setMaxThreadCount(1);
    QSemaphore poolBusy, releasePool;
    loadingPool->start(QRunnable::create([&]() {
        poolBusy.release();
        releasePool.tryAcquire(1, 15000);
    }));
    require(poolBusy.tryAcquire(1, 5000), "test holds the worker until the progress UI is inspected");
    window.loadSessionDirectory(sessionDir.path());
    QCoreApplication::processEvents();
    auto* progress = window.findChild<QProgressDialog*>(QStringLiteral("map3DSessionProgress"));
    auto* modeButtonDuringLoad = window.findChild<QToolButton*>(QStringLiteral("map3DModeButton"));
    require(progress && progress->isVisible() && progress->windowFlags().testFlag(Qt::FramelessWindowHint)
                && progress->findChild<QWidget*>(QStringLiteral("customTitleBar")),
            "Session loading uses the shared custom title bar");
    auto* progressContent = progress->findChild<QWidget*>(QStringLiteral("customTitleBarContent"));
    auto* progressText = progress->findChild<QLabel*>(QStringLiteral("map3DSessionProgressLabel"));
    auto* progressBar = progress->findChild<QProgressBar*>(QStringLiteral("map3DSessionProgressBar"));
    require(progressContent && progressText && progressBar && progressText->isVisible() && progressBar->isVisible()
                && progressText->parentWidget() == progressContent && progressBar->parentWidget() == progressContent
                && progressContent->rect().contains(progressText->geometry())
                && progressContent->rect().contains(progressBar->geometry()),
            "Session progress text and bar are visible inside the themed content");
    require(progress->value() == 0 && progressBar->value() == 0,
            "Session progress starts at zero while the worker is waiting");
    progress->setLabelText(QStringLiteral("正在读取 Session 数据…"));
    progress->setValue(43);
    require(progressBar->value() == 43, "Session progress updates the visible progress bar");
    progress->setLabelText(QStringLiteral("波形关联完成；生成显示数据…"));
    progress->setValue(80);
    require(progressText->text() == QStringLiteral("波形关联完成；生成显示数据…")
                && progressBar->value() == 80,
            "Session progress presents the current loading stage and percentage");
    require(modeButtonDuringLoad && modeButtonDuringLoad->isEnabled()
                && modeButtonDuringLoad->height() == 34
                && modeButtonDuringLoad->toolButtonStyle() == Qt::ToolButtonTextBesideIcon
                && modeButtonDuringLoad->font().pixelSize() == 16
                && modeButtonDuringLoad->iconSize() == QSize(22, 22)
                && !modeButtonDuringLoad->icon().isNull()
                && modeButtonDuringLoad->minimumWidth()
                    >= modeButtonDuringLoad->fontMetrics().horizontalAdvance(modeButtonDuringLoad->text())
                        + modeButtonDuringLoad->iconSize().width() + 24
                && modeButtonDuringLoad->styleSheet().contains(QStringLiteral("background: transparent"))
                && modeButtonDuringLoad->styleSheet().contains(QStringLiteral(":hover")),
            "the title-bar return-to-live action has the shared compact transparent style");
    QImage lightModeIcon;
    for (bool dark : {false, true})
    {
        qApp->setProperty(VaporView::kAppDarkThemeProperty, dark);
        qApp->setPalette(VaporView::appThemePalette(dark));
        QCoreApplication::processEvents();
        require(progressContent->rect().contains(progressText->geometry())
                    && progressContent->rect().contains(progressBar->geometry()),
                "Session progress stays inside its content after a theme change");
        const QImage contentImage = progressContent->grab().toImage();
        require(contentImage.pixelColor(3, 3) == VaporView::appThemeColor(
                    dark ? VaporView::AppThemeColor::Window : VaporView::AppThemeColor::Surface, dark),
                "Session loading content follows the active light or dark theme");
        require(!progress->styleSheet().contains(QStringLiteral("@vv-"))
                    && !modeButtonDuringLoad->styleSheet().contains(QStringLiteral("@vv-")),
                "Session progress and title-bar action resolve all theme tokens");
        const QImage modeIcon = modeButtonDuringLoad->icon().pixmap(QSize(22, 22), QIcon::Normal).toImage();
        require(!modeIcon.isNull(), "the title-bar mode icon is rendered");
        if (dark)
            require(modeIcon != lightModeIcon, "the title-bar mode icon follows the active theme");
        else
            lightModeIcon = modeIcon;
    }
    qApp->setProperty(VaporView::kAppDarkThemeProperty, originalTheme);
    qApp->setPalette(originalPalette);
    QCoreApplication::processEvents();
    releasePool.release();
    loadingPool->setMaxThreadCount(previousPoolSize);
    VaporView::Map3D::Map3DLiveTelemetrySnapshot replayLive;
    replayLive.calibrationEpoch = 9;
    replayLive.waterUnit = QStringLiteral("mol/mol");
    window.setLiveTelemetrySnapshot(replayLive);
    VaporView::Geo::TrajectoryRenderSample liveSample{sample, {}};
    liveSample.heat.calibrationEpoch = replayLive.calibrationEpoch;
    liveSample.heat.columnWaterConcentration = 0.000321;
    liveSample.heat.lidarDistanceM = 42.5;
    window.appendRenderSamples({liveSample, liveSample});
    window.appendSample(sample);
    require(waitForText(label, QStringLiteral("来源 会话"), 3000),
            "asynchronous session load completes without blocking the window");
    auto *navigationSource = window.findChild<QComboBox *>(QStringLiteral("map3DNavigationSourceCombo"));
    require(navigationSource && navigationSource->isEnabled() && navigationSource->count() == 2,
            "Session exposes the shared Original/PPK navigation source");
    auto* navigationModel = qobject_cast<QStandardItemModel*>(navigationSource->model());
    require(navigationModel && navigationModel->item(1), "navigation source model exposes the PPK entry");
    navigationModel->item(1)->setEnabled(true);
    navigationSource->setCurrentIndex(1);
    require(navigationSource->currentIndex() == 0 && refreshStatus->text() == QStringLiteral("刷新失败"),
            "an unavailable PPK source reports failure and restores the active selection");
    QDir sessionRoot(sessionDir.path());
    require(sessionRoot.mkpath(QStringLiteral("ppk")), "create temporary PPK fixtures");
    QFile quality(sessionRoot.filePath(QStringLiteral("ppk/ppk_quality.json")));
    require(quality.open(QIODevice::WriteOnly), "create PPK quality fixture");
    quality.write("{\"state\":\"Completed\"}"); quality.close();
    QFile corrected(sessionRoot.filePath(QStringLiteral("ppk/trajectory.csv")));
    require(corrected.open(QIODevice::WriteOnly), "create PPK trajectory fixture");
    corrected.write("gnss_utc_ns,session_timestamp_us,latitude,longitude,ellipsoidal_height,ecef_x,ecef_y,ecef_z,quality,satellite_count,sd_n,sd_e,sd_u,age,ratio,source,reference_point\n"
                    "1000000000,1000000,39.9,116.3,45,-2170992.035,4392668.582,4069502.541,1,12,0.1,0.1,0.1,0,1,PPK,IMU\n"
                    "2000000000,2000000,39.90001,116.30002,46,-2170993.592,4392667.874,4069504.034,1,12,0.1,0.1,0.1,0,1,PPK,IMU\n");
    corrected.close();
    navigationSource->setCurrentIndex(1);
    require(refreshStatus->text() == QStringLiteral("刷新中…"), "source switch reports the queued reload");
    require(waitForText(refreshStatus, QStringLiteral("已刷新"), 3000), "source switch reports refresh only after loading");
    navigationSource->setCurrentIndex(0);
    require(refreshStatus->text() == QStringLiteral("刷新中…") &&
                waitForText(refreshStatus, QStringLiteral("已刷新"), 3000),
            "switching back to the original trajectory also completes asynchronously");
    {
        QSettings settings = map3DTestSettings();
        require(settings.value(QStringLiteral("lastSessionDir")).toString()
                    == QFileInfo(sessionDir.path()).absoluteFilePath(),
                "programmatic session load persists last session directory");
    }
    require(label->text().contains(QStringLiteral("采样点：2/2")), "session load keeps the complete track visible");
    require(label->text().contains(QStringLiteral("来源 会话")), "session load reports session source");
    diagnosticsAction->trigger();
    QCoreApplication::processEvents();
    auto* diagnosticsText = window.findChild<QPlainTextEdit*>();
    require(diagnosticsText != nullptr, "diagnostics text view exists");
    require(diagnosticsText->window()->findChild<QWidget*>(QStringLiteral("customTitleBar")),
            "map diagnostics uses the shared title bar");
    auto* buttons = diagnosticsText->parentWidget()->findChild<QDialogButtonBox*>();
    require(buttons && buttons->button(QDialogButtonBox::Close)->text() == QStringLiteral("关闭"),
            "diagnostics close button is localized");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("轨迹数据：")),
            "diagnostics include track data section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("地图场景加载：")),
            "diagnostics include earth runtime load section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("本地原生 OSG 建筑加载：")),
            "diagnostics include local native OSG building load section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("已清除原有预览：")),
            "diagnostics report whether a local 3D Tiles load removed an old preview");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("飞行器模型：")),
            "diagnostics include aircraft model section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("请求路径： <无>")),
            "diagnostics report no custom aircraft model in headless smoke test");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("内置标记： 是")),
            "diagnostics report built-in aircraft marker fallback");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("模式：")),
            "diagnostics include selected map mode");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("地图场景文件：")),
            "diagnostics include selected earth file");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Natural Earth 纹理：")),
            "diagnostics include Natural Earth texture path");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("本地网格回退：")),
            "diagnostics include local grid fallback availability");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Copernicus DEM VRT:")),
            "diagnostics include Copernicus DEM VRT path");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("SRTM VRT:")),
            "diagnostics include SRTM VRT path");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("底图优先级： 本地实景三维 > Copernicus DEM > SRTM > Natural Earth > 本地网格")),
            "diagnostics include the base map selection priority");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("已选底图模式：")),
            "diagnostics include selected base mode");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("OSM 图层约定：")),
            "diagnostics include expected OSM layer contract section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("layer roads")),
            "diagnostics include expected OSM roads GeoPackage layer name");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("仅生成数据")),
            "diagnostics include safe-default OSM generated-data contract");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("OSG_LIBRARY_PATH:")),
            "diagnostics include OSG plugin environment");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("osgEarth 环境变量：")),
            "diagnostics include osgEarth environment variable list");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("GDAL_DATA:")),
            "diagnostics include GDAL data environment");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("PROJ_DATA:")),
            "diagnostics include PROJ data environment");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("已尝试加载：")),
            "diagnostics include earth load attempt state");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("渲染性能：")),
            "diagnostics include render performance section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("显示帧间隔 P95"))
                && diagnosticsText->toPlainText().contains(QStringLiteral("卫星影像请求/失败次数")),
            "diagnostics distinguish presentation timing from satellite source failures");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("本地影像菜单：")),
            "diagnostics include local imagery menu availability");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("可选本地影像 VRT：")),
            "diagnostics include optional local imagery VRT count");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("可从菜单加载的本地影像：")),
            "diagnostics include menu-ready optional local imagery count");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("原生 OSG 建筑瓦片格式约定：")),
            "diagnostics include native OSG building tile contract status");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("原生 OSG 建筑瓦片集：")),
            "diagnostics include native OSG building tileset path");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("轨迹质量：")),
            "diagnostics include trajectory quality section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("固定解： 2")),
            "diagnostics include fixed quality count");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("可见轨迹线采样点： 2")),
            "diagnostics include visible line sample count");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("采样点：")),
            "diagnostics include visible and total sample counts");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("来源： 会话")),
            "diagnostics include latest track source");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("回放状态： 已暂停")),
            "diagnostics include explicit replay state");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("回放位置： 2/2")),
            "diagnostics include replay position");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("回放速度：")),
            "diagnostics include replay speed");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("回放时间： t 1.000/1.000 s")),
            "diagnostics include replay elapsed time");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("姿态来源： 无")),
            "diagnostics include attitude source");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("跟随飞行器： 关闭")),
            "diagnostics include follow camera state");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("高度使用说明： 高度基准仅用于显示")),
            "diagnostics include height reference safety note");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("定位质量： 固定解")),
            "diagnostics include readable GNSS fix quality");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("视角： 自动轨迹视角")),
            "diagnostics include latest automatic camera action");

    flyToAircraftAction->trigger();
    QCoreApplication::processEvents();
    require(diagnosticsText->toPlainText().contains(QStringLiteral("视角： 飞行器")),
            "fly to aircraft updates persistent camera status");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("纬度 39.9000100")),
            "camera action preserves latest sample status details");

    flyToTrackAction->trigger();
    QCoreApplication::processEvents();
    require(diagnosticsText->toPlainText().contains(QStringLiteral("视角： 轨迹")),
            "fly to track updates persistent camera status");

    resetViewAction->trigger();
    QCoreApplication::processEvents();
    require(diagnosticsText->toPlainText().contains(QStringLiteral("视角： 重置")),
            "reset view updates persistent camera status");

    window.noteLiveSampleDrop(QStringLiteral("实时"), QStringLiteral("missing LLH"), 123456);
    QCoreApplication::processEvents();
    require(diagnosticsText->toPlainText().contains(QStringLiteral("最近丢弃原因： missing LLH")),
            "diagnostics include latest live sample drop reason");

    QTemporaryDir supersededSessionDir;
    QTemporaryDir latestSessionDir;
    writeSessionTrack(supersededSessionDir);
    writeSessionTrack(latestSessionDir);
    window.loadSessionDirectory(supersededSessionDir.path());
    window.loadSessionDirectory(latestSessionDir.path());
    require(waitForSessionDirectory(latestSessionDir.path(), 3000),
            "the latest asynchronous session request wins over an older completed request");
    require(waitForText(label, QStringLiteral("来源 会话"), 1000),
            "the newest asynchronous session request applies its track on the GUI thread");
    window.appendSample(sample);
    require(navigationSource->isEnabled(), "live navigation cannot take over Session playback");
    auto* modeButton = window.findChild<QToolButton*>(QStringLiteral("map3DModeButton"));
    require(modeButton && modeButton->parentWidget()->objectName() == QStringLiteral("customTitleBar"),
            "map mode switch lives in the title bar");
    require(modeButton->text() == QStringLiteral("回到实时地图"), "Session shows a return-to-live button");
    auto* sessionBanner = window.findChild<QLabel*>(QStringLiteral("map3DSessionBanner"));
    require(sessionBanner && !sessionBanner->isHidden() && sessionBanner->text().contains(QStringLiteral("地图显示历史记录")),
            "sidebar identifies live readings alongside the historical map");
    const QImage returnToLiveIcon = modeButton->icon().pixmap(QSize(22, 22), QIcon::Normal).toImage();
    require(label->text().contains(QStringLiteral("采样点：2")), "live input does not append to the Session track");
    modeButton->click();
    require(!navigationSource->isEnabled(), "live mode disables the Session source selector");
    require(sessionBanner->isHidden(), "live mode clears the Session banner");
    require(modeButton->text() == QStringLiteral("返回 Session 回放"), "live mode offers the cached Session");
    require(modeButton->icon().pixmap(QSize(22, 22), QIcon::Normal).toImage() != returnToLiveIcon,
            "mode switch icons distinguish live navigation from Session playback");
    require(label->text().contains(QStringLiteral("采样点：4")), "returning live includes positions received during playback");
    auto* restoredLegend = window.findChild<QLabel*>(QStringLiteral("map3DMainHeatLegendText"));
    const int savedMetric = heatMetricCombo->currentIndex();
    heatMetricCombo->setCurrentIndex(4);
    require(restoredLegend->text().contains(QStringLiteral("0.000321 – 0.000321 mol/mol")) && restoredLegend->text().contains(QStringLiteral("n=2")),
            "return to live restores frozen concentrations, units and live calibration epoch");
    heatMetricCombo->setCurrentIndex(6);
    require(restoredLegend->text().contains(QStringLiteral("42.5 – 42.5 m")) && restoredLegend->text().contains(QStringLiteral("n=2")),
            "return to live preserves auxiliary lidar measurements");
    heatMetricCombo->setCurrentIndex(savedMetric);
    window.appendSample(sample);
    modeButton->click();
    require(label->text().contains(QStringLiteral("采样点：2")), "returning to Session restores its original track");
    require(navigationSource->isEnabled(), "returning to Session restores the source selector");
    modeButton->click();
    window.clearTrack();
    require(!navigationSource->isEnabled(), "cleared navigation has no active Session source selector");
    require(modeButton->isEnabled(), "clearing live keeps the Session cache available");
    modeButton->click();
    require(label->text().contains(QStringLiteral("采样点：2")), "clearing live leaves Session data intact");

    QTemporaryDir loadingSessionDir;
    writeSessionTrack(loadingSessionDir);
    window.loadSessionDirectory(loadingSessionDir.path());
    modeButton->click();
    window.appendSample(sample);
    require(waitForSessionDirectory(loadingSessionDir.path(), 3000), "loading completes after returning live");
    require(modeButton->text() == QStringLiteral("返回 Session 回放") && !navigationSource->isEnabled(),
            "background load completion does not take over the live map");
    modeButton->click();
    require(label->text().contains(QStringLiteral("采样点：2")), "background-loaded Session can be restored");
    qApp->setProperty("vaporViewEnglish", true);
    QCoreApplication::processEvents();
    require(modeButton->text() == QStringLiteral("Back to live map"), "map mode switch translates to English");
    qApp->setProperty("vaporViewEnglish", false);
    QCoreApplication::processEvents();
    window.close();
    window.show();
    require(!modeButton->isEnabled() && !navigationSource->isEnabled(), "closing discards the Session cache");

    // Force cancellation before the worker can publish any result.
    auto* pool = QThreadPool::globalInstance();
    require(pool->waitForDone(5000), "previous Session jobs have finished");
    const int savedThreadCount = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    pool->reserveThread();
    {
        VaporView::Map3D::Map3DWindow cancelledWindow;
        cancelledWindow.loadSessionDirectory(latestSessionDir.path());
        cancelledWindow.close();
        pool->releaseThread();
        require(pool->waitForDone(5000), "cancelled worker finishes");
        QCoreApplication::processEvents();
        require(!cancelledWindow.findChild<QToolButton*>(QStringLiteral("map3DModeButton"))->isEnabled(),
                "cancelled result cannot repopulate a closed map");
    }
    pool->setMaxThreadCount(savedThreadCount);

    auto* closingWindow = new VaporView::Map3D::Map3DWindow;
    closingWindow->loadSessionDirectory(latestSessionDir.path());
    delete closingWindow;
    require(pool->waitForDone(5000), "Session worker safely finishes after window destruction");
    QCoreApplication::processEvents();

    checkOnlineResourceEditor();
    return 0;
}
