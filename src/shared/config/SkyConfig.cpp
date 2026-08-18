#include "SkyConfig.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtGlobal>
#include <cmath>
#include <limits>

namespace VaporView
{
namespace
{
bool fuzzyEqual(double a, double b)
{
    return std::fabs(a - b) < 0.000001;
}

bool writeJsonFileAtomically(const QString& filename, const QJsonObject& object, QString *errorMessage)
{
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }

    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size())
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }

    if (!file.commit())
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }

    return true;
}

QString jsonTypeName(const QJsonValue& value)
{
    switch (value.type())
    {
    case QJsonValue::Null:
        return QStringLiteral("null");
    case QJsonValue::Bool:
        return QStringLiteral("boolean");
    case QJsonValue::Double:
        return QStringLiteral("number");
    case QJsonValue::String:
        return QStringLiteral("string");
    case QJsonValue::Array:
        return QStringLiteral("array");
    case QJsonValue::Object:
        return QStringLiteral("object");
    case QJsonValue::Undefined:
    default:
        return QStringLiteral("undefined");
    }
}

bool failType(const QString& path, const QString& expected, const QJsonValue& value, QString *errorMessage)
{
    if (errorMessage)
    {
        *errorMessage = QStringLiteral("%1 must be %2, got %3")
            .arg(path, expected, jsonTypeName(value));
    }
    return false;
}

bool readSectionObject(const QJsonObject& root, const QString& key, QJsonObject& section, QString *errorMessage)
{
    if (!root.contains(key))
    {
        return false;
    }
    const QJsonValue value = root.value(key);
    if (!value.isObject())
    {
        failType(key, QStringLiteral("object"), value, errorMessage);
        return false;
    }
    section = value.toObject();
    return true;
}

bool readBoolField(const QJsonObject& object, const QString& section, const QString& key, bool& target, QString *errorMessage)
{
    if (!object.contains(key))
    {
        return true;
    }
    const QJsonValue value = object.value(key);
    if (!value.isBool())
    {
        return failType(section + QLatin1Char('.') + key, QStringLiteral("boolean"), value, errorMessage);
    }
    target = value.toBool();
    return true;
}

bool readStringField(const QJsonObject& object, const QString& section, const QString& key, QString& target, QString *errorMessage)
{
    if (!object.contains(key))
    {
        return true;
    }
    const QJsonValue value = object.value(key);
    if (!value.isString())
    {
        return failType(section + QLatin1Char('.') + key, QStringLiteral("string"), value, errorMessage);
    }
    target = value.toString();
    return true;
}

bool readIntField(const QJsonObject& object, const QString& section, const QString& key, int& target, QString *errorMessage)
{
    if (!object.contains(key))
    {
        return true;
    }
    const QJsonValue value = object.value(key);
    if (!value.isDouble())
    {
        return failType(section + QLatin1Char('.') + key, QStringLiteral("integer"), value, errorMessage);
    }
    const double numeric = value.toDouble();
    if (!std::isfinite(numeric) ||
        std::floor(numeric) != numeric ||
        numeric < static_cast<double>(std::numeric_limits<int>::min()) ||
        numeric > static_cast<double>(std::numeric_limits<int>::max()))
    {
        return failType(section + QLatin1Char('.') + key, QStringLiteral("integer"), value, errorMessage);
    }
    target = static_cast<int>(numeric);
    return true;
}

bool readDoubleField(const QJsonObject& object, const QString& section, const QString& key, double& target, QString *errorMessage)
{
    if (!object.contains(key))
    {
        return true;
    }
    const QJsonValue value = object.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
    {
        return failType(section + QLatin1Char('.') + key, QStringLiteral("number"), value, errorMessage);
    }
    target = value.toDouble();
    return true;
}

QJsonObject serialToJson(const SerialDeviceConfig& config)
{
    QJsonObject object;
    object["enabled"] = config.enabled;
    object["port"] = config.port;
    object["baud"] = config.baud_rate;
    object["frequency_hz"] = config.frequency_hz;
    if (!config.source.isEmpty())
    {
        object["source"] = config.source;
    }
    return object;
}

QJsonObject epsilonToJson(const EpsilonSerialConfig& config)
{
    QJsonObject object;
    object["enabled"] = config.enabled;
    object["port"] = config.port;
    object["baud"] = config.baud_rate;
    return object;
}

