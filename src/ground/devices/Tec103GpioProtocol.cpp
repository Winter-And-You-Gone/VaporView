#include "Tec103GpioProtocol.h"
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace VaporView::Tec103GpioProtocol {
namespace {
bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

bool validChannel(int channel) { return channel == 1 || channel == 2; }

bool exchange(const Call& call, int channel, const QByteArray& command,
              Values& values, QString& error, bool full = false, QByteArray* raw = nullptr)
{
    const auto reply = call({{"op", "uart_exchange"}, {"channel", channel},
        {"data", QString::fromLatin1(command.toBase64())}, {"timeout_ms", full ? 5000 : 1200},
        {"max_rx", full ? 8192 : 2048}, {"quiet_ms", full ? 100 : 30}});
    if (!reply.value("ok").toBool()) {
        error = reply.value("detail").toString(QStringLiteral("TEC103 UART 事务失败；未自动重试。"));
        return false;
    }
    const auto encoded = reply.value("data").toString().toLatin1();
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded) return fail(&error, QStringLiteral("TEC103 UART 应答不是有效的 Base64 数据。"));
    if (raw) *raw = decoded.decoded;
    return parseValues(decoded.decoded, values, &error);
}

bool explicitAcknowledgment(QByteArray response, const Setting& setting)
{
    response.replace('\0',' ');
    for (auto token : response.split('@')) {
        token = token.trimmed();
        if (!token.startsWith("OK")) continue;
        token.remove(0,2);
        if (token.startsWith(':')) token.remove(0,1);
        if (!token.startsWith(setting.key + '=')) continue;
        bool ok = false;
        const auto value = token.mid(setting.key.size()+1).toLongLong(&ok);
        if (ok && value == setting.value) return true;
    }
    return false;
}

bool query(const Call& call, int channel, const QByteArray& key, qint64& value, QString& error)
{
    Values values;
    if (!exchange(call, channel, key + "=?@", values, error)) return false;
    if (!values.contains(key)) return fail(&error, QStringLiteral("TEC103 应答缺少 %1，未采用其他字段或 TC2 占位值。")
        .arg(QString::fromLatin1(key)));
    value = values.value(key);
    return true;
}

bool identity(const Call& call, int channel, QString& error)
{
    qint64 model = 0, firmware = 0;
    if (!query(call, channel, "TEC", model, error) || !query(call, channel, "FPV", firmware, error)) return false;
    if (model != 103 || firmware != 4370)
        return fail(&error, QStringLiteral("GPIO%1 实体型号/版本为 TEC=%2、FPV=%3，当前适配要求 TEC103/4370。")
            .arg(channel).arg(model).arg(firmware));
    return true;
}

bool inRange(qint64 value, qint64 minimum, qint64 maximum, const QByteArray& key, QString& error)
{
    if (value < minimum || value > maximum)
        return fail(&error, QStringLiteral("TEC103 %1 读回值 %2 超出已确认范围。")
            .arg(QString::fromLatin1(key)).arg(value));
    return true;
}

bool settingRange(const Setting& setting)
{
    const auto& key = setting.key;
    const auto value = setting.value;
    if (key == "TC1:TG") return value >= -40000000 && value <= 100000000;
    if (key == "TC1:ENABLE") return value == 0 || value == 1;
    if (key == "TC1:MODE") return value >= 0 && value <= 3;
    if (key == "TC1:LIMITED") return value >= 0 && value <= 90;
    if (key == "TC1:KP" || key == "TC1:KI" || key == "TC1:KD") return value >= 0 && value <= 9000000;
    if (key == "TC1:OVERTEMPUP" || key == "TC1:OVERTEMPLOWER") return value >= -300000000 && value <= 500000000;
    if (key == "TC1:SPEED") return value >= 0 && value <= 10000;
    if (key == "TC1:STARTUPDELAY") return value >= 3 && value <= 180;
    if (key == "TC1:AUTOPID") return value == 0 || value == 1;
    return false;
}

bool fullConfiguration(const Call& call, int logicalChannel, TemperatureControllerData& data, QString& error)
{
    // Read each documented field separately. Legacy INQUIRE streams contain
    // hundreds of bytes and can overrun the raw bridge while USB is delayed.
    Values values;
    auto& channel = data.channels[std::size_t(logicalChannel - 1)];
    struct Field { const char* key; qint64 minimum, maximum; int TemperatureControllerChannelData::* member; };
    const Field fields[] = {
        {"TC1:LIMITED", 0, 90, &TemperatureControllerChannelData::max_output_percent},
        {"TC1:MODE", 0, 3, &TemperatureControllerChannelData::output_mode},
        {"TC1:KP", 0, 9000000, &TemperatureControllerChannelData::kp},
        {"TC1:KI", 0, 9000000, &TemperatureControllerChannelData::ki},
        {"TC1:KD", 0, 9000000, &TemperatureControllerChannelData::kd},
        {"TC1:RP", 1, 9000000, &TemperatureControllerChannelData::ntc_r0},
        {"TC1:BX", 100000, 5000000, &TemperatureControllerChannelData::ntc_b},
        {"TC1:PT1000RP", 0, 10000000, &TemperatureControllerChannelData::pt_r0},
        {"TC1:PTA", -9000000, 9000000, &TemperatureControllerChannelData::pt_a},
        {"TC1:PTB", -9000000, 9000000, &TemperatureControllerChannelData::pt_b},
        {"TC1:PTC", -9000000, 9000000, &TemperatureControllerChannelData::pt_c}
    };
    for (const auto& field : fields) {
        const QByteArray key(field.key);
        qint64 value = 0;
        if (!query(call, logicalChannel, key, value, error)) return false;
        if (!inRange(value, field.minimum, field.maximum, key, error)) return false;
        channel.*(field.member) = int(value);
    }
    for (const auto& key : {QByteArray("TC1:OVERTEMPUP"), QByteArray("TC1:OVERTEMPLOWER"), QByteArray("TC1:SPEED")}) {
        qint64 value = 0;
        if (!query(call, logicalChannel, key, value, error)) return false;
        values.insert(key, value);
    }
    if (!inRange(values.value("TC1:OVERTEMPUP"), -300000000, 500000000, "TC1:OVERTEMPUP", error) ||
        !inRange(values.value("TC1:OVERTEMPLOWER"), -300000000, 500000000, "TC1:OVERTEMPLOWER", error) ||
        !inRange(values.value("TC1:SPEED"), 0, 10000, "TC1:SPEED", error)) return false;
    channel.overtemp_upper_c = values.value("TC1:OVERTEMPUP") / 100000.0;
    channel.overtemp_lower_c = values.value("TC1:OVERTEMPLOWER") / 100000.0;
    channel.temperature_slope_c_per_s = values.value("TC1:SPEED") / 1000.0;
    struct Extra { const char* key; qint64 minimum, maximum; int* destination; };
    Extra extra[] = {
        {"TC1:POLYOMIAL", 0, 3, &channel.sensor_model},
        {"TC1:AUTOPID", 0, 2, &channel.auto_pid_mode},
        {"TC1:STARTUPDELAY", 3, 180, &channel.startup_delay_s},
        {"CONTMODE", 0, 3, &data.controller_mode},
        {"OVERTTEMP", 0, 1, &data.overtemp_output_mode},
        {"ADDRESS", 0, 255, &data.device_address}
    };
    for (auto& field : extra) {
        qint64 value = 0;
        if (!query(call, logicalChannel, field.key, value, error) ||
            !inRange(value, field.minimum, field.maximum, field.key, error)) return false;
        *field.destination = int(value);
    }
    return true;
}
}

