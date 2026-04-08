#ifndef VaporView_SERIAL_PROBE_UTILS_H
#define VaporView_SERIAL_PROBE_UTILS_H

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <chrono>
#include <thread>

#include "serial_port.h"

namespace VaporView
{

enum class SerialHeaderProbeKind
{
    GnssPvt,
    Gga,
};

struct SerialHeaderProbeResult
{
    bool matched = false;
    QString baudText;
};

inline bool serialBufferHasHeaderSignature(const QString& buffer, SerialHeaderProbeKind kind)
{
    static const QRegularExpression kGgaPattern(QStringLiteral("^\\$..GGA,"));

    QString normalized = buffer;
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = normalized.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& rawLine : lines)
    {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
        {
            continue;
        }

        switch (kind)
        {
        case SerialHeaderProbeKind::GnssPvt:
        {
            const QString upperLine = line.toUpper();
            if (upperLine.contains(QStringLiteral("PVTSLN")) || upperLine.contains(QStringLiteral("PVT")))
            {
                return true;
            }
            break;
        }
        case SerialHeaderProbeKind::Gga:
            if (kGgaPattern.match(line).hasMatch())
            {
                return true;
            }
            break;
        }
    }

    return false;
}

inline SerialHeaderProbeResult probeSerialPortForHeader(
    const QString& portName,
    const QStringList& baudTexts,
    SerialHeaderProbeKind kind,
    int timeoutMs = 1800)
{
    constexpr int kReadPauseMs = 40;
    char chunk[512];

    for (const QString& baudText : baudTexts)
    {
        bool ok = false;
        const int baudrate = baudText.toInt(&ok);
        if (!ok || baudrate <= 0)
        {
            continue;
        }

        SerialPort serial;
        if (!serial.open(portName.toStdString(), SerialConfig::N81(baudrate)))
        {
            continue;
        }

        QString buffer;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs)
        {
            const ssize_t bytesRead = serial.read(chunk, sizeof(chunk));
            if (bytesRead > 0)
            {
                buffer.append(QString::fromLatin1(chunk, static_cast<int>(bytesRead)));
                if (buffer.size() > 8192)
                {
                    buffer = buffer.right(4096);
                }

                if (serialBufferHasHeaderSignature(buffer, kind))
                {
                    serial.close();
                    return {true, baudText};
                }
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(kReadPauseMs));
            }
        }

        serial.close();
    }

    return {};
}

} // namespace VaporView

#endif
