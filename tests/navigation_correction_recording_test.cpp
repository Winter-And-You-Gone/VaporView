#include "shared/session/NavigationStatusCsv.h"
#include "shared/session/SessionManifest.h"
#include "ground/session/GroundRecordingService.h"
#include "SkySessionRecorder.h"
#include "SkyRuntime.h"
#include "FailingRecordingStorage.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace VaporView;
using namespace VaporView::Session;

static void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
static QStringList readLines(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "open status CSV");
    return QString::fromUtf8(file.readAll()).remove(QLatin1Char('\r')).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}
static NavigationStatusRecord sample()
{
    NavigationStatusRecord r;
    r.sourceMode = QStringLiteral("remote");
    r.skyBootId = 1;
    r.rtcmStreamId = 2;
    r.timestampUs = 1'000'000;
    r.epsilonTimeUs = 900'000;
    r.skyReportTimeUs = 950'000;
    r.rtcm.remote = true;
    r.rtcm.available = true;
    r.rtcm.health = RtcmHealth::Normal;
    r.rtcm.ageMs = 85;
    r.rtcm.bytesPerSecond = 1600;
    r.rtcm.lossAvailable = true;
    r.rtcm.lossPercent = 0.1;
    r.rtcm.droppedChunks = 3;
    r.navigationAvailable = r.positionAvailable = r.filterStatusAvailable = true;
    r.gnssFixCode = 6;
    r.satellites = 24;
    r.horizontalAccuracyM = 0.015;
    r.verticalAccuracyM = 0.03;
    r.latitudeDeg = 30.25;
    r.longitudeDeg = 120.25;
    r.heightM = 42;
    return r;
}
static void testCorrelation()
{
    const auto header = navigationStatusCsvHeader().trimmed().split(QLatin1Char(','));
    auto field = [&](const NavigationStatusRecord& r, const char *name) {
        const auto row = navigationStatusCsvRow(r).trimmed().split(QLatin1Char(','), Qt::KeepEmptyParts);
        require(row.size() == header.size(), "status CSV row preserves header alignment");
        return row.at(header.indexOf(QString::fromLatin1(name)));
    };
    auto r = sample();
    NavigationStatusChangeTracker changes;
    require(changes.observe(r).value(QStringLiteral("baseline_reset")).toBool(), "first state establishes baseline");
    require(field(r, "rtk_fixed") == QStringLiteral("1") && field(r, "hacc_m").toDouble() == 0.015 &&
            field(r, "record_timestamp_us") == QStringLiteral("1000000"), "joint row contains time, fixed solution and accuracy");
    r.horizontalAccuracyM += 0.001;
    r.filterStatusBits++;
    require(changes.observe(r).isEmpty(), "metric and raw filter-bit fluctuations do not spam status logs");
    r.rtcm.health = RtcmHealth::Interrupted;
    r.rtcm.ageMs = 6000;
    require(changes.observe(r).value(QStringLiteral("rtcm_health")) == QStringLiteral("interrupted"), "RTCM interruption is recorded before fix degradation");
    r.gnssFixCode = 5;
    r.horizontalAccuracyM = 3;
    auto event = changes.observe(r);
    require(event.value(QStringLiteral("rtk_fix_transition")) == QStringLiteral("degraded") &&
            event.value(QStringLiteral("rtcm_age_ms")).toLongLong() == 6000 &&
            event.value(QStringLiteral("hacc_m")).toDouble() == 3,
            "fix degradation log carries simultaneous RTCM age and position accuracy");
    require(field(r, "rtcm_health") == QStringLiteral("interrupted") && field(r, "gnss_fix_code") == QStringLiteral("5") &&
            field(r, "nav_lon_deg").toDouble() == 120.25, "joint degradation row preserves trajectory coordinates");
    r.rtcm.health = RtcmHealth::Normal;
    r.gnssFixCode = 9;
    require(changes.observe(r).value(QStringLiteral("rtk_fix_transition")) == QStringLiteral("recovered"), "dual antenna fixed code also restores FIX");
    r.skyBootId++;
    r.gnssFixCode = 5;
    require(changes.observe(r).value(QStringLiteral("rtk_fix_transition")) == QStringLiteral("baseline"), "new Sky does not fabricate a degradation event");
    r.navigationAvailable = r.positionAvailable = false;
    require(changes.observe(r).value(QStringLiteral("rtk_fix_transition")) == QStringLiteral("unavailable") &&
            field(r, "gnss_fix_code").isEmpty() && field(r, "hacc_m").isEmpty() && field(r, "nav_lat_deg").isEmpty(),
            "unavailable navigation does not reuse a stale fixed solution, accuracy or position");
    r.sourceMode = QStringLiteral("local");
    r.rtcm.remote = false;
    require(field(r, "rtcm_link_loss_percent").isEmpty() && field(r, "rtcm_dropped_chunks").isEmpty(),
            "Local leaves unmeasured Sky metrics empty");
    r = sample();
    NavigationStatusSampleGate gate;
    require(gate.accept(r), "first status row accepted");
    r.timestampUs += 1000;
    require(!gate.accept(r), "duplicate refresh is throttled");
    r.rtcm.health = RtcmHealth::Interrupted;
    require(gate.accept(r), "state transition is retained within periodic interval");
    r.timestampUs += 1'000'000;
    require(gate.accept(r), "unchanged status still produces one-second samples");
    r.timestampUs = 3'950'000;
    require(gate.accept(r), "periodic sample reaches next UTC second");
    r.timestampUs += 950'000;
    require(gate.accept(r), "coarse timer jitter does not halve sampling rate");

    QJsonObject legacy = sessionManifestToJson(SessionManifest{});
    QJsonObject legacyCounts = legacy.value(QStringLiteral("counts")).toObject();
    legacyCounts.remove(QStringLiteral("navigation_status_rows"));
    legacy.insert(QStringLiteral("counts"), legacyCounts);
    QJsonObject legacyPaths = legacy.value(QStringLiteral("paths")).toObject();
    legacyPaths.remove(QStringLiteral("navigation_status_csv"));
    legacy.insert(QStringLiteral("paths"), legacyPaths);
    const auto parsed = sessionManifestFromJson(legacy);
    require(parsed.success && parsed.manifest.counts.navigationStatusRows == 0,
            "older sessions remain readable without navigation status metadata");
}

