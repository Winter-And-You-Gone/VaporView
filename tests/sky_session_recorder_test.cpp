#include "SkySessionRecorder.h"
#include "geo/CoordinateTransform.h"
#include "shared/session/SessionDeviceConfig.h"
#include "shared/session/SessionSensorCsv.h"
#include "shared/session/UnifiedRawDat.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
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
    epsilon.attitude_source_count = 3;
    epsilon.attitude_delta_max_deg = 0.42;
    epsilon.attitude_delta_ahrs_euler_deg = 0.12;
    epsilon.attitude_delta_ahrs_quat_deg = 0.42;
    epsilon.attitude_delta_euler_quat_deg = 0.31;
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
    require(QFileInfo::exists(sessionDir + QStringLiteral("/sensors/sensor_summary.csv")), "sensor summary csv");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/sensors/waveform_features.csv")), "waveform features csv");
    require(!QFileInfo::exists(sessionDir + QStringLiteral("/waveform_index.csv")), "no waveform index csv");
    require(!QFileInfo::exists(sessionDir + QStringLiteral("/waveforms")), "no waveform bin directory");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/raw/navigation.dat")), "navigation raw dat");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/raw/waveform.dat")), "waveform raw dat");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/raw/waveform_peaks.csv")), "waveform peaks csv");

    recorder.stop();
    require(!recorder.isRecording(), "recorder stopped");

    QFile devicesFile(sessionDir + QStringLiteral("/sensors/sensor_summary.csv"));
    require(devicesFile.open(QIODevice::ReadOnly | QIODevice::Text), "open sensor summary csv");
    const QStringList deviceLines = QString::fromUtf8(devicesFile.readAll()).trimmed().split('\n');
    require(deviceLines.size() == 2, "devices csv row count");
    require(deviceLines.at(0) + QLatin1Char('\n') == VaporView::SessionSensorCsv::header(),
            "sky sensor summary uses shared header exactly");
    const QStringList deviceHeaders = deviceLines.at(0).split(',');
    require(deviceHeaders.size() == 77, "devices csv header column count");
    require(deviceHeaders.contains(QStringLiteral("gnss_satellites")), "devices csv has satellites");
    require(deviceHeaders.contains(QStringLiteral("imu_acc_x_mps2")), "devices csv has imu accel");
    require(deviceHeaders.contains(QStringLiteral("attitude_source_count")), "devices csv has attitude source count");
    require(deviceHeaders.contains(QStringLiteral("attitude_delta_max_deg")), "devices csv has attitude max delta");
    require(deviceHeaders.contains(QStringLiteral("epsilon_imu_packet_rate_hz")), "devices csv has epsilon packet rates");
    require(deviceHeaders.contains(QStringLiteral("lidar_signal_strength")), "devices csv has lidar strength");
    const QStringList deviceCells = deviceLines.at(1).split(',');
    require(deviceCells.size() == 77, "devices csv column count");
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
    require(deviceCells.at(30) == QStringLiteral("3"), "attitude source count csv value");
    require(deviceCells.at(31) == QStringLiteral("0.420000"), "attitude max delta csv value");
    require(deviceCells.at(32) == QStringLiteral("0.120000"), "ahrs/euler delta csv value");
    require(deviceCells.at(33) == QStringLiteral("0.420000"), "ahrs/quat delta csv value");
    require(deviceCells.at(34) == QStringLiteral("0.310000"), "euler/quat delta csv value");
    require(deviceCells.at(38) == QStringLiteral("9.100000"), "imu acc x csv value");
    require(deviceCells.at(47) == QStringLiteral("RTK_FIXED"), "gnss fix text");
    require(deviceCells.at(48) == QStringLiteral("18"), "satellites csv value");
    require(deviceCells.at(51) == QStringLiteral("0.0123"), "hacc csv value");
    require(deviceCells.at(57) == QStringLiteral("true"), "heading valid csv value");
    require(deviceCells.at(61) == QStringLiteral("100.0000"), "epsilon imu packet rate csv value");
    require(deviceCells.at(62) == QStringLiteral("40.0000"), "epsilon ahrs packet rate csv value");
    require(deviceCells.at(63) == QStringLiteral("41.0000"), "epsilon insgps packet rate csv value");
    require(deviceCells.at(64) == QStringLiteral("42.0000"), "epsilon sys state packet rate csv value");
    require(deviceCells.at(65) == QStringLiteral("50.0000"), "epsilon raw gnss packet rate csv value");
    require(deviceCells.at(66) == QStringLiteral("59.0000"), "epsilon satellite packet rate csv value");
    require(deviceCells.at(67) == QStringLiteral("10.0000"), "epsilon geodetic packet rate csv value");
    require(deviceCells.at(68) == QStringLiteral("11.0000"), "epsilon ecef packet rate csv value");
    require(deviceCells.at(71) == QStringLiteral("23.500000"), "temperature csv precision");
    require(deviceCells.at(72) == QStringLiteral("45.250000"), "humidity csv precision");
    require(deviceCells.at(73) == QStringLiteral("900.750000"), "pressure csv precision");
    require(deviceCells.at(74) == QStringLiteral("120.125000"), "lidar csv precision");
    require(deviceCells.at(75) == QStringLiteral("180"), "lidar strength csv value");

    QFile featuresFile(sessionDir + QStringLiteral("/sensors/waveform_features.csv"));
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

    QFile peakIndexFile(sessionDir + QStringLiteral("/raw/waveform_peaks.csv"));
    require(peakIndexFile.open(QIODevice::ReadOnly | QIODevice::Text), "open waveform peaks csv");
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
    require(root.value(QStringLiteral("recording_origin")).toString() == QStringLiteral("sky"),
            "metadata recording origin");
    require(!root.contains(QStringLiteral("mode")), "new sky metadata omits legacy mode");
    require(root.value(QStringLiteral("waveform_export_mode")).toString() == QStringLiteral("per_frame"),
            "per-frame waveform export mode");
    require(root.value(QStringLiteral("waveform_points_per_frame")).toInt() == 4,
            "waveform points per frame");
    const QJsonObject counts = root.value(QStringLiteral("counts")).toObject();
    require(counts.value(QStringLiteral("waveform_frames")).toString().toULongLong() == 1,
            "waveform frame count");
    require(counts.value(QStringLiteral("waveform_feature_rows")).toString().toULongLong() == 1,
            "waveform feature row count");
    require(root.value(QStringLiteral("waveform_file_count")).toString().toULongLong() == 1,
            "waveform file count");
    const QJsonObject rawFiles = root.value(QStringLiteral("raw_files")).toObject();
    require(rawFiles.value(QStringLiteral("waveform")).toObject().value(QStringLiteral("records")).toString().toULongLong() == 1,
            "waveform raw record count");
    const QJsonObject paths = root.value(QStringLiteral("paths")).toObject();
    require(paths.value(QStringLiteral("waveform_peaks_csv")).toString() == QStringLiteral("raw/waveform_peaks.csv"),
            "metadata waveform peaks path");

    QFile deviceConfigFile(sessionDir + QStringLiteral("/config/device_config.json"));
    require(deviceConfigFile.open(QIODevice::ReadOnly), "open sky device config");
    const QJsonDocument deviceConfigDocument = QJsonDocument::fromJson(deviceConfigFile.readAll());
    require(deviceConfigDocument.isObject(), "sky device config object");
    const QJsonObject deviceConfig = deviceConfigDocument.object();
    const QJsonObject expectedDeviceConfigSchema =
        VaporView::Session::sessionDeviceConfigToJson(VaporView::Session::SessionDeviceConfig{});
    require(deviceConfig.keys() == expectedDeviceConfigSchema.keys(),
            "sky device config uses shared top-level schema");
    require(deviceConfig.value(QStringLiteral("recording_origin")).toString()
                == QStringLiteral("sky"),
            "sky device config origin");
    require(deviceConfig.value(QStringLiteral("epsilon_schema_version")).isDouble(),
            "sky device config epsilon schema version is numeric");
    const QJsonObject deviceTelemetry = deviceConfig.value(QStringLiteral("telemetry")).toObject();
    require(deviceTelemetry.keys()
                == expectedDeviceConfigSchema.value(QStringLiteral("telemetry")).toObject().keys(),
            "sky device config telemetry schema");
    require(deviceTelemetry.value(QStringLiteral("transport")).toString()
                == QStringLiteral("serial") &&
                deviceTelemetry.value(QStringLiteral("endpoint")).toString()
                == QStringLiteral("COM50") &&
                deviceTelemetry.value(QStringLiteral("port")).toString()
                == QStringLiteral("COM50") &&
                deviceTelemetry.value(QStringLiteral("baud")).toString()
                == QStringLiteral("921600"),
            "sky device config telemetry values");
    const QJsonObject deviceSensors = deviceConfig.value(QStringLiteral("sensors")).toObject();
    require(deviceSensors.keys()
                == expectedDeviceConfigSchema.value(QStringLiteral("sensors")).toObject().keys(),
            "sky device config sensor schema");
    for (const QString& sensorKey : deviceSensors.keys())
    {
        const QJsonObject sensor = deviceSensors.value(sensorKey).toObject();
        require(sensor.value(QStringLiteral("port")).isNull() &&
                    sensor.value(QStringLiteral("baud")).isNull() &&
                    sensor.value(QStringLiteral("rate_hz")).isNull(),
                "sky unavailable sensor settings are explicit nulls");
    }
    const QJsonObject waveformConfig = deviceConfig.value(QStringLiteral("waveform")).toObject();
    require(waveformConfig.value(QStringLiteral("host")).isNull() &&
                waveformConfig.value(QStringLiteral("port")).isNull(),
            "sky unavailable waveform endpoint is explicit null");

    QFile rawFile(sessionDir + QStringLiteral("/raw/waveform.dat"));
    require(rawFile.open(QIODevice::ReadOnly), "open waveform raw dat");
    VaporView::SessionRawDat::RawScanOptions scanOptions;
    scanOptions.expectedSourceId = VaporView::SessionRawDat::kSourceWaveform;
    const VaporView::SessionRawDat::RawScanResult rawResult =
        VaporView::SessionRawDat::scan(rawFile, scanOptions);
    require(rawResult.success() && rawResult.records.size() == 1,
            "shared reader scans sky tcp wave raw dat");
    require(rawResult.fileHeader.version == VaporView::SessionRawDat::kCurrentFormatVersion,
            "sky writer uses shared current raw version");
    const VaporView::SessionRawDat::RawRecordIndex& rawRecord = rawResult.records.first();
    require(rawRecord.header.hostTimestampUs == 1000, "tcp wave record timestamp");
    require(rawRecord.header.sourceId == VaporView::SessionRawDat::kSourceWaveform,
            "tcp wave record source id");
    require((rawRecord.header.flags & VaporView::SessionRawDat::kWaveformCombinedPayloadFlag) != 0,
            "tcp wave combined payload flag");
    require(rawFile.seek(static_cast<qint64>(rawRecord.payloadOffset)),
            "seek shared tcp wave payload offset");

    const QByteArray payloadPrefix = rawFile.read(VaporView::SessionRawDat::kWaveformPayloadPrefixSize);
    VaporView::SessionRawDat::WaveformPayloadLayout payloadLayout;
    require(VaporView::SessionRawDat::parseWaveformPayloadLayout(
                payloadPrefix,
                rawRecord.header.payloadSize,
                &payloadLayout),
            "shared tcp wave payload layout parses");
    require(payloadLayout.rawSignalSize == 2 * sizeof(float), "raw signal payload byte count");
    require(payloadLayout.harmonicSize == 4 * sizeof(float), "harmonic payload byte count");
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
