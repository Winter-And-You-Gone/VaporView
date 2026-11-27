#include "test_settings_sandbox.h"
#include "ground/main/MainWindow.h"
#include "ground/main/GroundMainWindowState.h"
#include "ground/main/UiTestReplayWindow.h"
#include "ground/map/Map3DController.h"
#include "ground/wave/WaterProfileAccumulator.h"
#include "IndependentCalibrationFixture.h"
#include <QtNetwork/QTcpServer>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QLabel>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>

#include <cstdlib>
#include <iostream>

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

VaporView::EpsilonData makeSample(double latitudeDeg)
{
    VaporView::EpsilonData sample;
    sample.valid = true;
    sample.latitude_deg = latitudeDeg;
    sample.longitude_deg = 116.3;
    sample.height_m = 45.0;
    sample.gnss_fix_code = 6;
    sample.gnss_satellites = 12;
    sample.hdop = 0.9;
    sample.vdop = 1.1;
    sample.yaw_deg = 91.0;
    return sample;
}

VaporView::TelemetryBasic makeRemoteTelemetry(double latitudeDeg, quint64 hostTimeUs)
{
    VaporView::TelemetryBasic telemetry;
    telemetry.host_time_us = hostTimeUs;
    telemetry.epsilon_time_us = hostTimeUs - 2000;
    telemetry.validity_flags = VaporView::BasicHasEpsilonTime |
                               VaporView::BasicHasPosition |
                               VaporView::BasicHasGnssQuality |
                               VaporView::BasicHasAttitude;
    telemetry.latitude_deg = latitudeDeg;
    telemetry.longitude_deg = 116.4;
    telemetry.height_m = 50.0;
    telemetry.gnss_fix_code = 6;
    telemetry.gnss_satellites = 14;
    telemetry.hdop = 0.8f;
    telemetry.vdop = 1.0f;
    telemetry.roll_deg = 1.5f;
    telemetry.pitch_deg = -2.0f;
    telemetry.yaw_deg = 93.0f;
    return telemetry;
}

} // namespace

