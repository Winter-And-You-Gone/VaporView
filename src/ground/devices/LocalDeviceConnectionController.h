#pragma once

#include "ground/devices/CollectorRegistry.h"
#include "TelemetryTypes.h"

#include <QString>

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
    TemperatureController
};

struct LocalSerialDeviceSettings
{
    QString port;
    QString baudText;
    int sampleRateHz = 1;
    bool skipDeviceRate = false;
};

struct LocalConnectionRequest
{
    bool english = false;
    QString selectText;
    LocalSerialDeviceSettings epsilon;
    LocalSerialDeviceSettings ptb;
    LocalSerialDeviceSettings hmp;
    LocalSerialDeviceSettings lidar;
    LocalSerialDeviceSettings temperatureController;
    PressureSensorProtocol pressureProtocol = PressureSensorProtocol::Ptb210;
    HumiditySensorProtocol humidityProtocol = HumiditySensorProtocol::Hmp3Modbus;
    int temperatureSlaveAddress = 1;
    std::map<uint8_t, int> epsilonPacketRates;
    int epsilonConfiguredRateHz = 100;
    QString epsilonPacketRateSignature;
    QString epsilonPacketRateSummary;
    bool epsilonUsesCustomPacketRates = false;
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

struct LocalConnectionCallbacks
{
    std::function<void(const QString&)> log;
    std::function<void(bool)> finished;
    std::function<void(LocalDeviceKind)> dataReady;
    std::function<void(quint64, quint8, quint8, const void *, size_t)> rawEpsilonFrame;
    std::function<void(quint64, const void *, size_t)> rawPtbResponse;
    std::function<void(quint64, const void *, size_t)> rawHmpResponse;
    std::function<void(quint64, quint16, const void *, size_t)> rawLidarFrame;
};

struct LocalSampleRateConfiguration
{
    int epsilonCallbackRateHz = 100;
    std::map<uint8_t, int> epsilonPacketRates;
    bool applyEpsilonDeviceRate = true;
    int ptbRateHz = 1;
    bool applyPtbDeviceRate = true;
    int hmpRateHz = 1;
    int lidarRateHz = 1;
    bool applyLidarDeviceRate = true;
    int temperatureRateHz = 1;
};

struct LocalSampleRateApplyResult
{
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
    void setEpsilonSampleRate(
        int callbackRateHz,
        const std::map<uint8_t, int>& packetRates,
        bool applyDeviceRate);
    LocalSampleRateApplyResult setPtbSampleRate(int rateHz, bool applyDeviceRate);
    void setHmpSampleRate(int rateHz);
    void setLidarSampleRate(int rateHz, bool applyDeviceRate);
    void setTemperatureSampleRate(int rateHz);
    bool applyImuProfile(const ImuProfileRequest& request);
    LocalTemperatureCommandResult sendTemperatureCommand(
        CommandId command,
        const TemperatureControllerCommand& payload);
    bool disconnectTemperatureController();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace VaporView::Ground::Devices
