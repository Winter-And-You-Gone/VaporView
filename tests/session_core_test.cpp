#include "ground/session/SessionCsv.h"
#include "ground/session/SessionIndex.h"
#include "ground/session/SessionLoader.h"
#include "ground/session/SessionPlaybackController.h"
#include "ground/session/RecordingSessionLayout.h"
#include "ground/session/SessionWaveformRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTemporaryDir>
#include <QtEndian>

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template <typename T>
void appendLittleEndian(QByteArray& bytes, T value)
{
    const T encoded = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char*>(&encoded), sizeof(encoded));
}

void appendLittleEndianFloat(QByteArray& bytes, float value)
{
    quint32 bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    appendLittleEndian(bytes, bits);
}

QByteArray floatPayload(std::initializer_list<float> values)
{
    QByteArray payload;
    for (float value : values)
    {
        appendLittleEndianFloat(payload, value);
    }
    return payload;
}

QByteArray unifiedTcpWaveBytes(quint32 version, int recordCount)
{
    QByteArray fileBytes("VVRAWDAT", 8);
    appendLittleEndian(fileBytes, version);
    appendLittleEndian<quint32>(fileBytes, 20);
    appendLittleEndian<quint16>(fileBytes, 5);
    appendLittleEndian<quint16>(fileBytes, 0);

    const QByteArray rawSignal = floatPayload({99.0f});
    const QByteArray harmonic = floatPayload({1.5f, 2.5f, 3.5f});
    const quint32 payloadSize = static_cast<quint32>(8 + rawSignal.size() + harmonic.size());
    for (int index = 0; index < recordCount; ++index)
    {
        appendLittleEndian<quint32>(fileBytes, 0x44525756u);
        appendLittleEndian<quint32>(fileBytes, 36);
        appendLittleEndian<quint64>(fileBytes, 4000000u + static_cast<quint64>(index));
        appendLittleEndian(fileBytes, payloadSize);
        appendLittleEndian<quint16>(fileBytes, 5);
        appendLittleEndian<quint16>(fileBytes, 1);
        appendLittleEndian<quint32>(
            fileBytes,
            0x00000001u |
                VaporView::tcpFloatEncodingToRawDatFlags(VaporView::TcpFloatEncoding::LittleEndian));
        appendLittleEndian<quint64>(fileBytes, static_cast<quint64>(index));
        appendLittleEndian<quint32>(fileBytes, static_cast<quint32>(rawSignal.size()));
        appendLittleEndian<quint32>(fileBytes, static_cast<quint32>(harmonic.size()));
        fileBytes.append(rawSignal);
        fileBytes.append(harmonic);
    }
    return fileBytes;
}

void writeBytes(const QString& filename, const QByteArray& bytes)
{
    QFile file(filename);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "binary fixture opens");
    require(file.write(bytes) == bytes.size(), "binary fixture is written");
}

void testTimestampIndexBoundaries()
{
    const QVector<quint64> timestamps{100, 200, 400};
    require(VaporView::Ground::Session::closestTimestampIndex({}, 100) == -1,
            "empty timestamp index has no match");
    require(VaporView::Ground::Session::closestTimestampIndex(timestamps, 50) == 0,
            "timestamp before range selects first row");
    require(VaporView::Ground::Session::closestTimestampIndex(timestamps, 300) == 1,
            "equal timestamp distance selects earlier row");
    require(VaporView::Ground::Session::closestTimestampIndex(timestamps, 390) == 2,
            "nearest timestamp selects upper row");
    require(VaporView::Ground::Session::closestTimestampIndex(timestamps, 500) == 2,
            "timestamp after range selects final row");
}

void testMeasuredRateIgnoresMissingSamples()
{
    const QVector<quint64> timestamps{0, 1000000, 1500000, 2000000, 0};
    require(std::abs(VaporView::Ground::Session::measuredRateHz(timestamps) - 2.0) < 1e-9,
            "measured rate uses valid timestamp span");
    require(VaporView::Ground::Session::measuredRateHz({0, 1000000}) == 0.0,
            "single valid timestamp has no measurable rate");
}

