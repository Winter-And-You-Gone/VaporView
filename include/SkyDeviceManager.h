#ifndef VaporView_SKY_DEVICE_MANAGER_H_
#define VaporView_SKY_DEVICE_MANAGER_H_

#include "SkyConfig.h"
#include "TelemetryTypes.h"
#include "TcpWaveEncoding.h"
#include "data_collector.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <memory>

namespace VaporView
{

struct ApplyConfigResult
{
    bool success = true;
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

    EpsilonData latestEpsilon() const;
    PtbData latestPtb() const;
    HmpData latestHmp() const;
    LidarData latestLidar() const;
    QVector<float> latestRawWaveform() const;
    QVector<float> latestWaveform() const;
    WaveformFeature latestWaveformFeature() const;

signals:
    void deviceStatusChanged(SkyDeviceId id, DeviceStatusItem status);
    void epsilonDataUpdated(const EpsilonData& data);
    void ptbDataUpdated(const PtbData& data);
    void hmpDataUpdated(const HmpData& data);
    void lidarDataUpdated(const LidarData& data);
    void waveformUpdated(quint64 timestampUs, QVector<float> samples);
    void waveformFeatureUpdated(const WaveformFeature& feature);
    void logMessage(const QString& message);

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
    void publishWaveform(const QVector<float>& raw, const QVector<float>& harmonic);
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

    std::shared_ptr<EpsilonCollector> epsilon_;
    std::shared_ptr<PtbCollector> ptb_;
    std::shared_ptr<HmpCollector> hmp_;
    std::shared_ptr<LidarCollector> lidar_;

    QTcpSocket *wave_socket_ = nullptr;
    QByteArray wave_buffer_;
    TcpFloatEncoding wave_float_encoding_ = TcpFloatEncoding::Unknown;
    quint64 wave_frame_count_ = 0;
    quint64 feature_frame_count_ = 0;
    quint64 last_feature_compute_time_us_ = 0;

    EpsilonData latest_epsilon_;
    PtbData latest_ptb_;
    HmpData latest_hmp_;
    LidarData latest_lidar_;
    QVector<float> latest_raw_waveform_;
    QVector<float> latest_waveform_;
    WaveformFeature latest_feature_;
};

}  // namespace VaporView

#endif
