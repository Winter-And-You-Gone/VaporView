#include "test_settings_sandbox.h"
#include "ground/main/MainWindow.h"
#include "map3d/OsgEarthViewWidget.h"
#include "map3d/Map3DWindow.h"
#include "map3d/MapLanguage.h"

#include <QAction>
#include <QToolButton>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QDialog>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QOpenGLContext>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>
#include <QWindow>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QProcess>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <functional>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const QString& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message.toStdString() << '\n';
        std::exit(1);
    }
}

void processEventsFor(int timeoutMs)
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

QAction* findActionByText(QWidget* root, const QStringList& expectedTexts)
{
    const QList<QAction*> actions = root->findChildren<QAction*>();
    for (QAction* action : actions)
    {
        if (action && expectedTexts.contains(action->text()))
        {
            return action;
        }
    }
    return nullptr;
}

QWidget* findMap3DWindow()
{
    const QWidgetList topLevelWidgets = QApplication::topLevelWidgets();
    for (QWidget* widget : topLevelWidgets)
    {
        if (widget && widget->objectName() == QStringLiteral("map3DWindow"))
        {
            return widget;
        }
    }
    return nullptr;
}

class OpenProbe final : public QObject
{
public:
    QElapsedTimer clock;
    qint64 shown=-1,exposed=-1,loadingPaint=-1;
    std::function<void(VaporView::Map3D::OsgEarthViewWidget*)> viewShown;
    bool eventFilter(QObject* object,QEvent* event) override
    {
        if(event->type()==QEvent::Show && object->objectName()=="map3DWindow" && shown<0)shown=clock.elapsed();
        if(event->type()==QEvent::Paint && object->objectName()=="map3DRenderPlaceholder" && loadingPaint<0)loadingPaint=clock.elapsed();
        if(event->type()==QEvent::Show && object->objectName()=="map3DView" && viewShown)
            if(auto* view=qobject_cast<VaporView::Map3D::OsgEarthViewWidget*>(object))viewShown(view);
        if(event->type()==QEvent::Expose && exposed<0) {
            auto* map=findMap3DWindow();
            if(auto* window=qobject_cast<QWindow*>(object);window && map && window->isExposed()) {
                for(auto* ancestor=window;ancestor;ancestor=ancestor->parent())
                    if(ancestor==map->windowHandle()){exposed=clock.elapsed();break;}
            }
        }
        return false;
    }
};

void measureOpen(QApplication& app,QAction* action,const char* kind,int iteration)
{
    OpenProbe probe;app.installEventFilter(&probe);
    auto* window=findMap3DWindow();
    auto* view=window?window->findChild<VaporView::Map3D::OsgEarthViewWidget*>("map3DView"):nullptr;
    qint64 firstFrame=-1,ready=-1,responsive=-1;
    const auto observeFrame=[&] {
        return QObject::connect(view,&QOpenGLWidget::frameSwapped,&probe,[&]{
            if(firstFrame<0 && view->isRenderingStarted() && !view->framebufferSize().isEmpty()) {
                firstFrame=probe.clock.elapsed();if(ready<0)ready=firstFrame;
                QTimer::singleShot(0,&probe,[&]{responsive=probe.clock.elapsed();});
            }
        });
    };
    // Reopening an existing renderer can present synchronously inside show().
    // Observe it before triggering, otherwise the first warm frame is missed.
    QMetaObject::Connection connection;if(view)connection=observeFrame();
    probe.viewShown=[&](auto* created){if(!view){view=created;connection=observeFrame();}};
    probe.clock.start();action->trigger();const qint64 returned=probe.clock.elapsed();
    window=findMap3DWindow();require(window && window->isVisible(),"map is visible after action");
    const auto nativeHandle=window->internalWinId();
    while(probe.clock.elapsed()<10000 && (responsive<0 || !view || !view->earthLoadDiagnostics().attempted)) {
        QCoreApplication::processEvents(QEventLoop::AllEvents,10);
        if(!view) {
            view=window->findChild<VaporView::Map3D::OsgEarthViewWidget*>("map3DView");
            if(view)connection=observeFrame();
        }
        if(view && ready<0 && view->isRenderingStarted() && view->context() && view->context()->isValid()
            && !view->framebufferSize().isEmpty())ready=probe.clock.elapsed();
        QThread::msleep(1);
    }
    QObject::disconnect(connection);app.removeEventFilter(&probe);
    std::cout<<QJsonDocument(QJsonObject{{"kind",kind},{"iteration",iteration},{"action_return_ms",returned},
        {"window_show_ms",probe.shown},{"window_expose_ms",probe.exposed},{"renderer_ready_ms",ready},
        {"loading_paint_ms",probe.loadingPaint},{"first_presented_ms",firstFrame},{"responsive_after_frame_ms",responsive}}).toJson(QJsonDocument::Compact).constData()<<std::endl;
    require(returned<750,QStringLiteral("map action retains 750 ms gate, elapsed %1").arg(returned));
    require(window->internalWinId()==nativeHandle,"starting GL preserves the existing native map window");
    if(QLatin1String(kind)=="cold")require(probe.loadingPaint>=0 && probe.loadingPaint<firstFrame,
        "loading state actually paints before GL driver/scene initialization completes");
    require(probe.shown>=0 && probe.exposed>=0 && ready>=0 && firstFrame>=0 && responsive>=0,
            "real window exposure, valid GL renderer, presented frame and responsive event loop complete");
    require(view->earthLoadDiagnostics().attempted,"normal asynchronous Earth loading starts");
    window->close();processEventsFor(100);
}

