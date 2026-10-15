#pragma once

#include "FpgaControlConfig.h"
#include "FpgaDeviceSession.h"
#include "FpgaSensorAdapter.h"
#include "LogRecord.h"
#include "EpsilonRawSatellite.h"

#include <QMap>
#include <QJsonObject>
#include <QSet>
#include <deque>
#include <functional>
#include <optional>

namespace VaporView::Ground::Devices
{

// Lives in the USB worker thread. The page edits desired configuration; this
// controller owns hardware readback and serialized, verified operations.
class FpgaDeviceController final : public QObject
{
    Q_OBJECT
public:
    explicit FpgaDeviceController(std::unique_ptr<FpgaUsbTransport> transport = {}, QObject *parent = nullptr);
    ~FpgaDeviceController() override;
    bool connected() const { return session_ && session_->isOpen(); }
    bool ready() const { return ready_; }
    bool busy() const { return busy_ && !backgroundRefresh_; }
    const QMap<quint32, quint32>& hardwareValues() const { return registers_; }
    QJsonObject discoverDevices(const QString& backend = {}) const;

public slots:
    void connectDevice(const QString& locator, const QString& backend);
    void disconnectDevice();
    void applyConfiguration(const FpgaControlConfig& config, const QString& section = QStringLiteral("all"));
    void setConfiguration(const FpgaControlConfig& config);
    void refresh();
    void executeOperation(const QJsonObject& request);
    void setAcquisition(bool enable);
    void setWaveform(int channel, bool enable);
    void setDac(int channel, bool enable);
    void setSensorEnabled(quint16 source, bool enable);
    void connectSensor(const QJsonObject& request);
    void configureSensor(const QJsonObject& request);
    void setRawEnabled(bool enable);
    void setTemperature(double celsius);
    void replaySession(const QString& directory);
    void exportSession(const QString& directory, const QString& output);
    void snapshotConfiguration();

signals:
    void transportConnectionChanged(bool connected);
    void connectionChanged(bool ready, bool busy, const QString& detail);
    void hardwareValuesChanged(const QMap<quint32, quint32>& values);
    void sensorConfigurationApplied(quint16 source, const QMap<quint32, quint32>& values, const QJsonObject& parameters);
    void managementUpdated(const QJsonObject& state);
    void observationEpochReceived(const QByteArray& encodedEpoch);
    void measurementUpdated(const VaporView::FpgaSensor::AdaptedMeasurements& measurements);
    void waveformUpdated(const VaporView::FpgaWave::CompletedStream& stream);
    void logRecordGenerated(const VaporView::LogRecord& record);
    void rawFrame(quint64 hostTimestampUs, const QByteArray& wireBytes);
    void rawCommand(quint64 hostTimestampUs, const QByteArray& wireBytes);
    void rawUsbBytes(quint64 hostTimestampUs, const QByteArray& bytes);
    void snapshot(quint64 hostTimestampUs, const QJsonObject& document);
    void replayFinished(bool success, const QString& detail);

private:
    enum class Kind { Management, Read, Write, Commit, Ping, Capabilities, Sensor, Wait, Delay, PrepareSensorConfig, FinishSensorConfig, CheckVersions, RecoverConnection, FinishRawStop, CheckTemperature, ConfirmConnection, FinishWatchdog };
    struct Step {
        Kind kind = Kind::Read;
        quint32 address = 0;
        QVector<quint32> values;
        quint16 count = 1, source = 1;
        quint32 mask = 0, expected = 0;
        QString label;
    };
    void installSession(std::unique_ptr<FpgaUsbTransport> transport);
    bool begin(const QString& label, bool background = false);
    void advance();
    void completed(quint32 sequence, bool success, quint32 status);
    void fail(const QString& detail);
    void report(const QString& detail, LogLevel level = LogLevel::Warning);
    void reportRawResult(bool enabled, bool succeeded, const QString& detail);
    void trackConnection(quint16 source);
    void confirmConnection(quint16 source);
    void checkConnectionResults();
    void reportConnection(quint16 source, const QString& result, const QString& reasonCode, const QString& detail);
    void read(quint32 address, quint16 count = 1);
    void write(quint32 address, quint32 value);
    void verify(quint32 address, quint32 value);
    void waitFor(quint32 address, quint32 mask, quint32 expected);
    void commit(quint16 source, quint32 base);
    void readAll();
    void startSensorConfiguration(const QJsonObject& request, bool connectAfter);
    void prepareSensorConfiguration(quint16 source, quint32 baud, quint32 pollMs, bool connectAfter);
    bool appendSensorEnable(quint16 source, bool enable);
    bool enableUpload();
    void stopAndDrain();
    void resetAcquisition();
    void recoverConnection();
    void acceptReading(const VaporView::FpgaSensor::Reading& reading);
    void flushPresentation();
    bool versionsMatch(QString *detail) const;
    static quint64 nowUs();

    FpgaDeviceSession *session_ = nullptr;
    FpgaControlConfig configuration_;
    FpgaSensor::FpgaSensorAdapter adapter_;
    Ppk::RawSatelliteAssembler observations_;
    QJsonObject management_, pendingSensorParameters_;
    std::deque<Step> steps_;
    std::optional<Step> current_;
    QMap<quint32, quint32> registers_;
    QMap<quint16, FpgaSensor::AdaptedMeasurements> latestReadings_;
    QMap<quint16, FpgaWave::CompletedStream> latestWaves_;
    QMap<quint16, qint64> lastReadingMs_;
    QSet<quint16> dirtyReadings_, expiredReadings_;
    QMap<quint16, qint64> pendingConnections_;
    QSet<quint16> operationConnections_;
    QTimer presentationTimer_, statusTimer_, watchdogTimer_;
    QElapsedTimer waitTimer_;
    quint32 pendingSequence_ = 0;
    quint64 operationGeneration_ = 0;
    quint32 temperatureBaseline_ = 0;
    quint32 hmpCompensationBaseline_ = 0;
    qint16 temperatureTarget_ = 0;
    bool injected_ = false, ready_ = false, busy_ = false, replaying_ = false;
    bool drainWaveforms_ = false, backgroundRefresh_ = false, recoveringConnection_ = false, connectionRecoveryFailed_ = false;
    QString operation_, detail_;
    std::optional<bool> rawOperation_;
};
}

Q_DECLARE_METATYPE(VaporView::FpgaSensor::AdaptedMeasurements)