QJsonObject epsilonRtcmToJson(const EpsilonRtcmConfig& config)
{
    QJsonObject object;
    object["enabled"] = config.enabled;
    object["device_port_index"] = config.device_port_index;
    object["forward_port"] = config.forward_port;
    object["baud"] = config.baud_rate;
    return object;
}

bool epsilonRtcmFromJson(const QJsonObject& object, EpsilonRtcmConfig& config, QString *errorMessage)
{
    EpsilonRtcmConfig next = config;
    const QString section = QStringLiteral("epsilon.rtcm");
    if (!readBoolField(object, section, QStringLiteral("enabled"), next.enabled, errorMessage) ||
        !readIntField(object, section, QStringLiteral("device_port_index"), next.device_port_index, errorMessage) ||
        !readStringField(object, section, QStringLiteral("forward_port"), next.forward_port, errorMessage) ||
        !readIntField(object, section, QStringLiteral("baud"), next.baud_rate, errorMessage) ||
        !readIntField(object, section, QStringLiteral("baud_rate"), next.baud_rate, errorMessage))
    {
        return false;
    }
    if (next.device_port_index < 2 || next.device_port_index > 5 || next.baud_rate <= 0)
    {
        if (errorMessage) *errorMessage = QStringLiteral("epsilon.rtcm device_port_index/baud is invalid");
        return false;
    }
    if (next.enabled && next.forward_port.trimmed().isEmpty())
    {
        if (errorMessage) *errorMessage = QStringLiteral("epsilon.rtcm forward_port is empty");
        return false;
    }
    next.forward_port = next.forward_port.trimmed();
    config = next;
    return true;
}

bool epsilonFromJson(const QJsonObject& object, EpsilonSerialConfig& config, QString *errorMessage)
{
    EpsilonSerialConfig next = config;
    const QString section = QStringLiteral("epsilon");
    if (!readBoolField(object, section, QStringLiteral("enabled"), next.enabled, errorMessage) ||
        !readStringField(object, section, QStringLiteral("port"), next.port, errorMessage) ||
        !readIntField(object, section, QStringLiteral("baud"), next.baud_rate, errorMessage) ||
        !readIntField(object, section, QStringLiteral("baud_rate"), next.baud_rate, errorMessage))
    {
        return false;
    }
    if (next.enabled && next.port.trimmed().isEmpty())
    {
        if (errorMessage) *errorMessage = QStringLiteral("epsilon port is empty");
        return false;
    }
    if (next.baud_rate <= 0)
    {
        if (errorMessage) *errorMessage = QStringLiteral("epsilon baud is invalid");
        return false;
    }
    config = next;
    return true;
}

bool serialFromJson(const QJsonObject& object, SerialDeviceConfig& config, const QString& name, QString *errorMessage)
{
    SerialDeviceConfig next = config;
    if (!readBoolField(object, name, QStringLiteral("enabled"), next.enabled, errorMessage) ||
        !readStringField(object, name, QStringLiteral("port"), next.port, errorMessage) ||
        !readIntField(object, name, QStringLiteral("baud"), next.baud_rate, errorMessage) ||
        !readIntField(object, name, QStringLiteral("baud_rate"), next.baud_rate, errorMessage) ||
        !readDoubleField(object, name, QStringLiteral("frequency_hz"), next.frequency_hz, errorMessage) ||
        !readStringField(object, name, QStringLiteral("source"), next.source, errorMessage))
    {
        return false;
    }
    if (next.enabled && next.port.trimmed().isEmpty())
    {
        if (errorMessage) *errorMessage = QStringLiteral("%1 port is empty").arg(name);
        return false;
    }
    if (next.baud_rate <= 0 || next.frequency_hz <= 0.0)
    {
        if (errorMessage) *errorMessage = QStringLiteral("%1 baud/frequency is invalid").arg(name);
        return false;
    }
    config = next;
    return true;
}

bool validateSource(const QString& section,
                    const QString& value,
                    const QString& defaultValue,
                    const QStringList& allowed,
                    QString *errorMessage)
{
    const QString source = value.trimmed().isEmpty() ? defaultValue : value.trimmed();
    if (!allowed.contains(source))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("%1 source is invalid").arg(section);
        }
        return false;
    }
    return true;
}

QJsonObject temperatureControllerToJson(const TemperatureControllerConfig& config)
{
    QJsonObject object;
    object["enabled"] = config.enabled;
    object["port"] = config.port;
    object["baud"] = config.baud_rate;
    object["frequency_hz"] = config.frequency_hz;
    object["slave_address"] = config.slave_address;
    return object;
}

