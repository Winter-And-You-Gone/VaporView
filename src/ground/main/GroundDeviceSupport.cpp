#include "ground/main/GroundMainWindowSupport.h"

#include <QComboBox>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include "shared/config/ApplicationConfig.h"
#include "shared/config/SettingsWriteBarrier.h"
#include <QStyle>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>

namespace VaporView::Ground::MainSupport
{

bool isTemperatureCommonCommand(VaporView::CommandId command)
{
    return command == VaporView::CommandId::SetTemperatureControllerMode ||
           command == VaporView::CommandId::SetTemperatureDeviceAddress ||
           command == VaporView::CommandId::SetTemperatureRs485Baud ||
           command == VaporView::CommandId::SetTemperatureOvertempOutputMode ||
           command == VaporView::CommandId::RestoreTemperatureFactoryDefaults;
}

int rememberedTemperatureSlaveAddress()
{
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    return std::clamp(settings.value(QStringLiteral("serial/temperature_slave_address"), 1).toInt(), 1, 247);
}

QString formatTemperaturePolynomial(qint64 mantissa, int exponent)
{
    if (mantissa == 0)
    {
        return QStringLiteral("0E+0");
    }
    const double coefficient = static_cast<double>(mantissa) / 10000000000000.0;
    return QStringLiteral("%1E%2%3")
        .arg(coefficient, 0, 'g', 14)
        .arg(exponent >= 0 ? QLatin1Char('+') : QLatin1Char('-'))
        .arg(std::abs(exponent));
}

bool parseTemperaturePolynomial(const QString& text, qint64& mantissa, qint16& exponent)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
    {
        mantissa = 0;
        exponent = 0;
        return true;
    }
    bool ok = false;
    const double value = QLocale::c().toDouble(trimmed, &ok);
    if (!ok || !std::isfinite(value))
    {
        return false;
    }
    if (qFuzzyIsNull(value))
    {
        mantissa = 0;
        exponent = 0;
        return true;
    }
    int exp = 0;
    double normalized = value;
    while (std::abs(normalized) >= 10.0 && exp < 100)
    {
        normalized /= 10.0;
        ++exp;
    }
    while (std::abs(normalized) < 1.0 && exp > -100)
    {
        normalized *= 10.0;
        --exp;
    }
    const qint64 scaled = qRound64(normalized * 10000000000000.0);
    if (scaled < -99999999999999LL || scaled > 99999999999999LL || exp < -100 || exp > 100)
    {
        return false;
    }
    mantissa = scaled;
    exponent = static_cast<qint16>(exp);
    return true;
}

QString formatTemperatureSensorDecimal(qint64 scaledValue, double scale, int decimals)
{
    return QLocale::c().toString(static_cast<double>(scaledValue) / scale, 'f', decimals);
}

QString formatTemperatureSensorDouble(double value, int decimals)
{
    return QLocale::c().toString(value, 'f', decimals);
}


void configureTemperatureControllerTwoRowGrid(QGridLayout *layout, int horizontalSpacing)
{
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(horizontalSpacing);
    layout->setVerticalSpacing(kTemperatureControllerRowSpacing);
    layout->setAlignment(Qt::AlignTop);
    layout->setRowMinimumHeight(0, kTemperatureControllerConfigRowHeight);
    layout->setRowMinimumHeight(1, kTemperatureControllerConfigRowHeight);
}