bool performanceMatrix()
{
    QMap<QString,QVector<qint64>> samples;bool success=true;
    for(int run=0;run<5;++run) {
        QProcess child;child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(QCoreApplication::applicationFilePath(),{"--performance"});
        if(!child.waitForStarted(10000))return false;
        QElapsedTimer timeout;timeout.start();
        while(!child.waitForFinished(100))if(timeout.elapsed()>60000) {
            child.kill();child.waitForFinished(5000);return false;
        }
        const auto output=child.readAll();std::cout<<output.constData()<<std::flush;
        success &= child.exitStatus()==QProcess::NormalExit && child.exitCode()==0;
        for(const auto& line:output.split('\n')) {
            const auto data=QJsonDocument::fromJson(line).object();if(!data.contains("kind"))continue;
            for(const auto& metric:{"action_return_ms","window_show_ms","window_expose_ms","renderer_ready_ms","first_presented_ms","responsive_after_frame_ms"})
                samples[data["kind"].toString()+'/'+QLatin1String(metric)].append(data[metric].toInteger());
        }
    }
    for(auto it=samples.begin();it!=samples.end();++it) {
        auto values=it.value();std::sort(values.begin(),values.end());QJsonArray distribution;
        for(auto value:values)distribution.append(value);
        std::cout<<QJsonDocument(QJsonObject{{"metric",it.key()},{"samples",distribution},
            {"median_ms",values[values.size()/2]},{"p95_ms",values[int(std::ceil(values.size()*0.95))-1]},
            {"max_ms",values.last()}}).toJson(QJsonDocument::Compact).constData()<<std::endl;
    }
    return success && samples["cold/action_return_ms"].size()==5 && samples["warm/action_return_ms"].size()==25;
}

} // namespace

