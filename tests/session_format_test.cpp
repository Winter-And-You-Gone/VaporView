#include "shared/session/SessionSensorCsv.h"
#include "shared/session/RecordingOrigin.h"
#include "shared/session/SessionManifest.h"
#include "shared/session/SessionPackageInitializer.h"
#include "shared/session/SessionPackageLayout.h"
#include "shared/session/UnifiedRawDat.h"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QTemporaryDir>
#include <QtEndian>

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{

using namespace VaporView::SessionRawDat;

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QSet<QString> relativeEntries(const QString& rootPath, QDir::Filters filters)
{
    QSet<QString> entries;
    QDir root(rootPath);
    QDirIterator iterator(rootPath,
                          filters | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        iterator.next();
        entries.insert(QDir::fromNativeSeparators(root.relativeFilePath(iterator.filePath())));
    }
    return entries;
}

QByteArray readFileBytes(const QString& filename)
{
    QFile file(filename);
    require(file.open(QIODevice::ReadOnly), "file opens for reading");
    return file.readAll();
}

QByteArray withoutUtf8Bom(QByteArray bytes)
{
    const QByteArray bom = QByteArray::fromHex("efbbbf");
    if (bytes.startsWith(bom))
    {
        bytes.remove(0, bom.size());
    }
    return bytes;
}

QByteArray firstTextLine(QByteArray bytes)
{
    bytes = withoutUtf8Bom(std::move(bytes));
    const qsizetype newline = bytes.indexOf('\n');
    QByteArray line = newline >= 0 ? bytes.left(newline) : bytes;
    if (line.endsWith('\r'))
    {
        line.chop(1);
    }
    return line;
}

QByteArray headerLine(QString header)
{
    QByteArray bytes = header.toUtf8();
    if (bytes.endsWith('\n'))
    {
        bytes.chop(1);
    }
    if (bytes.endsWith('\r'))
    {
        bytes.chop(1);
    }
    return bytes;
}

QJsonObject readJsonObject(const QString& filename)
{
    const QJsonDocument document = QJsonDocument::fromJson(readFileBytes(filename));
    require(document.isObject(), "json document is an object");
    return document.object();
}

void requireSameKeySet(const QJsonObject& left, const QJsonObject& right, const char *message)
{
    QStringList leftKeys = left.keys();
    QStringList rightKeys = right.keys();
    leftKeys.sort();
    rightKeys.sort();
    require(leftKeys == rightKeys, message);
}

void requireSameJsonTypes(const QJsonValue& left, const QJsonValue& right, const QString& path)
{
    require(left.type() == right.type(), path.toUtf8().constData());
    if (left.isObject())
    {
        const QJsonObject leftObject = left.toObject();
        const QJsonObject rightObject = right.toObject();
        requireSameKeySet(leftObject, rightObject, path.toUtf8().constData());
        for (const QString& key : leftObject.keys())
        {
            requireSameJsonTypes(leftObject.value(key),
                                 rightObject.value(key),
                                 path + QLatin1Char('.') + key);
        }
    }
}

VaporView::Session::SessionPackageInitResult initializeTestPackage(
    const QString& outputDirectory,
    VaporView::Session::RecordingOrigin origin)
{
    VaporView::Session::SessionPackageInitOptions options;
    options.origin = origin;
    options.sessionName = QStringLiteral("session_format_test");
    options.outputDirectory = outputDirectory;
    options.softwareVersion = QStringLiteral("test");
    options.startTimeUtc = QStringLiteral("2026-07-20T00:00:00.000Z");
    options.startTimeUs = 123456789u;
    options.sensorExportRateHz = 50;
    options.otherDevicesExportRateHz = 10;
    options.waveformExportRateHz = 0;
    options.waveformPointsPerFrame = 50000;
    options.initialDeviceConfig.insert(QStringLiteral("schema"), QStringLiteral("test"));
    return VaporView::Session::initializeSessionPackage(options);
}

template <typename T>
void writeLittleEndianAt(QByteArray& bytes, qsizetype offset, T value)
{
    const T encoded = qToLittleEndian(value);
    std::memcpy(bytes.data() + offset, &encoded, sizeof(encoded));
}

QByteArray makeRawFile(int recordCount = 1, const QByteArray& payload = QByteArray::fromHex("aabbccdd"))
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    require(buffer.open(QIODevice::WriteOnly), "raw DAT fixture buffer opens");
    QString error;
    require(writeFileHeader(buffer, kSourceTcpWave, &error), "raw DAT file header writes");
    for (int index = 0; index < recordCount; ++index)
    {
        RawRecordHeader header;
        header.hostTimestampUs = 1000u + static_cast<quint64>(index);
        header.sourceId = kSourceTcpWave;
        header.recordType = kRecordTypeTcpWavePayload;
        header.flags = 0u;
        header.sequence = static_cast<quint64>(index);
        require(writeRecord(buffer, header, payload, &error), "raw DAT record writes");
    }
    return bytes;
}

