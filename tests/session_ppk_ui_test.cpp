#include "ground/session/SessionPpkWidget.h"
#include "ground/session/SessionPpkWindow.h"
#include "ground/session/SessionViewerWindow.h"
#include "ground/session/SessionViewerPages.h"
#include "shared/theme/AppTheme.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "ground/main/GroundMainWindowSupport.h"
#include "ppk/AttitudeStore.h"
#include "EpsilonRawSatellite.h"
#include "ppk/SessionNavigationSource.h"
#include "test_ui_helpers.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QJsonDocument>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableView>
#include <QPushButton>
#include <QSettings>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTimer>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "rtklib.h"
#undef lock
#undef unlock

namespace
{
void require(bool ok, const char *reason)
{
    if (!ok)
        throw std::runtime_error(reason);
}

// QSettings(org, app) still uses the Windows registry even when the default
// format is IniFormat. Restore the one startup key and suspend all other writes.
class ScopedViewerSettings
{
  public:
    ScopedViewerSettings() : settings_("VaporView", "SessionViewer"),
        hadPath_(settings_.contains("last_session_directory")),
        path_(settings_.value("last_session_directory")),
        writesSuspended_(VaporView::settingsWritesSuspended())
    {
        settings_.remove("last_session_directory");
        settings_.sync();
        VaporView::setSettingsWritesSuspended(true);
    }
    ~ScopedViewerSettings()
    {
        if (hadPath_)
            settings_.setValue("last_session_directory", path_);
        else
            settings_.remove("last_session_directory");
        settings_.sync();
        VaporView::setSettingsWritesSuspended(writesSuspended_);
    }
  private:
    QSettings settings_;
    bool hadPath_;
    QVariant path_;
    bool writesSuspended_;
};

void writeFile(const QString &path, const QByteArray &bytes)
{
    require(QDir().mkpath(QFileInfo(path).absolutePath()), "create temporary Session directory");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write temporary Session file");
}

void makeSession(const QTemporaryDir &session)
{
    writeFile(session.filePath("session.json"), R"({"session_name":"PPK UI test","paths":{"devices_csv":"sensors/devices.csv"}})");
    writeFile(session.filePath("sensors/devices.csv"), "record_timestamp_us,temperature_c\n1000000,23.5\n");
}

QPushButton *button(QWidget &widget, const QString &text)
{
    for (auto *candidate : widget.findChildren<QPushButton *>())
        if (candidate->text() == text)
            return candidate;
    return nullptr;
}
} // namespace
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settingsDirectory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDirectory.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDirectory.path());
    try
    {
        using namespace VaporView::Ppk;
        using namespace VaporView::Ground::SessionUi;
        QTemporaryDir session;
        QTemporaryDir otherSession;
        makeSession(session);
        makeSession(otherSession);
        ScopedViewerSettings settingsGuard;
        SessionViewerWindow viewer;
        viewer.setUiTestMode(true);
        viewer.resize(1280, 800);
        viewer.show();
        auto *overview = viewer.findChild<SessionOverviewWidget *>();
        auto *openPpk = viewer.findChild<QPushButton *>("sessionPpkProcessingButton");
        auto *summary = viewer.findChild<QLabel *>("sessionPpkSummaryStatus");
        auto *navigation = viewer.findChild<QLabel *>("sessionPpkSummarySource");
        require(overview && openPpk && summary && navigation && !openPpk->isEnabled(), "no Session disables PPK entry");
        require(!viewer.findChild<SessionPpkWidget *>(), "main viewer has no embedded PPK panel");
        require(viewer.openSessionPath(session.path()) && openPpk->isEnabled(), "old Session enables PPK entry without observations");
        require(summary->text() == QString::fromUtf8("未处理"), "old Session summary is Not processed");
        openPpk->click();
        auto *window = viewer.findChild<SessionPpkWindow *>();
        require(window && window->isWindow() && window->isVisible(), "PPK entry opens a dedicated window");
        require(window->findChild<QWidget *>("customTitleBar"), "PPK window uses custom title bar");
        require(!viewer.centralWidget()->isAncestorOf(window), "PPK workflow is outside main page");
        openPpk->click();
        require(viewer.findChildren<SessionPpkWindow *>().size() == 1, "repeated clicks reuse PPK window");
        require(window->sessionDirectory() == QDir::fromNativeSeparators(session.path()), "PPK binds current Session");
        require(viewer.openSessionPath(otherSession.path()), "switch to Session B");
        require(window->sessionDirectory() == QDir::fromNativeSeparators(otherSession.path()), "PPK follows Session B");
        require(viewer.openSessionPath(session.path()), "switch back to Session A");
        auto &panel = *window->findChild<SessionPpkWidget *>();
        require(panel.findChild<QLabel *>("sessionPpkRoverStatus")->text().contains(QString::fromUtf8("缺少")),
                "missing Rover is visible in PPK window");
        auto *run = panel.findChild<QPushButton *>("sessionPpkRunButton");
        auto *cancel = panel.findChild<QPushButton *>("sessionPpkCancelButton");
        auto *source = panel.findChild<QComboBox *>("sessionPpkSourceCombo");
        require(run && cancel && source && !run->isEnabled(), "old Session disables PPK run");
        auto *model = qobject_cast<QStandardItemModel *>(source->model());
        require(model && !model->item(1)->isEnabled(), "old Session only enables Original");
        viewer.setEnglish(true);
        require(run->text() == "Run PPK", "PPK uses Session English translations");
        require(openPpk->text() == "PPK Processing" && window->windowTitle() == "PPK Processing", "both windows switch to English");
        require(navigation->text() == "Original", "English navigation summary");
        viewer.setEnglish(false);
        require(run->text() == QString::fromUtf8("运行 PPK"), "PPK uses Session Chinese translations");
        require(window->windowTitle() == QString::fromUtf8("PPK 后处理"), "PPK title switches to Chinese");
        writeFile(session.filePath("ppk/ppk_quality.json"), R"({"state":"Failed","error":"TEST_FAILURE"})");
        SessionNavigationEvents::instance()->notify(QFileInfo(session.path()).absoluteFilePath());
        require(VaporViewTest::processEventsUntil(5000, [&] { return summary->text() == QString::fromUtf8("失败"); }),
                "failed status reaches main summary");
        require(panel.findChild<QLabel *>("sessionPpkQuality")->text() == "TEST_FAILURE", "failure persists in PPK window");
        require(PpkProcessor::clearResult(session.path()), "clear failure fixture");
        const QString fixtures = QStringLiteral(VAPORVIEW_SOURCE_DIR "/tests/fixtures/ppk/");
        QDir().mkpath(session.filePath("ppk/rover"));
        require(QFile::copy(fixtures + "rover.obs", session.filePath("ppk/rover/rover.obs")),
                "archive real rover fixture");
        auto initialConfig = PpkProcessor::loadConfig(session.path());
        initialConfig.roverObs = "ppk/rover/rover.obs";
        require(PpkProcessor::saveConfig(session.path(), initialConfig), "enable recorded Rover fixture");
        require(viewer.openSessionPath(session.path()), "reload partial inputs");
        require(summary->text() == QString::fromUtf8("等待输入数据"), "partial inputs show waiting state");
        require(PpkProcessor::importBase(session.path(), fixtures + "base.obs"), "archive base");
        require(PpkProcessor::importNavigation(session.path(), fixtures + "navigation.nav"), "archive ephemerides");
        auto config = PpkProcessor::loadConfig(session.path());
        config.roverObs = "ppk/rover/rover.obs";
        config.frequencies = 2;
        config.constellations = SYS_GPS;
        config.elevationMaskDeg = 10;
        require(PpkProcessor::saveConfig(session.path(), config), "save reproducible settings");
        obs_t obs{};
        auto nav = std::make_unique<nav_t>();
        sta_t station{};
        const auto name = QFile::encodeName(QDir::toNativeSeparators(fixtures + "rover.obs"));
        require(readrnxt(name.constData(), 1, {}, {}, 0, "", &obs, nav.get(), &station) > 0 && obs.n,
                "read fixture dates");
        const auto first = gpst2utc(obs.data[0].time), last = gpst2utc(obs.data[obs.n - 1].time);
        AttitudeStore attitudes;
        for (qint64 micros = (qint64(first.time) - 1) * 1000000; micros <= (qint64(last.time) + 1) * 1000000;
             micros += 200000)
        {
            std::vector<uint8_t> payload(102, 0);
            const uint32_t seconds = uint32_t(micros / 1000000), fraction = uint32_t(micros % 1000000);
            std::memcpy(payload.data() + 6, &seconds, 4);
            std::memcpy(payload.data() + 10, &fraction, 4);
            const auto frame = encodeFdilinkFrame(0x50, payload, 0);
            require(attitudes.appendSystemState(session.path(), micros + 10000000,
                                                QByteArray(reinterpret_cast<const char *>(frame.data()), frame.size())),
                    "record independent UTC attitude");
        }
        attitudes.close();
        freeobs(&obs);
        freenav(nav.get(), 0xFF);
        viewer.setEnglish(true);
        require(viewer.openSessionPath(session.path()), "reload complete inputs");
        require(run->isEnabled(), "complete inputs enable run");
        require(summary->text() == "Ready", "ready status uses formal PPK model");
        run->click();
        require(panel.busy(), "UI dispatches processing asynchronously");
        require(summary->text() == "Processing" && !button(*overview, "Open Data")->isEnabled() &&
                    !button(*overview, "Reload")->isEnabled() && !button(*overview, "Clear Page")->isEnabled(),
                "busy disables only Session mutations");
        require(openPpk->isEnabled() && button(*overview, "Raw Data Parser")->isEnabled() &&
                    viewer.findChild<QTableView *>()->isEnabled() && viewer.findChild<SessionWaveformWidget *>()->isEnabled(),
                "PPK window and data browsing remain usable");
        require(!viewer.openSessionPath(otherSession.path()) && window->sessionDirectory() == QDir::fromNativeSeparators(session.path()),
                "programmatic Session switch is rejected while busy");
        QMetaObject::invokeMethod(&viewer, "onClearViewClicked", Qt::DirectConnection);
        QMetaObject::invokeMethod(&viewer, "onReloadClicked", Qt::DirectConnection);
        require(window->sessionDirectory() == QDir::fromNativeSeparators(session.path()), "busy clear and reload cannot change binding");
        window->close();
        require(panel.busy() && !window->isVisible(), "closing PPK window retains active worker");
        openPpk->click();
        require(window->isVisible() && panel.busy() && viewer.findChildren<SessionPpkWindow *>().size() == 1,
                "reopening busy PPK reuses the worker");
        cancel->click();
        require(VaporViewTest::processEventsUntil(5000, [&] { return !panel.busy(); }), "UI cancellation completes");
        int heartbeat = 0;
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, [&] { ++heartbeat; });
        timer.start(1);
        run->click();
        require(panel.busy() && !source->isEnabled(), "source switching disabled during processing");
        require(VaporViewTest::processEventsUntil(15000, [&] { return !panel.busy(); }),
                "real PPK worker completes from UI");
        timer.stop();
        require(PpkProcessor::status(session.path()).completed && heartbeat > 0,
                "UI stays responsive during real RTKLIB processing");
        require(model->item(1)->isEnabled(), "completed PPK enables track selection");
        require(summary->text().contains("Completed") && summary->text().contains("FIX"), "completed overview displays FIX percentage");
        source->setCurrentIndex(1);
        require(sessionNavigationSource(session.path()) == NavigationSource::Ppk, "UI activates PPK");
        require(VaporViewTest::processEventsUntil(5000, [&] { return navigation->text() == "PPK corrected"; }),
                "PPK source updates main summary via navigation events");
        require(panel.findChild<QLabel *>("sessionPpkQuality")->text().contains("FIX"), "UI exposes FIX FLOAT quality");
        window->close();
        require(sessionNavigationSource(session.path()) == NavigationSource::Ppk, "closing PPK window preserves source");
        openPpk->click();
        require(window->isVisible() && viewer.findChildren<SessionPpkWindow *>().size() == 1 && source->currentIndex() == 1,
                "reopening preserves result and source");
        require(setSessionNavigationSource(session.path(), NavigationSource::Original), "external navigation source update");
        require(VaporViewTest::processEventsUntil(5000, [&] { return source->currentIndex() == 0 && navigation->text() == "Original"; }),
                "external source updates both windows");
        QFile style(QStringLiteral(VAPORVIEW_SOURCE_DIR "/resources/modern_style.qss"));
        require(style.open(QIODevice::ReadOnly), "load actual runtime stylesheet");
        auto styleText = QString::fromUtf8(style.readAll());
        styleText.replace("url(lucide/", "url(" + QStringLiteral(VAPORVIEW_SOURCE_DIR "/resources/lucide/"));
        const QFont originalFont = app.font();
        for (bool dark : {false, true})
        {
            app.setProperty(VaporView::kAppDarkThemeProperty, dark);
            app.setPalette(VaporView::appThemePalette(dark));
            QString themeStyle = VaporView::applyAppThemeTokens(styleText, false);
            if (dark)
            {
                themeStyle += VaporView::applyAppThemeTokens(VaporView::Ground::MainSupport::darkThemeStyleSheet(), true);
                themeStyle.replace("url(lucide/", "url(" + QStringLiteral(VAPORVIEW_SOURCE_DIR "/resources/lucide/"));
            }
            app.setStyleSheet(themeStyle);
            for (bool english : {false, true})
            {
                viewer.setEnglish(english);
                for (double scale : {1.0, 1.3})
                {
                    QFont font = originalFont;
                    font.setPointSizeF(originalFont.pointSizeF() * scale);
                    app.setFont(font);
                    viewer.resize(1900, 1000);
                    VaporViewTest::processEventsFor(20);
                    viewer.resize(1280, 800);
                    window->resize(820, 780);
                    VaporViewTest::processEventsFor(100);
                    auto *scroll = viewer.findChild<QScrollArea *>("sessionViewerScrollArea");
                    require(scroll && scroll->horizontalScrollBar()->maximum() == 0, "viewer has no unnecessary horizontal scrollbar");
                    auto *ppkScroll = window->findChild<QScrollArea *>("sessionPpkScrollArea");
                    require(ppkScroll && ppkScroll->horizontalScrollBar()->maximum() == 0, "PPK layout fits both themes and languages");
                    for (auto *action : overview->findChildren<QPushButton *>())
                        require(overview->rect().contains(action->mapTo(overview, action->rect().bottomRight())), "top buttons remain inside overview");
                    require(window->grab().toImage().pixelColor(4, window->height() - 4) ==
                                VaporView::appThemeColor(dark ? VaporView::AppThemeColor::Window : VaporView::AppThemeColor::Surface, dark),
                            "PPK window follows real application theme");
                }
            }
        }
        app.setFont(originalFont);
        app.setStyleSheet({});
        viewer.setEnglish(true);
        source->setCurrentIndex(0);
        require(sessionNavigationSource(session.path()) == NavigationSource::Original, "UI restores Original");
        button(*overview, "Clear Page")->click();
        require(window->sessionDirectory().isEmpty() && !openPpk->isEnabled(), "clear page resets PPK to No Session");
        require(viewer.openSessionPath(session.path()), "Session reload restores PPK binding");
        run->click();
        require(panel.busy(), "start worker before closing viewer");
        QPointer<SessionPpkWindow> lifetime(window);
        viewer.close();
        require(!lifetime, "viewer close cancels worker and destroys PPK window safely");
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