bool parseValues(const QByteArray& response, Values& values, QString* error)
{
    values.clear();
    if (response.isEmpty() || response.size() > 16384) return fail(error, QStringLiteral("TEC103 应答为空或超过长度限制。"));
    for (const auto byte : response) if (static_cast<unsigned char>(byte) > 127)
        return fail(error, QStringLiteral("TEC103 应答含非 ASCII 字节。"));
    QByteArray text = response;
    text.replace('\0',' ');
    static const QRegularExpression expression(QStringLiteral(
        "^(?:OK:?)?((?:TC[12]:)?[A-Z][A-Z0-9]*)=(-?[0-9]+)$"));
    const auto tokens = text.split('@');
    if (!tokens.last().trimmed().isEmpty()) return fail(error, QStringLiteral("TEC103 应答末尾字段没有完整 @ 终止符。"));
    for (auto token : tokens) {
        token = token.trimmed();
        if (token.isEmpty()) continue;
        const auto match = expression.match(QString::fromLatin1(token));
        if (!match.hasMatch()) return fail(error, QStringLiteral("TEC103 应答含无效字段或拒绝信息：%1。")
            .arg(QString::fromLatin1(token)));
        const auto key = match.captured(1).toLatin1();
        if (key == "NACK" || key == "ERR" || key == "ERROR")
            return fail(error, QStringLiteral("TEC103 返回拒绝字段：%1。").arg(QString::fromLatin1(key)));
        bool ok = false;
        const auto number = match.captured(2).toLongLong(&ok);
        if (!ok || values.contains(key)) return fail(error, QStringLiteral("TEC103 数值溢出或字段重复：%1。")
            .arg(QString::fromLatin1(key)));
        values.insert(key, number);
    }
    if (values.isEmpty()) return fail(error, QStringLiteral("TEC103 应答没有完整的 key=value@ 字段。"));
    return true;
}

