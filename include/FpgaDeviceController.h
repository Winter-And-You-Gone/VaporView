#pragma once

#include "FpgaControlConfig.h"
#include "FpgaDeviceSession.h"
#include "FpgaSensorAdapter.h"
#include "LogService.h"
#include "EpsilonRawSatellite.h"
#include "data_types.h"

#include <QMap>
#include <QJsonObject>
#include <QSet>
#include <QThread>
#include <deque>
#include <functional>
#include <optional>

namespace VaporView::Ground::Devices
{
class FpgaUartExchange;

// Lives in the USB worker thread. The page edits desired configuration; this
// controller owns hardware readback and serialized, verified operations.
class FpgaDeviceController final : public QObject
{
    Q_OBJECT
public:
    // Accessed only on the controller's owning worker thread.
    void setOperationLogContext(quint64 identity, const QString& correlationId, const QString& sessionId)
    { operationLogIdentity_ = identity; operationCorrelationId_ = correlationId; operationSessionId_ = sessionId; }
    void clearOperationLogContext(quint64 identity)
    { if (operationLogIdentity_ == identity) { operationLogIdentity_ = 0; operationCorrelationId_.clear(); operationSessionId_.clear(); } }
    enum class ConnectMode { RecoverEnabledModules, PreserveRunningModules };
    explicit FpgaDeviceController(std::unique_ptr<FpgaUsbTransport> transport = {}, QObject *parent = nullptr);
    ~FpgaDeviceController() override;
    bool connected() const { return session_ && session_->isOpen(); }
    bool ready() const { return ready_; }
    bool busy() const { return busy_ && !backgroundRefresh_; }
    bool temperatureConnected() const { return tec103Connected_; }
    const QMap<quint32, quint32>& hardwareValues() const { return registers_; }
    quint32 extensions() const { return session_?session_->capabilities().extensions:0; }
    QJsonObject extensionResult() const { return extensionResult_; }
    QJsonObject discoverDevices(const QString& backend = {}) const;

public slots:
    void connectDevice(const QString& locator, const QString& backend,
                       ConnectMode mode = ConnectMode::RecoverEnabledModules);
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
    void capabilitiesUpdated(quint32 extensions);
    void extensionCompleted(const QJsonObject& result);
    void transportConnectionChanged(bool connected);
    void connectionChanged(bool ready, bool busy, const QString& detail);
    void hardwareValuesChanged(const QMap<quint32, quint32>& values);
    void sensorConfigurationApplied(quint16 source, const QMap<quint32, quint32>& values, const QJsonObject& parameters);
    void managementUpdated(const QJsonObject& state);
    void observationEpochReceived(const QByteArray& encodedEpoch);
    void measurementUpdated(const VaporView::FpgaSensor::AdaptedMeasurements& measurements);
    void temperatureDataUpdated(const VaporView::TemperatureControllerData& data);
    void temperaturePortsChanged(const QJsonObject& ports);
    void temperatureConnectionChanged(bool connected, const QString& detail, bool identifying = false);
    void waveformUpdated(const VaporView::FpgaWave::CompletedStream& stream);
    void harmonicCycleReady(quint64 hostTimestampUs, const VaporView::FpgaWave::CompletedStream& stream);
    void logRecordGenerated(const VaporView::LogRecord& record);
    void rawFrame(quint64 hostTimestampUs, const QByteArray& wireBytes);
    void rawCommand(quint64 hostTimestampUs, const QByteArray& wireBytes);
    void rawUsbBytes(quint64 hostTimestampUs, const QByteArray& bytes);
    void snapshot(quint64 hostTimestampUs, const QJsonObject& document);
    void replayFinished(bool success, const QString& detail);

private:
    quint64 operationLogIdentity_ = 0;
    QString operationCorrelationId_;
    QString operationSessionId_;
    struct PendingConnectionLogContext { QString correlationId; QString sessionId; };
    QHash<quint16, PendingConnectionLogContext> pendingConnectionLogContexts_;
    enum class Kind { Management, Read, Write, Commit, Ping, Capabilities, Sensor, Wait, Delay, PrepareSensorConfig, FinishSensorConfig, CheckVersions, RecoverConnection, FinishRawStop, CheckTemperature, ConfirmConnection, FinishWatchdog, Callback, UartExchange };
    struct Step {
        Kind kind = Kind::Read;
        quint32 address = 0;
        QVector<quint32> values;
        quint16 count = 1, source = 1;
        quint32 mask = 0, expected = 0;
        QString label;
        std::function<void()> callback;
    };
    void installSession(std::unique_ptr<FpgaUsbTransport> transport);
    bool begin(const QString& label, bool background = false, bool notify = true);
    void advance();
    void completed(quint32 sequence, bool success, quint32 status, bool statusOnlyResponse);
    void fail(const QString& detail, bool operationScoped = true, bool preserveMotorOwner = false);
    void report(const QString& detail, LogLevel level = LogLevel::Warning, const char *file = VAPORVIEW_LOG_FILE, const char *function = VAPORVIEW_LOG_FUNCTION, int line = VAPORVIEW_LOG_LINE, bool operationScoped = true);
    void reportRawResult(bool enabled, bool succeeded, const QString& detail, const char *file = VAPORVIEW_LOG_FILE, const char *function = VAPORVIEW_LOG_FUNCTION, int line = VAPORVIEW_LOG_LINE);
    void trackConnection(quint16 source);
    void confirmConnection(quint16 source);
    void checkConnectionResults();
    void reportConnection(quint16 source, const QString& result, const QString& reasonCode, const QString& detail, const char *file = VAPORVIEW_LOG_FILE, const char *function = VAPORVIEW_LOG_FUNCTION, int line = VAPORVIEW_LOG_LINE);
    void read(quint32 address, quint16 count = 1);
    void probe(quint32 address, quint32 api);
    void write(quint32 address, quint32 value);
    void verify(quint32 address, quint32 value);
    void waitFor(quint32 address, quint32 mask, quint32 expected);
    void commit(quint16 source, quint32 base);
    void readAll();
    void readExtensions();
    void callback(std::function<void()> action);
    void executeExtension(const QJsonObject& request, bool polling = false);
    void deferExtensionOperation(const QJsonObject& request,quint64 generation,qint64 deadline);
    void configureTec103(bool enable, quint16 source);
    void publishTec103Ports();
    void pollTec103();
    void submitTec103Exchange(QJsonObject request, std::function<void(const QJsonObject&)> reply,
                              quint64 generation, quint64 revision, qint64 deadline);
    void stopTec103();
    void startSensorConfiguration(const QJsonObject& request, bool connectAfter);
    void prepareSensorConfiguration(quint16 source, quint32 baud, quint32 pollMs, bool connectAfter);
    bool appendSensorEnable(quint16 source, bool enable);
    bool enableUpload();
    void stopAndDrain();
    void resetAcquisition();
    void recoverConnection();
    void publishManagementEvent(const QJsonObject& event);
    void logManagementEvent(const QJsonObject& event, bool afterStartup = false,
                            const char *file = VAPORVIEW_LOG_FILE,
                            const char *function = VAPORVIEW_LOG_FUNCTION,
                            int line = VAPORVIEW_LOG_LINE);
    void checkStartupWarnings();
    void acceptReading(const VaporView::FpgaSensor::Reading& reading);
    void flushPresentation();
    bool versionsMatch(QString *detail) const;
    static quint64 nowUs();