void testPlaybackStateAndBoundaries()
{
    VaporView::Ground::SessionPlaybackController controller;
    int lastFrame = -2;
    int frameSignalCount = 0;
    QObject::connect(&controller,
                     &VaporView::Ground::SessionPlaybackController::currentFrameChanged,
                     [&lastFrame, &frameSignalCount](int frame) {
                         lastFrame = frame;
                         ++frameSignalCount;
                     });

    controller.setTimeline(3, {1000, 2000, 3000});
    require(controller.currentFrame() == 0 && lastFrame == 0,
            "timeline starts on its first frame");
    controller.seek(99);
    require(controller.currentFrame() == 2,
            "seek clamps to final frame");
    controller.play();
    require(controller.isPlaying() && controller.currentFrame() == 0,
            "play at end restarts from first frame");

    QElapsedTimer elapsed;
    elapsed.start();
    while (controller.isPlaying() && elapsed.elapsed() < 200)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    require(!controller.isPlaying() && controller.currentFrame() == 2,
            "playback stops at final frame");
    require(frameSignalCount >= 4,
            "playback emits frame changes while advancing");

    controller.setSpeed(4.0);
    require(controller.speed() == 4.0,
            "playback speed is configurable");
    controller.setSpeed(0.0);
    require(controller.speed() == 4.0,
            "invalid playback speed is ignored");
    controller.clear();
    require(controller.frameCount() == 0 && controller.currentFrame() == -1,
            "clearing playback resets timeline state");
}