QFont numericFontFrom(const QFont& base)
{
    QFont font(base);
    font.setFamilies({
        QStringLiteral("Consolas"),
        QStringLiteral("Monaco"),
        QStringLiteral("Courier New")
    });
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

int widestTextWidth(const QFont& font, const QStringList& candidates)
{
    const QFontMetrics metrics(font);
    int width = 0;
    for (const QString& candidate : candidates)
    {
        width = std::max(width, metrics.horizontalAdvance(candidate));
    }
    return width;
}

void applyFixedNumericLabelWidth(QLabel *label, const QStringList& candidates, int padding)
{
    if (!label)
    {
        return;
    }
    label->setFont(numericFontFrom(label->font()));
    QStringList widthCandidates = candidates;
    if (!label->text().isEmpty())
    {
        widthCandidates.append(label->text());
    }
    const int width = widestTextWidth(label->font(), widthCandidates) + padding;
    label->setMinimumWidth(width);
    label->setMaximumWidth(width);
    label->setSizePolicy(QSizePolicy::Fixed, label->sizePolicy().verticalPolicy());
}

void setFixedNumericLabelWidth(QLabel *label, const QStringList& candidates, int padding)
{
    if (!label)
    {
        return;
    }
    label->setProperty(kNumericWidthCandidatesProperty, candidates);
    label->setProperty(kNumericWidthPaddingProperty, padding);
    applyFixedNumericLabelWidth(label, candidates, padding);
}

void applyFixedTextLabelWidth(QLabel *label, const QStringList& candidates, int padding)
{
    if (!label)
    {
        return;
    }
    QStringList widthCandidates = candidates;
    if (!label->text().isEmpty())
    {
        widthCandidates.append(label->text());
    }
    const int width = widestTextWidth(label->font(), widthCandidates) + padding;
    label->setMinimumWidth(width);
    label->setMaximumWidth(width);
    label->setSizePolicy(QSizePolicy::Fixed, label->sizePolicy().verticalPolicy());
}

void setFixedTextLabelWidth(QLabel *label, const QStringList& candidates, int padding)
{
    if (!label)
    {
        return;
    }
    label->setProperty(kTextWidthCandidatesProperty, candidates);
    label->setProperty(kTextWidthPaddingProperty, padding);
    applyFixedTextLabelWidth(label, candidates, padding);
}

void refreshFixedTextLabelWidth(QLabel *label)
{
    if (!label)
    {
        return;
    }
    const QStringList widthCandidates = label->property(kTextWidthCandidatesProperty).toStringList();
    if (widthCandidates.isEmpty())
    {
        return;
    }
    const int padding = label->property(kTextWidthPaddingProperty).toInt();
    applyFixedTextLabelWidth(label, widthCandidates, std::max(0, padding));
}

void refreshFixedNumericLabelWidth(QLabel *label)
{
    if (!label)
    {
        return;
    }
    const QStringList widthCandidates = label->property(kNumericWidthCandidatesProperty).toStringList();
    if (widthCandidates.isEmpty())
    {
        return;
    }
    const int padding = label->property(kNumericWidthPaddingProperty).toInt();
    applyFixedNumericLabelWidth(label, widthCandidates, std::max(0, padding));
}

void polishNumericLabel(QLabel *label)
{
    if (!label)
    {
        return;
    }
    label->style()->unpolish(label);
    label->style()->polish(label);
    refreshFixedNumericLabelWidth(label);
}

QStringList environmentFieldLabelWidthCandidates()
{
    return {
        QStringLiteral("Distance:"),
        QStringLiteral("Strength:"),
        QStringLiteral("Pressure:"),
        QStringLiteral("Temp:"),
        QStringLiteral("Humidity:"),
        QStringLiteral("距离:"),
        QStringLiteral("强度:"),
        QStringLiteral("气压:"),
        QStringLiteral("温度:"),
        QStringLiteral("湿度:")
    };
}

QStringList temperatureControllerFieldLabelWidthCandidates()
{
    return {
        QStringLiteral("Internal:"),
        QStringLiteral("Error:"),
        QStringLiteral("Target:"),
        QStringLiteral("Output Enable:"),
        QStringLiteral("Mode:"),
        QStringLiteral("Max Output:"),
        QStringLiteral("自身温度:"),
        QStringLiteral("错误码:"),
        QStringLiteral("目标温度(°C):"),
        QStringLiteral("输出使能"),
        QStringLiteral("输出模式"),
        QStringLiteral("最大输出电压百分比(%)"),
        QStringLiteral("PID:")
    };
}

QStringList temperatureControllerStatusLabelWidthCandidates()
{
    return {
        QStringLiteral("Internal:"),
        QStringLiteral("Error:"),
        QStringLiteral("Mode:"),
        QStringLiteral("Controller Mode:"),
        QStringLiteral("自身温度:"),
        QStringLiteral("错误码:"),
        QStringLiteral("温控器模式:")
    };
}

QStringList temperatureControllerCompactStatusLabelWidthCandidates()
{
    return {
        QStringLiteral("Internal:"),
        QStringLiteral("Error:"),
        QStringLiteral("自身温度:"),
        QStringLiteral("错误码:")
    };
}

QStringList temperatureControllerRateLabelWidthCandidates()
{
    return {
        QStringLiteral("Polling rate:"),
        QStringLiteral("轮询频率:")
    };
}

QString fixedTextField(const QString& text, int width, Qt::Alignment alignment)
{
    const int targetWidth = std::max(width, static_cast<int>(text.size()));
    return alignment == Qt::AlignLeft
        ? text.leftJustified(targetWidth, QLatin1Char(' '))
        : text.rightJustified(targetWidth, QLatin1Char(' '));
}

QString fixedDecimalWithUnit(double value, int decimals, int numberWidth, const QString& unit)
{
    const QString number = std::isfinite(value)
        ? QString::number(value, 'f', decimals)
        : QStringLiteral("---");
    return unit.isEmpty()
        ? fixedTextField(number, numberWidth)
        : QStringLiteral("%1 %2").arg(fixedTextField(number, numberWidth), unit);
}

QString compactDecimalWithUnit(double value, int decimals, const QString& unit)
{
    const QString number = std::isfinite(value)
        ? QString::number(value, 'f', decimals)
        : QStringLiteral("---");
    return unit.isEmpty()
        ? number
        : QStringLiteral("%1 %2").arg(number, unit);
}


QString skyDeviceDisplayName(VaporView::SkyDeviceId device)
{
    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon: return QStringLiteral("EPSILON");
    case VaporView::SkyDeviceId::Ptb: return QStringLiteral("PTB210");
    case VaporView::SkyDeviceId::Hmp: return QStringLiteral("HMP3");
    case VaporView::SkyDeviceId::Lidar: return QStringLiteral("TFA1005-L");
    case VaporView::SkyDeviceId::TemperatureController: return QStringLiteral("RD105");
    case VaporView::SkyDeviceId::Ai8TemperatureController: return QStringLiteral("AI-8288");
    case VaporView::SkyDeviceId::WaveTcp: return QStringLiteral("Wave TCP");
    case VaporView::SkyDeviceId::All: return QStringLiteral("全部设备");
    }
    return QStringLiteral("Device");
}