int main(int argc, char** argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), QStringLiteral("temporary settings directory"));
    qputenv("OSGEARTH_CACHE_PATH", settingsDir.filePath(QStringLiteral("map-cache")).toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewMap3DOpenTest"));
    app.setApplicationName(QStringLiteral("main_window_map3d_open_test"));
    if(app.arguments().contains("--performance-matrix")) {
        require(performanceMatrix(),"five fresh processes and 25 warm opens retain all timing/rendering gates");return 0;
    }

    const QString originalPath = QStringLiteral("  请求路径： X:/地图/会话%2.earth");
    const QString translatedPath = VaporView::Map3D::mapRenderedText(originalPath, true);
    require(translatedPath == QStringLiteral("  Requested path: X:/地图/会话%2.earth"),
            QStringLiteral("translation preserves Chinese paths and literal placeholders"));
    require(VaporView::Map3D::mapRenderedText(translatedPath, false) == originalPath,
            QStringLiteral("diagnostic path round trip is lossless"));

    MainWindow window;
    window.resize(1000, 700);
    window.show();
    processEventsFor(200);

    QAction* mapAction = findActionByText(&window,
                                          {QStringLiteral("三维地图"),
                                           QStringLiteral("3D Map")});
    require(mapAction != nullptr, QStringLiteral("3D map action exists"));

    if(app.arguments().contains("--performance")) {
        measureOpen(app,mapAction,"cold",0);
        for(int iteration=0;iteration<5;++iteration)measureOpen(app,mapAction,"warm",iteration);
        window.close();processEventsFor(100);return 0;
    }

    QElapsedTimer triggerTimer;
    triggerTimer.start();
    mapAction->trigger();
    const qint64 triggerMs = triggerTimer.elapsed();
    require(triggerMs < 750,
            QStringLiteral("3D map action should return quickly, elapsed %1 ms").arg(triggerMs));
    auto* openingMap = qobject_cast<VaporView::Map3D::Map3DWindow*>(findMap3DWindow());
    require(openingMap && !openingMap->findChild<VaporView::Map3D::OsgEarthViewWidget*>(),
            QStringLiteral("loading window opens before constructing the GL renderer"));
    VaporView::Geo::NavSample pendingSample;
    pendingSample.recordTimestampUs = 1000000;
    pendingSample.latDeg = 31.2;
    pendingSample.lonDeg = 121.5;
    pendingSample.heightM = 100;
    openingMap->appendSample(pendingSample);
    processEventsFor(250);

    QWidget* mapWindow = findMap3DWindow();
    require(mapWindow != nullptr && mapWindow->isVisible(),
            QStringLiteral("3D map window opens from MainWindow"));

    QLabel* mapStatusLabel = mapWindow->findChild<QLabel*>(QStringLiteral("map3DStatusLabel"));
    require(mapStatusLabel != nullptr, QStringLiteral("3D map status label exists"));
    QLabel* renderPlaceholder = mapWindow->findChild<QLabel*>(
        QStringLiteral("map3DRenderPlaceholder"));
    require(renderPlaceholder != nullptr, QStringLiteral("render failure placeholder exists"));
    require(mapWindow->findChild<QAction*>(QStringLiteral("map3DStartRenderingAction")) == nullptr,
            QStringLiteral("manual start action is removed"));
    processEventsFor(3000);
    auto* view = mapWindow->findChild<VaporView::Map3D::OsgEarthViewWidget*>(
        QStringLiteral("map3DView"));
    require(view != nullptr && view->isVisible(),
            QStringLiteral("3D map starts the real renderer without closing the window"));
    require(view->isRenderingStarted(),
            QStringLiteral("3D map activates osgEarth automatically when opened"));
    require(!view->framebufferSize().isEmpty(),
            QStringLiteral("3D map initializes an OpenGL framebuffer after rendering starts"));
    require(view->earthLoadDiagnostics().attempted,
            QStringLiteral("3D map starts loading the built-in Earth scene after rendering starts"));
    require(view->sampleCount() == 1,
            QStringLiteral("live navigation arriving before GL initialization is retained"));
    const QImage framebuffer = view->grabFramebuffer();
    require(!framebuffer.isNull(), QStringLiteral("real GL framebuffer can be read after startup"));

    auto* notice = mapWindow->findChild<QLabel*>(QStringLiteral("map3DNoticeBubble"));
    require(notice && !notice->isVisible(), QStringLiteral("imagery notice is hidden until needed"));
    emit view->imageryFallbackNotice();
    processEventsFor(100);
    require(notice->isVisible() && notice->text().contains(QStringLiteral("影像")),
            QStringLiteral("missing imagery shows a temporary notice above the real renderer"));
    require(notice->testAttribute(Qt::WA_TransparentForMouseEvents),
            QStringLiteral("imagery notice does not intercept map interaction"));
    mapWindow->resize(850, 650);
    processEventsFor(100);
    require(notice->parentWidget()->rect().contains(notice->geometry())
                && notice->y() == 12,
            QStringLiteral("imagery notice remains inside the top of the resized map"));
    // Repeated tile failures must not restart the lifetime of the bubble.
    for (int i = 0; i < 5; ++i)
    {
        processEventsFor(1000);
        emit view->imageryFallbackNotice();
    }
    require(!notice->isVisible(), QStringLiteral("notice expires even while tile failures continue"));

    emit view->renderingFailed(QStringLiteral("test renderer failure"));
    processEventsFor(100);
    require(mapWindow->isVisible() && renderPlaceholder->isVisible()
                && renderPlaceholder->text().contains(QStringLiteral("test renderer failure")),
            QStringLiteral("render failure leaves the window open with a diagnostic"));
    auto* cornerControls = mapWindow->findChild<QWidget*>(QStringLiteral("map3DCornerControls"));
    require(cornerControls && cornerControls->isVisible()
                && cornerControls->parentWidget()->rect().contains(cornerControls->geometry()),
            QStringLiteral("map controls remain visible and contained after renderer failure"));

    auto* diagnosticsAction = mapWindow->findChild<QAction*>(QStringLiteral("map3DDiagnosticsAction"));
    diagnosticsAction->trigger();
    processEventsFor(100);
    auto* diagnostics = mapWindow->findChild<QPlainTextEdit*>();
    QToolButton* languageButton = nullptr;
    for (auto* button : mapWindow->findChildren<QToolButton*>())
        if (button->accessibleName() == QStringLiteral("titleLanguageButton")
            && button->window() == mapWindow) languageButton = button;
    require(languageButton && diagnostics, QStringLiteral("map language button and diagnostics exist"));
    mapWindow->findChild<QAction*>(QStringLiteral("map3DDisplayAction"))->trigger();
    mapWindow->findChild<QAction*>(QStringLiteral("map3DMapFilesAction"))->trigger();
    mapWindow->findChild<QAction*>(QStringLiteral("map3DMapResourcesAction"))->trigger();
    processEventsFor(100);
    auto* metricCombo = mapWindow->findChild<QComboBox*>(QStringLiteral("map3DHeatMetricCombo"));
    const int originalMetric = metricCombo->currentIndex();
    const bool originalEnglish = qApp->property("vaporViewEnglish").toBool();
    for (int i = 0; i < 2; ++i) {
        languageButton->click();
        processEventsFor(300);
        const bool english = qApp->property("vaporViewEnglish").toBool();
        require(english == (i == 0 ? !originalEnglish : originalEnglish), QStringLiteral("map language button switches global language"));
        require(mapWindow->windowTitle() == (english ? QStringLiteral("VaporView 3D Map - Live") : QStringLiteral("VaporView 三维地图 - 实时")), QStringLiteral("map title switches immediately: %1").arg(mapWindow->windowTitle()));
        require(diagnosticsAction->text() == (english ? QStringLiteral("Map diagnostics") : QStringLiteral("地图诊断")), QStringLiteral("map menu switches immediately"));
        require(diagnostics->toPlainText().contains(english ? QStringLiteral("Track data:") : QStringLiteral("轨迹数据：")), QStringLiteral("open diagnostics switches immediately"));
        require(diagnostics->window()->windowTitle() == (english ? QStringLiteral("3D Map Diagnostics") : QStringLiteral("三维地图数据诊断")), QStringLiteral("diagnostics title is fully translated"));
        require(diagnostics->toPlainText().contains(english ? QStringLiteral("Terrain detail:") : QStringLiteral("地形细节：")), QStringLiteral("cached diagnostic details switch language"));
        require(mapWindow->findChild<QDialog*>(QStringLiteral("map3DDisplayPanel"))->windowTitle() == (english ? QStringLiteral("Display settings") : QStringLiteral("显示设置")), QStringLiteral("display settings title switches"));
        require(metricCombo->currentIndex() == originalMetric, QStringLiteral("language switch preserves selected heat metric"));
        require(metricCombo->itemText(0) == (english ? QStringLiteral("Peak") : QStringLiteral("峰值")), QStringLiteral("heat metric options switch language"));
        require(mapWindow->findChild<QTableWidget*>(QStringLiteral("map3DMapFilesTable"))->horizontalHeaderItem(0)->text() == (english ? QStringLiteral("Map files") : QStringLiteral("地图文件")), QStringLiteral("map file table switches language"));
        auto* buttons = diagnostics->window()->findChild<QDialogButtonBox*>();
        require(buttons->button(QDialogButtonBox::Close)->text() == (english ? QStringLiteral("Close") : QStringLiteral("关闭")), QStringLiteral("dialog close button switches immediately"));
        require(mapWindow->findChild<VaporView::Map3D::OsgEarthViewWidget*>(QStringLiteral("map3DView")) == view, QStringLiteral("language change preserves renderer"));
    }
    mapWindow->close();
    window.close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    processEventsFor(200);

    std::cout << "main_window_map3d_open_test passed\n";
    return 0;
}
