#include "ground/devices/DeviceRatePolicy.h"

#include <QSettings>
#include <QStringList>

#include <algorithm>

namespace VaporView::Ground::DeviceRates
{

int parseRate(const QString& text)
{
    bool ok = false;
    const int rate = text.toInt(&ok);
    return ok && rate >= 1 && rate <= 1000 ? rate : 20;
}

bool isRateUnspecified(const QString& text)
{
    const QString trimmed = text.trimmed();
    return trimmed.compare(QStringLiteral("No Set"), Qt::CaseInsensitive) == 0
        || trimmed == QStringLiteral("不设定");
}

int effectiveRateOrDefault(const QString& text, int defaultRate, int maxRate)
{
    const int boundedDefault = std::clamp(defaultRate, 1, std::max(1, maxRate));
    if (isRateUnspecified(text))
    {
        return boundedDefault;
    }
    return std::clamp(parseRate(text), 1, std::max(1, maxRate));
}

int clampPtbSampleRate(int hz)
{
    return std::clamp(hz, kPtbMinSampleRateHz, kPtbMaxSampleRateHz);
}

const std::vector<EpsilonPacketConfigOption>& epsilonPacketConfigOptions()
{
    static const std::vector<EpsilonPacketConfigOption> kOptions = {
        {0x40, "MSG_IMU", "IMU原始数据", "IMU Raw Data", 56, {0, 1, 2, 5, 10, 20, 50, 100, 200, 250, 500, 1000}},
        {0x41, "MSG_AHRS", "AHRS姿态解", "AHRS Attitude", 48, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x42, "MSG_INSGPS", "INS/GPS融合解", "INS/GPS Navigation", 72, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x50, "MSG_SYS_STATE", "系统状态", "System State", 102, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x53, "MSG_STATUS", "系统/滤波状态", "System / Filter Status", 4, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x59, "MSG_RAW_GNSS", "原始GNSS", "Raw GNSS", 74, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5A, "MSG_SATELLITE", "卫星汇总", "Satellite Summary", 9, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5C, "MSG_GEODETIC_POS", "大地坐标", "Geodetic Position", 32, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5D, "MSG_ECEF_POS", "ECEF坐标", "ECEF Position", 24, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x63, "MSG_EULER_ORIEN", "欧拉姿态", "Euler Orientation", 12, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x64, "MSG_QUAT_ORIEN", "四元数姿态", "Quaternion Orientation", 16, {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
    };
    return kOptions;
}

QString epsilonPacketRateSettingsKey(quint8 packetId)
{
    return QStringLiteral("epsilon_custom_packet_rate_%1")
        .arg(packetId, 2, 16, QLatin1Char('0'))
        .toUpper();
}

int nearestSupportedEpsilonPacketRate(const EpsilonPacketConfigOption& option, int desiredRateHz)
{
    int fallbackRateHz = 0;
    for (int rateHz : option.supported_rates_hz)
    {
        if (rateHz == desiredRateHz)
        {
            return rateHz;
        }
        if (rateHz <= desiredRateHz)
        {
            fallbackRateHz = rateHz;
        }
    }
    return fallbackRateHz;
}

std::map<uint8_t, int> groupedEpsilonPacketRates(int baseRateHz)
{
    const int lowRateHz = std::min(baseRateHz, 20);
    std::map<uint8_t, int> rates;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const int desiredRateHz =
            (option.packet_id == 0x53 || option.packet_id == 0x59 || option.packet_id == 0x5A ||
             option.packet_id == 0x5C || option.packet_id == 0x5D)
                ? lowRateHz
                : baseRateHz;
        rates[option.packet_id] = nearestSupportedEpsilonPacketRate(option, desiredRateHz);
    }
    return rates;
}

std::map<uint8_t, int> defaultEpsilonPacketRates()
{
    return {
        {0x40, 250},
        {0x41, 50},
        {0x42, 100},
        {0x50, 100},
        {0x53, 100},
        {0x59, 10},
        {0x5A, 1},
        {0x5C, 10},
        {0x5D, 10},
        {0x63, 50},
        {0x64, 50},
    };
}

bool epsilonPacketRateSupported(const EpsilonPacketConfigOption& option, int rateHz)
{
    return std::find(option.supported_rates_hz.cbegin(), option.supported_rates_hz.cend(), rateHz) != option.supported_rates_hz.cend();
}

std::map<uint8_t, int> loadCustomEpsilonPacketRates(QSettings& settings, int fallbackBaseRateHz)
{
    std::map<uint8_t, int> packetRates = groupedEpsilonPacketRates(fallbackBaseRateHz);
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const int fallbackRate = packetRates[option.packet_id];
        const int storedRate = settings.value(epsilonPacketRateSettingsKey(option.packet_id), fallbackRate).toInt();
        packetRates[option.packet_id] = epsilonPacketRateSupported(option, storedRate) ? storedRate : fallbackRate;
    }
    return packetRates;
}

std::map<uint8_t, int> effectiveEpsilonPacketRates(QSettings& settings, int baseRateHz, bool *usingCustomProfile)
{
    bool useCustomProfile = settings.value("epsilon_custom_packet_rates_enabled", false).toBool();
    if (useCustomProfile &&
        !settings.value("epsilon_custom_packet_rates_user_saved", false).toBool() &&
        loadCustomEpsilonPacketRates(settings, baseRateHz) == defaultEpsilonPacketRates())
    {
        useCustomProfile = false;
    }
    if (usingCustomProfile)
    {
        *usingCustomProfile = useCustomProfile;
    }
    return useCustomProfile ? loadCustomEpsilonPacketRates(settings, baseRateHz) : groupedEpsilonPacketRates(baseRateHz);
}

QString epsilonPacketRatesSignature(const std::map<uint8_t, int>& packetRates)
{
    QStringList parts;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const auto it = packetRates.find(option.packet_id);
        const int rateHz = (it != packetRates.end()) ? it->second : -1;
        parts << QStringLiteral("%1=%2")
                     .arg(option.packet_id, 2, 16, QLatin1Char('0'))
                     .toUpper()
                     .arg(rateHz);
    }
    return parts.join(';');
}