static void testWriters()
{
    QTemporaryDir root;
    Ground::Session::GroundRecordingService ground;
    Ground::Session::GroundRecordingOptions options;
    options.baseDirectory = root.path();
    SkySessionRecorder sky;
    require(ground.start(options) && sky.start(root.path(), QStringLiteral("test"), 115200), "start both recorders");
    const QString groundDirectory = ground.status().sessionDirectory;
    const QString skyDirectory = sky.sessionDirectory();
    auto r = sample();
    auto recordBoth = [&]() {
        r.timestampUs = Ground::Session::GroundRecordingService::currentTimestampUs();
        require(ground.recordNavigationStatus(r) && sky.recordNavigationStatus(r), "write joint status to both sessions");
    };
    recordBoth();
    r.rtcm.health = RtcmHealth::Interrupted;
    recordBoth();
    r.gnssFixCode = 5;
    r.horizontalAccuracyM = 3;
    recordBoth();
    require(ground.pause(), "pause Ground");
    sky.pause();
    require(!ground.recordNavigationStatus(r) && !sky.recordNavigationStatus(r), "pause excludes status rows");
    require(ground.start(options) && sky.start(root.path(), QStringLiteral("test"), 115200), "resume both sessions");
    recordBoth();
    ground.stop();
    require(sky.stop(), "stop Sky");
    const auto& layout = standardSessionPackageLayout();
    const auto groundRows = readLines(sessionPackageFilePath(groundDirectory, layout.navigationStatusCsvPath));
    const auto skyRows = readLines(sessionPackageFilePath(skyDirectory, layout.navigationStatusCsvPath));
    require(groundRows == skyRows && groundRows.size() == 5, "Ground and Sky write identical aligned schemas with resume baseline");
    for (const auto& directory : {groundDirectory, skyDirectory})
    {
        QFile manifestFile(sessionPackageFilePath(directory, layout.manifestPath));
        require(manifestFile.open(QIODevice::ReadOnly), "read recorder manifest");
        const auto parsed = sessionManifestFromJson(QJsonDocument::fromJson(manifestFile.readAll()).object());
        require(parsed.success && parsed.manifest.counts.navigationStatusRows == 4, "manifest counts status rows");
        for (int i = 1; i < groundRows.size(); ++i)
        {
            const quint64 time = groundRows.at(i).section(QLatin1Char(','), 0, 0).toULongLong();
            require(time >= parsed.manifest.startTimeUs && time <= parsed.manifest.endTimeUs, "status samples stay within recording boundaries");
        }
    }
    require(!ground.recordNavigationStatus(r) && !sky.recordNavigationStatus(r), "stop excludes status rows");
    require(ground.start(options) && sky.start(root.path(), QStringLiteral("test"), 115200), "new recording session");
    recordBoth();
    const QString newDirectory = sky.sessionDirectory();
    ground.stop();
    sky.stop();
    require(readLines(sessionPackageFilePath(newDirectory, layout.navigationStatusCsvPath)).size() == 2, "new session resets sample gate and counters");
}

