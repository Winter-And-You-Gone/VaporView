#ifndef VAPORVIEW_BACKENDS_H_
#define VAPORVIEW_BACKENDS_H_

#include "RtkStreamService.h"
#include "TcpWaveEncoding.h"
#include "data_collector.h"
#include "data_types.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDate>
#include <QElapsedTimer>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVector>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

class AppBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(bool english READ english WRITE setEnglish NOTIFY languageChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)
    Q_PROPERTY(int fontScale READ fontScale WRITE setFontScale NOTIFY fontScaleChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(int uiRadius READ uiRadius WRITE setUiRadius NOTIFY uiStyleChanged)
    Q_PROPERTY(int uiControlHeight READ uiControlHeight WRITE setUiControlHeight NOTIFY uiStyleChanged)
    Q_PROPERTY(int uiButtonHeight READ uiButtonHeight WRITE setUiButtonHeight NOTIFY uiStyleChanged)
    Q_PROPERTY(int uiCardHeaderHeight READ uiCardHeaderHeight WRITE setUiCardHeaderHeight NOTIFY uiStyleChanged)
    Q_PROPERTY(int uiCardPadding READ uiCardPadding WRITE setUiCardPadding NOTIFY uiStyleChanged)
    Q_PROPERTY(int uiControlPaddingX READ uiControlPaddingX WRITE setUiControlPaddingX NOTIFY uiStyleChanged)
    Q_PROPERTY(int uiSpacing READ uiSpacing WRITE setUiSpacing NOTIFY uiStyleChanged)
    Q_PROPERTY(int uiBorderWidth READ uiBorderWidth WRITE setUiBorderWidth NOTIFY uiStyleChanged)
    Q_PROPERTY(int uiSmallFontSize READ uiSmallFontSize WRITE setUiSmallFontSize NOTIFY uiStyleChanged)
    Q_PROPERTY(int uiBodyFontSize READ uiBodyFontSize WRITE setUiBodyFontSize NOTIFY uiStyleChanged)
    Q_PROPERTY(int uiValueFontSize READ uiValueFontSize WRITE setUiValueFontSize NOTIFY uiStyleChanged)
    Q_PROPERTY(bool uiCompactMode READ uiCompactMode WRITE setUiCompactMode NOTIFY uiStyleChanged)
    Q_PROPERTY(bool uiShowDebugOutlines READ uiShowDebugOutlines WRITE setUiShowDebugOutlines NOTIFY uiStyleChanged)

public:
    explicit AppBackend(QObject *parent = nullptr);

    QString language() const;
    bool english() const;
    bool dark() const;
    int fontScale() const;
    QString version() const;
    int uiRadius() const;
    int uiControlHeight() const;
    int uiButtonHeight() const;
    int uiCardHeaderHeight() const;
    int uiCardPadding() const;
    int uiControlPaddingX() const;
    int uiSpacing() const;
    int uiBorderWidth() const;
    int uiSmallFontSize() const;
    int uiBodyFontSize() const;
    int uiValueFontSize() const;
    bool uiCompactMode() const;
    bool uiShowDebugOutlines() const;

    Q_INVOKABLE QString t(const QString& key) const;
    Q_INVOKABLE void toggleLanguage();
    Q_INVOKABLE void toggleTheme();
    Q_INVOKABLE void notify(const QString& level, const QString& message);
    Q_INVOKABLE QString loadIconLibrary() const;
    Q_INVOKABLE void saveIconLibrary(const QString& library);
    Q_INVOKABLE void resetUiStyle();
    Q_INVOKABLE void saveUiStyle();
    Q_INVOKABLE void loadUiStyle();

public slots:
    void setLanguage(const QString& language);
    void setEnglish(bool english);
    void setDark(bool dark);
    void setFontScale(int percent);
    void setUiRadius(int value);
    void setUiControlHeight(int value);
    void setUiButtonHeight(int value);
    void setUiCardHeaderHeight(int value);
    void setUiCardPadding(int value);
    void setUiControlPaddingX(int value);
    void setUiSpacing(int value);
    void setUiBorderWidth(int value);
    void setUiSmallFontSize(int value);
    void setUiBodyFontSize(int value);
    void setUiValueFontSize(int value);
    void setUiCompactMode(bool value);
    void setUiShowDebugOutlines(bool value);

signals:
    void languageChanged();
    void darkChanged();
    void fontScaleChanged();
    void uiStyleChanged();
    void notificationRequested(const QString& level, const QString& message);

private:
    QString language_;
    bool dark_;
    int font_scale_;
    int ui_radius_ = 7;
    int ui_control_height_ = 34;
    int ui_button_height_ = 30;
    int ui_card_header_height_ = 32;
    int ui_card_padding_ = 12;
    int ui_control_padding_x_ = 10;
    int ui_spacing_ = 8;
    int ui_border_width_ = 1;
    int ui_small_font_size_ = 10;
    int ui_body_font_size_ = 11;
    int ui_value_font_size_ = 12;
    bool ui_compact_mode_ = false;
    bool ui_show_debug_outlines_ = false;
};

class DeviceModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        NameKeyRole,
        DisplayNameRole,
        PortRole,
        BaudRateRole,
        SampleRateRole,
        ConnectedRole,
        OnlineRole,
        ActualRateRole,
        StatusTextRole,
        KindRole,
    };

    struct Device
    {
        QString id;
        QString name_key;
        QString display_name;
        QString port;
        int baud_rate = 0;
        int sample_rate = 0;
        bool connected = false;
        bool online = false;
        double actual_rate = 0.0;
        QString status_text;
        QString kind;
    };

    explicit DeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantMap get(int row) const;
    int indexOf(const QString& id) const;
    Device deviceAt(const QString& id) const;
    QList<Device> devices() const;
    void setDevice(const QString& id, const Device& device);
    void setDeviceValue(const QString& id, int role, const QVariant& value);