    FpgaDeviceSession *session_ = nullptr;
    FpgaUartExchange *uartExchange_ = nullptr;
    std::array<QThread,2> tec103WorkerThreads_;
    std::array<QObject*,2> tec103Workers_{};
    std::array<bool,2> tec103PollPending_{};
    void pollTec103Port(int i);
    QTimer tec103Timer_;
    TemperatureControllerData tec103Data_;
    std::array<TemperatureControllerData,2> tec103PortData_;
    std::array<bool,2> tec103PortEnabled_{}, tec103PortConnected_{}, tec103PortIdentifying_{};
    std::array<bool,2> tec103Identified_{};
    std::array<bool,2> tec103Online_{};
    std::array<bool,2> tec103Valid_{};
    std::array<quint64,2> tec103Revision_{};
    quint64 tec103Generation_ = 0, tec103Ticket_ = 0;
    bool tec103Enabled_ = false, tec103Connected_ = false;
    QString tec103Detail_;
    bool tec103PollExchange_ = false;
    FpgaControlConfig configuration_;
    FpgaSensor::FpgaSensorAdapter adapter_;
    Ppk::RawSatelliteAssembler observations_;
    QJsonObject management_, pendingSensorParameters_;
    QJsonObject extensionRequest_, extensionResult_;
    QVector<QJsonObject> pendingManagement_;
    std::optional<quint64> connectionStartTick_;
    QElapsedTimer connectionClock_;
    QMap<quint16,qint64> startupWarningDeadlines_;
    QMap<quint16,QJsonObject> startupWarnings_;
    QHash<quint16, PendingConnectionLogContext> startupWarningLogContexts_;
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
    bool motorOwnerEnabled_ = false;
    bool interruptingForMotorStop_ = false;
    QSet<quint32> motorSafetySequences_;
    qint16 temperatureTarget_ = 0;
    bool injected_ = false, ready_ = false, busy_ = false, replaying_ = false;
    bool drainWaveforms_ = false, backgroundRefresh_ = false, recoveringConnection_ = false, connectionRecoveryFailed_ = false;
    QString operation_, detail_, installedBackend_;
    std::optional<bool> rawOperation_;
};
}

Q_DECLARE_METATYPE(VaporView::FpgaSensor::AdaptedMeasurements)