void testSessionMetadataLoading()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary session directory is available");
    QFile metadataFile(sessionDir.filePath(QStringLiteral("session.json")));
    require(metadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "session metadata fixture opens");
    metadataFile.write(R"json({
        "session_name": "legacy-compatible",
        "start_time_utc": "2026-07-16T00:00:00Z",
        "sensor_rows": 42,
        "waveform_frames": 7,
        "waveform_export_rate_hz": 0,
        "paths": {"devices_csv": "custom/devices.csv"}
    })json");
    metadataFile.close();

    const QString resolvedFromDirectory =
        VaporView::Ground::SessionLoader::resolveSessionDirectory(sessionDir.path());
    const QString resolvedFromFile =
        VaporView::Ground::SessionLoader::resolveSessionDirectory(metadataFile.fileName());
    require(!resolvedFromDirectory.isEmpty() && resolvedFromDirectory == resolvedFromFile,
            "directory and session.json resolve to the same session root");

    const VaporView::Ground::SessionMetadataLoadResult result =
        VaporView::Ground::SessionLoader::loadMetadata(sessionDir.path());
    require(result.success, "valid session metadata loads without a widget");
    require(result.metadata.sessionName == QStringLiteral("legacy-compatible"),
            "session name is preserved");
    require(result.metadata.recordingOrigin == VaporView::Session::RecordingOrigin::Ground,
            "legacy session without origin defaults to ground");
    require(result.metadata.sensorRows == 42 && result.metadata.waveformFrames == 7,
            "session counters are preserved");
    require(result.metadata.waveformExportMode == QStringLiteral("per_frame"),
            "missing export mode keeps the historical rate-based default");
    require(result.metadata.sensorSummaryCsvFilename.endsWith(QStringLiteral("custom/devices.csv")),
            "custom sensor path is resolved relative to the session");
    require(result.metadata.waveformRawFilename.endsWith(QStringLiteral("raw/waveform.dat")),
            "missing raw path uses the semantic default");

    QTemporaryDir skySessionDir;
    require(skySessionDir.isValid(), "temporary sky session directory is available");
    QFile skyMetadataFile(skySessionDir.filePath(QStringLiteral("session.json")));
    require(skyMetadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "sky session metadata fixture opens");
    skyMetadataFile.write(R"json({
        "session_format": "vaporview.session",
        "session_format_version": 1,
        "recording_origin": "sky",
        "session_name": "new-sky",
        "state": "complete",
        "start_time_utc": "2026-07-16T00:00:00Z",
        "start_time_us": "1000",
        "end_time_utc": "2026-07-16T00:00:01Z",
        "end_time_us": "2000",
        "elapsed_ms": "1000",
        "software_version": "test",
        "timestamp_unit": "microseconds",
        "raw_dat_format_version": 2,
        "epsilon_schema_version": 1,
        "sensor_export_rate_hz": 25,
        "other_devices_export_rate_hz": 25,
        "raw_export_mode": "unified_raw_dat",
        "waveform_export_rate_hz": 0,
        "waveform_export_mode": "per_frame",
        "waveform_value_type": "float32",
        "waveform_timestamp_type": "uint64",
        "waveform_points_per_frame": 50000,
        "waveform_file_count": "1",
        "capture": {
            "telemetry_transport": null,
            "telemetry_endpoint": null,
            "telemetry_port": null,
            "telemetry_baud": null
        },
        "counts": {
            "sensor_rows": "11",
            "temperature_controller_rows": "0",
            "waveform_frames": "3",
            "waveform_feature_rows": "0",
            "event_rows": "0",
            "error_rows": "0"
        },
        "paths": {
            "devices_csv": "sensors/devices.csv",
            "temperature_controller_csv": "sensors/rd105_temperature_controller.csv",
            "waveform_features_csv": "sensors/waveform_features.csv",
            "epsilon_raw": "raw/epsilon.dat",
            "ptb_raw": "raw/ptb.dat",
            "hmp_raw": "raw/hmp.dat",
            "lidar_raw": "raw/lidar.dat",
            "tcp_wave_raw": "raw/tcp_wave.dat",
            "tcp_wave_peaks_csv": "raw/tcp_wave_peaks.csv",
            "event_log": "logs/event_log.csv",
            "error_log": "logs/error_log.txt",
            "device_config": "config/device_config.json",
            "raw_format_document": "raw_dat_format.md"
        },
        "raw_files": {
            "epsilon": {"path": "raw/epsilon.dat", "source_id": 1, "format_version": 2, "records": "0"},
            "ptb": {"path": "raw/ptb.dat", "source_id": 2, "format_version": 2, "records": "0"},
            "hmp": {"path": "raw/hmp.dat", "source_id": 3, "format_version": 2, "records": "0"},
            "lidar": {"path": "raw/lidar.dat", "source_id": 4, "format_version": 2, "records": "0"},
            "tcp_wave": {"path": "raw/tcp_wave.dat", "source_id": 5, "format_version": 2, "records": "3"}
        }
    })json");
    skyMetadataFile.close();

    const VaporView::Ground::SessionMetadataLoadResult skyResult =
        VaporView::Ground::SessionLoader::loadMetadata(skySessionDir.path());
    require(skyResult.success, "new shared session metadata loads without a widget");
    require(skyResult.metadata.recordingOrigin == VaporView::Session::RecordingOrigin::Sky,
            "new recording_origin is exposed by the loader");
    require(skyResult.metadata.sensorRows == 11 && skyResult.metadata.waveformFrames == 3,
            "new nested session counters are exposed by the loader");
    require(skyResult.metadata.waveformPeaksCsvFilename.endsWith(QStringLiteral("raw/tcp_wave_peaks.csv")),
            "new tcp wave peaks path is resolved by the loader");

    QTemporaryDir legacySkyDir;
    require(legacySkyDir.isValid(), "temporary legacy sky session directory is available");
    QFile legacySkyMetadataFile(legacySkyDir.filePath(QStringLiteral("session.json")));
    require(legacySkyMetadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "legacy sky metadata fixture opens");
    legacySkyMetadataFile.write(R"json({
        "mode": "sky",
        "session_name": "legacy-sky",
        "sensor_rows": "1"
    })json");
    legacySkyMetadataFile.close();
    const VaporView::Ground::SessionMetadataLoadResult legacySkyResult =
        VaporView::Ground::SessionLoader::loadMetadata(legacySkyDir.path());
    require(legacySkyResult.success &&
                legacySkyResult.metadata.recordingOrigin == VaporView::Session::RecordingOrigin::Sky,
            "legacy mode=sky is exposed by the loader");

    QFile invalidFile(sessionDir.filePath(QStringLiteral("invalid.txt")));
    require(invalidFile.open(QIODevice::WriteOnly), "invalid path fixture opens");
    invalidFile.close();
    require(VaporView::Ground::SessionLoader::resolveSessionDirectory(invalidFile.fileName()).isEmpty(),
            "non-session files are rejected");
}