private:
    QVector<Device> devices_;
};

class TcpWaveReceiverWorker : public QObject
{
    Q_OBJECT

public:
    explicit TcpWaveReceiverWorker(QObject *parent = nullptr);
    ~TcpWaveReceiverWorker() override;

public slots:
    void connectToHost(const QString& host, int port);
    void disconnectFromHost();
    void setRawFrameForwardingEnabled(bool enabled);
    void setPeakSearchWindow(int startIndex, int endIndex);
    void stop();

signals:
    void connected();
    void disconnected();
    void socketError(const QString& message);
    void displayFrameDecoded(
        QVector<float> rawSamples,
        QVector<float> harmonicSamples);
    void frameMetricsDecoded(
        quint64 timestampUs,
        float peakValue,
        double frameRate);
    void rawFrameDecoded(
        quint64 timestampUs,
        QByteArray rawSignalPayload,
        QByteArray harmonicPayload,
        VaporView::TcpFloatEncoding floatEncoding);

private slots:
    void onReadyRead();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError();

private:
    enum class ReadState
    {
        RawHeader,
        RawPayload,
        HarmonicHeader,
        HarmonicPayload,
    };

    enum class HeaderByteOrder
    {
        Unknown,
        LittleEndian,
        BigEndian,
    };

    void resetStreamState();
    void processBuffer();
    bool tryConsumeHeader();
    bool tryConsumePayload(QVector<float>& output, QByteArray *rawPayload);
    bool isValidPayloadSize(qint32 candidate) const;
    qint32 decodeHeaderValue(const char *raw, HeaderByteOrder order) const;
    double updateFrameRate(qint64 nowMs);

    QTcpSocket *socket_;
    QByteArray buffer_;
    QByteArray pending_raw_payload_;
    QVector<float> pending_raw_samples_;
    ReadState read_state_;
    HeaderByteOrder header_byte_order_;
    VaporView::TcpFloatEncoding float_encoding_;
    int expected_payload_size_;
    QVector<qint64> frame_arrivals_ms_;
    qint64 last_display_emit_ms_;
    bool raw_frame_forwarding_enabled_;
    int peak_search_start_index_;
    int peak_search_end_index_;
};

class DeviceBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(DeviceModel* devices READ devices CONSTANT)
    Q_PROPERTY(QStringList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(QStringList logLines READ logLines NOTIFY logLinesChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionStateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool connectionInProgress READ connectionInProgress NOTIFY busyChanged)
    Q_PROPERTY(bool autoDetectInProgress READ autoDetectInProgress NOTIFY busyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(int progressValue READ progressValue NOTIFY progressChanged)
    Q_PROPERTY(int progressMaximum READ progressMaximum NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap coordinateData READ coordinateData NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap environmentData READ environmentData NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap detailedData READ detailedData NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap systemData READ systemData NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap allDeviceFields READ allDeviceFields NOTIFY dataChanged)

public:
    explicit DeviceBackend(QObject *parent = nullptr);
    ~DeviceBackend() override;

    DeviceModel *devices();
    QStringList ports() const;
    QStringList logLines() const;
    bool connected() const;
    bool busy() const;
    bool connectionInProgress() const;
    bool autoDetectInProgress() const;
    QString statusText() const;
    int progressValue() const;
    int progressMaximum() const;
    QVariantMap coordinateData() const;
    QVariantMap environmentData() const;
    QVariantMap detailedData() const;
    QVariantMap systemData() const;
    QVariantMap allDeviceFields() const;

    VaporView::EpsilonData epsilonData() const;
    VaporView::PtbData ptbData() const;
    VaporView::HmpData hmpData() const;
    VaporView::LidarData lidarData() const;
    double collectorActualRate(const QString& id) const;
    bool anySerialCollectorRunning() const;
    Q_INVOKABLE QString selectedPort(const QString& id) const;
    int selectedBaud(const QString& id) const;
    int selectedSampleRate(const QString& id) const;

    Q_INVOKABLE QVariantMap device(int row) const;
    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE void autoDetectPortsOrCancel();
    Q_INVOKABLE void connectDevices();
    Q_INVOKABLE void connectDevice(const QString& id);
    Q_INVOKABLE void disconnectDevices();
    Q_INVOKABLE void disconnectDevice(const QString& id);
    Q_INVOKABLE void cancelConnect();
    Q_INVOKABLE void clearLog();
    Q_INVOKABLE void appendLogLine(const QString& message, const QString& level = QStringLiteral("info"));
    Q_INVOKABLE void updateDevicePort(const QString& id, const QString& port);
    Q_INVOKABLE void updateDeviceBaud(const QString& id, int baud);
    Q_INVOKABLE void updateDeviceSampleRate(const QString& id, int hz);
    Q_INVOKABLE void setWaveformDeviceState(bool connected, const QString& endpoint, double hz);
    Q_INVOKABLE void reconfigureEpsilonOutput();
    Q_INVOKABLE void configureEpsilonRtcmPort(const QString& forwardPort, int forwardBaud);
    Q_INVOKABLE void saveEpsilonPacketRates(const QVariantMap& packetRates, bool customEnabled);
    Q_INVOKABLE bool applyEpsilonMainAntennaLeverArm(double xM, double yM, double zM);
    Q_INVOKABLE QStringList supportedBaudRates() const;
    Q_INVOKABLE QStringList supportedRates(const QString& id) const;

public slots:
    void setEnglish(bool english);

signals:
    void portsChanged();
    void logLinesChanged();
    void connectionStateChanged();
    void busyChanged();
    void statusTextChanged();
    void progressChanged();
    void dataChanged();
    void notificationRequested(const QString& level, const QString& message);
    void epsilonRawFrame(quint64 hostTimestampUs, int packetId, int serialNumber, QByteArray payload);
    void ptbRawFrame(quint64 hostTimestampUs, QByteArray payload);
    void hmpRawFrame(quint64 hostTimestampUs, QByteArray payload);
    void lidarRawFrame(quint64 hostTimestampUs, int protocol, QByteArray payload);

private:
    struct CollectorSnapshot
    {
        std::shared_ptr<VaporView::EpsilonCollector> epsilon;
        std::shared_ptr<VaporView::PtbCollector> ptb;
        std::shared_ptr<VaporView::HmpCollector> hmp;
        std::shared_ptr<VaporView::LidarCollector> lidar;
    };

    CollectorSnapshot snapshotCollectors() const;
    void setCollectors(CollectorSnapshot collectors);
    void stopAllCollectors();
    bool stopCollector(const QString& id);
    void log(const QString& message, const QString& level = QStringLiteral("info"));
    void setBusyState(bool connectionBusy, bool detectBusy, bool reconfigureBusy);
    void setStatusText(const QString& text);
    void setProgress(int value, int maximum);
    bool shouldAbort() const;
    int parseRateForDevice(const QString& id, int hz) const;
    QString selectPlaceholder() const;
    void updateDeviceRates();
    void saveDeviceSettings() const;
    void loadDeviceSettings();
    std::map<uint8_t, int> effectiveEpsilonPacketRates(int baseRateHz, bool *usingCustom = nullptr) const;

    static QVariantList makeEpsilonFields(const VaporView::EpsilonData& e);
    static QVariantList makePtbFields(const VaporView::PtbData& p);
    static QVariantList makeHmpFields(const VaporView::HmpData& h);
    static QVariantList makeLidarFields(const VaporView::LidarData& l);
    static QString formatDouble(double v, int precision = 3);
    static QString formatBool(bool v);
    static QString formatStdString(const std::string& s);
    static QString formatTimestamp(const std::chrono::steady_clock::time_point& tp);

    DeviceModel devices_;
    QStringList ports_;
    QStringList log_lines_;
    bool is_english_;
    bool connected_;
    bool connection_in_progress_;
    bool auto_detect_in_progress_;
    bool epsilon_reconfigure_in_progress_;
    QString status_text_;
    int progress_value_;
    int progress_maximum_;
    std::atomic<bool> cancel_requested_;
    mutable std::mutex collector_mutex_;
    CollectorSnapshot collectors_;
    mutable QMutex data_mutex_;
    VaporView::EpsilonData current_epsilon_;
    VaporView::PtbData current_ptb_;
    VaporView::HmpData current_hmp_;
    VaporView::LidarData current_lidar_;
    QTimer refresh_timer_;
    std::thread connection_thread_;
    std::thread detect_thread_;
    std::thread reconfigure_thread_;
};

class WaveformBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY endpointChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY endpointChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY frameRateChanged)
    Q_PROPERTY(QVariantList rawSamples READ rawSamples NOTIFY samplesChanged)
    Q_PROPERTY(QVariantList harmonicSamples READ harmonicSamples NOTIFY samplesChanged)
    Q_PROPERTY(QVariantList peakSamples READ peakSamples NOTIFY peakSamplesChanged)
    Q_PROPERTY(int rawSampleCount READ rawSampleCount NOTIFY samplesChanged)
    Q_PROPERTY(int harmonicSampleCount READ harmonicSampleCount NOTIFY samplesChanged)
    Q_PROPERTY(int peakTotalCount READ peakTotalCount NOTIFY peakSamplesChanged)
    Q_PROPERTY(bool filterEnabled READ filterEnabled WRITE setFilterEnabled NOTIFY filterChanged)
    Q_PROPERTY(double filterMin READ filterMin NOTIFY filterChanged)
    Q_PROPERTY(double filterMax READ filterMax NOTIFY filterChanged)
    Q_PROPERTY(int peakFilterMode READ peakFilterMode NOTIFY filterChanged)
    Q_PROPERTY(int peakSearchStartIndex READ peakSearchStartIndex NOTIFY filterChanged)
    Q_PROPERTY(int peakSearchEndIndex READ peakSearchEndIndex NOTIFY filterChanged)
    Q_PROPERTY(bool harmonicFilteredView READ harmonicFilteredView WRITE setHarmonicFilteredView NOTIFY filterChanged)
    Q_PROPERTY(bool scatterMode READ scatterMode WRITE setScatterMode NOTIFY filterChanged)
    Q_PROPERTY(double latestPeak READ latestPeak NOTIFY peakSamplesChanged)

public:
    explicit WaveformBackend(QObject *parent = nullptr);
    ~WaveformBackend() override;

    QString host() const;
    int port() const;
    bool connected() const;
    QString statusText() const;
    double frameRate() const;
    QVariantList rawSamples() const;
    QVariantList harmonicSamples() const;
    QVariantList peakSamples() const;
    int rawSampleCount() const;
    int harmonicSampleCount() const;
    int peakTotalCount() const;
    bool filterEnabled() const;
    double filterMin() const;
    double filterMax() const;
    int peakFilterMode() const;
    int peakSearchStartIndex() const;
    int peakSearchEndIndex() const;
    bool harmonicFilteredView() const;
    bool scatterMode() const;
    double latestPeak() const;

    Q_INVOKABLE void connectToHost();
    Q_INVOKABLE void disconnectFromHost();
    Q_INVOKABLE void toggleConnection();
    Q_INVOKABLE void clearPeakHistory();
    Q_INVOKABLE void configurePeakFilter(double minValue, double maxValue, bool enabled);
    Q_INVOKABLE void configurePeakSettings(int startIndex, int endIndex, int mode, double minValue, double maxValue);
    void setRawFrameForwardingEnabled(bool enabled);

