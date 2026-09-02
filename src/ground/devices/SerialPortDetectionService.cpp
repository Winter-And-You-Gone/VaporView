#include "ground/devices/SerialPortDetectionService.h"

#include "Ai8TemperatureControllerCollector.h"
#include "data_collector.h"
#include "SerialBaudRateCapabilities.h"
#include "SerialBaudRate.h"

#include <QHash>
#include <QSerialPortInfo>
#include <QSet>

#include <memory>
#include <utility>

namespace VaporView::Ground::Devices
{
namespace
{

struct ProbeSpec
{
    QString key;
    QString label;
    QString baud;
    std::function<bool(const QString&)> probe;
};

struct SelectedProbeSpec
{
    ProbeSpec probe;
    QString port;
};

QVariantMap probeFields(const ProbeSpec& probe, const QString& port = QString())
{
    QVariantMap fields{{QStringLiteral("device_key"), probe.key},
                       {QStringLiteral("device"), probe.label},
                       {QStringLiteral("baud"), probe.baud}};
    if (!port.isEmpty())
    {
        fields.insert(QStringLiteral("port"), port);
    }
    return fields;
}

void postSerialPortDetectionLog(const SerialPortDetectionService::LogCallback& log,
                                LogLevel level,
                                const QString& event,
                                const QString& message,
                                QVariantMap fields = QVariantMap())
{
    if (!fields.contains(QStringLiteral("ui_visibility")))
    {
        fields.insert(QStringLiteral("ui_visibility"),
                      level >= LogLevel::Warning ? QStringLiteral("attention")
                                                 : QStringLiteral("details"));
    }
    if (log)
    {
        log({level, event, message, std::move(fields)});
    }
}

QString normalizedBaud(const QString& baud,
                       const QString& fallback,
                       const VaporView::BaudRateCapabilities& capabilities)
{
    const QString normalized = VaporView::normalizedSerialBaudRateText(baud);
    if (VaporView::isBaudRateSupported(capabilities, normalized))
    {
        return normalized;
    }
    return VaporView::normalizedSerialBaudRateText(fallback);
}

template <typename Collector>
bool probeCollector(const QString& port,
                    std::unique_ptr<Collector> collector,
                    const SerialConfig& config,
                    const SerialPortDetectionService::CancelCallback& cancelRequested)
{
    collector->setCancelCallback(cancelRequested);
    if (!collector->start(port.toStdString(), config))
    {
        return false;
    }
    const bool responded = collector->checkDeviceResponse();
    collector->stop();
    return responded;
}

}  // namespace

QStringList SerialPortDetectionService::availablePorts()
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& info : infos)
    {
#ifdef _WIN32
        ports.append(info.portName());
#else
        const QString path = info.systemLocation();
        ports.append(path.isEmpty() ? info.portName() : path);
#endif
    }
    ports.removeDuplicates();
    ports.sort();
    return ports;
}

