#ifndef VaporView_TCP_WAVE_PANEL_H_
#define VaporView_TCP_WAVE_PANEL_H_

#include "TcpWaveEncoding.h"
#include "TelemetryTypes.h"

#include <QByteArray>
#include <QWidget>
#include <QVector>

class QLabel;
class QLineEdit;
class QPushButton;
class QGroupBox;
class QSpinBox;
class QTcpSocket;
class QTimer;
class QGridLayout;
class QHBoxLayout;
class WavePlotWidget;
class PeakTrendPlotWidget;
class RangeSelectionAxisWidget;

class TcpWavePanel : public QWidget
{
    Q_OBJECT

public:
    explicit TcpWavePanel(QWidget *parent = nullptr);
    ~TcpWavePanel() override;

    void setEnglish(bool english);
    QString host() const;
    int port() const;
    bool isConnected() const;
    void attachWaveformSplitControls(QLabel *label, QSpinBox *spinBox);
    void setRemoteSkyMode(bool enabled);
    void setRemoteWaveTcpState(VaporView::DeviceState state);
    void injectRemoteSecondHarmonicFrame(quint64 timestampUs, const QVector<float>& samples);
    void injectRemoteWaveformFeature(const VaporView::WaveformFeature& feature);

    enum class ParseMode
    {
        AutoDetect,
        LengthPrefixed
    };

    enum class ReadState
    {
        Wave1Header,
        Wave1Payload,
        Wave4Header,
        Wave4Payload
    };

    enum class HeaderByteOrder
    {
        Unknown,
        LittleEndian,
        BigEndian
    };

    using FloatEncoding = VaporView::TcpFloatEncoding;

signals:
    void normalizedSecondHarmonicFrameReady(quint64 timestampUs, QVector<float> samples);
    void rawWaveFrameReady(quint64 timestampUs, QByteArray rawSignalPayload, QByteArray harmonicPayload, VaporView::TcpFloatEncoding floatEncoding);
    void connectionStateChanged(bool connected);
    void remoteWaveTcpConnectionRequested(bool connectRequested);

private slots:
    void onToggleConnectionClicked();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketStateChanged();
    void onSocketError();
    void onTogglePeakPlotModeClicked();
    void onClearPeakPlotClicked();
    void onConfigurePeakFilterClicked();

private:
    enum class PeakFilterMode
    {
        None,
        IqrOutlier,
        KeepRange,
        ExcludeRange
    };

    struct PeakFilterSettings
    {
        PeakFilterMode mode = PeakFilterMode::None;
        double min_value = 0.0;
        double max_value = 0.0;
    };

    void setupUi();
    void setupSocket();
    void recreateSocket();
    void requestGracefulDisconnect();
    void setConnectedUiState(bool connected);
    void loadRememberedInputState();
    void saveRememberedInputState() const;
    void updatePeakPlotModeButtonText();
    void updatePeakFilterButtonText();
    QString peakFilterModeText(PeakFilterMode mode) const;
    void resetFrameRateDisplay();
    void updateFrameRateDisplay(qint64 arrivalTimeMs);
    void updateLiveDisplay();
    void setStatusText(const QString& text);
    void resetParserState();
    void processBuffer();
    bool trySynchronizeLengthPrefixedStream();
    bool isValidPayloadSize(qint32 candidate) const;
    qint32 decodeHeaderValue(const char *raw, HeaderByteOrder order) const;
    bool tryConsumeHeader();
    bool tryConsumePayload(QVector<float>& output, QByteArray *rawPayload = nullptr);
    QVector<float> decodeFloatPayload(const QByteArray& payload) const;
    float currentWaveformPeakValue(const QVector<float>& samples) const;
    void rebuildPeakHistory();

    QLineEdit *host_edit_;
    QSpinBox *port_spin_;
    QPushButton *connect_button_;
    QLabel *host_label_;
    QLabel *port_label_;
    QLabel *panel_title_label_;
    QLabel *frame_rate_label_;
    QLabel *status_label_;
    QLabel *hint_label_;
    QLabel *wave1_title_label_;
    QLabel *wave4_title_label_;
    QLabel *peak_title_label_;
    QLabel *wave1_info_label_;
    QLabel *wave4_info_label_;
    QGroupBox *wave1_group_;
    QGroupBox *wave4_group_;
    QGroupBox *peak_group_;
    WavePlotWidget *wave1_plot_;
    WavePlotWidget *wave4_plot_;
    PeakTrendPlotWidget *peak_plot_;
    RangeSelectionAxisWidget *peak_range_axis_;
    QPushButton *peak_filter_button_;
    QPushButton *peak_mode_button_;
    QPushButton *peak_clear_button_;
    QGridLayout *control_layout_;
    QHBoxLayout *top_controls_layout_;
    QTcpSocket *socket_;
    QTimer *live_display_timer_;

    QByteArray buffer_;
    QByteArray pending_wave1_payload_;
    QVector<float> wave1_history_;
    QVector<float> wave4_history_;
    QVector<float> peak_raw_history_;
    QVector<float> peak_history_;
    QVector<float> pending_wave1_;
    QString pending_wave1_info_text_;
    QString pending_wave4_info_text_;
    QString pending_live_status_text_;
    PeakFilterSettings peak_filter_settings_;
    int peak_search_start_index_;
    int peak_search_end_index_;
    bool peak_plot_scatter_mode_;
    ParseMode parse_mode_;
    ReadState read_state_;
    HeaderByteOrder header_byte_order_;
    FloatEncoding float_encoding_;
    int expected_payload_size_;
    qint64 frame_count_;
    QVector<qint64> frame_arrival_times_ms_;
    bool live_display_dirty_;
    bool is_english_;
    bool remote_sky_mode_;
    bool remote_wave_tcp_connected_;
};

#endif