QString homeDeviceDisplayName(VaporView::SkyDeviceId device, bool english)
{
    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon:
        return english ? QStringLiteral("EPSILON Nav") : QStringLiteral("EPSILON 组合导航");
    case VaporView::SkyDeviceId::Ptb:
        return english ? QStringLiteral("PTB210 Barometer") : QStringLiteral("PTB210 气压计");
    case VaporView::SkyDeviceId::Hmp:
        return english ? QStringLiteral("HMP Temp/Humidity") : QStringLiteral("HMP 温湿度");
    case VaporView::SkyDeviceId::Lidar:
        return english ? QStringLiteral("TFA1005-L LiDAR") : QStringLiteral("TFA1005-L 激光测距");
    case VaporView::SkyDeviceId::TemperatureController:
        return english ? QStringLiteral("RD105 Thermal") : QStringLiteral("RD105 激光温控");
    case VaporView::SkyDeviceId::Ai8TemperatureController:
        return english ? QStringLiteral("AI-8288 8-Channel Thermal") : QStringLiteral("AI-8288八路温控");
    case VaporView::SkyDeviceId::WaveTcp:
        return english ? QStringLiteral("Wave Source") : QStringLiteral("波形源");
    case VaporView::SkyDeviceId::All:
        return english ? QStringLiteral("All devices") : QStringLiteral("全部设备");
    }
    return english ? QStringLiteral("Device") : QStringLiteral("设备");
}