void testRecordingSessionLayout()
{
    QTemporaryDir recordsDir;
    require(recordsDir.isValid(), "recording layout root is available");

    const auto first = VaporView::Ground::Session::createRecordingSessionLayout(
        recordsDir.path(), QStringLiteral("session"));
    require(first.has_value(), "first recording session layout is created");
    require(first->sessionName == QStringLiteral("session"),
            "first recording session keeps the requested name");
    require(QFileInfo::exists(first->sensorSummaryFilename) == false,
            "recording layout creates directories without opening data files");
    require(QDir(first->sessionDirectory).exists(QStringLiteral("sensors")) &&
                QDir(first->sessionDirectory).exists(QStringLiteral("raw")) &&
                QDir(first->sessionDirectory).exists(QStringLiteral("logs")) &&
                QDir(first->sessionDirectory).exists(QStringLiteral("config")),
            "recording layout creates all compatible subdirectories");
    require(first->waveformPeaksFilename.endsWith(
                QStringLiteral("raw/waveform_peaks.csv")),
            "recording layout preserves the raw waveform peak index path");

    const auto second = VaporView::Ground::Session::createRecordingSessionLayout(
        recordsDir.path(), QStringLiteral("session"));
    require(second.has_value() && second->sessionName == QStringLiteral("session_1"),
            "existing recording session names receive a deterministic suffix");
}

void testManifestlessAndDualPathSessionCompatibility()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "manifestless compatibility session is available");
    require(QDir(sessionDir.path()).mkpath(QStringLiteral("sensors")) &&
                QDir(sessionDir.path()).mkpath(QStringLiteral("raw")),
            "manifestless compatibility directories are created");

    const QString legacySummary = sessionDir.filePath(QStringLiteral("sensors/devices.csv"));
    const QString legacyWaveform = sessionDir.filePath(QStringLiteral("raw/tcp_wave.dat"));
    writeBytes(legacySummary, QByteArrayLiteral("legacy summary"));
    writeBytes(legacyWaveform, unifiedTcpWaveBytes(2, 0));

    auto result = VaporView::Ground::SessionLoader::loadMetadata(sessionDir.path());
    require(result.success && result.metadata.sensorSummaryCsvFilename.endsWith(
                QStringLiteral("sensors/devices.csv")) &&
                result.metadata.waveformRawFilename.endsWith(QStringLiteral("raw/tcp_wave.dat")),
            "manifestless legacy session resolves old standard paths");
    require(!result.warning.isEmpty(), "manifestless session emits a compatibility warning");

    const QString preferredSummary = sessionDir.filePath(QStringLiteral("sensors/sensor_summary.csv"));
    const QString preferredWaveform = sessionDir.filePath(QStringLiteral("raw/waveform.dat"));
    writeBytes(preferredSummary, QByteArrayLiteral("preferred summary"));
    writeBytes(preferredWaveform, unifiedTcpWaveBytes(2, 0));
    result = VaporView::Ground::SessionLoader::loadMetadata(sessionDir.path());
    require(result.success && result.metadata.sensorSummaryCsvFilename.endsWith(
                QStringLiteral("sensors/sensor_summary.csv")) &&
                result.metadata.waveformRawFilename.endsWith(QStringLiteral("raw/waveform.dat")),
            "manifestless session prefers semantic paths when both files exist");
    require(result.warning.contains(QStringLiteral("preferred session file")),
            "dual-path manifestless session reports the selected semantic files");
}