SerialPortDetectionOutcome SerialPortDetectionService::detect(
    const SerialPortDetectionRequest& request,
    CancelCallback cancelRequested,
    LogCallback log)
{
    const bool english = request.english;
    if (!cancelRequested)
    {
        cancelRequested = []() { return false; };
    }

    const QString epsilonDefaultBaud = QStringLiteral("921600");
    const bool useBmp390 = request.pressureProtocol == VaporView::PressureSensorProtocol::Bmp390Serial;
    const bool useSht45 = request.humidityProtocol == VaporView::HumiditySensorProtocol::Sht45Serial;
    const QString ptbDefaultBaud = useBmp390 ? QStringLiteral("115200") : QStringLiteral("9600");
    const QString hmpDefaultBaud = useSht45 ? QStringLiteral("115200") : QStringLiteral("19200");
    const QString lidarDefaultBaud = QStringLiteral("500000");
    const QString temperatureDefaultBaud = QStringLiteral("38400");
    const QString ai8TemperatureDefaultBaud = QStringLiteral("19200");

    auto makeEpsilonProbe = [&](const QString& baud) {
        return ProbeSpec{QStringLiteral("epsilon"), QStringLiteral("EPSILON"), baud,
            [cancelRequested, baud](const QString& port) {
                const auto baudRate = VaporView::parseSerialBaudRate(baud);
                if (!baudRate)
                {
                    return false;
                }
                return probeCollector(port,
                                      std::make_unique<EpsilonCollector>(),
                                      SerialConfig::N81(*baudRate),
                                      cancelRequested);
            }};
    };
    auto makePtbProbe = [&](const QString& baud) {
        return ProbeSpec{QStringLiteral("ptb"),
            useBmp390 ? QStringLiteral("BMP390") : QStringLiteral("PTB210"),
            baud,
            [cancelRequested, baud, useBmp390](const QString& port) {
                const auto baudRate = VaporView::parseSerialBaudRate(baud);
                if (!baudRate)
                {
                    return false;
                }
                auto collector = std::make_unique<PtbCollector>();
                collector->setProtocol(useBmp390
                                           ? VaporView::PressureSensorProtocol::Bmp390Serial
                                           : VaporView::PressureSensorProtocol::Ptb210);
                return probeCollector(port,
                                      std::move(collector),
                                      useBmp390 ? SerialConfig::N81(*baudRate)
                                                 : SerialConfig::E71(*baudRate),
                                      cancelRequested);
            }};
    };
    auto makeHmpProbe = [&](const QString& baud) {
        return ProbeSpec{QStringLiteral("hmp"),
            useSht45 ? QStringLiteral("SHT45") : QStringLiteral("HMP3"),
            baud,
            [cancelRequested, baud, useSht45](const QString& port) {
                const auto baudRate = VaporView::parseSerialBaudRate(baud);
                if (!baudRate)
                {
                    return false;
                }
                auto collector = std::make_unique<HmpCollector>();
                collector->setProtocol(useSht45
                                           ? VaporView::HumiditySensorProtocol::Sht45Serial
                                           : VaporView::HumiditySensorProtocol::Hmp3Modbus);
                return probeCollector(port,
                                      std::move(collector),
                                      useSht45 ? SerialConfig::N81(*baudRate)
                                               : SerialConfig::N82(*baudRate),
                                      cancelRequested);
            }};
    };
    auto makeLidarProbe = [&](const QString& baud) {
        return ProbeSpec{QStringLiteral("lidar"), QStringLiteral("TFA1500-L"), baud,
            [cancelRequested, baud](const QString& port) {
                const auto baudRate = VaporView::parseSerialBaudRate(baud);
                if (!baudRate)
                {
                    return false;
                }
                return probeCollector(port,
                                      std::make_unique<LidarCollector>(),
                                      SerialConfig::N81(*baudRate),
                                      cancelRequested);
            }};
    };
    auto makeTemperatureProbe = [&](const QString& baud) {
        return ProbeSpec{QStringLiteral("temperature"), QStringLiteral("RD105"), baud,
            [cancelRequested, baud, slaveAddress = request.temperatureSlaveAddress](const QString& port) {
                const auto baudRate = VaporView::parseSerialBaudRate(baud);
                if (!baudRate)
                {
                    return false;
                }
                auto collector = std::make_unique<TemperatureControllerCollector>();
                collector->setSlaveAddress(static_cast<uint8_t>(slaveAddress));
                return probeCollector(port,
                                      std::move(collector),
                                      SerialConfig::N81(*baudRate),
                                      cancelRequested);
            }};
    };
    auto makeAi8TemperatureProbe = [&](const QString& baud) {
        return ProbeSpec{QStringLiteral("ai8"), QStringLiteral("AI-8288"), baud,
            [cancelRequested, baud, slaveAddress = request.ai8SlaveAddress](const QString& port) {
                const auto baudRate = VaporView::parseSerialBaudRate(baud);
                if (!baudRate || !VaporView::isBaudRateSupported(
                                     VaporView::ai8TemperatureControllerBaudCapabilities(),
                                     *baudRate))
                {
                    return false;
                }
                auto collector = std::make_unique<Ai8TemperatureControllerCollector>();
                collector->setSlaveAddress(static_cast<quint8>(slaveAddress));
                return probeCollector(port,
                                      std::move(collector),
                                      SerialConfig::N81(*baudRate),
                                      cancelRequested);
            }};
    };

    QVector<SelectedProbeSpec> selected;
    QSet<QString> selectedIds;
    auto addSelected = [&selected, &selectedIds](ProbeSpec probe, const QString& port) {
        const QString normalizedPort = port.trimmed();
        if (normalizedPort.isEmpty() || normalizedPort.startsWith(QStringLiteral("--")))
        {
            return;
        }
        const QString id = probe.key + QLatin1Char('@') + normalizedPort + QLatin1Char('@') + probe.baud;
        if (!selectedIds.contains(id))
        {
            selectedIds.insert(id);
            selected.push_back({std::move(probe), normalizedPort});
        }
    };
    auto selectedBaud = [&](const QString& key,
                            const QString& label,
                            const QString& configuredBaud,
                            const QString& fallbackBaud,
                            const VaporView::BaudRateCapabilities& capabilities) {
        const QString normalized = VaporView::normalizedSerialBaudRateText(configuredBaud);
        if (!normalized.isEmpty() && !VaporView::isBaudRateSupported(capabilities, normalized))
        {
            postSerialPortDetectionLog(
                log,
                LogLevel::Warning,
                QStringLiteral("serial_port_detection_rejected_unsupported_baud"),
                QStringLiteral("自动识别已忽略设备不支持的已选波特率，并改用默认值。"),
                {{QStringLiteral("device_key"), key},
                 {QStringLiteral("device"), label},
                 {QStringLiteral("configured_baud"), configuredBaud},
                 {QStringLiteral("fallback_baud"), fallbackBaud},
                 {QStringLiteral("error_code"), QStringLiteral("UNSUPPORTED_BAUD_RATE")},
                 {QStringLiteral("reason_code"), QStringLiteral("UNSUPPORTED_BAUD_RATE")}});
        }
        return normalizedBaud(configuredBaud, fallbackBaud, capabilities);
    };
    addSelected(makeEpsilonProbe(selectedBaud(QStringLiteral("epsilon"),
                                              QStringLiteral("EPSILON"),
                                              request.epsilon.baud,
                                              epsilonDefaultBaud,
                                              VaporView::epsilonConnectionBaudCapabilities())),
                request.epsilon.port);
    addSelected(makePtbProbe(selectedBaud(QStringLiteral("ptb"),
                                          useBmp390 ? QStringLiteral("BMP390")
                                                     : QStringLiteral("PTB210"),
                                          request.ptb.baud,
                                          ptbDefaultBaud,
                                              useBmp390 ? VaporView::bmp390SerialAdapterBaudCapabilities()
                                                         : VaporView::ptb210BaudCapabilities())),
                request.ptb.port);
    addSelected(makeHmpProbe(selectedBaud(QStringLiteral("hmp"),
                                          useSht45 ? QStringLiteral("SHT45")
                                                   : QStringLiteral("HMP3"),
                                          request.hmp.baud,
                                          hmpDefaultBaud,
                                          useSht45 ? VaporView::sht45SerialAdapterBaudCapabilities()
                                                   : VaporView::hmp3BaudCapabilities())),
                request.hmp.port);
    addSelected(makeLidarProbe(selectedBaud(QStringLiteral("lidar"),
                                            QStringLiteral("TFA1500-L"),
                                            request.lidar.baud,
                                            lidarDefaultBaud,
                                            VaporView::lidarBaudCapabilities())),
                request.lidar.port);
    addSelected(makeTemperatureProbe(selectedBaud(
                    QStringLiteral("temperature"),
                    QStringLiteral("RD105"),
                    request.temperatureController.baud,
                    temperatureDefaultBaud,
                    VaporView::rd105BaudCapabilities())),
                request.temperatureController.port);
    addSelected(makeAi8TemperatureProbe(selectedBaud(
                    QStringLiteral("ai8"),
                    QStringLiteral("AI-8288"),
                    request.ai8TemperatureController.baud,
                    ai8TemperatureDefaultBaud,
                    VaporView::ai8TemperatureControllerBaudCapabilities())),
                request.ai8TemperatureController.port);

    QVector<ProbeSpec> defaults;
    defaults << makeEpsilonProbe(epsilonDefaultBaud)
             << makePtbProbe(ptbDefaultBaud)
             << makeHmpProbe(hmpDefaultBaud)
             << makeLidarProbe(lidarDefaultBaud)
             << makeTemperatureProbe(temperatureDefaultBaud)
             << makeAi8TemperatureProbe(ai8TemperatureDefaultBaud);

    SerialPortDetectionOutcome outcome;
    if (request.availablePorts.isEmpty() && selected.isEmpty())
    {
        postSerialPortDetectionLog(
            log,
            LogLevel::Warning,
            QStringLiteral("serial_port_detection_no_ports"),
            QStringLiteral("自动识别结束，当前没有发现可用串口。"),
            {{QStringLiteral("reason_code"), QStringLiteral("NO_SERIAL_PORTS")},
             {QStringLiteral("serial_port_count"), request.availablePorts.size()},
             {QStringLiteral("selected_candidates"), selected.size()},
             {QStringLiteral("ui_message"),
              english ? QStringLiteral("Auto detect stopped: no serial ports found.")
                      : QStringLiteral("自动识别结束：当前没有发现可用串口。")}});
        return outcome;
    }

    QSet<QString> detectedKeys;
    QSet<QString> detectedPorts;
    QSet<QString> attempted;
    postSerialPortDetectionLog(
        log,
        LogLevel::Info,
        QStringLiteral("serial_port_detection_plan_created"),
        QStringLiteral("串口自动识别计划已创建。"),
        {{QStringLiteral("serial_port_count"), request.availablePorts.size()},
         {QStringLiteral("selected_candidates"), selected.size()},
         {QStringLiteral("default_probe_count"), defaults.size()},
         {QStringLiteral("ui_message"),
          QString(english
              ? "Auto detect: selected settings first, then %1 serial port(s) with default bauds."
              : "自动识别：先探测已选配置，再用默认波特率探测 %1 个串口。")
              .arg(request.availablePorts.size())}});

    auto attemptId = [](const ProbeSpec& probe, const QString& port) {
        return probe.key + QLatin1Char('@') + port + QLatin1Char('@') + probe.baud;
    };
    auto record = [&](const ProbeSpec& probe, const QString& port) {
        outcome.detections.push_back({probe.key, port, probe.baud});
        detectedKeys.insert(probe.key);
        detectedPorts.insert(port);
        QVariantMap fields = probeFields(probe, port);
        fields.insert(QStringLiteral("detected_devices"), outcome.detections.size());
        fields.insert(QStringLiteral("ui_message"),
                      QString(english ? "[Auto Detect] Identified %1 on %2 @ %3"
                                      : "[自动识别] 已识别 %1: %2 @ %3")
                          .arg(probe.label, port, probe.baud));
        postSerialPortDetectionLog(log,
                                   LogLevel::Info,
                                   QStringLiteral("serial_port_detection_device_identified"),
                                   QStringLiteral("串口自动识别已识别设备。"),
                                   std::move(fields));
    };
    auto canceled = [&]() {
        if (!cancelRequested()) return false;
        outcome.canceled = true;
        postSerialPortDetectionLog(
            log,
            LogLevel::Info,
            QStringLiteral("serial_port_detection_cancelled"),
            QStringLiteral("串口自动识别已取消。"),
            {{QStringLiteral("reason_code"), QStringLiteral("USER_CANCELLED")},
             {QStringLiteral("detected_devices"), outcome.detections.size()},
             {QStringLiteral("ui_visibility"), QStringLiteral("attention")},
             {QStringLiteral("ui_message"),
              QString(english
                  ? "Auto detect canceled; keeping %1 identified device(s)."
                  : "自动识别已取消；保留已识别出的 %1 个设备。")
                  .arg(outcome.detections.size())}});
        return true;
    };

    if (!selected.isEmpty())
    {
        postSerialPortDetectionLog(
            log,
            LogLevel::Info,
            QStringLiteral("serial_port_detection_selected_pass_started"),
            QStringLiteral("开始按已选串口和波特率探测设备。"),
            {{QStringLiteral("selected_candidates"), selected.size()},
             {QStringLiteral("ui_message"),
              english
                  ? QStringLiteral("[Auto Detect] Selected port/baud pass: probing the current configured port for each device.")
                  : QStringLiteral("[自动识别] 已选串口/波特率阶段：先探测每个设备当前配置的串口。")}});
    }
    for (const SelectedProbeSpec& selectedProbe : selected)
    {
        if (detectedKeys.contains(selectedProbe.probe.key) || detectedPorts.contains(selectedProbe.port))
        {
            continue;
        }
        if (canceled()) return outcome;
        attempted.insert(attemptId(selectedProbe.probe, selectedProbe.port));
        {
            QVariantMap fields = probeFields(selectedProbe.probe, selectedProbe.port);
            fields.insert(QStringLiteral("probe_phase"), QStringLiteral("selected"));
            fields.insert(QStringLiteral("ui_visibility"), QStringLiteral("details"));
            fields.insert(QStringLiteral("ui_message"),
                          QString(english
                              ? "[Auto Detect] Probing selected %1 on %2 @ %3..."
                              : "[自动识别] 正在探测已选 %1: %2 @ %3 ...")
                              .arg(selectedProbe.probe.label, selectedProbe.port, selectedProbe.probe.baud));
            postSerialPortDetectionLog(log,
                                       LogLevel::Info,
                                       QStringLiteral("serial_port_detection_probe_started"),
                                       QStringLiteral("开始探测串口设备。"),
                                       std::move(fields));
        }
        if (selectedProbe.probe.probe(selectedProbe.port))
        {
            record(selectedProbe.probe, selectedProbe.port);
        }
    }

    if (!defaults.isEmpty() && !request.availablePorts.isEmpty())
    {
        postSerialPortDetectionLog(
            log,
            LogLevel::Info,
            QStringLiteral("serial_port_detection_default_pass_started"),
            QStringLiteral("开始按默认波特率探测剩余设备。"),
            {{QStringLiteral("serial_port_count"), request.availablePorts.size()},
             {QStringLiteral("default_probe_count"), defaults.size()},
             {QStringLiteral("ui_message"),
              english
                  ? QStringLiteral("[Auto Detect] Default baud pass: probing remaining devices on available ports.")
                  : QStringLiteral("[自动识别] 默认波特率阶段：使用各设备默认波特率探测剩余串口。")}});
    }
    for (const ProbeSpec& probe : defaults)
    {
        if (detectedKeys.contains(probe.key))
        {
            continue;
        }
        for (const QString& port : request.availablePorts)
        {
            if (canceled()) return outcome;
            if (detectedPorts.contains(port) || attempted.contains(attemptId(probe, port)))
            {
                continue;
            }
            attempted.insert(attemptId(probe, port));
            {
                QVariantMap fields = probeFields(probe, port);
                fields.insert(QStringLiteral("probe_phase"), QStringLiteral("default"));
                fields.insert(QStringLiteral("ui_visibility"), QStringLiteral("details"));
                fields.insert(QStringLiteral("ui_message"),
                              QString(english ? "[Auto Detect] Probing %1 on %2 @ %3..."
                                              : "[自动识别] 正在探测 %1: %2 @ %3 ...")
                                  .arg(probe.label, port, probe.baud));
                postSerialPortDetectionLog(log,
                                           LogLevel::Info,
                                           QStringLiteral("serial_port_detection_probe_started"),
                                           QStringLiteral("开始探测串口设备。"),
                                           std::move(fields));
            }
            if (probe.probe(port))
            {
                record(probe, port);
                break;
            }
        }
    }

    const QHash<QString, QString> labels{
        {QStringLiteral("epsilon"), QStringLiteral("EPSILON")},
        {QStringLiteral("ptb"), QStringLiteral("PTB210")},
        {QStringLiteral("hmp"), QStringLiteral("HMP3")},
        {QStringLiteral("lidar"), QStringLiteral("TFA1500-L")},
        {QStringLiteral("temperature"), QStringLiteral("RD105")},
        {QStringLiteral("ai8"), QStringLiteral("AI-8288")},
    };
    for (auto it = labels.cbegin(); it != labels.cend(); ++it)
    {
        if (!detectedKeys.contains(it.key()))
        {
            postSerialPortDetectionLog(
                log,
                LogLevel::Info,
                QStringLiteral("serial_port_detection_device_not_found"),
                QStringLiteral("串口自动识别未找到设备。"),
                {{QStringLiteral("device_key"), it.key()},
                 {QStringLiteral("device"), it.value()},
                 {QStringLiteral("ui_message"),
                  QString(english ? "[Auto Detect] %1 not found" : "[自动识别] 未找到 %1").arg(it.value())}});
        }
    }
    postSerialPortDetectionLog(
        log,
        LogLevel::Info,
        QStringLiteral("serial_port_detection_completed"),
        QStringLiteral("串口自动识别已完成。"),
        {{QStringLiteral("detected_devices"), outcome.detections.size()},
         {QStringLiteral("attempted_probes"), attempted.size()},
         {QStringLiteral("ui_message"),
          QString(english ? "Auto detect finished: identified %1 device(s)."
                          : "自动识别完成：共识别出 %1 个设备。")
              .arg(outcome.detections.size())}});
    return outcome;
}

}  // namespace VaporView::Ground::Devices
