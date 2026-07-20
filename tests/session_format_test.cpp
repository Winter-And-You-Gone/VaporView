#include "shared/session/SessionSensorCsv.h"
#include "shared/session/UnifiedRawDat.h"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
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
    std::cout << "session_format_test passed\n";
    return 0;
}