void testSensorCsvLoadingAndTrackFiltering()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary sensor session directory is available");
    require(QDir(sessionDir.path()).mkpath(QStringLiteral("sensors")),
            "sensor fixture directory is created");

    const QString csvPath = sessionDir.filePath(QStringLiteral("sensors/devices.csv"));
    QFile csvFile(csvPath);
    require(csvFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "sensor CSV fixture opens");
    csvFile.write(
        "record_timestamp_us,nav_lat_deg,nav_lon_deg,nav_height_m,epsilon_valid,gnss_fix,"
        "hmp_temperature_c,hmp_humidity_rh,ptb_pressure_hpa\n"
        "1000000,30.0000000,120.0000000,10,true,FIXED,21.5,48.0,1001.2\n"
        "2000000,30.0000100,120.0000100,11,true,FIXED,21.6,48.5,1001.3\n"
        "3000000,30.0000200,120.0000200,12,false,FIXED,21.7,49.0,1001.4\n"
        "4000000,31.0000000,121.0000000,13,true,FIXED,21.8,49.5,1001.5\n");
    csvFile.close();

    VaporView::Ground::SessionMetadata metadata;
    metadata.sensorSummaryCsvFilename = csvPath;
    metadata.sensorRows = 4;
    const VaporView::Ground::SessionSensorLoadResult result =
        VaporView::Ground::SessionLoader::loadSensors(metadata);
    require(result.success && result.fileAvailable,
            "sensor CSV loads without a widget");
    require(result.data.rows.size() == 4 && result.data.timestamps_us.size() == 4,
            "all sensor rows and timestamps are indexed");
    require(result.data.temperature_values.at(1) == 21.6 &&
                result.data.humidity_values.at(1) == 48.5 &&
                result.data.pressure_values.at(1) == 1001.3,
            "environment series retain recorded values");
    require(result.data.track_stats.scanned_rows == 4 &&
                result.data.track_stats.accepted_points == 2 &&
                result.data.track_stats.rejected_invalid_nav == 1 &&
                result.data.track_stats.rejected_jump == 1,
            "track filtering preserves invalid-navigation and jump accounting");
    require(result.data.track_points.size() == 2 &&
                result.data.track_points.last().has_speed &&
                result.data.track_points.last().cumulative_distance_m > 0.0,
            "accepted track points retain distance and speed state");

    const QStringList quoted = VaporView::Ground::SessionCsv::parseCsvLine(
        QStringLiteral("plain,\"quoted, field\",\"escaped \"\"quote\"\"\""));
    require(quoted == QStringList{QStringLiteral("plain"),
                                  QStringLiteral("quoted, field"),
                                  QStringLiteral("escaped \"quote\"")},
            "CSV parser preserves commas and escaped quotes");
}

void testLegacyWaveformCatalogAndPeakCache()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary legacy waveform session is available");
    require(QDir(sessionDir.path()).mkpath(QStringLiteral("waveform")),
            "legacy waveform directory is created");

    QByteArray frames;
    appendLittleEndian<quint64>(frames, 1000000);
    frames.append(floatPayload({1.0f, 2.0f, 3.0f}));
    appendLittleEndian<quint64>(frames, 2000000);
    frames.append(floatPayload({4.0f, 5.0f, 6.0f}));
    QFile waveformFile(sessionDir.filePath(QStringLiteral("waveform/segment_000.dat")));
    require(waveformFile.open(QIODevice::WriteOnly), "legacy waveform fixture opens");
    require(waveformFile.write(frames) == frames.size(), "legacy waveform fixture is written");
    waveformFile.close();

    VaporView::Ground::SessionMetadata metadata;
    metadata.sessionDirectory = sessionDir.path();
    metadata.waveformDirectory = sessionDir.filePath(QStringLiteral("waveform"));
    metadata.waveformPeaksCsvFilename = sessionDir.filePath(QStringLiteral("raw/tcp_wave_peaks.csv"));
    metadata.waveformPointsPerFrame = 3;
    const auto catalogResult = VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata);
    require(catalogResult.success && catalogResult.catalog.frameCount == 2,
            "legacy waveform frames are indexed without a widget");
    require(catalogResult.catalog.sourceFileCount() == 1,
            "legacy waveform catalog reports its source file count");

    const auto frame = VaporView::Ground::SessionWaveformRepository::readFrame(
        catalogResult.catalog,
        1);
    require(frame.success && frame.timestampUs == 2000000 && frame.samples.size() == 3,
            "legacy waveform frame timestamp and samples are decoded");
    require(frame.samples.at(0) == 4.0f && frame.samples.at(2) == 6.0f,
            "legacy waveform sample values are preserved");

    const auto partialPeaks = VaporView::Ground::SessionWaveformRepository::calculatePeakSeries(
        catalogResult.catalog,
        1,
        3);
    require(partialPeaks.success && partialPeaks.peakValues == QVector<float>({3.0f, 6.0f}),
            "waveform peak calculation honors the sample search range");

    const auto fullPeaks = VaporView::Ground::SessionWaveformRepository::calculatePeakSeries(
        catalogResult.catalog,
        0,
        0);
    require(fullPeaks.success && fullPeaks.timestampsUs.size() == 2,
            "full-frame waveform peaks are calculated");
    require(VaporView::Ground::SessionWaveformRepository::writeCachedPeakSeries(
                catalogResult.catalog,
                fullPeaks.timestampsUs,
                fullPeaks.peakValues),
            "waveform peak cache is written atomically");
    const auto cached = VaporView::Ground::SessionWaveformRepository::loadCachedPeakSeries(
        catalogResult.catalog);
    require(cached.success && cached.peakValues == fullPeaks.peakValues,
            "waveform peak cache round-trips recorded values");

    VaporView::Ground::SessionPeakFilterSettings filter;
    filter.mode = VaporView::Ground::SessionPeakFilterMode::KeepRange;
    filter.minValue = 5.5;
    filter.maxValue = 6.5;
    const QVector<float> filtered =
        VaporView::Ground::SessionWaveformRepository::applyPeakFilter(fullPeaks.peakValues, filter);
    require(std::isnan(filtered.at(0)) && filtered.at(1) == 6.0f,
            "peak range filter replaces excluded values with NaN");
}