bool temperatureControllerFromJson(const QJsonObject& object, TemperatureControllerConfig& config, QString *errorMessage)
{
    TemperatureControllerConfig next = config;
    const QString section = QStringLiteral("temperature_controller");
    if (!readBoolField(object, section, QStringLiteral("enabled"), next.enabled, errorMessage) ||
        !readStringField(object, section, QStringLiteral("port"), next.port, errorMessage) ||
        !readIntField(object, section, QStringLiteral("baud"), next.baud_rate, errorMessage) ||
        !readIntField(object, section, QStringLiteral("baud_rate"), next.baud_rate, errorMessage) ||
        !readDoubleField(object, section, QStringLiteral("frequency_hz"), next.frequency_hz, errorMessage) ||
        !readIntField(object, section, QStringLiteral("slave_address"), next.slave_address, errorMessage))
    {
        return false;
    }
    if (next.enabled && next.port.trimmed().isEmpty())
    {
        if (errorMessage) *errorMessage = QStringLiteral("temperature_controller port is empty");
        return false;
    }
    if (next.baud_rate <= 0 || next.frequency_hz <= 0.0 || next.slave_address <= 0 || next.slave_address > 247)
    {
        if (errorMessage) *errorMessage = QStringLiteral("temperature_controller baud/frequency/slave_address is invalid");
        return false;
    }
    config = next;
    return true;
}

bool ai8TemperatureControllerFromJson(const QJsonObject& object, TemperatureControllerConfig& config, QString *errorMessage)
{
    TemperatureControllerConfig next = config;
    const QString section = QStringLiteral("ai8_temperature_controller");
    if (!readBoolField(object, section, QStringLiteral("enabled"), next.enabled, errorMessage) ||
        !readStringField(object, section, QStringLiteral("port"), next.port, errorMessage) ||
        !readIntField(object, section, QStringLiteral("baud"), next.baud_rate, errorMessage) ||
        !readIntField(object, section, QStringLiteral("baud_rate"), next.baud_rate, errorMessage) ||
        !readDoubleField(object, section, QStringLiteral("frequency_hz"), next.frequency_hz, errorMessage) ||
        !readIntField(object, section, QStringLiteral("slave_address"), next.slave_address, errorMessage))
    {
        return false;
    }
    if (next.enabled && next.port.trimmed().isEmpty())
    {
        if (errorMessage) *errorMessage = QStringLiteral("ai8_temperature_controller port is empty");
        return false;
    }
    if (next.baud_rate <= 0 || next.frequency_hz <= 0.0 || next.slave_address <= 0 || next.slave_address > 88)
    {
        if (errorMessage) *errorMessage = QStringLiteral("ai8_temperature_controller baud/frequency/slave_address is invalid");
        return false;
    }
    config = next;
    return true;
}

QJsonObject waveToJson(const WaveTcpConfig& config)
{
    QJsonObject object;
    object["enabled"] = config.enabled;
    object["host"] = config.host;
    object["port"] = config.port;
    object["downsample_ratio"] = config.downsample_ratio;
    object["peak_search_start_index"] = config.peak_search_start_index;
    object["peak_search_end_index"] = config.peak_search_end_index;
    return object;
}

bool waveFromJson(const QJsonObject& object, WaveTcpConfig& config, QString *errorMessage)
{
    WaveTcpConfig next = config;
    const QString section = QStringLiteral("wave_tcp");
    if (!readBoolField(object, section, QStringLiteral("enabled"), next.enabled, errorMessage) ||
        !readStringField(object, section, QStringLiteral("host"), next.host, errorMessage) ||
        !readIntField(object, section, QStringLiteral("port"), next.port, errorMessage) ||
        !readIntField(object, section, QStringLiteral("downsample_ratio"), next.downsample_ratio, errorMessage) ||
        !readIntField(object, section, QStringLiteral("peak_search_start_index"), next.peak_search_start_index, errorMessage) ||
        !readIntField(object, section, QStringLiteral("peak_search_end_index"), next.peak_search_end_index, errorMessage))
    {
        return false;
    }
    if (next.enabled && next.host.trimmed().isEmpty())
    {
        if (errorMessage) *errorMessage = QStringLiteral("wave_tcp host is empty");
        return false;
    }
    if (next.port <= 0 || next.port > 65535 || next.downsample_ratio <= 0)
    {
        if (errorMessage) *errorMessage = QStringLiteral("wave_tcp port/downsample_ratio is invalid");
        return false;
    }
    if (next.peak_search_start_index < 0 ||
        (next.peak_search_end_index > 0 && next.peak_search_end_index <= next.peak_search_start_index))
    {
        if (errorMessage) *errorMessage = QStringLiteral("wave_tcp peak search range is invalid");
        return false;
    }
    config = next;
    return true;
}

