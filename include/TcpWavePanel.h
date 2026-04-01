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
class WavePlotWidget;

class TcpWavePanel : public QWidget
{
    Q_OBJECT

public:
    explicit TcpWavePanel(QWidget *parent = nullptr);
    ~TcpWavePanel() override;

    void setEnglish(bool english);

private slots:
    void onToggleConnectionClicked();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();

private:
    enum class ReadState
    {
        Wave1Header,
        Wave1Payload,
        Wave4Header,
        Wave4Payload
    };

    void setupUi();
    void setConnectedUiState(bool connected);
    void setStatusText(const QString& text);
    void resetParserState();
    void processBuffer();
    bool tryConsumeHeader();
    bool tryConsumePayload(QVector<float>& output);
    QVector<float> decodeFloatPayload(const QByteArray& payload) const;

    QLineEdit *host_edit_;
    QSpinBox *port_spin_;
    QPushButton *connect_button_;
    QLabel *host_label_;
    QLabel *port_label_;
    QLabel *status_label_;
    QLabel *wave1_info_label_;
    QLabel *wave4_info_label_;
    QGroupBox *wave1_group_;
    QGroupBox *wave4_group_;
    WavePlotWidget *wave1_plot_;
    WavePlotWidget *wave4_plot_;
    QTcpSocket *socket_;

    QByteArray buffer_;
    QVector<float> pending_wave1_;
    ReadState read_state_;
    int expected_payload_size_;
    bool is_english_;
};

#endif
