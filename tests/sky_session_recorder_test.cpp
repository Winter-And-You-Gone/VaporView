#include "SkySessionRecorder.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QtEndian>
#include <cstring>
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

constexpr char kUnifiedRawMagic[8] = {'V', 'V', 'R', 'A', 'W', 'D', 'A', 'T'};
constexpr quint32 kUnifiedRawRecordMarker = 0x44525756u;
constexpr quint16 kRawSourceTcpWave = 5u;
constexpr quint32 kRawTcpWaveCombinedPayloadFlag = 0x00000001u;

#pragma pack(push, 1)
struct UnifiedRawFileHeader
{
    char magic[8];
    quint32 version;
    quint32 header_size;
    quint16 source_id;
    quint16 reserved;
};

struct UnifiedRawRecordHeader
{
    quint32 marker;
    quint32 header_size;
    quint64 host_timestamp_us;
    quint32 payload_size;
    quint16 source_id;
    quint16 record_type;
    quint32 flags;
    quint64 sequence;
};
#pragma pack(pop)
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("sky_session_recorder_test"));
    app.setApplicationVersion(QStringLiteral("test"));

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary recording directory");

    VaporView::SkySessionRecorder recorder;
    QString error;
    require(recorder.start(tempDir.path(), QStringLiteral("COM50"), 921600, &error), "start recorder");
    require(error.isEmpty(), "start recorder error text");
    require(recorder.isRecording(), "recorder state");
    require(!recorder.sessionName().isEmpty(), "session name");
    VaporView::TelemetryBasic basic;
    basic.host_time_us = 1100;
    basic.epsilon_time_us = 900;
    basic.latitude_deg = 31.230412345;
    basic.longitude_deg = 121.473712345;
    basic.height_m = 1200.1234567;
    basic.ecef_x_m = 1000.1234567;
    basic.ecef_y_m = 2000.1234567;
    basic.ecef_z_m = 3000.1234567;
    basic.lidar_height_m = 120.125f;
    basic.temperature_c = 23.5f;
    basic.humidity_percent = 45.25f;
    basic.pressure_hpa = 900.75f;
    basic.filter_status_bits = 96;
    basic.gnss_fix_code = 6;
    basic.validity_flags = VaporView::BasicHasEpsilonTime |
                           VaporView::BasicHasPosition |
                           VaporView::BasicHasEcef |
                           VaporView::BasicHasLidar |
                           VaporView::BasicHasTemperature |
                           VaporView::BasicHasHumidity |
                           VaporView::BasicHasPressure;
    recorder.recordBasicTelemetry(basic);
    VaporView::WaveformFeature feature;
    feature.host_time_us = 1200;
    feature.epsilon_time_us = 900;
    feature.original_point_count = 50000;
    feature.search_start_index = 1000;
    feature.search_end_index = 1250;
    feature.channel_id = 4;
    feature.peak = 0.125f;
    feature.mean = 0.25f;
    feature.rms = 0.5f;
    feature.peak_index = 1249.0f;
    feature.peak_x = 1249.0f;
    feature.min_value = -0.75f;
    feature.max_value = 1.25f;
    recorder.recordWaveformFeature(feature);
    recorder.recordWaveformSnapshot(1000,
                                    900,
                                    QVector<float>{10.0f, 11.0f},
                                    QVector<float>{1.0f, 2.0f, 3.0f, 4.0f});

    const QString sessionDir = recorder.sessionDirectory();
    require(QFileInfo::exists(sessionDir), "session directory");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/session.json")), "session metadata");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/sensors/devices.csv")), "devices csv");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/waveform_features.csv")), "waveform features csv");
    require(!QFileInfo::exists(sessionDir + QStringLiteral("/waveform_index.csv")), "no waveform index csv");
    require(!QFileInfo::exists(sessionDir + QStringLiteral("/waveforms")), "no waveform bin directory");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/raw/epsilon.dat")), "epsilon raw dat");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/raw/tcp_wave.dat")), "tcp wave raw dat");

    recorder.stop();
    require(!recorder.isRecording(), "recorder stopped");

    QFile devicesFile(sessionDir + QStringLiteral("/sensors/devices.csv"));
    require(devicesFile.open(QIODevice::ReadOnly | QIODevice::Text), "open devices csv");
    const QStringList deviceLines = QString::fromUtf8(devicesFile.readAll()).trimmed().split('\n');
    require(deviceLines.size() == 2, "devices csv row count");
    const QStringList deviceCells = deviceLines.at(1).split(',');
    require(deviceCells.size() == 21, "devices csv column count");
    require(deviceCells.at(3) == QStringLiteral("31.230412345"), "latitude csv precision");
    require(deviceCells.at(4) == QStringLiteral("121.473712345"), "longitude csv precision");
    require(deviceCells.at(5) == QStringLiteral("1200.123457"), "height csv precision");
    require(deviceCells.at(6) == QStringLiteral("1000.123457"), "ecef x csv precision");
    require(deviceCells.at(7) == QStringLiteral("2000.123457"), "ecef y csv precision");
    require(deviceCells.at(8) == QStringLiteral("3000.123457"), "ecef z csv precision");
    require(deviceCells.at(9) == QStringLiteral("120.125000"), "lidar csv precision");
    require(deviceCells.at(10) == QStringLiteral("23.500000"), "temperature csv precision");
    require(deviceCells.at(11) == QStringLiteral("45.250000"), "humidity csv precision");
    require(deviceCells.at(12) == QStringLiteral("900.750000"), "pressure csv precision");

    QFile featuresFile(sessionDir + QStringLiteral("/waveform_features.csv"));
    require(featuresFile.open(QIODevice::ReadOnly | QIODevice::Text), "open waveform features csv");
    const QStringList featureLines = QString::fromUtf8(featuresFile.readAll()).trimmed().split('\n');
    require(featureLines.size() == 2, "waveform features csv row count");
    const QStringList featureCells = featureLines.at(1).split(',');
    require(featureCells.size() == 14, "waveform features csv column count");
    require(featureCells.at(6) == QStringLiteral("0.125000"), "feature peak csv precision");
    require(featureCells.at(7) == QStringLiteral("0.250000"), "feature mean csv precision");
    require(featureCells.at(8) == QStringLiteral("0.500000"), "feature rms csv precision");
    require(featureCells.at(11) == QStringLiteral("-0.750000"), "feature min csv precision");
    require(featureCells.at(12) == QStringLiteral("1.250000"), "feature max csv precision");

    QFile metadataFile(sessionDir + QStringLiteral("/session.json"));
    require(metadataFile.open(QIODevice::ReadOnly), "open metadata");
    const QJsonDocument metadata = QJsonDocument::fromJson(metadataFile.readAll());
    require(metadata.isObject(), "metadata object");
    const QJsonObject root = metadata.object();
    require(root.value(QStringLiteral("waveform_export_mode")).toString() == QStringLiteral("per_frame"),
            "per-frame waveform export mode");
    require(root.value(QStringLiteral("waveform_points_per_frame")).toInt() == 4,
            "waveform points per frame");
    require(root.value(QStringLiteral("waveform_frames")).toString().toULongLong() == 1,
            "waveform frame count");
    require(root.value(QStringLiteral("waveform_file_count")).toString().toULongLong() == 1,
            "waveform file count");
    const QJsonObject rawFiles = root.value(QStringLiteral("raw_files")).toObject();
    require(rawFiles.value(QStringLiteral("tcp_wave")).toObject().value(QStringLiteral("record_count")).toString().toULongLong() == 1,
            "tcp wave raw record count");

    QFile rawFile(sessionDir + QStringLiteral("/raw/tcp_wave.dat"));
    require(rawFile.open(QIODevice::ReadOnly), "open tcp wave raw dat");
    UnifiedRawFileHeader fileHeader{};
    require(rawFile.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader)) == static_cast<qint64>(sizeof(fileHeader)),
            "read tcp wave file header");
    require(std::memcmp(fileHeader.magic, kUnifiedRawMagic, sizeof(fileHeader.magic)) == 0,
            "tcp wave magic");
    require(qFromLittleEndian(fileHeader.source_id) == kRawSourceTcpWave, "tcp wave source id");
    UnifiedRawRecordHeader recordHeader{};
    require(rawFile.read(reinterpret_cast<char*>(&recordHeader), sizeof(recordHeader)) == static_cast<qint64>(sizeof(recordHeader)),
            "read tcp wave record header");
    require(qFromLittleEndian(recordHeader.marker) == kUnifiedRawRecordMarker, "tcp wave record marker");
    require(qFromLittleEndian(recordHeader.host_timestamp_us) == 1000, "tcp wave record timestamp");
    require(qFromLittleEndian(recordHeader.source_id) == kRawSourceTcpWave, "tcp wave record source id");
    require((qFromLittleEndian(recordHeader.flags) & kRawTcpWaveCombinedPayloadFlag) != 0, "tcp wave combined payload flag");

    quint32 rawSizeLe = 0;
    quint32 harmonicSizeLe = 0;
    require(rawFile.read(reinterpret_cast<char*>(&rawSizeLe), sizeof(rawSizeLe)) == static_cast<qint64>(sizeof(rawSizeLe)),
            "read raw payload size");
    require(rawFile.read(reinterpret_cast<char*>(&harmonicSizeLe), sizeof(harmonicSizeLe)) == static_cast<qint64>(sizeof(harmonicSizeLe)),
            "read harmonic payload size");
    require(qFromLittleEndian(rawSizeLe) == 2 * sizeof(float), "raw signal payload byte count");
    require(qFromLittleEndian(harmonicSizeLe) == 4 * sizeof(float), "harmonic payload byte count");
    const QByteArray rawPayload = rawFile.read(2 * static_cast<qint64>(sizeof(float)));
    const QVector<float> decodedRaw = VaporView::decodeTcpFloatPayload(rawPayload, VaporView::TcpFloatEncoding::LittleEndian);
    require(decodedRaw.size() == 2, "decoded raw sample count");
    require(decodedRaw[0] == 10.0f && decodedRaw[1] == 11.0f, "decoded raw samples");
    const QByteArray harmonicPayload = rawFile.read(4 * static_cast<qint64>(sizeof(float)));
    const QVector<float> decoded = VaporView::decodeTcpFloatPayload(harmonicPayload, VaporView::TcpFloatEncoding::LittleEndian);
    require(decoded.size() == 4, "decoded harmonic sample count");
    require(decoded[0] == 1.0f && decoded[1] == 2.0f && decoded[2] == 3.0f && decoded[3] == 4.0f,
            "decoded harmonic samples");
    return 0;
}
