#include "ground/session/GroundRecordingService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>

#include <chrono>
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
    require(recorder.recordRawEpsilonFrame(1'000'000, 0x20, 4,
                                           rawFrame.constData(),
                                           static_cast<size_t>(rawFrame.size())),
            "record raw epsilon frame");
    require(recorder.recordTcpWaveFrame(1'000'100,
                                        littleEndianFloat(1.5f),
                                        littleEndianFloat(3.25f),
                                        VaporView::TcpFloatEncoding::LittleEndian),
            "queue raw TCP wave frame");

    std::this_thread::sleep_for(std::chrono::milliseconds(90));
    require(recorder.pause(), "pause recording");
    require(recorder.isSessionOpen(), "session remains open while paused");
    require(recorder.isPaused(), "paused state");
    require(recorder.start(options, &startError, &errorMessage), "resume recording");
    std::this_thread::sleep_for(std::chrono::milliseconds(35));

    const auto beforeStop = recorder.status();
    require(beforeStop.sensorRows >= 2, "sensor rows written");
    require(beforeStop.rawEpsilonRecords == 1, "raw epsilon count");
    require(beforeStop.rawTcpWaveRecords == 1, "raw TCP wave count");
    const QString sessionDirectory = beforeStop.sessionDirectory;

    const auto summary = recorder.stop();
    require(summary.hadOpenSession, "stop reports open session");
    require(!recorder.isSessionOpen(), "session closed after stop");
    require(summary.sensorRows >= 2, "stop summary rows");

    QFile devicesFile(QDir(sessionDirectory).filePath(QStringLiteral("sensors/devices.csv")));
    require(devicesFile.open(QIODevice::ReadOnly), "open devices.csv");
    const QByteArray devicesCsv = devicesFile.readAll();
    require(devicesCsv.contains("record_timestamp_us"), "devices.csv header");
    require(devicesCsv.contains("30.250000000"), "devices.csv sample");

    QFile metadataFile(QDir(sessionDirectory).filePath(QStringLiteral("session.json")));
    require(metadataFile.open(QIODevice::ReadOnly), "open session metadata");
    const QJsonDocument metadata = QJsonDocument::fromJson(metadataFile.readAll());
    require(metadata.isObject(), "session metadata object");
    const QJsonObject root = metadata.object();
    require(!root.value(QStringLiteral("end_time_utc")).toString().isEmpty(),
            "session end timestamp");
    require(root.value(QStringLiteral("raw_files")).toObject()
                .value(QStringLiteral("epsilon")).toObject()
                .value(QStringLiteral("record_count")).toString() == QStringLiteral("1"),
            "metadata raw epsilon count");

    QFile rawFile(QDir(sessionDirectory).filePath(QStringLiteral("raw/epsilon.dat")));
    require(rawFile.open(QIODevice::ReadOnly), "open raw epsilon file");
    require(rawFile.read(8) == QByteArrayLiteral("VVRAWDAT"), "raw DAT magic");

    std::cout << "ground_recording_service_test passed\n";
    return 0;
}