void testIndexedWaveformCatalog()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary indexed waveform session is available");
    require(QDir(sessionDir.path()).mkpath(QStringLiteral("waveform")),
            "indexed waveform directory is created");

    QFile frameFile(sessionDir.filePath(QStringLiteral("waveform/frame_000.bin")));
    require(frameFile.open(QIODevice::WriteOnly), "indexed waveform fixture opens");
    const QByteArray payload = floatPayload({-1.0f, 0.25f, 2.5f});
    require(frameFile.write(payload) == payload.size(), "indexed waveform fixture is written");
    frameFile.close();

    QFile indexFile(sessionDir.filePath(QStringLiteral("waveform_index.csv")));
    require(indexFile.open(QIODevice::WriteOnly | QIODevice::Text),
            "waveform index fixture opens");
    indexFile.write("host_time_us,point_count,filename\n");
    indexFile.write("3000000,3,waveform/frame_000.bin\n");
    indexFile.close();

    VaporView::Ground::SessionMetadata metadata;
    metadata.sessionDirectory = sessionDir.path();
    metadata.waveformDirectory = sessionDir.filePath(QStringLiteral("waveform"));
    metadata.waveformIndexFilename = indexFile.fileName();
    metadata.waveformPointsPerFrame = 0;
    const auto catalogResult = VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata);
    require(catalogResult.success && catalogResult.catalog.indexedFrames.size() == 1,
            "indexed waveform catalog is selected when raw data is absent");
    require(catalogResult.catalog.pointsPerFrame == 3,
            "indexed waveform point count supplies missing metadata");
    const auto frame = VaporView::Ground::SessionWaveformRepository::readFrame(
        catalogResult.catalog,
        0);
    require(frame.success && frame.timestampUs == 3000000 && frame.samples.at(2) == 2.5f,
            "indexed waveform file is decoded through the repository");
}

void testUnifiedRawWaveformCatalog()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary unified raw waveform session is available");
    require(QDir(sessionDir.path()).mkpath(QStringLiteral("raw")),
            "unified raw waveform directory is created");

    const QByteArray fileBytes = unifiedTcpWaveBytes(1u, 1);

    QFile rawFile(sessionDir.filePath(QStringLiteral("raw/tcp_wave.dat")));
    require(rawFile.open(QIODevice::WriteOnly), "unified raw waveform fixture opens");
    require(rawFile.write(fileBytes) == fileBytes.size(), "unified raw waveform fixture is written");
    rawFile.close();

    VaporView::Ground::SessionMetadata metadata;
    metadata.sessionDirectory = sessionDir.path();
    metadata.waveformRawFilename = rawFile.fileName();
    metadata.waveformPointsPerFrame = 0;
    const auto catalogResult = VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata);
    require(catalogResult.success && catalogResult.catalog.rawTcpFrames.size() == 1,
            "unified raw TCP waveform catalog has highest precedence");
    require(catalogResult.catalog.pointsPerFrame == 3,
            "unified raw harmonic payload supplies its frame size");
    const auto frame = VaporView::Ground::SessionWaveformRepository::readFrame(
        catalogResult.catalog,
        0);
    require(frame.success && frame.timestampUs == 4000000 && frame.samples.at(0) == 1.5f &&
                frame.samples.at(2) == 3.5f,
            "unified raw repository reads only the harmonic payload");
}