RawScanResult scanBytes(QByteArray bytes, quint16 expectedSourceId = kSourceTcpWave)
{
    QBuffer buffer(&bytes);
    require(buffer.open(QIODevice::ReadOnly), "raw DAT scan buffer opens");
    RawScanOptions options;
    options.expectedSourceId = expectedSourceId;
    return scan(buffer, options);
}

void testFormatConstantsAndGoldenBytes()
{
    require(kFileMagic == std::array<char, 8>{'V', 'V', 'R', 'A', 'W', 'D', 'A', 'T'},
            "raw DAT magic is unchanged");
    require(kCurrentFormatVersion == 2u, "current raw DAT format version is unchanged");
    require(kRecordMarker == 0x44525756u, "raw DAT record marker is unchanged");
    require(kFileHeaderSize == 20u, "raw DAT file header size is unchanged");
    require(kRecordHeaderSize == 36u, "raw DAT record header size is unchanged");
    require(kSourceEpsilon == 1u && kSourcePtb == 2u && kSourceHmp == 3u &&
                kSourceLidar == 4u && kSourceTcpWave == 5u,
            "raw DAT source IDs are unchanged");

    QByteArray bytes;
    QBuffer buffer(&bytes);
    require(buffer.open(QIODevice::WriteOnly), "golden raw DAT buffer opens");
    QString error;
    require(writeFileHeader(buffer, kSourceTcpWave, &error), "golden raw DAT file header writes");
    RawRecordHeader header;
    header.hostTimestampUs = 0x0102030405060708ULL;
    header.sourceId = kSourceTcpWave;
    header.recordType = kRecordTypeTcpWavePayload;
    header.flags = 0x11223344u;
    header.sequence = 0x1112131415161718ULL;
    require(writeRecord(buffer, header, QByteArray::fromHex("aabb"), &error),
            "golden raw DAT record writes");

    const QByteArray expected = QByteArray::fromHex(
        "5656524157444154"
        "02000000"
        "14000000"
        "0500"
        "0000"
        "56575244"
        "24000000"
        "0807060504030201"
        "02000000"
        "0500"
        "0100"
        "44332211"
        "1817161514131211"
        "aabb");
    require(bytes == expected, "shared raw DAT writer preserves exact little-endian bytes");

    const RawScanResult result = scanBytes(bytes);
    require(result.success() && result.status == RawReadStatus::Ok, "golden raw DAT scans");
    require(result.fileHeader.version == 2u && result.fileHeader.headerSize == 20u,
            "golden raw DAT file header decodes");
    require(result.records.size() == 1 &&
                result.records.first().header.hostTimestampUs == 0x0102030405060708ULL &&
                result.records.first().header.flags == 0x11223344u &&
                result.records.first().payloadOffset == 56u,
            "golden raw DAT record header decodes");
}

void testVersionValidation()
{
    QByteArray version1 = makeRawFile();
    writeLittleEndianAt<quint32>(version1, 8, 1u);
    require(scanBytes(version1).success(), "raw DAT version 1 remains supported");

    for (quint32 version : {0u, 3u})
    {
        QByteArray unsupported = makeRawFile();
        writeLittleEndianAt(unsupported, 8, version);
        const RawScanResult result = scanBytes(unsupported);
        require(result.status == RawReadStatus::UnsupportedVersion && !result.success(),
                "unsupported raw DAT version is rejected");
        require(result.error.contains(QString::number(version)) &&
                    result.error.contains(QStringLiteral("1, 2")),
                "unsupported raw DAT version error contains actual and supported versions");
    }

    QByteArray unknownMagic = makeRawFile();
    unknownMagic[0] = 'X';
    require(scanBytes(unknownMagic).status == RawReadStatus::NotUnifiedFormat,
            "missing unified magic is distinguished from unsupported version");

    QByteArray truncatedHeader = makeRawFile().left(12);
    require(scanBytes(truncatedHeader).status == RawReadStatus::InvalidHeader,
            "truncated raw DAT file header is rejected");
}