namespace VaporView::Ground {
struct Map3DLivePipelineTest {
    static void run(MainWindow& window, IndependentCalibrationTest::Rig& rig)
    {
        auto& state = *window.state_;
        auto& map = *state.map3d_controller_;
        const auto pending = [&]() -> const Geo::TrajectoryRenderSample& {
            require(map.pending_samples_.size() == 1, "one frozen render sample per flush interval");
            return map.pending_samples_.back();
        };
        QTcpServer server;
        require(server.listen(QHostAddress::LocalHost, 0), "loopback Sky endpoint");
        window.onDataSourceModeChanged(1);
        auto *remote = window.testRemoteSkyController();
        require(remote->openTcp(QStringLiteral("127.0.0.1"), server.serverPort()), "open real remote controller path");
        auto config = SkyConfig::defaults();
        config.sensor_transport_path = SensorTransportPath::FpgaRelay;
        window.testInjectRemoteSkyConfig(config.toJson());
        emit remote->fpgaStatusUpdated({{"ready", true}, {"connected", true}, {"simulation", false}});
        require(window.isRemoteSkyMode() && window.isFpgaRelayPath(), "remote relay is active");
        auto packet = makeRemoteTelemetry(39.91, 3000000);
        packet.validity_flags |= BasicHasPressure | BasicHasLidar | BasicHasTemperature | BasicHasHumidity;
        packet.pressure_hpa = 970;
        packet.lidar_distance_m = 12;
        packet.temperature_c = 99;
        packet.humidity_percent = 98;
        FpgaSensor::Reading raw;
        raw.validity.structure = raw.validity.measurement = true;
        raw.source = 0x41;
        raw.kind = FpgaSensor::SensorKind::Hc2a;
        raw.temperatureC = 20;
        raw.humidityPct = 45;
        emit remote->fpgaSensorUpdated(raw);
        auto internal = raw;
        internal.source = 0x44;
        internal.kind = FpgaSensor::SensorKind::Sht45;
        internal.temperatureC = 70;
        internal.humidityPct = 71;
        emit remote->fpgaSensorUpdated(internal);
        internal.source = 0x43;
        internal.kind = FpgaSensor::SensorKind::Bmp390;
        internal.pressurePa = 88000;
        emit remote->fpgaSensorUpdated(internal);
        auto aux = raw;
        aux.source = 0x40;
        aux.kind = FpgaSensor::SensorKind::Ptb210;
        aux.pressurePa = 90000;
        emit remote->fpgaSensorUpdated(aux);
        aux.source = 0x45;
        aux.kind = FpgaSensor::SensorKind::Sdfm300;
        aux.distanceMm = 2000;
        emit remote->fpgaSensorUpdated(aux);
        auto navOnly = makeRemoteTelemetry(39.90, 2900000);
        emit remote->basicTelemetryUpdated(navOnly);
        require(pending().heat.pressureHpa == 900 && pending().heat.lidarDistanceM == 2,
                "raw relay readings survive Basic packets without auxiliary flags");
        emit remote->basicTelemetryUpdated(packet);
        auto snapshot = window.map3DLiveTelemetrySnapshot();
        require(snapshot.hasTemperature && snapshot.temperatureC == 20 && snapshot.humidityRh == 45 &&
                snapshot.temperatureSource.contains("HC2A") && snapshot.pressureHpa == 970 && snapshot.lidarDistanceM == 12,
                "remote sidebar uses external HC2A and same-packet PTB210/SDFM300");
        const auto first = pending();
        require(first.heat.temperatureC == 20 && first.heat.humidityRh == 45 && first.heat.pressureHpa == 970 &&
                first.heat.lidarDistanceM == 12 && std::isnan(first.heat.inSituWaterConcentration) &&
                std::isnan(first.heat.columnWaterConcentration) && first.heat.calibrationEpoch == 0,
                "same-packet measurements reach track without a calibration");

        rig.service->setDevice(true, rig.identity);
        rig.service->synchronize();
        const auto file = rig.complete();
        rig.service->selectFile(file.filename);
        rig.service->synchronize();
        rig.pair(5, 4);
        emit remote->basicTelemetryUpdated(packet);
        const auto calibrated = pending();
        const auto close = [](double a, double b) { return std::abs(a - b) < 1.e-7; };
        require(calibrated.heat.calibrationEpoch > 0 && close(calibrated.heat.inSituWaterConcentration, 100) &&
                close(calibrated.heat.columnWaterConcentration, 250), "both wavelengths use the selected calibration");
        rig.pair(3, 8, true); // Invalid 816 nm result on the replacement point.
        raw.temperatureC = 22;
        raw.humidityPct = 46;
        emit remote->fpgaSensorUpdated(raw);
        packet.longitude_deg += 0.01;
        packet.host_time_us += 5000;
        packet.pressure_hpa = 960;
        packet.lidar_distance_m = 7;
        emit remote->basicTelemetryUpdated(packet);
        require(pending().navigation.lonDeg == packet.longitude_deg && pending().heat.pressureHpa == 960 &&
                pending().heat.lidarDistanceM == 7 && pending().heat.temperatureC == 22 && pending().heat.humidityRh == 46 &&
                close(pending().heat.inSituWaterConcentration, 200) && std::isnan(pending().heat.columnWaterConcentration),
                "merge replaces the entire point, including the now-invalid column measurement");
        state.current_hc2a_.timestamp -= std::chrono::milliseconds(3001);
        emit remote->basicTelemetryUpdated(packet);
        snapshot = window.map3DLiveTelemetrySnapshot();
        require(snapshot.humidityStale && snapshot.temperatureStale && !snapshot.pressureStale && !snapshot.lidarStale &&
                std::isnan(pending().heat.humidityRh) && std::isnan(pending().heat.temperatureC) && pending().heat.pressureHpa == 960 &&
                close(pending().heat.inSituWaterConcentration, 200), "expired HC2A does not invalidate pressure, distance or water");
        Calibration::CalibrationFile replacement;
        QString error;
        require(Calibration::CalibrationFileStore(rig.root.path()).generate(file.project, "replacement", replacement, &error), qPrintable(error));
        rig.service->selectFile(replacement.filename);
        rig.service->synchronize();
        rig.pair(3, 8);
        emit remote->basicTelemetryUpdated(packet);
        require(pending().heat.calibrationEpoch > calibrated.heat.calibrationEpoch && close(pending().heat.columnWaterConcentration, 150),
                "a replacement calibration gets a new epoch");
        rig.now += 2001;
        rig.bus.publish(1700000000000000ULL + rig.now.load() * 1000,
                        IndependentCalibrationTest::cycle(816, 3, rig.cycleId++));
        rig.service->synchronize();
        emit remote->basicTelemetryUpdated(packet);
        require(std::isnan(pending().heat.inSituWaterConcentration) && close(pending().heat.columnWaterConcentration, 150),
                "a silent water lane expires without dropping the refreshed lane");
        state.water_profile_->append(0, "profile-0", 101, 3, 7);
        state.water_profile_->append(1, "profile-1", 102, 5, 12);
        const auto profile = map.profile_provider_(0, 5);
        require(profile.calibrationSelected && profile.unit == "ppmv" && profile.channels[0].hasLatest &&
                !profile.channels[0].rawPoints.isEmpty() && profile.channels[0].layers.isEmpty() &&
                !profile.channels[1].layers.isEmpty() && profile.channels[1].rawPoints.isEmpty() &&
                profile.channels[1].latestHeightM == 12 && profile.channels[1].revision == map.revision_provider_(),
                "independent profile resolutions expose the accumulator revision, latest point and calibration");
        packet.gnss_fix_code = 0;
        emit remote->basicTelemetryUpdated(packet);
        require(std::isnan(pending().heat.columnWaterConcentration) && std::isnan(pending().heat.lidarDistanceM),
                "invalid-fix marker never carries science values");

        // No navigation packets are sent while the visible map's real timer runs.
        packet.validity_flags &= ~BasicHasPosition;
        emit remote->basicTelemetryUpdated(packet);
        state.current_lidar_.timestamp -= std::chrono::milliseconds(2001);
        int calls = 0;
        Map3D::Map3DLiveTelemetrySnapshot delivered;
        auto original = map.telemetry_provider_;
        map.setLiveTelemetryProvider([&] { ++calls; return delivered = original(); });
        int initial = calls;
        processEventsFor(700);
        require(calls > initial && !delivered.hasPosition && delivered.hasLidar && delivered.lidarStale && !delivered.pressureStale,
                "visible sidebar advances independently of navigation and marks just the expired distance");
        map.setLiveTelemetryProvider(original);

        window.openUiTestSourceWindow();
        auto *replay = state.ui_test_source_window_;
        replay->resetSource(true);
        UiTestReplayWindow::Sample historical{};
        historical.stream = UiTestReplayWindow::Stream::Sensors;
        historical.timestamp = 123000;
        historical.fields = {{"epsilon_valid", "true"}, {"nav_lat_deg", "33"}, {"nav_lon_deg", "115"}, {"nav_height_m", "40"},
                             {"gnss_fix", "RTK_FIXED"}, {"sdfm300_valid", "true"}, {"sdfm300_distance_m", "15"},
                             {"hc2a_temperature_c", "10"}, {"hc2a_humidity_rh", "50"}, {"ptb_pressure_hpa", "950"}};
        require(rig.service->liveSnapshot().column816.valid, "live concentration still exists while replaying history");
        replay->present(historical);
        const auto history = pending();
        require(history.navigation.recordTimestampUs == 123000 && history.heat.lidarDistanceM == 15 && history.heat.temperatureC == 10 &&
                history.heat.pressureHpa == 950 && history.heat.calibrationEpoch == 0 && std::isnan(history.heat.columnWaterConcentration) &&
                std::isnan(history.heat.inSituWaterConcentration), "Session presenter binds only historical measurements");
        replay->resetSource(false);
        require(!window.map3DLiveTelemetrySnapshot().hasLidar, "leaving replay clears historical distance");
        replay->close();
        remote->close();
        window.onDataSourceModeChanged(0);
    }
};
} // namespace VaporView::Ground