Result read(const Call& call, int logicalChannel, bool full, const std::function<void()>& onIdentity)
{
    Result result;
    result.logicalChannel = logicalChannel;
    result.data.rs485_baud_index = -1; // Mislabelled legacy baud replies do not establish physical RS485 settings.
    result.data.device_address = -1;
    if (!call || !validChannel(logicalChannel)) {
        result.message = QStringLiteral("TEC103 物理通道必须为 1 或 2，且 UART 后端必须可用。");
        return result;
    }
    if (full) {
        if (!identity(call, logicalChannel, result.message)) return result;
        if (onIdentity) onIdentity();
        if (!fullConfiguration(call, logicalChannel, result.data, result.message)) return result;
    }
    Values measurements;
    if (!exchange(call, logicalChannel, "DATADEMAND=1@", measurements, result.message)) return result;
    for (const auto key : {"TC1:TCADJTEMP", "TC1:RESISTOR", "SINTERIORTEMP"}) {
        if (!measurements.contains(key)) {
            result.message = QStringLiteral("TEC103 测量应答缺少 %1，不使用 TC2 占位值。")
                .arg(QString::fromLatin1(key));
            return result;
        }
    }
    qint64 target = 0, duty = 0, enabled = 0, mode = 0, errors = 0;
    if (!query(call, logicalChannel, "TC1:TG", target, result.message) ||
        !query(call, logicalChannel, "TC1:PWMDUTY", duty, result.message) ||
        !query(call, logicalChannel, "TC1:ENABLE", enabled, result.message) ||
        !query(call, logicalChannel, "TC1:MODE", mode, result.message) ||
        !query(call, logicalChannel, "ERRORCODE", errors, result.message)) return result;
    if (!inRange(target, -40000000, 100000000, "TC1:TG", result.message) ||
        !inRange(duty, -2000000, 2000000, "TC1:PWMDUTY", result.message) ||
        !inRange(enabled, 0, 1, "TC1:ENABLE", result.message) ||
        !inRange(mode, 0, 3, "TC1:MODE", result.message) ||
        !inRange(errors, 0, 65535, "ERRORCODE", result.message) ||
        !inRange(measurements.value("TC1:RESISTOR"), 0, 500000000000LL, "TC1:RESISTOR", result.message) ||
        !inRange(measurements.value("SINTERIORTEMP"), -20, 120, "SINTERIORTEMP", result.message)) return result;
    auto& channel = result.data.channels[std::size_t(logicalChannel - 1)];
    const auto actual = measurements.value("TC1:TCADJTEMP");
    const bool temperatureValid = actual != 999999999 && actual >= -40000000 && actual <= 100000000;
    channel.target_temperature_c = target / 100000.0;
    channel.measured_temperature_c = temperatureValid ? actual / 100000.0 : std::numeric_limits<double>::quiet_NaN();
    channel.sensor_resistance_ohm = measurements.value("TC1:RESISTOR") / 1000000.0;
    channel.output_percent = duty / 20000.0;
    channel.output_enabled = enabled != 0;
    channel.output_mode = int(mode);
    result.data.internal_temperature_c = double(measurements.value("SINTERIORTEMP"));
    result.data.error_code = quint16(errors);
    result.data.timestamp = std::chrono::steady_clock::now();
    result.data.valid = temperatureValid;
    result.channelValid[std::size_t(logicalChannel - 1)] = temperatureValid;
    result.success = true;
    if (!temperatureValid) result.message = QStringLiteral("TEC103 返回无效测温值，保留真实输出状态并将该口测量标为无效。");
    return result;
}

Result writeVerified(const Call& call, int logicalChannel, const QVector<Setting>& settings)
{
    Result result;
    result.logicalChannel = logicalChannel;
    if (!call || !validChannel(logicalChannel) || settings.isEmpty()) {
        result.message = QStringLiteral("TEC103 写入通道或参数无效；未发送。");
        return result;
    }
    for (const auto& setting : settings) if (!settingRange(setting)) {
        result.message = QStringLiteral("TEC103 不支持此参数或数值范围：%1；未发送。")
            .arg(QString::fromLatin1(setting.key));
        return result;
    }
    if (!identity(call, logicalChannel, result.message)) return result;
    for (const auto& setting : settings) {
        Values acknowledgment;
        QByteArray rawAcknowledgment;
        result.unverified = true;
        if (!exchange(call, logicalChannel, setting.key + '=' + QByteArray::number(setting.value) + '@',
                      acknowledgment, result.message, false, &rawAcknowledgment)) {
            result.message += QStringLiteral(" 写入结果未知，未自动重试；再次操作前请先读取实体状态。");
            return result;
        }
        if (!acknowledgment.contains(setting.key) || acknowledgment.value(setting.key) != setting.value ||
            (acknowledgment.contains("ERRORCODE") && acknowledgment.value("ERRORCODE") != 0) ||
            !explicitAcknowledgment(rawAcknowledgment, setting)) {
            result.message = QStringLiteral("TEC103 写应答未明确确认 %1=%2；结果未确认，未自动重试。")
                .arg(QString::fromLatin1(setting.key)).arg(setting.value);
            return result;
        }
        qint64 readback = 0;
        if (!query(call, logicalChannel, setting.key, readback, result.message)) {
            result.message += QStringLiteral(" 写后独立读回失败，未自动重试。");
            return result;
        }
        if (readback != setting.value) {
            result.message = QStringLiteral("TEC103 %1 写后读回为 %2，要求 %3；未自动重试。")
                .arg(QString::fromLatin1(setting.key)).arg(readback).arg(setting.value);
            return result;
        }
        result.unverified = false;
    }
    result.success = true;
    return result;
}

}