static void testStorageFailure()
{
    for (bool failFlush : {false, true})
    {
        QTemporaryDir root;
        auto storage = std::make_shared<FailingRecordingStorage>();
        Ground::Session::GroundRecordingService ground(storage);
        Ground::Session::GroundRecordingOptions options;
        options.baseDirectory = root.path();
        SkySessionRecorder sky(storage);
        require(ground.start(options) && sky.start(root.path(), QStringLiteral("test"), 115200), "start fault fixtures");
        storage->failWrites.store(!failFlush);
        storage->failFlush.store(failFlush);
        auto r = sample();
        r.timestampUs = Ground::Session::GroundRecordingService::currentTimestampUs();
        require(!ground.recordNavigationStatus(r) && !sky.recordNavigationStatus(r), "status write/flush failure is reported");
        require(ground.stop().writeFailed && !sky.stop(), "failed status file makes session incomplete");
    }
}

static void testRuntimeSampling()
{
    QTemporaryDir root;
    SkyRuntimeOptions options;
    options.simulate_data = true;
    options.telemetry_host = QStringLiteral("127.0.0.1");
    options.telemetry_tcp_port = 0;
    options.config_path = root.filePath(QStringLiteral("sky.json"));
    SkyConfig config = SkyConfig::defaults();
    config.epsilon.enabled = true;
    config.epsilon.port = QStringLiteral("test-epsilon");
    require(config.saveToFile(options.config_path), "save isolated simulation config");
    SkyRuntime runtime(options);
    QVector<LogRecord> changes;
    QObject::connect(&runtime, &SkyRuntime::logRecord, [&](const LogRecord& log) {
        if (log.fields.value(QStringLiteral("event")) == QStringLiteral("navigation_correction_status_changed"))
            changes.append(log);
    });
    auto waitForSample = [&]() {
        const int before = changes.size();
        QEventLoop loop;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
            if (changes.size() > before) loop.quit();
        });
        poll.start(20);
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec();
        return changes.size() > before;
    };
    require(runtime.start(), "start isolated simulated Sky runtime");
    require(waitForSample() && changes.last().fields.value(QStringLiteral("navigation_available")).toBool(),
            "production sampler emits joint status without a Ground connection");
    require(changes.last().fields.value(QStringLiteral("source_mode")) == QStringLiteral("sky") &&
            changes.last().fields.value(QStringLiteral("record_timestamp_us")).toULongLong() > 0,
            "Sky runtime samples its own session clock and source");
    require(!waitForSample(), "periodic metric updates do not emit repeated status logs");
    require(runtime.disconnectDevice(SkyDeviceId::Epsilon), "disconnect simulated navigation device");
    require(waitForSample() && !changes.last().fields.value(QStringLiteral("navigation_available")).toBool() &&
            changes.last().fields.value(QStringLiteral("rtk_fix_transition")) == QStringLiteral("unavailable"),
            "production sampler clears navigation rather than reporting stale FIX degradation");
    runtime.stop();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        testCorrelation();
        testWriters();
        testStorageFailure();
        testRuntimeSampling();
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "navigation_correction_recording_test passed\n";
}