QJsonObject telemetryToJson(const TelemetryRateConfig& config)
{
    QJsonObject object;
    object["basic_rate_hz"] = config.basic_rate_hz;
    object["feature_rate_hz"] = config.feature_rate_hz;
    object["waveform_rate_hz"] = config.waveform_rate_hz;
    object["heartbeat_rate_hz"] = config.heartbeat_rate_hz;
    object["status_rate_hz"] = config.status_rate_hz;
    return object;
}

bool telemetryFromJson(const QJsonObject& object, TelemetryRateConfig& config, QString *errorMessage)
{
    TelemetryRateConfig next = config;
    const QString section = QStringLiteral("telemetry");
    if (!readDoubleField(object, section, QStringLiteral("basic_rate_hz"), next.basic_rate_hz, errorMessage) ||
        !readDoubleField(object, section, QStringLiteral("feature_rate_hz"), next.feature_rate_hz, errorMessage) ||
        !readDoubleField(object, section, QStringLiteral("waveform_rate_hz"), next.waveform_rate_hz, errorMessage) ||
        !readDoubleField(object, section, QStringLiteral("heartbeat_rate_hz"), next.heartbeat_rate_hz, errorMessage) ||
        !readDoubleField(object, section, QStringLiteral("status_rate_hz"), next.status_rate_hz, errorMessage))
    {
        return false;
    }
    if (next.basic_rate_hz <= 0.0 || next.feature_rate_hz <= 0.0 || next.waveform_rate_hz <= 0.0 ||
        next.heartbeat_rate_hz <= 0.0 || next.status_rate_hz <= 0.0)
    {
        if (errorMessage) *errorMessage = QStringLiteral("telemetry rates must be positive");
        return false;
    }
    config = next;
    return true;
}

}  // namespace

bool SerialDeviceConfig::operator==(const SerialDeviceConfig& other) const
{
    return enabled == other.enabled &&
           port == other.port &&
           baud_rate == other.baud_rate &&
           fuzzyEqual(frequency_hz, other.frequency_hz) &&
           source == other.source;
}

bool SerialDeviceConfig::operator!=(const SerialDeviceConfig& other) const
{
    return !(*this == other);
}

bool EpsilonSerialConfig::operator==(const EpsilonSerialConfig& other) const
{
    return enabled == other.enabled &&
           port == other.port &&
           baud_rate == other.baud_rate;
}

bool EpsilonSerialConfig::operator!=(const EpsilonSerialConfig& other) const
{
    return !(*this == other);
}

bool EpsilonRtcmConfig::operator==(const EpsilonRtcmConfig& other) const
{
    return enabled == other.enabled &&
           device_port_index == other.device_port_index &&
           forward_port == other.forward_port &&
           baud_rate == other.baud_rate;
}

bool EpsilonRtcmConfig::operator!=(const EpsilonRtcmConfig& other) const
{
    return !(*this == other);
}

bool TemperatureControllerConfig::operator==(const TemperatureControllerConfig& other) const
{
    return enabled == other.enabled &&
           port == other.port &&
           baud_rate == other.baud_rate &&
           fuzzyEqual(frequency_hz, other.frequency_hz) &&
           slave_address == other.slave_address;
}

bool TemperatureControllerConfig::operator!=(const TemperatureControllerConfig& other) const
{
    return !(*this == other);
}

bool WaveTcpConfig::operator==(const WaveTcpConfig& other) const
{
    return enabled == other.enabled &&
           host == other.host &&
           port == other.port &&
           downsample_ratio == other.downsample_ratio &&
           peak_search_start_index == other.peak_search_start_index &&
           peak_search_end_index == other.peak_search_end_index;
}

bool WaveTcpConfig::operator!=(const WaveTcpConfig& other) const
{
    return !(*this == other);
}

bool TelemetryRateConfig::operator==(const TelemetryRateConfig& other) const
{
    return fuzzyEqual(basic_rate_hz, other.basic_rate_hz) &&
           fuzzyEqual(feature_rate_hz, other.feature_rate_hz) &&
           fuzzyEqual(waveform_rate_hz, other.waveform_rate_hz) &&
           fuzzyEqual(heartbeat_rate_hz, other.heartbeat_rate_hz) &&
           fuzzyEqual(status_rate_hz, other.status_rate_hz);
}