public slots:
    void setHost(const QString& host);
    void setPort(int port);
    void setFilterEnabled(bool enabled);
    void setHarmonicFilteredView(bool enabled);
    void setScatterMode(bool scatter);

signals:
    void endpointChanged();
    void connectedChanged();
    void statusTextChanged();
    void frameRateChanged();
    void samplesChanged();
    void peakSamplesChanged();
    void filterChanged();
    void notificationRequested(const QString& level, const QString& message);
    void rawWaveFrameReady(quint64 timestampUs, QByteArray rawSignalPayload, QByteArray harmonicPayload, VaporView::TcpFloatEncoding floatEncoding);

private slots:
    void onReadyRead();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError();
    void onWorkerDisplayFrameDecoded(
        QVector<float> rawSamples,
        QVector<float> harmonicSamples);
    void onWorkerFrameMetricsDecoded(
        quint64 timestampUs,
        float peakValue,
        double frameRate);
    void onWorkerRawFrameDecoded(
        quint64 timestampUs,
        QByteArray rawSignalPayload,
        QByteArray harmonicPayload,
        VaporView::TcpFloatEncoding floatEncoding);
    void updateLiveDisplay();

private:
    enum class ReadState
    {
        RawHeader,
        RawPayload,
        HarmonicHeader,
        HarmonicPayload,
    };

    enum class HeaderByteOrder
    {
        Unknown,
        LittleEndian,
        BigEndian,
    };

    void loadSettings();
    void saveSettings() const;
    void setStatusText(const QString& text);
    void processBuffer();
    bool tryConsumeHeader();
    bool tryConsumePayload(QVector<float>& output, QByteArray *rawPayload);
    bool isValidPayloadSize(qint32 candidate) const;
    qint32 decodeHeaderValue(const char *raw, HeaderByteOrder order) const;
    QVariantList vectorToVariantList(const QVector<float>& values, int maxCount = 700) const;
    QVariantList vectorToFilteredVariantList(const QVector<float>& values, int maxCount = 700) const;
    void updateFrameRate(qint64 nowMs);
    void markLiveDisplayDirty(bool samples, bool peak, bool frameRate);
    void rebuildFilteredPeakHistory();

    QTcpSocket socket_;
    QThread receiver_thread_;
    TcpWaveReceiverWorker *receiver_worker_;
    QString host_;
    int port_;
    QString status_text_;
    QByteArray buffer_;
    QByteArray pending_raw_payload_;
    QVector<float> raw_history_;
    QVector<float> harmonic_history_;
    QVector<float> peak_raw_history_;
    QVector<float> peak_history_;
    QVariantList raw_samples_cache_;
    QVariantList harmonic_samples_cache_;
    QVariantList harmonic_filtered_samples_cache_;
    QVariantList peak_samples_cache_;
    QTimer live_display_timer_;
    ReadState read_state_;
    HeaderByteOrder header_byte_order_;
    VaporView::TcpFloatEncoding float_encoding_;
    int expected_payload_size_;
    qint64 frame_count_;
    qint64 peak_total_count_;
    QVector<qint64> frame_arrivals_ms_;
    double frame_rate_;
    bool connected_;
    bool filter_enabled_;
    int peak_filter_mode_;
    int peak_search_start_index_;
    int peak_search_end_index_;
    bool harmonic_filtered_view_;
    bool scatter_mode_;
    bool live_display_dirty_;
    bool samples_dirty_;
    bool peak_dirty_;
    bool frame_rate_dirty_;
    bool raw_frame_forwarding_enabled_;
    double filter_min_;
    double filter_max_;
};

class RecordingBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString recordingDirectory READ recordingDirectory WRITE setRecordingDirectory NOTIFY recordingDirectoryChanged)
    Q_PROPERTY(QString sessionDirectory READ sessionDirectory NOTIFY recordingStateChanged)
    Q_PROPERTY(QString status READ status NOTIFY recordingStateChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingStateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY recordingStateChanged)
    Q_PROPERTY(qint64 sensorRows READ sensorRows NOTIFY recordingStatsChanged)
    Q_PROPERTY(qint64 waveformFrames READ waveformFrames NOTIFY recordingStatsChanged)
    Q_PROPERTY(QString fileSizeText READ fileSizeText NOTIFY recordingStatsChanged)
    Q_PROPERTY(QString recordUsageText READ recordUsageText NOTIFY recordingStatsChanged)
    Q_PROPERTY(QString diskRemainingText READ diskRemainingText NOTIFY recordingStatsChanged)
    Q_PROPERTY(QString diskTotalText READ diskTotalText NOTIFY recordingStatsChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY recordingStatsChanged)
    Q_PROPERTY(QString systemUptimeText READ systemUptimeText NOTIFY recordingStatsChanged)
    Q_PROPERTY(int exportRateHz READ exportRateHz WRITE setExportRateHz NOTIFY exportRateHzChanged)
    Q_PROPERTY(int waveformExportRateHz READ waveformExportRateHz WRITE setWaveformExportRateHz NOTIFY exportRateHzChanged)

public:
    RecordingBackend(DeviceBackend *deviceBackend, WaveformBackend *waveformBackend, QObject *parent = nullptr);
    ~RecordingBackend() override;

    QString recordingDirectory() const;
    QString sessionDirectory() const;
    QString status() const;
    bool recording() const;
    bool paused() const;
    qint64 sensorRows() const;
    qint64 waveformFrames() const;
    QString fileSizeText() const;
    QString recordUsageText() const;
    QString diskRemainingText() const;
    QString diskTotalText() const;
    QString durationText() const;
    QString systemUptimeText() const;
    int exportRateHz() const;
    int waveformExportRateHz() const;

    Q_INVOKABLE bool startRecording();
    Q_INVOKABLE void pauseRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void openLatestSessionInViewer();