int main(int argc, char** argv)
{
    qputenv("VAPORVIEW_MAP3D_HEADLESS_TEST", "1");

    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewMap3DLiveTest"));
    app.setApplicationName(QStringLiteral("main_window_map3d_live_test"));

    IndependentCalibrationTest::Rig rig;
    MainWindow window(nullptr, rig.service);
    window.resize(1000, 700);
    window.show();
    processEventsFor(200);

    QAction* mapAction = findActionByText(&window,
                                          {QStringLiteral("三维地图"),
                                           QStringLiteral("3D Map")});
    require(mapAction != nullptr, "3D map action exists");
    mapAction->trigger();
    processEventsFor(100);

    QWidget* mapWindow = findMap3DWindow();
    require(mapWindow != nullptr && mapWindow->isVisible(), "3D map window opens");
    QLabel* mapStatusLabel = mapWindow->findChild<QLabel*>(QStringLiteral("map3DStatusLabel"));
    require(mapStatusLabel != nullptr, "3D map status label exists");

    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900001), 1000000);
    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900002), 1005000);
    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900003), 1010000);

    require(window.testPendingMap3DSampleCount() == 1,
            "3D map live forwarding keeps only the latest pending sample per flush interval");
    require(window.testLatestPendingMap3DRecordTimestampUs() == 1010000,
            "3D map live forwarding retains the newest pending sample timestamp");
    require(window.testMap3DFlushTimerActive(),
            "3D map flush timer starts after a valid live sample");

    VaporView::TelemetryBasic remoteTelemetry = makeRemoteTelemetry(39.910001, 2010000);
    window.testOnRemoteBasicTelemetryUpdatedForMap3D(remoteTelemetry);
    require(window.testPendingMap3DSampleCount() == 1,
            "remote sky telemetry forwards one pending 3D map sample");
    require(window.testLatestPendingMap3DRecordTimestampUs() == 2010000,
            "remote sky telemetry uses host timestamp for the pending 3D map sample");
    processEventsFor(80);
    require(mapStatusLabel->text().contains(QStringLiteral("姿态 欧拉角")),
            "flushed remote sky telemetry reports Euler aircraft attitude source");

    VaporView::EpsilonData invalidSample = makeSample(39.900004);
    invalidSample.valid = false;
    window.testMaybeForwardMap3DSampleForMap3D(invalidSample, 1015000);
    require(window.testPendingMap3DSampleCount() == 0,
            "invalid live sample clears pending 3D map samples");
    require(!window.testMap3DFlushTimerActive(),
            "invalid live sample stops the 3D map flush timer");
    require(window.testLastMap3DDropReason() == QStringLiteral("epsilon invalid"),
            "invalid live sample records a 3D map drop reason");
    require(mapStatusLabel->text().contains(QStringLiteral("最近丢弃 Live：epsilon invalid")),
            "3D map status reports invalid live sample drop reason");

    VaporView::EpsilonData invalidFixSample = makeSample(39.900005);
    invalidFixSample.gnss_fix_code = 0;
    window.testMaybeForwardMap3DSampleForMap3D(invalidFixSample, 1018000);
    require(window.testPendingMap3DSampleCount() == 1,
            "live sample with invalid GNSS fix but valid LLH queues for 3D marker display");
    require(window.testLatestPendingMap3DRecordTimestampUs() == 1018000,
            "invalid-fix live sample retains its timestamp while queued");
    require(window.testMap3DFlushTimerActive(),
            "invalid-fix live sample starts the 3D map flush timer");
    require(window.testLastMap3DDropReason().isEmpty(),
            "invalid-fix live sample is not treated as a dropped 3D map sample");
    processEventsFor(80);
    require(mapStatusLabel->text().contains(QStringLiteral("定位 无效")),
            "3D map status reports the forwarded invalid GNSS fix");
    require(mapStatusLabel->text().contains(QStringLiteral("无效 1")),
            "3D map status counts invalid GNSS fixes as marker samples");

    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900005), 1020000);
    require(window.testPendingMap3DSampleCount() == 1,
            "valid live sample queues after invalid sample reset");

    VaporView::TelemetryBasic ecefOnlyRemote = makeRemoteTelemetry(39.920001, 2020000);
    ecefOnlyRemote.validity_flags = VaporView::BasicHasEpsilonTime |
                                    VaporView::BasicHasEcef |
                                    VaporView::BasicHasGnssQuality;
    ecefOnlyRemote.latitude_deg = 0.0;
    ecefOnlyRemote.longitude_deg = 0.0;
    ecefOnlyRemote.height_m = 0.0;
    ecefOnlyRemote.ecef_x_m = -2170000.0;
    ecefOnlyRemote.ecef_y_m = 4380000.0;
    ecefOnlyRemote.ecef_z_m = 4070000.0;
    window.testOnRemoteBasicTelemetryUpdatedForMap3D(ecefOnlyRemote);
    require(window.testPendingMap3DSampleCount() == 0,
            "remote telemetry without BasicHasPosition clears pending samples and does not inject a 0/0/0 3D map point");
    require(!window.testMap3DFlushTimerActive(),
            "remote telemetry without position leaves the 3D map flush timer stopped");
    require(window.testLastMap3DDropReason() == QStringLiteral("missing BasicHasPosition"),
            "remote telemetry without position records a 3D map drop reason");
    require(mapStatusLabel->text().contains(QStringLiteral("最近丢弃 Remote：missing BasicHasPosition")),
            "3D map status reports remote telemetry drop reason");

    VaporView::Ground::Map3DLivePipelineTest::run(window, rig);

    mapWindow->hide();
    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900006), 1025000);
    require(window.testPendingMap3DSampleCount() == 0,
            "hidden 3D map window clears pending live samples");
    require(!window.testMap3DFlushTimerActive(),
            "hidden 3D map window stops live sample flush timer");

    mapWindow->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    processEventsFor(200);
    window.close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    processEventsFor(200);
    std::cout << "main_window_map3d_live_test passed\n";
    return 0;
}