void testTcpWavePayloadCodec()
{
    QByteArray payload;
    QString error;
    require(encodeTcpWavePayload(QByteArray::fromHex("0102"),
                                 QByteArray::fromHex("03040506"),
                                 &payload,
                                 &error),
            "shared TCP wave payload encoder succeeds");
    require(payload == QByteArray::fromHex("0200000004000000010203040506"),
            "shared TCP wave payload encoder preserves exact little-endian bytes");

    TcpWavePayloadLayout layout;
    require(parseTcpWavePayloadLayout(payload.first(kTcpWavePayloadPrefixSize),
                                      static_cast<quint32>(payload.size()),
                                      &layout,
                                      &error),
            "shared TCP wave payload layout parser succeeds");
    require(layout.rawSignalSize == 2u && layout.harmonicSize == 4u &&
                layout.rawSignalOffset == 8u && layout.harmonicOffset == 10u,
            "shared TCP wave payload layout preserves field offsets");

    QByteArray fileBytes;
    QBuffer buffer(&fileBytes);
    require(buffer.open(QIODevice::WriteOnly), "combined TCP wave raw DAT buffer opens");
    require(writeFileHeader(buffer, kSourceTcpWave, &error),
            "combined TCP wave raw DAT header writes");
    RawRecordHeader header;
    header.sourceId = kSourceTcpWave;
    header.recordType = kRecordTypeTcpWavePayload;
    header.flags = kTcpWaveCombinedPayloadFlag;
    require(writeRecord(buffer, header, payload, &error),
            "combined TCP wave raw DAT record writes");
    require(scanBytes(fileBytes).success(),
            "shared raw scanner accepts valid combined TCP wave payload");

    writeLittleEndianAt<quint32>(fileBytes,
                                 kFileHeaderSize + kRecordHeaderSize,
                                 3u);
    require(scanBytes(fileBytes).status == RawReadStatus::CorruptRecord,
            "shared raw scanner rejects inconsistent TCP wave sub-payload sizes");
}

void testTruncatedTailRecovery()
{
    const QByteArray complete = makeRawFile(2);
    require(scanBytes(makeRawFile(0)).success(), "raw DAT with no records scans");

    QByteArray partialFirstHeader = makeRawFile(0);
    partialFirstHeader.append(complete.mid(kFileHeaderSize, 10));
    RawScanResult result = scanBytes(partialFirstHeader);
    require(result.recovered() && result.records.isEmpty() && result.lastValidOffset == kFileHeaderSize,
            "partial first record header is recovered without exposing a record");
    require(result.warning.contains(QStringLiteral("header")) &&
                result.warning.contains(QStringLiteral("0 complete records")),
            "partial record header recovery reports a warning");

    QByteArray partialFirstPayload = makeRawFile(1);
    partialFirstPayload.chop(1);
    result = scanBytes(partialFirstPayload);
    require(result.recovered() && result.records.isEmpty(),
            "partial first record payload is recovered without exposing a record");
    require(result.warning.contains(QStringLiteral("payload")),
            "partial payload recovery identifies the payload");

    QByteArray partialSecondPayload = complete;
    partialSecondPayload.chop(2);
    result = scanBytes(partialSecondPayload);
    require(result.recovered() && result.records.size() == 1,
            "complete records before a truncated final payload remain available");
    require(result.lastValidOffset == kFileHeaderSize + kRecordHeaderSize + 4u,
            "truncated tail recovery reports the last valid offset");
}

