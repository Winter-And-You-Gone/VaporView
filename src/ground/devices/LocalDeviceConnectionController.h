#pragma once

#include "LogRecord.h"
#include "ground/devices/CollectorRegistry.h"
#include "TelemetryTypes.h"

#include <QString>
#include <QVariantMap>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>

namespace VaporView::Ground::Devices
{

struct ImuProfileRequest;

enum class LocalDeviceKind
{
    Epsilon,
    Ptb,
    Hmp,
    Lidar,
    TemperatureController,
    Ai8TemperatureController
};

struct LocalSerialDeviceSettings
{
    bool requested = true;
    bool enabled = true;
    QString port;
    QString baudText;
    int sampleRateHz = 1;
    bool skipDeviceRate = false;
};

// Persistent local-device configuration edited by the Device Configuration page.
// This is deliberately UI-free so connection, auto-detect, settings and recording
// paths do not need to inspect QWidget state.
struct LocalDeviceConfig
{
    LocalSerialDeviceSettings epsilon{true, true, {}, QStringLiteral("921600"), 100, false};
    LocalSerialDeviceSettings ptb{true, true, {}, QStringLiteral("9600"), 1, false};
    LocalSerialDeviceSettings hmp{true, true, {}, QStringLiteral("19200"), 1, false};
    LocalSerialDeviceSettings lidar{true, true, {}, QStringLiteral("500000"), 1, false};
    LocalSerialDeviceSettings temperatureController{true, true, {}, QStringLiteral("38400"), 1, false};
    LocalSerialDeviceSettings ai8TemperatureController{true, true, {}, QStringLiteral("19200"), 5, false};

    PressureSensorProtocol pressureProtocol = PressureSensorProtocol::Ptb210;
    HumiditySensorProtocol humidityProtocol = HumiditySensorProtocol::Hmp3Modbus;
    QString pressureSource = QStringLiteral("ptb210");
    QString humiditySource = QStringLiteral("hmp3");
    QString ptbRateText = QStringLiteral("1");
    QString hmpRateText = QStringLiteral("1");
    QString lidarRateText = QStringLiteral("1");
    QString temperatureRateText = QStringLiteral("1");
    QString ai8RateText = QStringLiteral("5");

