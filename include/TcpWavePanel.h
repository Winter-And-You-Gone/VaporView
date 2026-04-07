#ifndef VaporView_TCP_WAVE_PANEL_H_
#define VaporView_TCP_WAVE_PANEL_H_

#include <QWidget>
#include <QVector>

class QLabel;
class QLineEdit;
class QPushButton;
class QGroupBox;
class QSpinBox;
class QTcpSocket;
class QGridLayout;
class WavePlotWidget;
class PeakTrendPlotWidget;

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

    enum class FloatEncoding
    {
        Unknown,
        LittleEndian,
        BigEndian,
        WordSwappedLittleEndian
    };

signals:
    void normalizedSecondHarmonicFrameReady(quint64 timestampUs, QVector<float> samples);
    void connectionStateChanged(bool connected);

private slots:
    void onToggleConnectionClicked();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketStateChanged();
    void onSocketError();
    void onTogglePeakPlotModeClicked();
    void onClearPeakPlotClicked();

private:
    void setupUi();
    void setupSocket();
    void recreateSocket();
    void requestGracefulDisconnect();
    void setConnectedUiState(bool connected);
    void loadRememberedInputState();
    void saveRememberedInputState() const;
    void updatePeakPlotModeButtonText();
    void setStatusText(const QString& text);
    void resetParserState();
    void processBuffer();
    bool trySynchronizeLengthPrefixedStream();
    bool isValidPayloadSize(qint32 candidate) const;
    qint32 decodeHeaderValue(const char *raw, HeaderByteOrder order) const;
    bool tryConsumeHeader();
    bool tryConsumePayload(QVector<float>& output);
    float decodeFloatSample(const char *raw, FloatEncoding encoding) const;
    FloatEncoding autoDetectFloatEncoding(const QByteArray& payload) const;
    QVector<float> decodeFloatPayload(const QByteArray& payload) const;

    QLineEdit *host_edit_;
    QSpinBox *port_spin_;
    QPushButton *connect_button_;
    QLabel *host_label_;
    QLabel *port_label_;
    QLabel *panel_title_label_;
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
    QPushButton *peak_mode_button_;
    QPushButton *peak_clear_button_;
    QGridLayout *control_layout_;
    QTcpSocket *socket_;

    QByteArray buffer_;
    QVector<float> wave1_history_;
    QVector<float> wave4_history_;
    QVector<float> peak_history_;
    QVector<float> pending_wave1_;
    bool peak_plot_scatter_mode_;
    ParseMode parse_mode_;
    ReadState read_state_;
    HeaderByteOrder header_byte_order_;
    FloatEncoding float_encoding_;
    int expected_payload_size_;
    qint64 frame_count_;
    bool is_english_;
};

#endif