void testUnifiedRawValidationRecoveryAndLegacyFallback()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary raw validation session is available");
    require(QDir(sessionDir.path()).mkpath(QStringLiteral("raw")),
            "raw validation directory is created");
    require(QDir(sessionDir.path()).mkpath(QStringLiteral("waveform")),
            "legacy fallback directory is created");

    const QString rawPath = sessionDir.filePath(QStringLiteral("raw/tcp_wave.dat"));
    const QString legacyPath = sessionDir.filePath(QStringLiteral("waveform/segment_000.dat"));
    QByteArray legacyFrame;
    appendLittleEndian<quint64>(legacyFrame, 1234u);
    legacyFrame.append(floatPayload({7.0f}));
    writeBytes(legacyPath, legacyFrame);

    VaporView::Ground::SessionMetadata metadata;
    metadata.sessionDirectory = sessionDir.path();
    metadata.waveformRawFilename = rawPath;
    metadata.waveformDirectory = sessionDir.filePath(QStringLiteral("waveform"));
    metadata.waveformPointsPerFrame = 1;

    writeBytes(rawPath, unifiedTcpWaveBytes(3u, 1));
    auto result = VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata);
    require(!result.success && result.error.contains(QStringLiteral("version 3")) &&
                result.error.contains(QStringLiteral("1, 2")),
            "unknown unified raw version is not allowed to fall back to legacy waveform data");

    writeBytes(rawPath, unifiedTcpWaveBytes(0u, 1));
    result = VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata);
    require(!result.success && result.error.contains(QStringLiteral("version 0")),
            "raw version zero is rejected explicitly");

    QByteArray nonUnified(20, '\0');
    nonUnified.replace(0, 8, QByteArrayLiteral("OLDFORMT"));
    writeBytes(rawPath, nonUnified);
    result = VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata);
    require(result.success && result.catalog.legacySegments.size() == 1,
            "file without unified magic retains the historical waveform fallback");

    writeBytes(rawPath, unifiedTcpWaveBytes(2u, 0).left(12));
    result = VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata);
    require(!result.success && result.error.contains(QStringLiteral("Truncated raw DAT file header")),
            "truncated unified file header is fatal and does not use legacy fallback");

    QByteArray truncatedPayload = unifiedTcpWaveBytes(2u, 2);
    truncatedPayload.chop(2);
    writeBytes(rawPath, truncatedPayload);
    result = VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata);
    require(result.success && result.catalog.rawTcpFrames.size() == 1 && !result.warning.isEmpty(),
            "truncated final raw payload preserves complete frames and reports recovery");

    QByteArray truncatedHeader = unifiedTcpWaveBytes(2u, 1);
    truncatedHeader.append(unifiedTcpWaveBytes(2u, 1).mid(20, 10));
    writeBytes(rawPath, truncatedHeader);
    result = VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata);
    require(result.success && result.catalog.rawTcpFrames.size() == 1 &&
                result.warning.contains(QStringLiteral("header")),
            "truncated final raw record header preserves complete frames and reports recovery");

    QByteArray corruptMiddle = unifiedTcpWaveBytes(2u, 2);
    const qsizetype secondRecordOffset = 20 + 36 + 24;
    const quint32 invalidMarker = qToLittleEndian<quint32>(0x12345678u);
    std::memcpy(corruptMiddle.data() + secondRecordOffset, &invalidMarker, sizeof(invalidMarker));
    writeBytes(rawPath, corruptMiddle);
    result = VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata);
    require(!result.success && result.error.contains(QStringLiteral("record marker")),
            "middle raw record corruption remains fatal");
}

}  // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    testTimestampIndexBoundaries();
    testMeasuredRateIgnoresMissingSamples();
    testPlaybackStateAndBoundaries();
    testSessionMetadataLoading();
    testManifestlessAndDualPathSessionCompatibility();
    testRecordingSessionLayout();
    testSensorCsvLoadingAndTrackFiltering();
    testLegacyWaveformCatalogAndPeakCache();
    testIndexedWaveformCatalog();
    testUnifiedRawWaveformCatalog();
    testUnifiedRawValidationRecoveryAndLegacyFallback();

    std::cout << "session_core_test passed\n";
    return 0;
}
