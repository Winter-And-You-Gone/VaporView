#include "SkySessionRecorder.h"
#include "geo/CoordinateTransform.h"

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
    VaporView::EpsilonData epsilon;
    epsilon.valid = true;
    epsilon.device_timestamp_us = 900;
    epsilon.utc_unix_s = 100;
    epsilon.utc_microseconds = 200;
    epsilon.latitude_deg = 31.230412345;
    epsilon.longitude_deg = 121.473712345;
    epsilon.height_m = 1200.1234567;
    epsilon.ecef_x_m = 365504425.008990;
    epsilon.ecef_y_m = 13374370.950326;
    epsilon.ecef_z_m = 58160.200631;
    epsilon.ned_n_m = 1.25;
    epsilon.ned_e_m = -2.5;
    epsilon.ned_d_m = 0.75;
    epsilon.vel_n_mps = 0.5;
    epsilon.vel_e_mps = -0.25;
    epsilon.vel_d_mps = 0.125;
    epsilon.body_vel_x_mps = 0.6;
    epsilon.body_vel_y_mps = -0.35;
    epsilon.body_vel_z_mps = 0.225;
    epsilon.body_acc_x_mps2 = 0.01;
    epsilon.body_acc_y_mps2 = -0.02;
    epsilon.body_acc_z_mps2 = 0.03;
    epsilon.roll_deg = 1.25;
    epsilon.pitch_deg = -2.5;
    epsilon.yaw_deg = 88.75;
    epsilon.quat_w = 0.75;
    epsilon.quat_x = 0.01;
    epsilon.quat_y = -0.02;
    epsilon.quat_z = 0.03;
    epsilon.ang_vel_x_radps = 0.001;
    epsilon.ang_vel_y_radps = -0.002;
    epsilon.ang_vel_z_radps = 0.003;
    epsilon.imu_acc_x_mps2 = 9.1;
    epsilon.imu_acc_y_mps2 = -0.2;
    epsilon.imu_acc_z_mps2 = 0.3;
    epsilon.imu_gyr_x_radps = 0.004;
    epsilon.imu_gyr_y_radps = -0.005;
    epsilon.imu_gyr_z_radps = 0.006;
    epsilon.mag_x_mg = 280.5;
    epsilon.mag_y_mg = -35.25;
    epsilon.mag_z_mg = 410.125;
    epsilon.gnss_fix_text = "RTK_FIXED";
    epsilon.gnss_satellites = 18;
    epsilon.hdop = 0.75;
    epsilon.vdop = 1.25;
    epsilon.hacc_m = 0.0123;
    epsilon.vacc_m = 0.0456;
    epsilon.lat_std_m = 0.0111;
    epsilon.lon_std_m = 0.0222;
    epsilon.height_std_m = 0.0333;
    epsilon.diff_age_s = 0.5;
    epsilon.heading_valid = true;
    epsilon.filter_status_bits = 96;
    epsilon.gnss_fix_code = 6;
    epsilon.imu_packet_rate_hz = 100.0;
    epsilon.ahrs_packet_rate_hz = 40.0;
    epsilon.insgps_packet_rate_hz = 41.0;
    epsilon.sys_state_packet_rate_hz = 42.0;
    epsilon.raw_gnss_packet_rate_hz = 50.0;
    epsilon.satellite_packet_rate_hz = 59.0;
    epsilon.geodetic_packet_rate_hz = 10.0;
    epsilon.ecef_packet_rate_hz = 11.0;

    VaporView::PtbData ptb;
    ptb.valid = true;
    ptb.pressure_hpa = 900.75;
    VaporView::HmpData hmp;
    hmp.valid = true;
    hmp.temperature = 23.5;
    hmp.humidity = 45.25;
    VaporView::LidarData lidar;
    lidar.valid = true;
    lidar.distance_m = 120.125;
    lidar.signal_strength = 180;
    recorder.recordDeviceSnapshot(1100, 1000, epsilon, true, ptb, true, hmp, true, lidar, true);
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
    require(QFileInfo::exists(sessionDir + QStringLiteral("/raw/tcp_wave_peaks.csv")), "tcp wave peak index csv");

    recorder.stop();
    require(!recorder.isRecording(), "recorder stopped");

    QFile devicesFile(sessionDir + QStringLiteral("/sensors/devices.csv"));
    require(devicesFile.open(QIODevice::ReadOnly | QIODevice::Text), "open devices csv");
    const QStringList deviceLines = QString::fromUtf8(devicesFile.readAll()).trimmed().split('\n');
    require(deviceLines.size() == 2, "devices csv row count");
    const QStringList deviceHeaders = deviceLines.at(0).split(',');
    require(deviceHeaders.size() == 72, "devices csv header column count");
    require(deviceHeaders.contains(QStringLiteral("gnss_satellites")), "devices csv has satellites");
    require(deviceHeaders.contains(QStringLiteral("imu_acc_x_mps2")), "devices csv has imu accel");
    require(deviceHeaders.contains(QStringLiteral("epsilon_imu_packet_rate_hz")), "devices csv has epsilon packet rates");
    require(deviceHeaders.contains(QStringLiteral("lidar_signal_strength")), "devices csv has lidar strength");
    const QStringList deviceCells = deviceLines.at(1).split(',');
    require(deviceCells.size() == 72, "devices csv column count");
    require(deviceCells.at(5) == QStringLiteral("31.230412345"), "latitude csv precision");
    require(deviceCells.at(6) == QStringLiteral("121.473712345"), "longitude csv precision");
    require(deviceCells.at(7) == QStringLiteral("1200.123457"), "height csv precision");
    VaporView::Geo::EcefPoint expectedEcef;
    require(VaporView::Geo::deriveEcefFromLlh(epsilon.latitude_deg,
                                              epsilon.longitude_deg,
                                              epsilon.height_m,
                                              expectedEcef),
            "derive recorder ECEF from LLH");
    require(deviceCells.at(8) == QString::number(expectedEcef.xM, 'f', 6), "derived ecef x csv precision");
    require(deviceCells.at(9) == QString::number(expectedEcef.yM, 'f', 6), "derived ecef y csv precision");
    require(deviceCells.at(10) == QString::number(expectedEcef.zM, 'f', 6), "derived ecef z csv precision");
    require(deviceCells.at(11) == QStringLiteral("1.250000"), "ned n csv value");
    require(deviceCells.at(23) == QStringLiteral("1.250000"), "roll csv value");
    require(deviceCells.at(33) == QStringLiteral("9.100000"), "imu acc x csv value");
    require(deviceCells.at(42) == QStringLiteral("RTK_FIXED"), "gnss fix text");
    require(deviceCells.at(43) == QStringLiteral("18"), "satellites csv value");
    require(deviceCells.at(46) == QStringLiteral("0.0123"), "hacc csv value");
    require(deviceCells.at(52) == QStringLiteral("true"), "heading valid csv value");
    require(deviceCells.at(56) == QStringLiteral("100.0000"), "epsilon imu packet rate csv value");
    require(deviceCells.at(57) == QStringLiteral("40.0000"), "epsilon ahrs packet rate csv value");
    require(deviceCells.at(58) == QStringLiteral("41.0000"), "epsilon insgps packet rate csv value");
    require(deviceCells.at(59) == QStringLiteral("42.0000"), "epsilon sys state packet rate csv value");
    require(deviceCells.at(60) == QStringLiteral("50.0000"), "epsilon raw gnss packet rate csv value");
    require(deviceCells.at(61) == QStringLiteral("59.0000"), "epsilon satellite packet rate csv value");
    require(deviceCells.at(62) == QStringLiteral("10.0000"), "epsilon geodetic packet rate csv value");
    require(deviceCells.at(63) == QStringLiteral("11.0000"), "epsilon ecef packet rate csv value");
    require(deviceCells.at(66) == QStringLiteral("23.500000"), "temperature csv precision");
    require(deviceCells.at(67) == QStringLiteral("45.250000"), "humidity csv precision");
    require(deviceCells.at(68) == QStringLiteral("900.750000"), "pressure csv precision");
    require(deviceCells.at(69) == QStringLiteral("120.125000"), "lidar csv precision");
    require(deviceCells.at(70) == QStringLiteral("180"), "lidar strength csv value");

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

    QFile peakIndexFile(sessionDir + QStringLiteral("/raw/tcp_wave_peaks.csv"));
    require(peakIndexFile.open(QIODevice::ReadOnly | QIODevice::Text), "open tcp wave peak index csv");
    const QStringList peakIndexLines = QString::fromUtf8(peakIndexFile.readAll()).trimmed().split('\n');
    require(peakIndexLines.size() == 2, "tcp wave peak index row count");
    const QStringList peakIndexHeaders = peakIndexLines.at(0).split(',');
    require(peakIndexHeaders.size() == 6, "tcp wave peak index header column count");
    require(peakIndexHeaders.at(0) == QStringLiteral("host_time_us"), "tcp wave peak index timestamp header");
    require(peakIndexHeaders.at(1) == QStringLiteral("peak_value"), "tcp wave peak index value header");
    const QStringList peakIndexCells = peakIndexLines.at(1).split(',');
    require(peakIndexCells.size() == 6, "tcp wave peak index column count");
    require(peakIndexCells.at(0) == QStringLiteral("1000"), "tcp wave peak index timestamp");
    require(peakIndexCells.at(1) == QStringLiteral("4"), "tcp wave peak index peak value");
    require(peakIndexCells.at(2) == QStringLiteral("3"), "tcp wave peak index peak sample");
    require(peakIndexCells.at(3) == QStringLiteral("4"), "tcp wave peak index point count");
    require(peakIndexCells.at(4) == QStringLiteral("0"), "tcp wave peak index search start");
    require(peakIndexCells.at(5) == QStringLiteral("0"), "tcp wave peak index search end");

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
    const QJsonObject paths = root.value(QStringLiteral("paths")).toObject();
    require(paths.value(QStringLiteral("waveform_peak_index")).toString() == QStringLiteral("raw/tcp_wave_peaks.csv"),
            "metadata waveform peak index path");

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