    // PTB/HMP keep independent port/baud values per source.  The active values
    // above are mirrored into these maps whenever a source is changed.
    std::map<QString, LocalSerialDeviceSettings> pressureBySource;
    std::map<QString, LocalSerialDeviceSettings> humidityBySource;
};

LocalSerialDeviceSettings makeLocalConnectionSettings(
    const LocalSerialDeviceSettings& config,
    bool requested,
    int sampleRateHz,
    bool skipDeviceRate);

struct LocalConnectionRequest
{
    bool english = false;
    bool includeWaveform = true;
    QString selectText;
    LocalSerialDeviceSettings epsilon;
    LocalSerialDeviceSettings ptb;
    LocalSerialDeviceSettings hmp;
    LocalSerialDeviceSettings lidar;
    LocalSerialDeviceSettings temperatureController;
    LocalSerialDeviceSettings ai8TemperatureController;
    PressureSensorProtocol pressureProtocol = PressureSensorProtocol::Ptb210;
    HumiditySensorProtocol humidityProtocol = HumiditySensorProtocol::Hmp3Modbus;
    int temperatureSlaveAddress = 1;
    int ai8SlaveAddress = 1;
    std::map<uint8_t, int> epsilonPacketRates;
    int epsilonConfiguredRateHz = 100;
    QString epsilonPacketRateSignature;
    QString epsilonPacketRateSummary;
    bool epsilonPacketRatesMatchDefault = true;
    bool epsilonConfigLikelyMatches = false;
};

struct LocalTemperatureConnectionRequest
{
    bool english = false;
    QString port;
    QString baudText;
    int baudRate = 38400;
    int sampleRateHz = 1;
    int slaveAddress = 1;
    bool usesDefaultRate = false;
};

struct LocalConnectionLogEntry
{
    VaporView::LogLevel level = VaporView::LogLevel::Info;
    QString category = QStringLiteral("device.connection");
    QString event;
    QString message;
    QVariantMap fields;
};

struct LocalConnectionCallbacks
{
    std::function<void(const LocalConnectionLogEntry&)> log;
    std::function<void(bool)> finished;
    std::function<void(LocalDeviceKind)> dataReady;
    std::function<void(quint64, quint8, quint8, const void *, size_t)> rawEpsilonFrame;
    std::function<void(quint64, const void *, size_t)> rawPtbResponse;
    std::function<void(quint64, const void *, size_t)> rawHmpResponse;
    std::function<void(quint64, quint16, const void *, size_t)> rawLidarFrame;
    std::function<void(quint64, quint16, const void *, size_t)> rawLaserTemperatureControllerResponse;
    std::function<void(quint64, quint16, const void *, size_t)> rawSystemTemperatureControllerResponse;
};

struct LocalSampleRateConfiguration
{
    int epsilonCallbackRateHz = 100;
    std::map<uint8_t, int> epsilonPacketRates;
    int ptbRateHz = 1;
    bool applyPtbDeviceRate = true;
    int hmpRateHz = 1;
    int lidarRateHz = 1;
    bool applyLidarDeviceRate = true;
    int temperatureRateHz = 1;
    int ai8TemperatureRateHz = 5;
};

struct LocalSampleRateApplyResult
{
    bool epsilonDeviceRateAttempted = false;
    bool epsilonDeviceRateSucceeded = true;
    bool ptbDeviceRateAttempted = false;
    bool ptbDeviceRateSucceeded = true;
};

enum class LocalTemperatureCommandStatus
{
    NotConnected,
    Confirmed,
    Rejected
};

struct LocalTemperatureCommandResult
{
    LocalTemperatureCommandStatus status = LocalTemperatureCommandStatus::NotConnected;
    TemperatureControllerData latestData;
};

struct LocalAi8OperationResult
{
    bool success = false;
    QString message;
    Ai8TemperatureControllerProtocol::PageData data;
};

class LocalDeviceConnectionController final
{
public:
    LocalDeviceConnectionController();
    ~LocalDeviceConnectionController();

    LocalDeviceConnectionController(const LocalDeviceConnectionController&) = delete;
    LocalDeviceConnectionController& operator=(const LocalDeviceConnectionController&) = delete;

    void setCallbacks(LocalConnectionCallbacks callbacks);
    bool connectAsync(LocalConnectionRequest request);
    bool connectTemperatureAsync(
        LocalTemperatureConnectionRequest request,
        std::function<void(bool, const QString&)> completion);
    void requestCancel();
    void disconnect();
    void wait();

    bool connectionInProgress() const;
    bool cancelRequested() const;
    bool anyCollectorRunning() const;
    CollectorSet snapshotCollectors() const;
    LocalSampleRateApplyResult applyRunningSampleRates(
        const LocalSampleRateConfiguration& configuration);
    LocalSampleRateApplyResult setEpsilonSampleRate(
        int callbackRateHz,
        const std::map<uint8_t, int>& packetRates);
    LocalSampleRateApplyResult setPtbSampleRate(int rateHz, bool applyDeviceRate);
    void setHmpSampleRate(int rateHz);
    void setLidarSampleRate(int rateHz, bool applyDeviceRate);
    void setTemperatureSampleRate(int rateHz);
    void setAi8TemperatureSampleRate(int rateHz);
    bool applyImuProfile(const ImuProfileRequest& request);
    LocalTemperatureCommandResult sendTemperatureCommand(
        CommandId command,
        const TemperatureControllerCommand& payload);
    bool disconnectTemperatureController();
    LocalAi8OperationResult readAi8Page(
        Ai8TemperatureControllerProtocol::Page page,
        const Ai8TemperatureControllerProtocol::Selection& selection);
    LocalAi8OperationResult writeAi8Page(
        const Ai8TemperatureControllerProtocol::PageData& data);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace VaporView::Ground::Devices
