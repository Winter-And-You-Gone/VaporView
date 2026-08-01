#ifndef VaporView_SKY_DEVICE_MANAGER_H_
#define VaporView_SKY_DEVICE_MANAGER_H_

#include "LogRecord.h"
#include "SkyConfig.h"
#include "TelemetryTypes.h"
#include "TcpWaveEncoding.h"
#include "data_collector.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

#include "shared/concurrency/BoundedByteQueue.h"

namespace VaporView
{

struct ApplyConfigResult
{
    bool success = true;
    CommandErrorCode error_code = CommandErrorCode::Ok;
    QJsonObject json;
};

class SkyDeviceManager : public QObject
{
    Q_OBJECT

public:
    explicit SkyDeviceManager(QObject *parent = nullptr);
    ~SkyDeviceManager() override;

    void setSimulateData(bool simulate);
    void loadConfig(const SkyConfig& config);
    const SkyConfig& config() const;

    bool connectDevice(SkyDeviceId id, CommandErrorCode *errorCode = nullptr);
    bool disconnectDevice(SkyDeviceId id, CommandErrorCode *errorCode = nullptr);
    bool reconnectDevice(SkyDeviceId id, CommandErrorCode *errorCode = nullptr);
    void connectAll();
    void disconnectAll();
    void reconnectAll();