void testCorruptionRemainsFatal()
{
    QByteArray invalidFileHeaderSize = makeRawFile();
    writeLittleEndianAt<quint32>(invalidFileHeaderSize, 12u, kFileHeaderSize - 1u);
    require(scanBytes(invalidFileHeaderSize).status == RawReadStatus::InvalidHeader,
            "undersized raw DAT file header is rejected");

    invalidFileHeaderSize = makeRawFile();
    writeLittleEndianAt<quint32>(invalidFileHeaderSize, 12u, kMaxFileHeaderSize + 1u);
    require(scanBytes(invalidFileHeaderSize).status == RawReadStatus::InvalidHeader,
            "oversized raw DAT file header is rejected");

    QByteArray invalidRecordHeaderSize = makeRawFile();
    writeLittleEndianAt<quint32>(invalidRecordHeaderSize,
                                 kFileHeaderSize + 4u,
                                 kRecordHeaderSize - 1u);
    require(scanBytes(invalidRecordHeaderSize).status == RawReadStatus::CorruptRecord,
            "undersized raw DAT record header is rejected");

    QByteArray corruptMarker = makeRawFile(2);
    const qsizetype secondRecordOffset = kFileHeaderSize + kRecordHeaderSize + 4u;
    writeLittleEndianAt<quint32>(corruptMarker, secondRecordOffset, 0x12345678u);
    RawScanResult result = scanBytes(corruptMarker);
    require(result.status == RawReadStatus::CorruptRecord && !result.success(),
            "invalid marker after a complete record remains fatal");

    QByteArray invalidPayloadSize = makeRawFile(2);
    writeLittleEndianAt<quint32>(invalidPayloadSize, kFileHeaderSize + 16u, 1u);
    result = scanBytes(invalidPayloadSize);
    require(result.status == RawReadStatus::CorruptRecord,
            "payload size that desynchronizes later records remains fatal");

    QByteArray oversizedPayload = makeRawFile();
    writeLittleEndianAt<quint32>(oversizedPayload,
                                 kFileHeaderSize + 16u,
                                 kMaxPayloadSize + 1u);
    result = scanBytes(oversizedPayload);
    require(result.status == RawReadStatus::CorruptRecord &&
                result.error.contains(QStringLiteral("exceeds limit")),
            "oversized payload is rejected before allocation");

    require(scanBytes(makeRawFile(), kSourceEpsilon).status == RawReadStatus::InvalidHeader,
            "file source mismatch is rejected");

    QByteArray recordSourceMismatch = makeRawFile();
    writeLittleEndianAt<quint16>(recordSourceMismatch,
                                 kFileHeaderSize + 20u,
                                 kSourceEpsilon);
    require(scanBytes(recordSourceMismatch).status == RawReadStatus::CorruptRecord,
            "record source mismatch is rejected");

    QByteArray invalidRecordType = makeRawFile();
    writeLittleEndianAt<quint16>(invalidRecordType,
                                 kFileHeaderSize + 22u,
                                 0u);
    require(scanBytes(invalidRecordType).status == RawReadStatus::CorruptRecord,
            "invalid source-specific record type is rejected");

    QByteArray sequenceWarning = makeRawFile();
    writeLittleEndianAt<quint64>(sequenceWarning,
                                 kFileHeaderSize + 28u,
                                 9u);
    result = scanBytes(sequenceWarning);
    require(result.success() && result.warning.contains(QStringLiteral("expected 0")),
            "sequence discontinuity is reported without discarding a valid record");
}

