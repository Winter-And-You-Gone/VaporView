#ifndef VAPORVIEW_SERIAL_BAUD_RATE_CAPABILITIES_H_
#define VAPORVIEW_SERIAL_BAUD_RATE_CAPABILITIES_H_

#include "Ai8TemperatureControllerProtocol.h"
#include "SerialBaudRate.h"

#include <QStringList>

#include <array>
#include <cstddef>
#include <limits>

namespace VaporView
{

enum class BaudRateInputMode
{
    PresetOnly,
    PresetAndCustom,
};

struct BaudRateCapabilities
{
    QStringList presets;
    BaudRateInputMode inputMode = BaudRateInputMode::PresetAndCustom;
    int customMinimum = 1;
    int customMaximum = std::numeric_limits<int>::max();
};

template <std::size_t N>
inline QStringList baudRatePresetTexts(const std::array<int, N>& rates)
{
    QStringList texts;
    texts.reserve(static_cast<qsizetype>(rates.size()));
    for (int rate : rates)
    {
        texts.append(QString::number(rate));
    }
    return texts;
}

inline bool isBaudRateSupported(const BaudRateCapabilities& capabilities, int baudRate)
{
    if (!isValidSerialBaudRate(baudRate))
    {
        return false;
    }
    if (capabilities.inputMode == BaudRateInputMode::PresetOnly)
    {
        return capabilities.presets.contains(QString::number(baudRate));
    }
    return baudRate >= capabilities.customMinimum && baudRate <= capabilities.customMaximum;
}

inline bool isBaudRateSupported(const BaudRateCapabilities& capabilities, const QString& baudText)
{
    const auto baudRate = parseSerialBaudRate(baudText);
    return baudRate && isBaudRateSupported(capabilities, *baudRate);
}

inline const BaudRateCapabilities& hostSerialLinkBaudCapabilities()
{
    static const BaudRateCapabilities capabilities{
        {QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"),
         QStringLiteral("57600"), QStringLiteral("115200"), QStringLiteral("230400"),
         QStringLiteral("460800"), QStringLiteral("500000"), QStringLiteral("921600")},
        BaudRateInputMode::PresetAndCustom};
    return capabilities;
}

inline const BaudRateCapabilities& skyLinkBaudCapabilities()
{
    return hostSerialLinkBaudCapabilities();
}

inline const BaudRateCapabilities& rtkOutputBaudCapabilities()
{
    return hostSerialLinkBaudCapabilities();
}

inline const BaudRateCapabilities& bmp390SerialAdapterBaudCapabilities()
{
    return hostSerialLinkBaudCapabilities();
}

inline const BaudRateCapabilities& sht45SerialAdapterBaudCapabilities()
{
    return hostSerialLinkBaudCapabilities();
}

inline const BaudRateCapabilities& hmp3BaudCapabilities()
{
    return hostSerialLinkBaudCapabilities();
}

inline const BaudRateCapabilities& lidarBaudCapabilities()
{
    return hostSerialLinkBaudCapabilities();
}

inline const BaudRateCapabilities& ptb210BaudCapabilities();

inline const BaudRateCapabilities& pressureSensorBaudCapabilities(const QString& source)
{
    return source.trimmed() == QStringLiteral("bmp390")
        ? bmp390SerialAdapterBaudCapabilities()
        : ptb210BaudCapabilities();
}

inline const BaudRateCapabilities& humiditySensorBaudCapabilities(const QString& source)
{
    return source.trimmed() == QStringLiteral("sht45")
        ? sht45SerialAdapterBaudCapabilities()
        : hmp3BaudCapabilities();
}

inline const BaudRateCapabilities& epsilonConnectionBaudCapabilities()
{
    static const BaudRateCapabilities capabilities{
        {QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"),
         QStringLiteral("76800"), QStringLiteral("115200"), QStringLiteral("230400"),
         QStringLiteral("460800"), QStringLiteral("921600")},
        BaudRateInputMode::PresetOnly};
    return capabilities;
}

inline const BaudRateCapabilities& ptb210BaudCapabilities()
{
    static const BaudRateCapabilities capabilities{
        {QStringLiteral("1200"), QStringLiteral("2400"), QStringLiteral("4800"),
         QStringLiteral("9600"), QStringLiteral("19200")},
        BaudRateInputMode::PresetOnly};
    return capabilities;
}

inline const BaudRateCapabilities& rd105BaudCapabilities()
{
    static const BaudRateCapabilities capabilities{
        {QStringLiteral("4800"), QStringLiteral("9600"), QStringLiteral("19200"),
         QStringLiteral("38400"), QStringLiteral("57600"), QStringLiteral("115200"),
         QStringLiteral("230400"), QStringLiteral("460800")},
        BaudRateInputMode::PresetOnly};
    return capabilities;
}

inline const BaudRateCapabilities& ai8TemperatureControllerBaudCapabilities()
{
    static const BaudRateCapabilities capabilities{
        baudRatePresetTexts(Ai8TemperatureControllerProtocol::kSupportedBaudRates),
        BaudRateInputMode::PresetOnly};
    return capabilities;
}

inline const BaudRateCapabilities& epsilonRtcmForwardBaudCapabilities()
{
    static const BaudRateCapabilities capabilities{
        {QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"),
         QStringLiteral("76800"), QStringLiteral("115200"), QStringLiteral("230400"),
         QStringLiteral("460800"), QStringLiteral("921600"), QStringLiteral("2625000"),
         QStringLiteral("5250000"), QStringLiteral("10500000")},
        BaudRateInputMode::PresetOnly};
    return capabilities;
}

}  // namespace VaporView

#endif