public slots:
    void setRecordingDirectory(const QString& directory);
    void setExportRateHz(int hz);
    void setWaveformExportRateHz(int hz);

signals:
    void recordingDirectoryChanged();
    void recordingStateChanged();
    void recordingStatsChanged();
    void exportRateHzChanged();
    void notificationRequested(const QString& level, const QString& message);
    void sessionCompleted(const QString& sessionDirectory);

private slots:
    void onEpsilonRawFrame(quint64 hostTimestampUs, int packetId, int serialNumber, QByteArray payload);
    void onPtbRawFrame(quint64 hostTimestampUs, QByteArray payload);
    void onHmpRawFrame(quint64 hostTimestampUs, QByteArray payload);
    void onLidarRawFrame(quint64 hostTimestampUs, int protocol, QByteArray payload);
    void onTcpRawWaveFrame(quint64 timestampUs, QByteArray rawSignalPayload, QByteArray harmonicPayload, VaporView::TcpFloatEncoding floatEncoding);

private:
    bool prepareRecordingSessionLayout(const QString& recordsPath, const QString& sessionName);
    bool copyRawDatFormatDocumentToSession();
    QString defaultRecordingDirectory() const;
    QString locateRepositoryRoot() const;
    bool openUnifiedRawDatFile(std::unique_ptr<QFile>& file, const QString& filename, quint16 sourceId);
    bool writeUnifiedRawRecord(QFile *file,
                               std::atomic<quint64>& recordCount,
                               quint16 sourceId,
                               quint16 recordType,
                               quint32 flags,
                               quint64 hostTimestampUs,
                               const void *payload,
                               size_t payloadSize);
    void writeSensorsHeader();
    void writeSessionMetadata(const QString& endTimeUtc = QString());
    void writeDeviceConfigSnapshot();
    void closeUnifiedRawDatFiles();
    void resetUnifiedRawDatFiles();
    void startRecordingWorkers();
    void stopRecordingWorkers();
    void appendEventLogLine(const QString& level, const QString& message);
    void appendErrorLogLine(const QString& message);
    quint64 currentTimestampUs() const;
    quint64 steadyToEpochUs(const std::chrono::steady_clock::time_point& timePoint) const;
    QString sessionNameTimestamp() const;
    QString timestampUtc() const;
    void updateStats();
    void log(const QString& message, const QString& level = QStringLiteral("info"));

    DeviceBackend *device_backend_;
    WaveformBackend *waveform_backend_;
    QString recording_directory_;
    QString session_directory_;
    QString session_name_;
    QString session_start_time_utc_;
    quint64 session_start_time_us_;
    QString sensors_filename_;
    QString raw_epsilon_filename_;
    QString raw_ptb_filename_;
    QString raw_hmp_filename_;
    QString raw_lidar_filename_;
    QString raw_tcp_wave_filename_;
    QString raw_dat_doc_filename_;
    QString session_metadata_filename_;
    QString event_log_filename_;
    QString error_log_filename_;
    QString device_config_filename_;
    std::unique_ptr<QFile> sensors_file_;
    std::unique_ptr<QFile> raw_epsilon_file_;
    std::unique_ptr<QFile> raw_ptb_file_;
    std::unique_ptr<QFile> raw_hmp_file_;
    std::unique_ptr<QFile> raw_lidar_file_;
    std::unique_ptr<QFile> raw_tcp_wave_file_;
    std::unique_ptr<QFile> event_log_file_;
    std::unique_ptr<QFile> error_log_file_;
    std::atomic<bool> recording_thread_running_;
    bool recording_paused_;
    std::thread recording_thread_;
    std::mutex recording_files_mutex_;
    int export_rate_hz_;
    int waveform_export_rate_hz_;
    std::atomic<qint64> recording_entry_count_;
    std::atomic<qint64> waveform_frame_count_;
    std::atomic<qint64> waveform_file_count_;
    std::atomic<quint64> raw_epsilon_record_count_;
    std::atomic<quint64> raw_ptb_record_count_;
    std::atomic<quint64> raw_hmp_record_count_;
    std::atomic<quint64> raw_lidar_record_count_;
    std::atomic<quint64> raw_tcp_wave_record_count_;
    std::chrono::steady_clock::time_point steady_clock_anchor_;
    std::chrono::system_clock::time_point system_clock_anchor_;
    QTimer stats_timer_;
};

class RtkBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString server READ server WRITE setServer NOTIFY configChanged)
    Q_PROPERTY(QString port READ port WRITE setPort NOTIFY configChanged)
    Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY configChanged)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY configChanged)
    Q_PROPERTY(QString mountpoint READ mountpoint WRITE setMountpoint NOTIFY configChanged)
    Q_PROPERTY(QString outputPort READ outputPort WRITE setOutputPort NOTIFY configChanged)
    Q_PROPERTY(int outputBaud READ outputBaud WRITE setOutputBaud NOTIFY configChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QStringList diagnostics READ diagnostics NOTIFY diagnosticsChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(bool detectingMountPoints READ detectingMountPoints NOTIFY detectingMountPointsChanged)
    Q_PROPERTY(QStringList mountPointOptions READ mountPointOptions NOTIFY mountPointOptionsChanged)
    Q_PROPERTY(QString mountPointDetectStatus READ mountPointDetectStatus NOTIFY mountPointOptionsChanged)
    Q_PROPERTY(QVariantList outputPortOptions READ outputPortOptions NOTIFY outputPortOptionsChanged)
    Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect NOTIFY configChanged)
    Q_PROPERTY(int timeoutMs READ timeoutMs WRITE setTimeoutMs NOTIFY configChanged)
    Q_PROPERTY(int reconnectMs READ reconnectMs WRITE setReconnectMs NOTIFY configChanged)
    Q_PROPERTY(int ggaGenerationRateHz READ ggaGenerationRateHz WRITE setGgaGenerationRateHz NOTIFY configChanged)
    Q_PROPERTY(bool testingConnection READ testingConnection NOTIFY testingConnectionChanged)