void testSensorCsvCompatibility()
{
    const QString expectedHeader = QStringLiteral(
        "record_timestamp_us,"
        "epsilon_host_timestamp_us,epsilon_device_timestamp_us,epsilon_utc_unix_s,epsilon_utc_microseconds,"
        "nav_lat_deg,nav_lon_deg,nav_height_m,"
        "ecef_x_m,ecef_y_m,ecef_z_m,"
        "ned_n_m,ned_e_m,ned_d_m,"
        "vel_n_mps,vel_e_mps,vel_d_mps,"
        "body_vel_x_mps,body_vel_y_mps,body_vel_z_mps,"
        "body_acc_x_mps2,body_acc_y_mps2,body_acc_z_mps2,"
        "roll_deg,pitch_deg,yaw_deg,"
        "quat_w,quat_x,quat_y,quat_z,"
        "attitude_source_count,attitude_delta_max_deg,"
        "attitude_delta_ahrs_euler_deg,attitude_delta_ahrs_quat_deg,attitude_delta_euler_quat_deg,"
        "ang_vel_x_radps,ang_vel_y_radps,ang_vel_z_radps,"
        "imu_acc_x_mps2,imu_acc_y_mps2,imu_acc_z_mps2,"
        "imu_gyr_x_radps,imu_gyr_y_radps,imu_gyr_z_radps,"
        "mag_x_mg,mag_y_mg,mag_z_mg,"
        "gnss_fix,gnss_satellites,hdop,vdop,hacc_m,vacc_m,"
        "lat_std_m,lon_std_m,height_std_m,diff_age_s,"
        "heading_valid,system_status_bits,filter_status_bits,update_status_bits,"
        "epsilon_imu_packet_rate_hz,epsilon_ahrs_packet_rate_hz,"
        "epsilon_insgps_packet_rate_hz,epsilon_sys_state_packet_rate_hz,"
        "epsilon_raw_gnss_packet_rate_hz,epsilon_satellite_packet_rate_hz,"
        "epsilon_geodetic_packet_rate_hz,epsilon_ecef_packet_rate_hz,"
        "epsilon_valid,epsilon_error_message,"
        "hmp_temperature_c,hmp_humidity_rh,ptb_pressure_hpa,lidar_distance_m,lidar_signal_strength,lidar_valid\n");
    require(VaporView::SessionSensorCsv::header() == expectedHeader,
            "devices.csv header and field order are unchanged");

    const VaporView::EpsilonData epsilon;
    const VaporView::PtbData ptb;
    const VaporView::HmpData hmp;
    const VaporView::LidarData lidar;
    const QString emptyRow = VaporView::SessionSensorCsv::formatRow(
        123u, 0u, epsilon, false, ptb, false, hmp, false, lidar, false);
    require(emptyRow == QStringLiteral("123") + QString(76, QLatin1Char(',')) + QLatin1Char('\n'),
            "devices.csv missing-value row is unchanged");
    require(VaporView::SessionSensorCsv::escape(QStringLiteral("a,b\"c\n")) ==
                QStringLiteral("\"a,b\"\"c\n\""),
            "devices.csv escaping is unchanged");
}

void testSessionPackageInitializerCreatesIdenticalGroundAndSkyPackages()
{
    using namespace VaporView::Session;

    QTemporaryDir groundRoot;
    QTemporaryDir skyRoot;
    require(groundRoot.isValid() && skyRoot.isValid(), "temporary package roots");

    const SessionPackageInitResult ground =
        initializeTestPackage(groundRoot.path(), RecordingOrigin::Ground);
    const SessionPackageInitResult sky =
        initializeTestPackage(skyRoot.path(), RecordingOrigin::Sky);
    require(ground.success && sky.success, "ground and sky package initialization succeeds");

    const QSet<QString> groundDirectories =
        relativeEntries(ground.sessionDirectory, QDir::Dirs);
    const QSet<QString> skyDirectories =
        relativeEntries(sky.sessionDirectory, QDir::Dirs);
    QSet<QString> expectedDirectories;
    for (const QString& path : standardSessionDirectories())
    {
        expectedDirectories.insert(path);
    }
    require(groundDirectories == expectedDirectories, "ground standard directory set");
    require(skyDirectories == expectedDirectories, "sky standard directory set");
    require(groundDirectories == skyDirectories, "ground and sky directory sets match");

    const QSet<QString> groundFiles =
        relativeEntries(ground.sessionDirectory, QDir::Files);
    const QSet<QString> skyFiles =
        relativeEntries(sky.sessionDirectory, QDir::Files);
    QSet<QString> expectedFiles;
    for (const QString& path : standardSessionFiles())
    {
        expectedFiles.insert(path);
    }
    require(groundFiles == expectedFiles, "ground standard file set");
    require(skyFiles == expectedFiles, "sky standard file set");
    require(groundFiles == skyFiles, "ground and sky file sets match");

    const SessionPackageLayout& layout = standardSessionPackageLayout();
    require(firstTextLine(readFileBytes(sessionPackageFilePath(ground.sessionDirectory, layout.devicesCsvPath)))
                == headerLine(VaporView::SessionSensorCsv::header()),
            "standard devices.csv header exists");
    require(firstTextLine(readFileBytes(sessionPackageFilePath(ground.sessionDirectory, layout.temperatureControllerCsvPath)))
                == headerLine(temperatureControllerCsvHeader()),
            "standard temperature controller header exists");
    require(firstTextLine(readFileBytes(sessionPackageFilePath(ground.sessionDirectory, layout.waveformFeaturesCsvPath)))
                == headerLine(waveformFeaturesCsvHeader()),
            "standard waveform features header exists");
    require(firstTextLine(readFileBytes(sessionPackageFilePath(ground.sessionDirectory, layout.tcpWavePeaksCsvPath)))
                == headerLine(tcpWavePeaksCsvHeader()),
            "standard tcp wave peaks header exists");
    require(firstTextLine(readFileBytes(sessionPackageFilePath(ground.sessionDirectory, layout.eventLogPath)))
                == headerLine(eventLogCsvHeader()),
            "standard event log header exists");
    require(readFileBytes(sessionPackageFilePath(ground.sessionDirectory, layout.errorLogPath)).isEmpty(),
            "standard error log starts empty");
    require(readJsonObject(sessionPackageFilePath(ground.sessionDirectory, layout.deviceConfigPath))
                .value(QStringLiteral("schema")).toString() == QStringLiteral("test"),
            "standard device config is valid json");
    require(readFileBytes(sessionPackageFilePath(ground.sessionDirectory, layout.rawFormatDocumentPath))
                .contains("built-in VaporView unified RAW DAT format"),
            "raw_dat_format.md is generated from built-in text");

    for (const RawFileDefinition& definition : standardRawFileDefinitions())
    {
        const QString groundRawPath = sessionPackageFilePath(ground.sessionDirectory, definition.relativePath);
        const QString skyRawPath = sessionPackageFilePath(sky.sessionDirectory, definition.relativePath);
        require(readFileBytes(groundRawPath) == readFileBytes(skyRawPath),
                "ground and sky empty raw header bytes match");

        QFile rawFile(groundRawPath);
        require(rawFile.open(QIODevice::ReadOnly), "standard raw file opens");
        RawScanOptions options;
        options.expectedSourceId = definition.sourceId;
        const RawScanResult scanResult = scan(rawFile, options);
        require(scanResult.success() && scanResult.records.isEmpty(),
                "standard zero-record raw file scans");
        require(scanResult.fileHeader.sourceId == definition.sourceId,
                "standard raw file source id matches definition");
    }
}

