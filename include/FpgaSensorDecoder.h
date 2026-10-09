#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <optional>
#include <vector>

namespace VaporView::FpgaSensor
{

enum class SensorKind { Unknown, Ptb210, Epsilon, Bmp390, Sht45, Tfa1500, Ai8 };

struct TlvRecord
{
    quint16 tag = 0;
    quint8 type = 0;
    QByteArray value;
};

struct Validity
{
    bool structure = false;
    bool crc = true;
    bool deviceOnline = true;
    bool measurement = false;
    bool continuityLoss = false;
    quint32 rawFlags = 0;
};

struct Reading
{
    SensorKind kind = SensorKind::Unknown;
    quint16 source = 0;
    quint16 message = 0;
    quint16 schema = 0;
    quint64 timestamp = 0;
    quint32 flags = 0;
    QByteArray rawPayload;
    std::vector<TlvRecord> records;
    Validity validity;

    std::optional<double> pressurePa;
    std::optional<double> temperatureC;
    std::optional<double> humidityPct;
    std::optional<quint32> heaterMode;
    std::optional<quint32> distanceMm;
    std::optional<qint32> pressureMilliPa;
    std::optional<qint32> ai8PvRaw;
    std::optional<qint32> ai8SpRaw;
    std::optional<qint32> ai8SvRaw;
    std::optional<qint32> ai8PvMicroC;
    std::optional<qint32> ai8SpMicroC;
    std::optional<quint32> ai8OpRaw;
    std::optional<quint32> ai8Alarm;
    std::optional<quint32> ai8Control;
    std::optional<quint32> ai8Host;
    std::optional<quint32> ai8SetResult;
    std::optional<quint32> ai8DeviceStatus;
    std::optional<quint32> ai8DeviceError;
    std::optional<quint32> ai8SampleCounter;
    QByteArray bmpCalibration;
    std::optional<quint32> bmpPressureRaw;
    std::optional<quint32> bmpTemperatureRaw;

    // EPSILON: validated FDILink envelope and the smallest useful metadata.
    std::optional<quint8> epsilonMessageId;
    std::optional<quint8> epsilonSequence;
    QByteArray epsilonData;
    std::optional<qint64> epsilonDeviceTimestampUs;
};

class FpgaSensorDecoder
{
public:
    static bool parseTlv(const QByteArray &payload, quint16 &schema,
                         std::vector<TlvRecord> &records);
    static Reading decode(const QByteArray &payload, quint16 source,
                          quint16 message = 0x1100, quint32 flags = 0,
                          quint64 timestamp = 0);
};

} // namespace VaporView::FpgaSensor