public:
    explicit RtkBackend(DeviceBackend *deviceBackend, QObject *parent = nullptr);
    ~RtkBackend() override;

    QString server() const;
    QString port() const;
    QString username() const;
    QString password() const;
    QString mountpoint() const;
    QString outputPort() const;
    int outputBaud() const;
    bool running() const;
    QStringList diagnostics() const;
    QVariantMap stats() const;
    bool detectingMountPoints() const;
    QStringList mountPointOptions() const;
    QString mountPointDetectStatus() const;
    QVariantList outputPortOptions() const;
    bool autoReconnect() const;
    int timeoutMs() const;
    int reconnectMs() const;
    int ggaGenerationRateHz() const;
    bool testingConnection() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void testConnection();
    Q_INVOKABLE void saveConfig();
    Q_INVOKABLE void loadConfig();
    Q_INVOKABLE void clearDiagnostics();
    Q_INVOKABLE void applyMainAntennaLeverArm(double xM, double yM, double zM);
    Q_INVOKABLE void detectMountPoints();
    Q_INVOKABLE void refreshOutputPortOptions();

public slots:
    void setServer(const QString& value);
    void setPort(const QString& value);
    void setUsername(const QString& value);
    void setPassword(const QString& value);
    void setMountpoint(const QString& value);
    void setOutputPort(const QString& value);
    void setOutputBaud(int value);
    void setAutoReconnect(bool value);
    void setTimeoutMs(int value);
    void setReconnectMs(int value);
    void setGgaGenerationRateHz(int value);

signals:
    void configChanged();
    void runningChanged();
    void diagnosticsChanged();
    void statsChanged();
    void detectingMountPointsChanged();
    void mountPointOptionsChanged();
    void outputPortOptionsChanged();
    void notificationRequested(const QString& level, const QString& message);
    void testingConnectionChanged();

private:
    RtkStreamConfig buildConfig() const;
    void appendDiagnostic(const QString& line, const QString& level = QStringLiteral("info"));
    void pollStats();

    DeviceBackend *device_backend_;
    RtkStreamService service_;
    QString server_;
    QString port_;
    QString username_;
    QString password_;
    QString mountpoint_;
    QString output_port_;
    int output_baud_;
    int timeout_ms_ = 5000;
    int reconnect_ms_ = 1000;
    bool auto_reconnect_ = true;
    bool testing_connection_ = false;
    int gga_generation_rate_hz_ = 1;
    QStringList diagnostics_;
    QVariantMap stats_;
    QTimer stats_timer_;
    bool detecting_mount_points_ = false;
    QStringList mount_point_options_;
    QString mount_point_detect_status_;
    QVariantList output_port_options_;
    std::thread fetch_mountpoints_thread_;
    std::thread test_connection_thread_;
    void setTestingConnection(bool value);
    void runStr2strConnectionTest(RtkStreamConfig config);
};

class SessionBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString recordingDirectory READ recordingDirectory WRITE setRecordingDirectory NOTIFY recordingDirectoryChanged)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(QVariantMap selectedSession READ selectedSession NOTIFY selectedSessionChanged)
    Q_PROPERTY(QStringList csvPreviewColumns READ csvPreviewColumns NOTIFY frameSelectionChanged)
    Q_PROPERTY(QVariantList csvPreviewRows READ csvPreviewRows NOTIFY frameSelectionChanged)
    Q_PROPERTY(QVariantList waveformPreview READ waveformPreview NOTIFY frameSelectionChanged)
    Q_PROPERTY(QVariantList waveformRawPreview READ waveformRawPreview NOTIFY frameSelectionChanged)
    Q_PROPERTY(QVariantList waveformHarmonicPreview READ waveformHarmonicPreview NOTIFY frameSelectionChanged)
    Q_PROPERTY(QVariantList peakTrendPreview READ peakTrendPreview NOTIFY selectedSessionChanged)
    Q_PROPERTY(QVariantList temperaturePreview READ temperaturePreview NOTIFY selectedSessionChanged)
    Q_PROPERTY(QVariantList humidityPreview READ humidityPreview NOTIFY selectedSessionChanged)
    Q_PROPERTY(QVariantList pressurePreview READ pressurePreview NOTIFY selectedSessionChanged)
    Q_PROPERTY(bool waveformIndexReady READ waveformIndexReady NOTIFY selectedSessionChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int loadingProgress READ loadingProgress NOTIFY loadingChanged)
    Q_PROPERTY(QString loadingText READ loadingText NOTIFY loadingChanged)
    Q_PROPERTY(int currentFrameIndex READ currentFrameIndex NOTIFY frameSelectionChanged)
    Q_PROPERTY(int currentCsvRow READ currentCsvRow NOTIFY frameSelectionChanged)
    Q_PROPERTY(int secondaryCsvRow READ secondaryCsvRow NOTIFY frameSelectionChanged)
    Q_PROPERTY(int csvPreviewGeneration READ csvPreviewGeneration NOTIFY frameSelectionChanged)
    Q_PROPERTY(int waveformRawPointCount READ waveformRawPointCount NOTIFY frameSelectionChanged)
    Q_PROPERTY(int waveformHarmonicPointCount READ waveformHarmonicPointCount NOTIFY frameSelectionChanged)
    Q_PROPERTY(QVariantList peakTrendRangePreview READ peakTrendRangePreview NOTIFY trendViewRangeChanged)
    Q_PROPERTY(QVariantList temperatureRangePreview READ temperatureRangePreview NOTIFY trendViewRangeChanged)
    Q_PROPERTY(QVariantList humidityRangePreview READ humidityRangePreview NOTIFY trendViewRangeChanged)
    Q_PROPERTY(QVariantList pressureRangePreview READ pressureRangePreview NOTIFY trendViewRangeChanged)
    Q_PROPERTY(int trendViewStart READ trendViewStart NOTIFY trendViewRangeChanged)
    Q_PROPERTY(int trendViewEnd READ trendViewEnd NOTIFY trendViewRangeChanged)
    Q_PROPERTY(int csvRangeStartRow READ csvRangeStartRow NOTIFY trendViewRangeChanged)
    Q_PROPERTY(int csvRangeEndRow READ csvRangeEndRow NOTIFY trendViewRangeChanged)
    Q_PROPERTY(int currentCsvRangeCursorIndex READ currentCsvRangeCursorIndex NOTIFY trendViewRangeChanged)

