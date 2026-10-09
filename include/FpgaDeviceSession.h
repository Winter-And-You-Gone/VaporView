#pragma once

#include "FpgaSensorDecoder.h"
#include "FpgaUsbTransport.h"
#include "FpgaVlp1.h"
#include "FpgaWaveformAssembler.h"
#include "data_types.h"

#include <QObject>
#include <QElapsedTimer>
#include <QQueue>
#include <QTimer>
#include <QVector>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace VaporView::Ground::Devices
{

struct FpgaCapabilities
{
    quint32 status = 0;
    quint32 capabilities = 0;
    quint32 protocolVersion = 0;
    quint32 timeClockHz = 0;
    quint32 maxBulkPayload = 0;
    quint32 maxCommandFrame = 0;
    quint32 extensions = 0;
    bool valid = false;
};

enum class FpgaSessionState
{
    Closed,
    Opening,
    Ready,
    Error,
};

struct FpgaRegisterReadResult
{
    quint32 address = 0;
    QVector<quint32> values;
};

class FpgaDeviceSession final : public QObject
{
    Q_OBJECT

public:
    explicit FpgaDeviceSession(std::unique_ptr<FpgaUsbTransport> transport = {},
                                QObject *parent = nullptr);
    ~FpgaDeviceSession() override;

    FpgaDeviceSession(const FpgaDeviceSession&) = delete;
    FpgaDeviceSession& operator=(const FpgaDeviceSession&) = delete;

    bool open(const QString& locator = QString(), QString *errorMessage = nullptr);
    void close();
    bool isOpen() const;
    FpgaSessionState state() const;
    QString stateDetail() const;
    const FpgaCapabilities& capabilities() const;
    FpgaUsbTransport* transport() const;

    // Polling is timer-driven after open(), and remains public for deterministic
    // replay tests and callers that own their event loop.
    void poll();

    quint32 ping(const QByteArray& echo = {}, quint16 source = 0x0001);
    quint32 requestCapabilities(quint16 source = 0x0001);
    quint32 readRegisters(quint32 address, quint16 count, quint16 source = 0x0001);
    quint32 writeRegisters(quint32 address, const QVector<quint32>& values,
                           quint16 flags = 0, quint16 source = 0x0001);
    quint32 maskedWrite(quint32 address, quint32 mask, quint32 value,
                        quint16 source = 0x0001);
    quint32 commitConfig(quint64 moduleMask, quint16 source = 0x0001);
    quint32 startAcquisition(quint64 sourceMask = 0x7D, quint16 source = 0x0001);
    quint32 stopAcquisition(quint64 sourceMask = 0x7D, quint16 source = 0x0001);
    quint32 setStreamMask(quint64 enableMask, quint64 rawEnableMask,
                          quint16 source = 0x0001);
    quint32 sensorAction(quint32 action, quint32 value, quint16 source = 0x0001);

signals:
    void stateChanged(VaporView::Ground::Devices::FpgaSessionState state,
                      const QString& detail);
    void frameReceived(quint8 frameType, quint32 sequence, quint16 source,
                       quint16 message, quint64 timestamp, quint32 flags,
                       const QByteArray& payload);
    void rawFrameReceived(quint32 sequence, quint16 source, quint16 message,
                          const QByteArray& frameBytes);
    void sensorReadingReceived(const VaporView::FpgaSensor::Reading& reading);
    void waveformCompleted(const VaporView::FpgaWave::CompletedStream& stream);
    void ptbDataUpdated(const PtbData& data);
    void hmpDataUpdated(const HmpData& data);
    void lidarDataUpdated(const LidarData& data);
    void capabilitiesChanged(const VaporView::Ground::Devices::FpgaCapabilities& capabilities);
    void registerReadCompleted(quint32 sequence, quint32 address,
                               const QVector<quint32>& values);
    void commandCompleted(quint32 sequence, quint16 message, bool success,
                          quint32 status, const QByteArray& payload);
    void commandTimedOut(quint32 sequence, quint16 message);
    void errorOccurred(const QString& message);

private:
    struct QueuedCommand
    {
        quint32 sequence = 0;
        quint16 source = 0;
        quint16 message = 0;
        QByteArray bytes;
    };

    struct PendingCommand
    {
        QueuedCommand command;
        QElapsedTimer timer;
    };

    quint32 enqueue(quint16 source, quint16 message,
                    const std::vector<std::uint8_t>& bytes);
    void dispatchNext();
    void processFrame(const VaporView::FpgaVlp1::Frame& frame);
    void setState(FpgaSessionState state, const QString& detail);
    static bool parseCapabilities(const QByteArray& payload, FpgaCapabilities& result);
    static bool parseRegisterRead(const QByteArray& payload, FpgaRegisterReadResult& result);
    static quint32 readU32(const char *data);

    std::unique_ptr<FpgaUsbTransport> transport_;
    VaporView::FpgaVlp1::StreamParser parser_;
    VaporView::FpgaWave::Assembler waveformAssembler_;
    QTimer pollTimer_;
    QQueue<QueuedCommand> commandQueue_;
    std::optional<PendingCommand> pendingCommand_;
    FpgaCapabilities capabilities_;
    FpgaSessionState state_ = FpgaSessionState::Closed;
    QString stateDetail_;
    quint32 nextSequence_ = 1;
    int commandTimeoutMs_ = 10000;
};

}  // namespace VaporView::Ground::Devices

Q_DECLARE_METATYPE(VaporView::Ground::Devices::FpgaCapabilities)
Q_DECLARE_METATYPE(VaporView::Ground::Devices::FpgaSessionState)
Q_DECLARE_METATYPE(VaporView::FpgaSensor::Reading)
Q_DECLARE_METATYPE(VaporView::FpgaWave::CompletedStream)