QString epsilonPacketRatesSummary(const std::map<uint8_t, int>& packetRates)
{
    QStringList parts;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const auto it = packetRates.find(option.packet_id);
        if (it == packetRates.end())
        {
            continue;
        }
        parts << QStringLiteral("%1=%2Hz")
                     .arg(option.packet_id, 2, 16, QLatin1Char('0'))
                     .toUpper()
                     .arg(it->second);
    }
    return parts.join(QStringLiteral(", "));
}

int epsilonPacketCallbackRate(const std::map<uint8_t, int>& packetRates, int fallbackRateHz)
{
    int maxRateHz = 0;
    for (const auto& entry : packetRates)
    {
        maxRateHz = std::max(maxRateHz, entry.second);
    }
    return maxRateHz > 0 ? maxRateHz : fallbackRateHz;
}

EpsilonSerialBandwidth epsilonSerialBandwidth(
    const std::map<uint8_t, int>& packetRates,
    int baudRate)
{
    EpsilonSerialBandwidth result;
    result.baud_rate = baudRate;
    result.limit_bits_per_second = baudRate > 0
        ? static_cast<qint64>(baudRate) * kEpsilonSerialUtilizationLimitPercent / 100
        : 0;

    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const auto rateIt = packetRates.find(option.packet_id);
        if (rateIt == packetRates.end() || rateIt->second <= 0)
        {
            continue;
        }
        constexpr qint64 kFdilinkFrameOverheadBytes = 8;
        constexpr qint64 kSerialBitsPerByte8N1 = 10;
        result.required_bits_per_second +=
            static_cast<qint64>(option.payload_size_bytes + kFdilinkFrameOverheadBytes) *
            kSerialBitsPerByte8N1 * rateIt->second;
    }
    return result;
}

QString epsilonPacketDialogRowLabel(const EpsilonPacketConfigOption& option, bool english)
{
    if (english)
    {
        return QStringLiteral("%1 [%2]")
            .arg(QString::fromLatin1(option.message_name))
            .arg(option.packet_id, 2, 16, QLatin1Char('0'));
    }
    return QStringLiteral("%1 [%2]")
        .arg(QString::fromUtf8(option.title_zh))
        .arg(option.packet_id, 2, 16, QLatin1Char('0'));
}

QString epsilonPacketRateDisplayText(int rateHz, bool english)
{
    return rateHz == 0
        ? (english ? QStringLiteral("No Output (0 Hz)") : QStringLiteral("不输出 (0 Hz)"))
        : QStringLiteral("%1 Hz").arg(rateHz);
}

} // namespace VaporView::Ground::DeviceRates