public:
    explicit SessionBackend(QObject *parent = nullptr);
    ~SessionBackend() override
    {
        if (session_load_thread_)
        {
            session_load_thread_->requestInterruption();
            session_load_thread_->wait(3000);
        }
    }

    QString recordingDirectory() const;
    QVariantList sessions() const;
    QVariantMap selectedSession() const;
    QStringList csvPreviewColumns() const;
    QVariantList csvPreviewRows() const;
    QVariantList waveformPreview() const;
    QVariantList waveformRawPreview() const;
    QVariantList waveformHarmonicPreview() const;
    QVariantList peakTrendPreview() const;
    QVariantList temperaturePreview() const;
    QVariantList humidityPreview() const;
    QVariantList pressurePreview() const;
    QVariantList peakTrendRangePreview() const;
    QVariantList temperatureRangePreview() const;
    QVariantList humidityRangePreview() const;
    QVariantList pressureRangePreview() const;
    int trendViewStart() const;
    int trendViewEnd() const;
    int csvRangeStartRow() const;
    int csvRangeEndRow() const;
    int currentCsvRangeCursorIndex() const;
    bool waveformIndexReady() const;
    bool loading() const;
    int loadingProgress() const;
    QString loadingText() const;
    int currentFrameIndex() const;
    int currentCsvRow() const;
    int secondaryCsvRow() const;
    int csvPreviewGeneration() const;
    int waveformRawPointCount() const;
    int waveformHarmonicPointCount() const;

    Q_INVOKABLE void refreshSessions();
    Q_INVOKABLE void sortSessions(int mode);
    Q_INVOKABLE void setSessionFilter(const QString& text);
    Q_INVOKABLE void clearSessionFilter();
    Q_INVOKABLE void setSessionFilterCriteria(const QVariantMap& criteria);
    Q_INVOKABLE QVariantMap sessionFilterCriteria() const;
    Q_INVOKABLE void clearSessionFilters();
    Q_INVOKABLE void openSessionPath(const QString& path);
    Q_INVOKABLE void selectSession(int index);
    Q_INVOKABLE void reloadSelectedSession();
    Q_INVOKABLE void loadSessionFrame(int frameIndex);
    Q_INVOKABLE void setFrameCursor(int frameIndex);
    Q_INVOKABLE void setTrendViewRange(int startFrame, int endFrame);
    Q_INVOKABLE void clear();

public slots:
    void setRecordingDirectory(const QString& directory);

signals:
    void recordingDirectoryChanged();
    void sessionsChanged();
    void selectedSessionChanged();
    void loadingChanged();
    void frameSelectionChanged();
    void notificationRequested(const QString& level, const QString& message);
    void trendViewRangeChanged();