void testSessionManifestSchemaAndOriginCompatibility()
{
    using namespace VaporView::Session;

    QTemporaryDir groundRoot;
    QTemporaryDir skyRoot;
    require(groundRoot.isValid() && skyRoot.isValid(), "temporary manifest roots");
    const SessionPackageInitResult ground =
        initializeTestPackage(groundRoot.path(), RecordingOrigin::Ground);
    const SessionPackageInitResult sky =
        initializeTestPackage(skyRoot.path(), RecordingOrigin::Sky);
    require(ground.success && sky.success, "manifest packages initialize");

    const SessionPackageLayout& layout = standardSessionPackageLayout();
    const QJsonObject groundJson =
        readJsonObject(sessionPackageFilePath(ground.sessionDirectory, layout.manifestPath));
    const QJsonObject skyJson =
        readJsonObject(sessionPackageFilePath(sky.sessionDirectory, layout.manifestPath));
    requireSameKeySet(groundJson, skyJson, "manifest top-level key sets match");
    requireSameKeySet(groundJson.value(QStringLiteral("capture")).toObject(),
                      skyJson.value(QStringLiteral("capture")).toObject(),
                      "manifest capture key sets match");
    requireSameKeySet(groundJson.value(QStringLiteral("counts")).toObject(),
                      skyJson.value(QStringLiteral("counts")).toObject(),
                      "manifest counts key sets match");
    requireSameKeySet(groundJson.value(QStringLiteral("paths")).toObject(),
                      skyJson.value(QStringLiteral("paths")).toObject(),
                      "manifest paths key sets match");
    requireSameKeySet(groundJson.value(QStringLiteral("raw_files")).toObject(),
                      skyJson.value(QStringLiteral("raw_files")).toObject(),
                      "manifest raw_files key sets match");
    requireSameJsonTypes(QJsonValue(groundJson), QJsonValue(skyJson), QStringLiteral("manifest"));
    for (const QString& key : groundJson.keys())
    {
        if (key == QStringLiteral("recording_origin"))
        {
            continue;
        }
        require(groundJson.value(key) == skyJson.value(key),
                "manifest values differ only by recording_origin");
    }

    require(groundJson.value(QStringLiteral("recording_origin")).toString() == QStringLiteral("ground"),
            "ground manifest origin");
    require(skyJson.value(QStringLiteral("recording_origin")).toString() == QStringLiteral("sky"),
            "sky manifest origin");
    require(!groundJson.contains(QStringLiteral("mode")) && !skyJson.contains(QStringLiteral("mode")),
            "new manifests do not write legacy mode");

    const QJsonObject capture = groundJson.value(QStringLiteral("capture")).toObject();
    require(capture.value(QStringLiteral("telemetry_transport")).isNull() &&
                capture.value(QStringLiteral("telemetry_endpoint")).isNull() &&
                capture.value(QStringLiteral("telemetry_port")).isNull() &&
                capture.value(QStringLiteral("telemetry_baud")).isNull(),
            "empty capture fields are explicit nulls");
    const QJsonObject counts = groundJson.value(QStringLiteral("counts")).toObject();
    require(counts.value(QStringLiteral("sensor_rows")).toString() == QStringLiteral("0") &&
                counts.value(QStringLiteral("temperature_controller_rows")).toString() == QStringLiteral("0") &&
                counts.value(QStringLiteral("waveform_frames")).toString() == QStringLiteral("0") &&
                counts.value(QStringLiteral("waveform_feature_rows")).toString() == QStringLiteral("0") &&
                counts.value(QStringLiteral("event_rows")).toString() == QStringLiteral("0") &&
                counts.value(QStringLiteral("error_rows")).toString() == QStringLiteral("0"),
            "zero counts remain present as strings");
    const QJsonObject rawFiles = groundJson.value(QStringLiteral("raw_files")).toObject();
    for (const RawFileDefinition& definition : standardRawFileDefinitions())
    {
        const QJsonObject raw = rawFiles.value(definition.key).toObject();
        require(raw.value(QStringLiteral("path")).toString() == definition.relativePath,
                "raw file manifest path matches layout");
        require(raw.value(QStringLiteral("source_id")).toInt() == definition.sourceId,
                "raw file manifest source id matches layout");
        require(raw.value(QStringLiteral("format_version")).toInt() == kCurrentFormatVersion,
                "raw file manifest format version matches current format");
        require(raw.value(QStringLiteral("records")).toString() == QStringLiteral("0"),
                "raw file zero records remain present as strings");
    }

    const auto groundOrigin = recordingOriginFromString(QStringLiteral("ground"));
    const auto skyOrigin = recordingOriginFromString(QStringLiteral("sky"));
    require(groundOrigin.has_value() && *groundOrigin == RecordingOrigin::Ground,
            "recording origin parses ground");
    require(skyOrigin.has_value() && *skyOrigin == RecordingOrigin::Sky,
            "recording origin parses sky");
    require(!recordingOriginFromString(QStringLiteral("air")).has_value(),
            "invalid recording origin is rejected");

    QJsonObject legacySky;
    legacySky.insert(QStringLiteral("mode"), QStringLiteral("sky"));
    const SessionManifestParseResult legacySkyResult = sessionManifestFromJson(legacySky);
    require(legacySkyResult.success &&
                legacySkyResult.manifest.recordingOrigin == RecordingOrigin::Sky,
            "legacy mode=sky parses as sky");

    QJsonObject legacyGround;
    legacyGround.insert(QStringLiteral("session_name"), QStringLiteral("legacy_ground"));
    const SessionManifestParseResult legacyGroundResult = sessionManifestFromJson(legacyGround);
    require(legacyGroundResult.success &&
                legacyGroundResult.manifest.recordingOrigin == RecordingOrigin::Ground,
            "missing origin parses as legacy ground");

    QJsonObject invalidOrigin;
    invalidOrigin.insert(QStringLiteral("recording_origin"), QStringLiteral("air"));
    require(!sessionManifestFromJson(invalidOrigin).success,
            "invalid manifest recording_origin fails parsing");
}

}  // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    testFormatConstantsAndGoldenBytes();
    testVersionValidation();
    testTcpWavePayloadCodec();
    testTruncatedTailRecovery();
    testCorruptionRemainsFatal();
    testSensorCsvCompatibility();
    testSessionPackageInitializerCreatesIdenticalGroundAndSkyPackages();
    testSessionManifestSchemaAndOriginCompatibility();
    std::cout << "session_format_test passed\n";
    return 0;
}
