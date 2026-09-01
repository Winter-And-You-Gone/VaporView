#ifndef VAPORVIEW_SERIAL_BAUD_RATE_H_
#define VAPORVIEW_SERIAL_BAUD_RATE_H_

#include <QString>

#include <limits>
#include <optional>

namespace VaporView
{

// A host serial backend accepts a positive signed integer.  Device-specific
// protocol enums (for example AI-8 bAud) must validate their own values.
inline std::optional<int> parseSerialBaudRate(const QString& input)
{
    const QString text = input.trimmed();
    if (text.isEmpty())
    {
        return std::nullopt;
    }
    for (const QChar character : text)
    {
        if (character.unicode() < QLatin1Char('0').unicode() ||
            character.unicode() > QLatin1Char('9').unicode())
        {
            return std::nullopt;
        }
    }

    bool ok = false;
    const qlonglong value = text.toLongLong(&ok, 10);
    if (!ok || value <= 0 || value > std::numeric_limits<int>::max())
    {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

inline bool isValidSerialBaudRate(const QString& input)
{
    return parseSerialBaudRate(input).has_value();
}

inline bool isValidSerialBaudRate(int value)
{
    return value > 0;
}

inline QString normalizedSerialBaudRateText(const QString& input)
{
    const auto value = parseSerialBaudRate(input);
    return value.has_value() ? QString::number(*value) : QString();
}

}  // namespace VaporView

#endif
