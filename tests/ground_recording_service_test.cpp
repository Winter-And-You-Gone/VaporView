#include "ground/session/GroundRecordingService.h"
#include "shared/session/SessionDeviceConfig.h"
#include "shared/session/SessionSensorCsv.h"
#include "shared/session/UnifiedRawDat.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>

#include <chrono>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

QByteArray littleEndianFloat(float value)
{
    quint32 bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    bits = qToLittleEndian(bits);
    return QByteArray(reinterpret_cast<const char *>(&bits), sizeof(bits));
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporaryDirectory;
    require(temporaryDirectory.isValid(), "temporary directory");

    VaporView::Ground::Session::GroundRecordingService recorder;
    recorder.setSensorSnapshotProvider([]() {
        VaporView::Ground::Session::GroundSensorSnapshot snapshot;
        snapshot.hasEpsilon = true;
        snapshot.epsilon.valid = true;
        snapshot.epsilon.timestamp = std::chrono::steady_clock::now();
        snapshot.epsilon.latitude_deg = 30.25;
        snapshot.epsilon.longitude_deg = 120.15;
        snapshot.epsilon.height_m = 42.5;
        snapshot.epsilon.gnss_fix_text = "RTK_FIXED";
        snapshot.hasPtb = true;
        snapshot.ptb.valid = true;
        snapshot.ptb.timestamp = std::chrono::steady_clock::now();
        snapshot.ptb.pressure_hpa = 1012.3;
        return snapshot;
    });

    VaporView::Ground::Session::GroundRecordingOptions options;
    options.baseDirectory = temporaryDirectory.path();
    options.exportRateHz = 50;
    options.deviceConfig.waveformHost = QStringLiteral("192.0.2.5");
    options.deviceConfig.waveformPort = 9000;
    options.deviceConfig.epsilon.port = QStringLiteral("COM7");
    options.deviceConfig.epsilon.baud = QStringLiteral("460800");

    VaporView::Ground::Session::GroundRecordingStartError startError{};
    QString errorMessage;
    require(recorder.start(options, &startError, &errorMessage), "start recording");
    require(startError == VaporView::Ground::Session::GroundRecordingStartError::None,
            "start error state");
    require(recorder.isSessionOpen(), "session open after start");
    require(recorder.isActive(), "recorder active after start");

    const QByteArray rawFrame("\x01\x02\x03", 3);
    const QByteArray laserResponse = QByteArray::fromHex("01030411223344");
    const QByteArray systemResponse = QByteArray::fromHex("0103080001000200030004");
    require(recorder.recordRawEpsilonFrame(1'000'000, 0x20, 4,
                                           rawFrame.constData(),
                                           static_cast<size_t>(rawFrame.size())),
            "record raw epsilon frame");
    require(recorder.recordRawLaserTemperatureControllerResponse(
                1'000'050,
                0x0120,
                laserResponse.constData(),
                static_cast<size_t>(laserResponse.size())),
            "record raw laser temperature response");
    for (quint16 recordType = 1; recordType <= 4; ++recordType)
    {
        require(recorder.recordRawSystemTemperatureControllerResponse(
                    1'000'100 + recordType,
                    recordType,
                    systemResponse.constData(),
                    static_cast<size_t>(systemResponse.size())),
                "record raw system temperature response");
    }
    require(recorder.recordTcpWaveFrame(1'000'100,
                                        littleEndianFloat(1.5f),
                                        littleEndianFloat(3.25f),
                                        VaporView::TcpFloatEncoding::LittleEndian),
            "queue raw TCP wave frame");

    std::this_thread::sleep_for(std::chrono::milliseconds(90));
    require(recorder.pause(), "pause recording");
    require(recorder.isSessionOpen(), "session remains open while paused");
    require(recorder.isPaused(), "paused state");
    const auto pausedStatus = recorder.status();
    require(pausedStatus.recordingElapsedMs > 0, "paused recording preserves elapsed time");
    require(!recorder.recordRawLaserTemperatureControllerResponse(
                1'000'200,
                0x0121,
                laserResponse.constData(),
                static_cast<size_t>(laserResponse.size())),
            "paused recording rejects raw laser temperature response");
    require(recorder.status().rawLaserTemperatureControllerRecords ==
                pausedStatus.rawLaserTemperatureControllerRecords,
            "paused recording does not increase laser raw count");
    require(recorder.start(options, &startError, &errorMessage), "resume recording");
    require(recorder.recordRawLaserTemperatureControllerResponse(
                1'000'250,
                0x0121,
                laserResponse.constData(),
                static_cast<size_t>(laserResponse.size())),
            "resumed recording accepts raw laser temperature response");
    std::this_thread::sleep_for(std::chrono::milliseconds(35));

    const auto beforeStop = recorder.status();
    require(beforeStop.recordingElapsedMs >= pausedStatus.recordingElapsedMs,
            "resumed recording keeps elapsed time monotonic");
    require(beforeStop.sensorRows >= 2, "sensor rows written");
    require(beforeStop.rawNavigationRecords == 1, "raw epsilon count");
    require(beforeStop.rawWaveformRecords == 1, "raw TCP wave count");
    require(beforeStop.rawLaserTemperatureControllerRecords == 2,
            "raw laser temperature count");
    require(beforeStop.rawSystemTemperatureControllerRecords == 4,
            "raw system temperature count");
    const QString sessionDirectory = beforeStop.sessionDirectory;

    const auto summary = recorder.stop();
    require(summary.hadOpenSession, "stop reports open session");
    require(!recorder.isSessionOpen(), "session closed after stop");
    require(summary.sensorRows >= 2, "stop summary rows");

    QFile devicesFile(QDir(sessionDirectory).filePath(QStringLiteral("sensors/sensor_summary.csv")));
    require(devicesFile.open(QIODevice::ReadOnly), "open sensor_summary.csv");
    const QByteArray devicesCsv = devicesFile.readAll();
    require(devicesCsv.contains("record_timestamp_us"), "sensor summary header");
    require(devicesCsv.contains("30.250000000"), "sensor summary sample");
    QByteArray devicesWithoutBom = devicesCsv;
    const QByteArray utf8Bom = QByteArray::fromHex("efbbbf");
    if (devicesWithoutBom.startsWith(utf8Bom))
    {
        devicesWithoutBom.remove(0, utf8Bom.size());
    }
    const qsizetype headerEnd = devicesWithoutBom.indexOf('\n');
    QByteArray actualDevicesHeader = devicesWithoutBom.left(headerEnd);
    if (actualDevicesHeader.endsWith('\r'))
    {
        actualDevicesHeader.chop(1);
    }
    QByteArray expectedDevicesHeader = VaporView::SessionSensorCsv::header().toUtf8();
    expectedDevicesHeader.chop(1);
    require(headerEnd > 0 && actualDevicesHeader == expectedDevicesHeader,
            "ground sensor summary uses shared header exactly");

    QFile metadataFile(QDir(sessionDirectory).filePath(QStringLiteral("session.json")));
    require(metadataFile.open(QIODevice::ReadOnly), "open session metadata");
    const QJsonDocument metadata = QJsonDocument::fromJson(metadataFile.readAll());
    require(metadata.isObject(), "session metadata object");
    const QJsonObject root = metadata.object();
    require(root.value(QStringLiteral("recording_origin")).toString() == QStringLiteral("ground"),
            "metadata recording origin");
    require(!root.contains(QStringLiteral("mode")), "new ground metadata omits legacy mode");
    require(!root.value(QStringLiteral("end_time_utc")).toString().isEmpty(),
            "session end timestamp");
    require(root.value(QStringLiteral("waveform_points_per_frame")).toInt() == 1,
            "ground waveform point count comes from the harmonic payload");
    const QJsonObject counts = root.value(QStringLiteral("counts")).toObject();
    require(counts.value(QStringLiteral("sensor_rows")).toString().toULongLong() >= 2,
            "metadata sensor row count");
    require(root.value(QStringLiteral("raw_files")).toObject()
                 .value(QStringLiteral("navigation")).toObject()
                .value(QStringLiteral("records")).toString() == QStringLiteral("1"),
            "metadata raw epsilon count");
    const QJsonObject paths = root.value(QStringLiteral("paths")).toObject();
    require(paths.value(QStringLiteral("sensor_summary_csv")).toString() == QStringLiteral("sensors/sensor_summary.csv"),
            "metadata sensor summary path");
    require(paths.value(QStringLiteral("laser_temperature_controller_csv")).toString()
                == QStringLiteral("sensors/laser_temperature_controller.csv"),
            "metadata laser temperature controller path");
    require(paths.value(QStringLiteral("system_temperature_controller_csv")).toString()
                == QStringLiteral("sensors/system_temperature_controller.csv"),
            "metadata system temperature controller path");
    require(paths.value(QStringLiteral("waveform_features_csv")).toString()
                == QStringLiteral("sensors/waveform_features.csv"),
            "metadata waveform features path");
    require(QFile::exists(QDir(sessionDirectory).filePath(QStringLiteral("sensors/laser_temperature_controller.csv"))),
            "laser temperature controller csv exists");
    require(QFile::exists(QDir(sessionDirectory).filePath(QStringLiteral("sensors/system_temperature_controller.csv"))),
            "system temperature controller csv exists");
    require(QFile::exists(QDir(sessionDirectory).filePath(QStringLiteral("sensors/waveform_features.csv"))),
            "waveform features csv exists");
    require(QFile::exists(QDir(sessionDirectory).filePath(QStringLiteral("raw/laser_temperature_controller.dat"))),
            "laser temperature controller raw file exists");
    require(QFile::exists(QDir(sessionDirectory).filePath(QStringLiteral("raw/system_temperature_controller.dat"))),
            "system temperature controller raw file exists");

    QFile deviceConfigFile(QDir(sessionDirectory).filePath(QStringLiteral("config/device_config.json")));
    require(deviceConfigFile.open(QIODevice::ReadOnly), "open ground device config");
    const QJsonDocument deviceConfigDocument = QJsonDocument::fromJson(deviceConfigFile.readAll());
    require(deviceConfigDocument.isObject(), "ground device config object");
    const QJsonObject deviceConfig = deviceConfigDocument.object();
    const QJsonObject expectedDeviceConfigSchema =
        VaporView::Session::sessionDeviceConfigToJson(VaporView::Session::SessionDeviceConfig{});
    require(deviceConfig.keys() == expectedDeviceConfigSchema.keys(),
            "ground device config uses shared top-level schema");
    require(deviceConfig.value(QStringLiteral("recording_origin")).toString()
                == QStringLiteral("ground"),
            "ground device config origin");
    require(deviceConfig.value(QStringLiteral("epsilon_schema_version")).isDouble(),
            "ground device config epsilon schema version is numeric");
    const QJsonObject deviceTelemetry = deviceConfig.value(QStringLiteral("telemetry")).toObject();
    require(deviceTelemetry.keys()
                == expectedDeviceConfigSchema.value(QStringLiteral("telemetry")).toObject().keys(),
            "ground device config telemetry schema");
    require(deviceTelemetry.value(QStringLiteral("transport")).toString()
                == QStringLiteral("tcp_wave") &&
                deviceTelemetry.value(QStringLiteral("endpoint")).toString()
                == QStringLiteral("192.0.2.5") &&
                deviceTelemetry.value(QStringLiteral("port")).toString()
                == QStringLiteral("9000") &&
                deviceTelemetry.value(QStringLiteral("baud")).isNull(),
            "ground device config telemetry values");
    const QJsonObject deviceSensors = deviceConfig.value(QStringLiteral("sensors")).toObject();
    require(deviceSensors.keys()
                == expectedDeviceConfigSchema.value(QStringLiteral("sensors")).toObject().keys(),
            "ground device config sensor schema");
    require(deviceSensors.value(QStringLiteral("epsilon")).toObject()
                .value(QStringLiteral("port")).toString() == QStringLiteral("COM7"),
            "ground device config epsilon port");
    require(deviceSensors.value(QStringLiteral("laser_temperature_controller")).toObject()
                .value(QStringLiteral("port")).isNull(),
            "ground device config missing laser temperature port is null");
    require(deviceSensors.value(QStringLiteral("system_temperature_controller")).toObject()
                .value(QStringLiteral("port")).isNull(),
            "ground device config missing system temperature port is null");

    QFile rawFile(QDir(sessionDirectory).filePath(QStringLiteral("raw/navigation.dat")));
    require(rawFile.open(QIODevice::ReadOnly), "open navigation raw file");
    VaporView::SessionRawDat::RawScanOptions scanOptions;
    scanOptions.expectedSourceId = VaporView::SessionRawDat::kSourceNavigation;
    const VaporView::SessionRawDat::RawScanResult rawResult =
        VaporView::SessionRawDat::scan(rawFile, scanOptions);
    require(rawResult.success() && rawResult.records.size() == 1,
            "shared reader scans ground navigation raw DAT");
    require(rawResult.fileHeader.version == VaporView::SessionRawDat::kCurrentFormatVersion,
            "ground writer uses shared current raw version");

    QFile waveformFile(QDir(sessionDirectory).filePath(QStringLiteral("raw/waveform.dat")));
    require(waveformFile.open(QIODevice::ReadOnly), "open waveform raw file");
    VaporView::SessionRawDat::RawScanOptions waveformScanOptions;
    waveformScanOptions.expectedSourceId = VaporView::SessionRawDat::kSourceWaveform;
    const auto waveformRawResult = VaporView::SessionRawDat::scan(waveformFile, waveformScanOptions);
    require(waveformRawResult.success() && waveformRawResult.records.size() == 1,
            "shared reader scans ground waveform raw DAT");
    const auto& waveformRecord = waveformRawResult.records.first();
    require(waveformRecord.header.sourceId == VaporView::SessionRawDat::kSourceWaveform,
            "ground waveform source id");
    require(waveformRecord.header.recordType == VaporView::SessionRawDat::kRecordTypeWaveformPayload,
            "ground waveform record type");
    require((waveformRecord.header.flags & VaporView::SessionRawDat::kWaveformCombinedPayloadFlag) != 0,
            "ground waveform combined flag");
    require(waveformFile.seek(static_cast<qint64>(waveformRecord.payloadOffset)),
            "seek ground waveform payload");
    QByteArray expectedWaveformPayload;
    require(VaporView::SessionRawDat::encodeWaveformPayload(
                littleEndianFloat(1.5f),
                littleEndianFloat(3.25f),
                &expectedWaveformPayload),
            "encode expected ground waveform payload");
    require(waveformFile.read(waveformRecord.header.payloadSize) == expectedWaveformPayload,
            "ground waveform payload remains byte-for-byte equal");

    auto readRawPayload = [](const QString& filename,
                             quint16 sourceId,
                             quint16 expectedRecordType,
                             int expectedRecords) {
        QFile file(filename);
        require(file.open(QIODevice::ReadOnly), "open temperature raw file");
        VaporView::SessionRawDat::RawScanOptions options;
        options.expectedSourceId = sourceId;
        const auto result = VaporView::SessionRawDat::scan(file, options);
        require(result.success() && result.records.size() == expectedRecords,
                "temperature raw record count");
        for (const auto& record : result.records)
        {
            require(record.header.sourceId == sourceId &&
                        record.header.recordType >= expectedRecordType,
                    "temperature raw source and record type");
        }
        require(file.seek(static_cast<qint64>(result.records.first().payloadOffset)),
                "seek temperature raw payload");
        return file.read(result.records.first().header.payloadSize);
    };
    require(readRawPayload(
                QDir(sessionDirectory).filePath(QStringLiteral("raw/laser_temperature_controller.dat")),
                VaporView::SessionRawDat::kSourceLaserTemperatureController,
                0x0120,
                2) == laserResponse,
            "laser temperature raw payload remains byte-for-byte equal");
    require(readRawPayload(
                QDir(sessionDirectory).filePath(QStringLiteral("raw/system_temperature_controller.dat")),
                VaporView::SessionRawDat::kSourceSystemTemperatureController,
                1,
                4) == systemResponse,
            "system temperature raw payload remains byte-for-byte equal");

    require(root.value(QStringLiteral("raw_files")).toObject()
                 .value(QStringLiteral("laser_temperature_controller")).toObject()
                 .value(QStringLiteral("records")).toString() == QStringLiteral("2"),
            "metadata laser temperature raw count");
    require(root.value(QStringLiteral("raw_files")).toObject()
                 .value(QStringLiteral("system_temperature_controller")).toObject()
                 .value(QStringLiteral("records")).toString() == QStringLiteral("4"),
            "metadata system temperature raw count");

    QTemporaryDir concurrentDirectory;
    require(concurrentDirectory.isValid(), "concurrent recording temporary directory");
    VaporView::Ground::Session::GroundRecordingService concurrentRecorder;
    VaporView::Ground::Session::GroundRecordingOptions concurrentOptions = options;
    concurrentOptions.baseDirectory = concurrentDirectory.path();
    require(concurrentRecorder.start(concurrentOptions, &startError, &errorMessage),
            "start concurrent raw recording");

    std::atomic<bool> keepWriting{true};
    std::thread rawWriter([&]() {
        quint64 timestampUs = 2'000'000;
        while (keepWriting.load(std::memory_order_relaxed))
        {
            concurrentRecorder.recordRawEpsilonFrame(
                timestampUs++,
                0x40,
                0,
                rawFrame.constData(),
                static_cast<size_t>(rawFrame.size()));
            std::this_thread::yield();
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto concurrentSummary = concurrentRecorder.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    keepWriting.store(false, std::memory_order_relaxed);
    rawWriter.join();
    require(concurrentSummary.hadOpenSession, "concurrent stop reports open session");
    require(!concurrentRecorder.recordRawEpsilonFrame(
                3'000'000,
                0x40,
                0,
                rawFrame.constData(),
                static_cast<size_t>(rawFrame.size())),
            "raw writes are rejected after concurrent stop");

    std::cout << "ground_recording_service_test passed\n";
    return 0;
}