private:
    struct WaveformFrameIndex
    {
        quint64 rawPayloadOffset = 0;
        quint32 rawPayloadSize = 0;
        quint64 harmonicPayloadOffset = 0;
        quint32 harmonicPayloadSize = 0;
        quint32 flags = 0;
        quint64 timestampUs = 0;
    };

    struct LegacyWaveformSegment
    {
        QString filename;
        quint64 start_frame = 0;
        quint64 frame_count = 0;
    };

    struct SessionLoadResult
    {
        bool ok = false;
        QString error;
        QString path;
        QVariantMap selectedSession;
        QStringList csvColumns;
        QVariantList csvRowsAll;
        QVector<quint64> csvTimestampsUs;
        QVariantList temperatureValues;
        QVariantList humidityValues;
        QVariantList pressureValues;
        QVariantList rawPreview;
        QVariantList harmonicPreview;
        QVariantList peakValues;
        QVector<quint64> waveformTimestampsUs;
        QVector<WaveformFrameIndex> waveformFrameIndex;
        QString waveformIndexPath;
        int rawPointCount = 0;
        int harmonicPointCount = 0;
        int currentFrameIndex = -1;
        bool legacyFormat = false;
        QVector<LegacyWaveformSegment> legacySegments;
        int legacyPointsPerFrame = 0;
        QString legacyWaveformPath;
    };

    QVariantMap sessionSummaryForDirectory(const QString& path, bool detailed = false) const;
    void clearPreviewData();
    void loadSelectedSession(const QString& path);
    void startLoadingSession(const QString& path);
    void setLoadingState(bool loading, int progress, const QString& text);
    void postLoadProgress(int generation, int progress, const QString& text);
    void applySessionLoadResult(const std::shared_ptr<SessionLoadResult>& result, int generation);
    QVariantList readCsvPreview(const QString& csvPath, int maxRows) const;
    QVariantMap readWaveformPreviews(const QString& rawPath);
    QVariantMap readWaveformFramePreview(const QString& rawPath, int frameIndex) const;
    QVariantMap readLegacyWaveformFrame(int frameIndex) const;
    QVariantMap readSensorTrendPreviews(const QString& csvPath, int maxRows) const;
    QVariantMap countSensorRowsAndRange(const QString& csvPath) const;
    int countWaveformFrames(const QString& rawPath) const;
    int findClosestCsvRow(quint64 timestampUs) const;
    QVector<int> closestCsvRows(quint64 timestampUs) const;
    void updateCsvPreviewForTimestamp(quint64 timestampUs);
    void updateCsvPreviewForTimestamp(quint64 timestampUs, int maxRows);
    void applySessionSortFilter();
    void rebuildTrendRangePreviews();
    SessionLoadResult buildSessionLoadResult(const QString& path, int generation);

    QString recording_directory_;
    QVariantList sessions_;
    QVariantList all_sessions_;
    int sort_sessions_mode_ = 0;
    QString session_filter_text_;
    QString session_filter_date_preset_;
    QDate session_filter_start_date_;
    QDate session_filter_end_date_;
    int session_filter_min_frames_ = -1;
    int session_filter_max_frames_ = -1;
    double session_filter_min_size_mb_ = -1.0;
    double session_filter_max_size_mb_ = -1.0;
    QVariantMap selected_session_;
    QStringList csv_preview_columns_;
    QVariantList csv_preview_rows_;
    QVariantList waveform_preview_;
    QVariantList waveform_raw_preview_;
    QVariantList waveform_harmonic_preview_;
    QVariantList peak_trend_preview_;
    QVariantList temperature_preview_;
    QVariantList humidity_preview_;
    QVariantList pressure_preview_;
    QVariantList csv_rows_all_;
    QVector<quint64> csv_timestamps_us_;
    QVector<quint64> waveform_timestamps_us_;
    QString waveform_index_path_;
    QVector<WaveformFrameIndex> waveform_frame_index_;
    bool legacy_format_ = false;
    QVector<LegacyWaveformSegment> legacy_waveform_segments_;
    int legacy_points_per_frame_ = 0;
    QString legacy_waveform_path_;
    QThread *session_load_thread_ = nullptr;
    int load_generation_ = 0;
    bool loading_ = false;
    int loading_progress_ = 0;
    QString loading_text_;
    int current_frame_index_ = -1;
    int current_csv_row_ = -1;
    int secondary_csv_row_ = -1;
    int csv_preview_generation_ = 0;
    int waveform_raw_point_count_ = 0;
    int waveform_harmonic_point_count_ = 0;
    int trend_view_start_ = 0;
    int trend_view_end_ = 0;
    int csv_range_start_row_ = 0;
    int csv_range_end_row_ = 0;
    QVariantList peak_trend_full_;
    QVariantList temperature_full_;
    QVariantList humidity_full_;
    QVariantList pressure_full_;
    QVariantList peak_trend_range_preview_;
    QVariantList temperature_range_preview_;
    QVariantList humidity_range_preview_;
    QVariantList pressure_range_preview_;

    struct WaveformCacheEntry
    {
        int frameIndex = -1;
        QVariantList rawPreview;
        QVariantList harmonicPreview;
        int rawPointCount = 0;
        int harmonicPointCount = 0;
        quint64 timestampUs = 0;
    };
    static constexpr int kWaveformCacheSize = 12;
    WaveformCacheEntry waveform_cache_[kWaveformCacheSize] = {};
    int waveform_cache_pos_ = 0;

    QVariantMap getWaveformFrame(const QString& rawPath, int frameIndex);
    void clearWaveformCache();

    QTimer waveform_prefetch_timer_;
    int pending_prefetch_frame_index_ = -1;

    void scheduleWaveformPrefetch(int centerFrameIndex);
    void prefetchWaveformFrames(int centerFrameIndex);
    bool waveformFrameCached(int frameIndex) const;
};

class RawParserBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString rawFilePath READ rawFilePath NOTIFY recordsChanged)
    Q_PROPERTY(QVariantList records READ records NOTIFY recordsChanged)
    Q_PROPERTY(QVariantMap selectedRecord READ selectedRecord NOTIFY selectedRecordChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY recordsChanged)

public:
    explicit RawParserBackend(QObject *parent = nullptr);

    QString rawFilePath() const;
    QVariantList records() const;
    QVariantMap selectedRecord() const;
    QString statusText() const;

    Q_INVOKABLE void openRawFile(const QString& path);
    Q_INVOKABLE void openSessionPath(const QString& path);
    Q_INVOKABLE void selectRecord(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE bool exportListCsv(const QString& path) const;
    Q_INVOKABLE bool exportSelectedPayload(const QString& path) const;
    Q_INVOKABLE bool exportDecodedJson(const QString& path) const;

signals:
    void recordsChanged();
    void selectedRecordChanged();
    void notificationRequested(const QString& level, const QString& message);

private:
    bool loadRawFile(const QString& path);
    QString sourceName(int sourceId) const;
    QString recordTypeName(int sourceId, int recordType) const;

    QString raw_file_path_;
    QVariantList records_;
    QVariantMap selected_record_;
    QString status_text_;
};

class SettingsBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString recordDirectory READ recordDirectory WRITE setRecordDirectory NOTIFY recordDirectoryChanged)
    Q_PROPERTY(QString aboutText READ aboutText CONSTANT)

public:
    SettingsBackend(AppBackend *appBackend, RecordingBackend *recordingBackend, QObject *parent = nullptr);

    QString recordDirectory() const;
    QString aboutText() const;

    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();

public slots:
    void setRecordDirectory(const QString& directory);

signals:
    void recordDirectoryChanged();
    void notificationRequested(const QString& level, const QString& message);

private:
    AppBackend *app_backend_;
    RecordingBackend *recording_backend_;
};

#endif