QString formatBitRate(double bitsPerSecond)
{
    if (!std::isfinite(bitsPerSecond) || bitsPerSecond <= 0.0)
    {
        return QStringLiteral("0 bps");
    }
    if (bitsPerSecond < 1000.0)
    {
        return QStringLiteral("%1 bps").arg(bitsPerSecond, 0, 'f', 0);
    }
    if (bitsPerSecond < 1'000'000.0)
    {
        const double kilobitsPerSecond = bitsPerSecond / 1000.0;
        const int decimals = kilobitsPerSecond < 10.0 ? 2 : (kilobitsPerSecond < 100.0 ? 1 : 0);
        return QStringLiteral("%1 kbps").arg(kilobitsPerSecond, 0, 'f', decimals);
    }
    if (bitsPerSecond < 1'000'000'000.0)
    {
        const double megabitsPerSecond = bitsPerSecond / 1'000'000.0;
        const int decimals = megabitsPerSecond < 10.0 ? 2 : 1;
        return QStringLiteral("%1 Mbps").arg(megabitsPerSecond, 0, 'f', decimals);
    }
    const double gigabitsPerSecond = bitsPerSecond / 1'000'000'000.0;
    const int decimals = gigabitsPerSecond < 10.0 ? 2 : 1;
    return QStringLiteral("%1 Gbps").arg(gigabitsPerSecond, 0, 'f', decimals);
}

QString formatFrequencyText(double hz)
{
    if (!std::isfinite(hz) || hz < 0.0)
    {
        hz = 0.0;
    }
    return QStringLiteral("%1 Hz").arg(hz, 0, 'f', 1);
}

QString remoteNoDataText(bool english)
{
    return english ? QStringLiteral("No data") : QStringLiteral("无数据");
}

QString remoteDisconnectedText(bool english)
{
    return english ? QStringLiteral("Not connected") : QStringLiteral("未连接");
}

QString remoteStaleText(bool english)
{
    return english ? QStringLiteral("Stale") : QStringLiteral("超时");
}

QString formatElapsedCompact(quint64 elapsedMs)
{
    const quint64 totalSeconds = elapsedMs / 1000ULL;
    const quint64 hours = totalSeconds / 3600ULL;
    const quint64 minutes = (totalSeconds / 60ULL) % 60ULL;
    const quint64 seconds = totalSeconds % 60ULL;
    if (hours > 0)
    {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}


QString deviceConfigRemoteActionKey(VaporView::CommandId command)
{
    switch (command)
    {
    case VaporView::CommandId::ConnectDevice:
        return QStringLiteral("connect");
    case VaporView::CommandId::DisconnectDevice:
        return QStringLiteral("disconnect");
    case VaporView::CommandId::ReconnectDevice:
        return QStringLiteral("reconnect");
    default:
        return QStringLiteral("remote");
    }
}

QString deviceConfigRemoteActionText(VaporView::CommandId command, bool english)
{
    switch (command)
    {
    case VaporView::CommandId::ConnectDevice:
        return english ? QStringLiteral("Connect") : QStringLiteral("连接");
    case VaporView::CommandId::DisconnectDevice:
        return english ? QStringLiteral("Disconnect") : QStringLiteral("断开");
    case VaporView::CommandId::ReconnectDevice:
        return english ? QStringLiteral("Reconnect") : QStringLiteral("重连");
    default:
        return english ? QStringLiteral("Command") : QStringLiteral("命令");
    }
}

QString deviceConfigRemoteIconName(VaporView::CommandId command)
{
    switch (command)
    {
    case VaporView::CommandId::ConnectDevice:
        return QStringLiteral("link");
    case VaporView::CommandId::DisconnectDevice:
        return QStringLiteral("unlink");
    case VaporView::CommandId::ReconnectDevice:
        return QStringLiteral("refresh-cw");
    default:
        return QStringLiteral("link");
    }
}

QColor deviceConfigRemoteIconColor(VaporView::CommandId command)
{
    switch (command)
    {
    case VaporView::CommandId::ConnectDevice:
        return toolbarColor(AppThemeColor::ToolbarGreen);
    case VaporView::CommandId::DisconnectDevice:
        return toolbarColor(AppThemeColor::ToolbarRed);
    case VaporView::CommandId::ReconnectDevice:
        return toolbarColor(AppThemeColor::ToolbarBlue);
    default:
        return toolbarColor(AppThemeColor::ToolbarBlue);
    }
}

void applyDeviceConfigRemoteButtonPresentation(QToolButton *button,
                                               VaporView::CommandId command,
                                               VaporView::SkyDeviceId device,
                                               bool english,
                                               bool applyMetrics)
{
    if (!button)
    {
        return;
    }

    if (applyMetrics)
    {
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(kHomeDeviceIconSize, kHomeDeviceIconSize));
        button->setFixedSize(kHomeDeviceButtonSize, kHomeDeviceButtonSize);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setAutoRaise(true);
    }
    button->setObjectName(QStringLiteral("homeDeviceActionButton"));
    button->setProperty("deviceConfigAction", true);
    button->setProperty(kDeviceConfigRemoteActionProperty, deviceConfigRemoteActionKey(command));
    button->setProperty(kDeviceConfigRemoteCommandProperty, static_cast<int>(command));
    button->setProperty(kDeviceConfigRemoteDeviceProperty, static_cast<int>(device));
    button->setText(QString());
    button->setIcon(createLucideIcon(deviceConfigRemoteIconName(command),
                                     deviceConfigRemoteIconColor(command)));

    const QString actionText = deviceConfigRemoteActionText(command, english);
    const QString deviceName = skyDeviceDisplayName(device);
    const QString tooltip = english
        ? QStringLiteral("Request Sky to %1 %2").arg(actionText.toLower(), deviceName)
        : QStringLiteral("请求天空端%1 %2").arg(actionText, deviceName);
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
}


QString imuFrameTypeName(VaporView::ImuFrameType type)
{
    switch (type)
    {
    case VaporView::ImuFrameType::HI81:
        return QStringLiteral("HI81");
    case VaporView::ImuFrameType::HI83:
        return QStringLiteral("HI83");
    case VaporView::ImuFrameType::HI91:
        return QStringLiteral("HI91");
    case VaporView::ImuFrameType::HI92:
        return QStringLiteral("HI92");
    case VaporView::ImuFrameType::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

void applyComboText(QComboBox *combo, const QString& value)
{
    if (!combo || value.isEmpty())
    {
        return;
    }
    const QSignalBlocker blocker(combo);
    const int idx = combo->findText(value);
    if (idx >= 0)
    {
        combo->setCurrentIndex(idx);
        return;
    }
    if (combo->isEditable())
    {
        combo->setEditText(value);
    }
    else
    {
        combo->setCurrentText(value);
    }
}

QString sensorBaudSettingsKey(const QString& source)
{
    if (source == QStringLiteral("ptb210"))
    {
        return QStringLiteral("serial/ptb210_baud");
    }
    if (source == QStringLiteral("bmp390"))
    {
        return QStringLiteral("serial/bmp390_baud");
    }
    if (source == QStringLiteral("hmp3"))
    {
        return QStringLiteral("serial/hmp3_baud");
    }
    if (source == QStringLiteral("sht45"))
    {
        return QStringLiteral("serial/sht45_baud");
    }
    return QString();
}

QString sensorDefaultBaud(const QString& source)
{
    if (source == QStringLiteral("ptb210"))
    {
        return QStringLiteral("9600");
    }
    if (source == QStringLiteral("hmp3"))
    {
        return QStringLiteral("19200");
    }
    if (source == QStringLiteral("bmp390") || source == QStringLiteral("sht45"))
    {
        return QStringLiteral("115200");
    }
    return QString();
}

QString rememberedSensorBaud(const QSettings& settings,
                             const QString& source,
                             const QString& legacyKey)
{
    const QString key = sensorBaudSettingsKey(source);
    if (!key.isEmpty() && settings.contains(key))
    {
        return settings.value(key).toString();
    }
    if (!legacyKey.isEmpty() && settings.contains(legacyKey))
    {
        return settings.value(legacyKey).toString();
    }
    return sensorDefaultBaud(source);
}

void saveRememberedSensorBaud(QSettings& settings,
                              const QString& source,
                              const QComboBox *baudCombo)
{
    const QString key = sensorBaudSettingsKey(source);
    if (key.isEmpty() || !baudCombo)
    {
        return;
    }
    const QString baud = baudCombo->currentText().trimmed();
    if (!baud.isEmpty())
    {
        VaporView::setPersistentSetting(settings, key, baud);
    }
}

QString sourceModeDisplayText(bool english, int index)
{
    return index == 1
        ? (english ? QStringLiteral("Sky-Ground Remote Mode") : QStringLiteral("天地远程模式"))
        : (english ? QStringLiteral("Local") : QStringLiteral("本地"));
}

QString sourceModeStorageValue(int index)
{
    return index == 1 ? QStringLiteral("remote") : QStringLiteral("local");
}

QString skyTelemetryTransportDisplayText(bool english, const QString& transport)
{
    return transport == QStringLiteral("serial")
        ? (english ? QStringLiteral("Serial") : QStringLiteral("串口"))
        : QStringLiteral("TCP");
}

void updateSkyTelemetryTransportComboTexts(QComboBox *combo, bool english)
{
    if (!combo)
    {
        return;
    }

    const QSignalBlocker blocker(combo);
    for (int i = 0; i < combo->count(); ++i)
    {
        combo->setItemText(i, skyTelemetryTransportDisplayText(english, combo->itemData(i).toString()));
    }
}

int sourceModeIndexFromStoredValue(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("remote") ||
        normalized == QStringLiteral("remote sky") ||
        normalized == QStringLiteral("1") ||
        normalized.contains(QStringLiteral("sky")) ||
        normalized.contains(QStringLiteral("天空")) ||
        normalized.contains(QStringLiteral("天地")) ||
        normalized.contains(QStringLiteral("远程")))
    {
        return 1;
    }
    if (normalized == QStringLiteral("local") ||
        normalized == QStringLiteral("0") ||
        normalized.contains(QStringLiteral("本地")))
    {
        return 0;
    }
    return -1;
}

void rememberBaseMetric(QObject *object, const char *propertyName, int value)
{
    if (!object->property(propertyName).isValid())
    {
        object->setProperty(propertyName, value);
    }
}


} // namespace VaporView::Ground::MainSupport
