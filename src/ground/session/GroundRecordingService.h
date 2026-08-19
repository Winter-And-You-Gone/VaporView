#pragma once

#include "TcpWaveEncoding.h"
#include "data_types.h"

#include <QByteArray>
#include <QString>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>

namespace VaporView::Ground::Session
{

struct GroundSensorSnapshot
{
    EpsilonData epsilon;
    PtbData ptb;
    HmpData hmp;
    LidarData lidar;
    bool hasEpsilon = false;
    bool hasPtb = false;
    bool hasHmp = false;
    bool hasLidar = false;
};

struct GroundRecordingSerialConfig
{
    QString port;
    QString baud;
    QString rateHz;
    QString slaveAddress;
};

struct GroundRecordingDeviceConfig
{
    QString waveformHost = QStringLiteral("127.0.0.1");
    int waveformPort = 8888;
    GroundRecordingSerialConfig epsilon;
    GroundRecordingSerialConfig ptb;
    GroundRecordingSerialConfig hmp;
    GroundRecordingSerialConfig lidar;
    GroundRecordingSerialConfig laserTemperatureController;
    GroundRecordingSerialConfig systemTemperatureController;
};

struct GroundRecordingOptions
{
    QString baseDirectory;
    int exportRateHz = 20;
    GroundRecordingDeviceConfig deviceConfig;
};

struct GroundRecordingStatus
{
    bool sessionOpen = false;
    bool active = false;
    bool paused = false;
    QString sessionName;
    QString sessionDirectory;
    qint64 sensorRows = 0;
    qint64 waveformFrames = 0;
    quint64 rawNavigationRecords = 0;
    quint64 rawPressureRecords = 0;
    quint64 rawTemperatureHumidityRecords = 0;
    quint64 rawDistanceRecords = 0;
    quint64 rawWaveformRecords = 0;
    quint64 rawLaserTemperatureControllerRecords = 0;
    quint64 rawSystemTemperatureControllerRecords = 0;
};

struct GroundRecordingStopSummary
{
    bool hadOpenSession = false;
    QString sessionDirectory;
    qint64 sensorRows = 0;
    qint64 waveformFrames = 0;
};

enum class GroundRecordingStartError
{
    None,
    CreateSessionLayout,
    OpenSessionFiles,
    WriteSessionMetadata
};

enum class GroundRecordingWarning
{
    RawFormatDocumentCopyFailed,
    DeviceConfigSnapshotFailed,
    MetadataUpdateFailed,
    TcpQueueBacklog,
    TcpQueueFull,
    TcpFramesDropped,
    DeviceRawQueueBacklog,
    DeviceRawQueueFull,
    DeviceRawFramesDropped
};

class GroundRecordingService final
{
public:
    using SensorSnapshotProvider = std::function<GroundSensorSnapshot()>;
    using StatusCallback = std::function<void()>;
    using WarningCallback = std::function<void(GroundRecordingWarning, quint64)>;

    GroundRecordingService();
    ~GroundRecordingService();

    GroundRecordingService(const GroundRecordingService&) = delete;
    GroundRecordingService& operator=(const GroundRecordingService&) = delete;

    void setSensorSnapshotProvider(SensorSnapshotProvider provider);
    void setStatusCallback(StatusCallback callback);
    void setWarningCallback(WarningCallback callback);

    bool start(const GroundRecordingOptions& options,
               GroundRecordingStartError *startError = nullptr,
               QString *errorMessage = nullptr);
    bool pause();
    GroundRecordingStopSummary stop();

    GroundRecordingStatus status() const;
    bool isSessionOpen() const;
    bool isActive() const;
    bool isPaused() const;

    bool recordRawEpsilonFrame(quint64 hostTimestampUs,
                               quint8 packetId,
                               quint8 serialNumber,
                               const void *data,
                               size_t size);
    bool recordRawPtbResponse(quint64 hostTimestampUs, const void *data, size_t size);
    bool recordRawHmpResponse(quint64 hostTimestampUs, const void *data, size_t size);
    bool recordRawLidarFrame(quint64 hostTimestampUs,
                             quint16 protocol,
                             const void *data,
                             size_t size);
    bool recordRawLaserTemperatureControllerResponse(quint64 hostTimestampUs,
                                                     quint16 recordType,
                                                     const void *data,
                                                     size_t size);
    bool recordRawSystemTemperatureControllerResponse(quint64 hostTimestampUs,
                                                      quint16 recordType,
                                                      const void *data,
                                                      size_t size);
    bool recordTcpWaveFrame(quint64 hostTimestampUs,
                            const QByteArray& rawSignalPayload,
                            const QByteArray& harmonicPayload,
                            TcpFloatEncoding floatEncoding);

    bool appendEvent(const QString& level, const QString& message);
    bool appendError(const QString& message);

    quint64 steadyToEpochUs(const std::chrono::steady_clock::time_point& timePoint) const;
    static quint64 currentTimestampUs();
    static QString defaultRecordingDirectory();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace VaporView::Ground::Session