    DeviceStatusItem status(SkyDeviceId id) const;
    QVector<DeviceStatusItem> allStatuses() const;
    ApplyConfigResult applyConfig(const SkyConfig& newConfig);
    bool setPeakSearchRange(quint32 startIndex, quint32 endIndex, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureTarget(quint8 channel, double celsius, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureOutputEnabled(quint8 channel, bool enabled, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureOutputMode(quint8 channel, quint16 mode, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureMaxOutputPercent(quint8 channel, quint16 percent, CommandErrorCode *errorCode = nullptr);
    bool setTemperaturePid(quint8 channel, quint32 kp, quint32 ki, quint32 kd, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureAutoPid(quint8 channel, quint16 mode, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureOvertempUpper(quint8 channel, double celsius, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureOvertempLower(quint8 channel, double celsius, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureSlope(quint8 channel, double celsiusPerSecond, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureStartupDelay(quint8 channel, quint16 seconds, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureControllerMode(quint16 mode, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureDeviceAddress(quint16 address, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureRs485Baud(quint16 baudIndex, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureOvertempOutputMode(quint16 mode, CommandErrorCode *errorCode = nullptr);
    bool setTemperatureSensorConfig(const TemperatureControllerCommand& command, CommandErrorCode *errorCode = nullptr);
    bool restoreTemperatureFactoryDefaults(CommandErrorCode *errorCode = nullptr);

    EpsilonData latestEpsilon() const;
    PtbData latestPtb() const;
    HmpData latestHmp() const;
    LidarData latestLidar() const;
    TemperatureControllerData latestTemperatureController() const;
    QVector<float> latestRawWaveform() const;
    QVector<float> latestWaveform() const;
    WaveformFeature latestWaveformFeature() const;
    double waveTcpActualRateHz() const;

signals:
    void deviceStatusChanged(SkyDeviceId id, DeviceStatusItem status);
    void epsilonDataUpdated(const EpsilonData& data);
    void ptbDataUpdated(const PtbData& data);
    void hmpDataUpdated(const HmpData& data);
    void lidarDataUpdated(const LidarData& data);
    void temperatureControllerDataUpdated(const TemperatureControllerData& data);
    void waveformUpdated(quint64 timestampUs, QVector<float> samples);
    void waveformFeatureUpdated(const WaveformFeature& feature);
    void epsilonRawFrameReceived(quint64 timestampUs, quint8 packetId, quint8 serialNumber, QByteArray frame);
    void ptbRawResponseReceived(quint64 timestampUs, QByteArray response);
    void hmpRawResponseReceived(quint64 timestampUs, QByteArray response);
    void lidarRawFrameReceived(quint64 timestampUs, quint16 protocol, QByteArray frame);
    void tcpRawWaveFrameReceived(quint64 timestampUs,
                                 QByteArray rawPayload,
                                 QByteArray harmonicPayload,
                                 TcpFloatEncoding floatEncoding);
    void logMessage(const QString& message);
    void logRecord(const VaporView::LogRecord& record);

private slots:
    void generateSimulatedData();
    void onWaveTcpConnected();
    void onWaveTcpDisconnected();
    void onWaveTcpReadyRead();
    void onWaveTcpError();

private:
    void initializeStatuses();
    void setState(SkyDeviceId id, DeviceState state, quint16 errorCode = 0);
    DeviceStatusItem& mutableStatus(SkyDeviceId id);
    const SerialDeviceConfig& serialConfigFor(SkyDeviceId id) const;
    bool connectSerialCollector(SkyDeviceId id, const SerialDeviceConfig& config, CommandErrorCode *errorCode);
    bool connectWaveTcp(CommandErrorCode *errorCode);
    void disconnectWaveTcp();
    void processWaveTcpBuffer();
    void publishDeviceLog(LogLevel level,
                          const QString& category,
                          const QString& event,
                          const QString& message,
                          QVariantMap fields = QVariantMap());
    void publishWaveform(const QVector<float>& raw, const QVector<float>& harmonic);
    void handleEpsilonData(const EpsilonData& data);
    void handlePtbData(const PtbData& data);
    void handleHmpData(const HmpData& data);
    void handleLidarData(const LidarData& data);
    void handleTemperatureControllerData(const TemperatureControllerData& data);
    struct PendingRawEvent
    {
        SkyDeviceId deviceId = SkyDeviceId::All;
        const void *collectorIdentity = nullptr;
        quint64 timestampUs = 0;
        quint16 metadata = 0;
        quint8 serialNumber = 0;
        QByteArray payload;
    };
    void enqueueRawEvent(PendingRawEvent event);
    void drainRawEvents();
    void scheduleRawEventDrain();
    void recordWaveTcpFrameTime(quint64 timestampUs);
    void invalidateDeviceData(SkyDeviceId id);

    SkyConfig config_ = SkyConfig::defaults();
    bool simulate_data_ = false;
    QTimer simulate_timer_;
    qreal simulate_phase_ = 0.0;

    DeviceStatusItem epsilon_status_;
    DeviceStatusItem ptb_status_;
    DeviceStatusItem hmp_status_;
    DeviceStatusItem lidar_status_;
    DeviceStatusItem wave_tcp_status_;
    DeviceStatusItem temperature_controller_status_;

    std::shared_ptr<EpsilonCollector> epsilon_;
    std::shared_ptr<PtbCollector> ptb_;
    std::shared_ptr<HmpCollector> hmp_;
    std::shared_ptr<LidarCollector> lidar_;
    std::shared_ptr<TemperatureControllerCollector> temperature_controller_;

    QTcpSocket *wave_socket_ = nullptr;
    QByteArray wave_buffer_;
    TcpFloatEncoding wave_float_encoding_ = TcpFloatEncoding::Unknown;
    quint64 wave_frame_count_ = 0;
    quint64 feature_frame_count_ = 0;
    quint64 last_feature_compute_time_us_ = 0;
    QVector<quint64> wave_frame_time_samples_us_;

    EpsilonData latest_epsilon_;
    PtbData latest_ptb_;
    HmpData latest_hmp_;
    LidarData latest_lidar_;
    TemperatureControllerData latest_temperature_controller_;
    QVector<float> latest_raw_waveform_;
    QVector<float> latest_waveform_;
    WaveformFeature latest_feature_;

    BoundedByteQueue<PendingRawEvent> pending_raw_events_{4ULL * 1024ULL * 1024ULL, 2048};
    std::atomic<bool> raw_event_drain_scheduled_{false};
    quint64 raw_event_drops_reported_ = 0;
};

}  // namespace VaporView

#endif