bool TelemetryRateConfig::operator!=(const TelemetryRateConfig& other) const
{
    return !(*this == other);
}

SkyConfig SkyConfig::defaults()
{
    SkyConfig config;
#ifdef _WIN32
    config.epsilon = {true, QStringLiteral("COM3"), 921600};
    config.epsilon_rtcm = {false, 2, QString(), 115200};
    config.ptb = {true, QStringLiteral("COM5"), 9600, 20.0};
    config.ptb.source = QStringLiteral("ptb210");
    config.hmp = {true, QStringLiteral("COM6"), 19200, 20.0};
    config.hmp.source = QStringLiteral("hmp3");
    config.lidar = {true, QStringLiteral("COM7"), 500000, 100.0};
    config.temperature_controller = {false, QStringLiteral("COM9"), 38400, 5.0, 1};
    config.ai8_temperature_controller = {false, QStringLiteral("COM10"), 19200, 5.0, 1};
#else
    config.epsilon = {true, QStringLiteral("/dev/ttyEPSILON"), 921600};
    config.epsilon_rtcm = {false, 2, QString(), 115200};
    config.ptb = {true, QStringLiteral("/dev/ttyBARO"), 9600, 20.0};
    config.ptb.source = QStringLiteral("ptb210");
    config.hmp = {true, QStringLiteral("/dev/ttyHMP"), 19200, 20.0};
    config.hmp.source = QStringLiteral("hmp3");
    config.lidar = {true, QStringLiteral("/dev/ttyLidar"), 500000, 100.0};
    config.temperature_controller = {false, QStringLiteral("/dev/ttyRD105"), 38400, 5.0, 1};
    config.ai8_temperature_controller = {false, QStringLiteral("/dev/ttyAI8"), 19200, 5.0, 1};
#endif
    config.wave_tcp = {true, QStringLiteral("127.0.0.1"), 8888, 10, 0, 0};
    config.telemetry = {10.0, 10.0, 1.0, 1.0, 1.0};
    return config;
}

