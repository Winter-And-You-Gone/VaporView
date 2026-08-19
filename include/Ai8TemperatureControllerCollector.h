#pragma once

#include "Ai8TemperatureControllerProtocol.h"
#include "data_collector.h"

#include <functional>
#include <mutex>
#include <vector>

namespace VaporView
{

class Ai8TemperatureControllerCollector final : public DataCollector
{
public:
    using RawFrameCallback = std::function<void(uint64_t hostTimestampUs,
                                                quint16 recordType,
                                                const quint8 *frame,
                                                size_t size)>;
    using RegisterReadBackendForTest = std::function<bool(quint16,
                                                          quint16,
                                                          std::vector<quint16>&)>;
    using RegisterWriteBackendForTest = std::function<bool(quint16,
                                                           quint16,
                                                           QString*)>;

    Ai8TemperatureControllerProtocol::LiveData getLatestData();
    bool checkDeviceResponse() override;
    void setSlaveAddress(quint8 slaveAddress);
    quint8 slaveAddress() const;
    void setRawFrameCallback(RawFrameCallback callback);
    void setRegisterBackendForTest(RegisterReadBackendForTest readBackend,
                                   RegisterWriteBackendForTest writeBackend);

    bool readPage(Ai8TemperatureControllerProtocol::Page page,
                  const Ai8TemperatureControllerProtocol::Selection& selection,
                  Ai8TemperatureControllerProtocol::PageData& data,
                  QString *errorMessage = nullptr);
    bool writePage(const Ai8TemperatureControllerProtocol::PageData& data,
                   QString *resultMessage = nullptr);

protected:
    bool initialize() override;
    void run() override;

private:
    bool readRegisters(quint16 address,
                       quint16 count,
                       std::vector<quint16>& values,
                       int waitMs = 250,
                       quint16 rawRecordType = 0);
    bool readRegistersUnlocked(quint16 address,
                               quint16 count,
                               std::vector<quint16>& values,
                               int waitMs,
                               quint16 rawRecordType = 0);
    bool writeAndConfirm(quint16 address, quint16 value, QString *errorMessage);
    bool writeAndConfirmUnlocked(quint16 address, quint16 value, QString *errorMessage);
    bool readResponseFrame(quint8 functionCode, std::vector<quint8>& frame, int waitMs);
    bool readRegisterValue(quint16 address, quint16& value, QString *errorMessage = nullptr);
    bool readRegisterValueUnlocked(quint16 address, quint16& value, QString *errorMessage = nullptr);
    bool readChannelPage(const Ai8TemperatureControllerProtocol::Selection& selection,
                         Ai8TemperatureControllerProtocol::PageData& data,
                         QString *errorMessage);
    bool readInputPage(const Ai8TemperatureControllerProtocol::Selection& selection,
                       Ai8TemperatureControllerProtocol::PageData& data,
                       QString *errorMessage);
    bool readOutputPage(const Ai8TemperatureControllerProtocol::Selection& selection,
                        Ai8TemperatureControllerProtocol::PageData& data,
                        QString *errorMessage);
    bool readGlobalPage(const Ai8TemperatureControllerProtocol::Selection& selection,
                        Ai8TemperatureControllerProtocol::PageData& data,
                        QString *errorMessage);
    bool writeChannelPage(const Ai8TemperatureControllerProtocol::PageData& data, QString *errorMessage);
    bool writeInputPage(const Ai8TemperatureControllerProtocol::PageData& data, QString *errorMessage);
    bool writeOutputPage(const Ai8TemperatureControllerProtocol::PageData& data, QString *errorMessage);
    bool writeGlobalPage(const Ai8TemperatureControllerProtocol::PageData& data, QString *resultMessage);
    bool readLiveData(Ai8TemperatureControllerProtocol::LiveData& data);
    void publishRawFrame(quint16 recordType, const std::vector<quint8>& frame);

    Ai8TemperatureControllerProtocol::LiveData latestData_;
    std::atomic<quint8> slaveAddress_{1};
    std::mutex modbusMutex_;
    RawFrameCallback rawFrameCallback_;
    RegisterReadBackendForTest readBackendForTest_;
    RegisterWriteBackendForTest writeBackendForTest_;
};

} // namespace VaporView