bool SkyConfig::loadFromFile(const QString& filename, SkyConfig& config, QString *errorMessage)
{
    QFile file(filename);
    if (!file.exists())
    {
        config = SkyConfig::defaults();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
    {
        if (errorMessage) *errorMessage = QStringLiteral("sky config JSON root is not an object");
        return false;
    }
    return fromJson(document.object(), config, errorMessage);
}

bool SkyConfig::saveToFile(const QString& filename, QString *errorMessage) const
{
    return writeJsonFileAtomically(filename, toJson(), errorMessage);
}

bool SkyConfig::fromJson(const QJsonObject& object, SkyConfig& config, QString *errorMessage)
{
    SkyConfig next = SkyConfig::defaults();
    QJsonObject section;
    if (object.contains("epsilon"))
    {
        if (!readSectionObject(object, QStringLiteral("epsilon"), section, errorMessage) ||
            !epsilonFromJson(section, next.epsilon, errorMessage))
        {
            return false;
        }
        if (section.contains(QStringLiteral("rtcm")))
        {
            QJsonObject rtcmSection;
            if (!readSectionObject(section, QStringLiteral("rtcm"), rtcmSection, errorMessage) ||
                !epsilonRtcmFromJson(rtcmSection, next.epsilon_rtcm, errorMessage))
            {
                return false;
            }
        }
    }
    if (object.contains("epsilon_rtcm") &&
        (!readSectionObject(object, QStringLiteral("epsilon_rtcm"), section, errorMessage) ||
         !epsilonRtcmFromJson(section, next.epsilon_rtcm, errorMessage))) return false;
    if (object.contains("ptb") &&
        (!readSectionObject(object, QStringLiteral("ptb"), section, errorMessage) ||
         !serialFromJson(section, next.ptb, QStringLiteral("ptb"), errorMessage))) return false;
    if (next.ptb.source.isEmpty()) next.ptb.source = QStringLiteral("ptb210");
    if (object.contains("hmp") &&
        (!readSectionObject(object, QStringLiteral("hmp"), section, errorMessage) ||
         !serialFromJson(section, next.hmp, QStringLiteral("hmp"), errorMessage))) return false;
    if (next.hmp.source.isEmpty()) next.hmp.source = QStringLiteral("hmp3");
    if (object.contains("lidar") &&
        (!readSectionObject(object, QStringLiteral("lidar"), section, errorMessage) ||
         !serialFromJson(section, next.lidar, QStringLiteral("lidar"), errorMessage))) return false;
    if (object.contains("temperature_controller") &&
        (!readSectionObject(object, QStringLiteral("temperature_controller"), section, errorMessage) ||
         !temperatureControllerFromJson(section, next.temperature_controller, errorMessage))) return false;
    if (object.contains("ai8_temperature_controller") &&
        (!readSectionObject(object, QStringLiteral("ai8_temperature_controller"), section, errorMessage) ||
         !ai8TemperatureControllerFromJson(section, next.ai8_temperature_controller, errorMessage))) return false;
    if (object.contains("wave_tcp") &&
        (!readSectionObject(object, QStringLiteral("wave_tcp"), section, errorMessage) ||
         !waveFromJson(section, next.wave_tcp, errorMessage))) return false;
    if (object.contains("telemetry") &&
        (!readSectionObject(object, QStringLiteral("telemetry"), section, errorMessage) ||
         !telemetryFromJson(section, next.telemetry, errorMessage))) return false;
    if (!next.validate(errorMessage))
    {
        return false;
    }
    config = next;
    return true;
}

QJsonObject SkyConfig::toJson() const
{
    QJsonObject root;
    QJsonObject epsilonObject = epsilonToJson(epsilon);
    epsilonObject["rtcm"] = epsilonRtcmToJson(epsilon_rtcm);
    root["epsilon"] = epsilonObject;
    root["ptb"] = serialToJson(ptb);
    root["hmp"] = serialToJson(hmp);
    root["lidar"] = serialToJson(lidar);
    root["temperature_controller"] = temperatureControllerToJson(temperature_controller);
    root["ai8_temperature_controller"] = temperatureControllerToJson(ai8_temperature_controller);
    root["wave_tcp"] = waveToJson(wave_tcp);
    root["telemetry"] = telemetryToJson(telemetry);
    return root;
}

bool SkyConfig::validate(QString *errorMessage) const
{
    SkyConfig copy = *this;
    return validateSource(QStringLiteral("ptb"),
                          ptb.source,
                          QStringLiteral("ptb210"),
                          {QStringLiteral("ptb210"), QStringLiteral("bmp390")},
                          errorMessage) &&
           validateSource(QStringLiteral("hmp"),
                          hmp.source,
                          QStringLiteral("hmp3"),
                          {QStringLiteral("hmp3"), QStringLiteral("sht45")},
                          errorMessage) &&
           epsilonFromJson(epsilonToJson(epsilon), copy.epsilon, errorMessage) &&
           epsilonRtcmFromJson(epsilonRtcmToJson(epsilon_rtcm), copy.epsilon_rtcm, errorMessage) &&
           serialFromJson(serialToJson(ptb), copy.ptb, QStringLiteral("ptb"), errorMessage) &&
           serialFromJson(serialToJson(hmp), copy.hmp, QStringLiteral("hmp"), errorMessage) &&
           serialFromJson(serialToJson(lidar), copy.lidar, QStringLiteral("lidar"), errorMessage) &&
           temperatureControllerFromJson(temperatureControllerToJson(temperature_controller), copy.temperature_controller, errorMessage) &&
           ai8TemperatureControllerFromJson(temperatureControllerToJson(ai8_temperature_controller), copy.ai8_temperature_controller, errorMessage) &&
           waveFromJson(waveToJson(wave_tcp), copy.wave_tcp, errorMessage) &&
           telemetryFromJson(telemetryToJson(telemetry), copy.telemetry, errorMessage);
}

SkyConfigDiff SkyConfig::diff(const SkyConfig& other) const
{
    SkyConfigDiff result;
    result.epsilon_changed = epsilon != other.epsilon;
    result.epsilon_rtcm_changed = epsilon_rtcm != other.epsilon_rtcm;
    result.ptb_changed = ptb != other.ptb;
    result.hmp_changed = hmp != other.hmp;
    result.lidar_changed = lidar != other.lidar;
    result.temperature_controller_changed = temperature_controller != other.temperature_controller;
    result.ai8_temperature_controller_changed = ai8_temperature_controller != other.ai8_temperature_controller;
    result.wave_tcp_changed = wave_tcp != other.wave_tcp;
    result.telemetry_changed = telemetry != other.telemetry;
    return result;
}

}  // namespace VaporView
