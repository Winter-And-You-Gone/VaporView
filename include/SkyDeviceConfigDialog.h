#ifndef VaporView_SKY_DEVICE_CONFIG_DIALOG_H_
#define VaporView_SKY_DEVICE_CONFIG_DIALOG_H_

#include "GroundTelemetryService.h"
#include "SkyConfig.h"

#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

namespace VaporView
{

class SkyDeviceConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SkyDeviceConfigDialog(GroundTelemetryService *service, QWidget *parent = nullptr);

    void setEnglish(bool english);

private slots:
    void onReadClicked();
    void onApplyClicked();
    void onSaveClicked();
    void onSkyConfigReceived(const QJsonObject& config);
    void onApplyResultReceived(const QJsonObject& result);

private:
    struct SerialRow
    {
        QCheckBox *enabled = nullptr;
        QLineEdit *port = nullptr;
        QSpinBox *baud = nullptr;
        QDoubleSpinBox *frequency = nullptr;
        QLabel *enabled_label = nullptr;
        QLabel *port_label = nullptr;
        QLabel *baud_label = nullptr;
        QLabel *frequency_label = nullptr;
    };

    void setupUi();
    SerialRow createSerialRow(QFormLayout *layout, const QString& title);
    void setConfig(const SkyConfig& config);
    SkyConfig currentConfigFromUi() const;
    void setSerialRow(const SerialRow& row, const SerialDeviceConfig& config);
    SerialDeviceConfig serialConfigFromRow(const SerialRow& row) const;
    void updateTexts();

    GroundTelemetryService *service_;
    bool is_english_ = false;
    SkyConfig current_config_;
    SerialRow epsilon_;
    SerialRow ptb_;
    SerialRow hmp_;
    SerialRow lidar_;
    QCheckBox *wave_enabled_ = nullptr;
    QLineEdit *wave_host_ = nullptr;
    QSpinBox *wave_port_ = nullptr;
    QDoubleSpinBox *wave_frequency_ = nullptr;
    QSpinBox *wave_downsample_ = nullptr;
    QDoubleSpinBox *telemetry_basic_rate_ = nullptr;
    QDoubleSpinBox *telemetry_feature_rate_ = nullptr;
    QDoubleSpinBox *telemetry_waveform_rate_ = nullptr;
    QDoubleSpinBox *telemetry_heartbeat_rate_ = nullptr;
    QDoubleSpinBox *telemetry_status_rate_ = nullptr;
    QGroupBox *epsilon_group_ = nullptr;
    QGroupBox *ptb_group_ = nullptr;
    QGroupBox *hmp_group_ = nullptr;
    QGroupBox *lidar_group_ = nullptr;
    QGroupBox *wave_group_ = nullptr;
    QGroupBox *telemetry_group_ = nullptr;
    QLabel *wave_enabled_label_ = nullptr;
    QLabel *wave_host_label_ = nullptr;
    QLabel *wave_port_label_ = nullptr;
    QLabel *wave_frequency_label_ = nullptr;
    QLabel *wave_downsample_label_ = nullptr;
    QLabel *telemetry_basic_label_ = nullptr;
    QLabel *telemetry_feature_label_ = nullptr;
    QLabel *telemetry_waveform_label_ = nullptr;
    QLabel *telemetry_heartbeat_label_ = nullptr;
    QLabel *telemetry_status_label_ = nullptr;
    QPushButton *read_button_ = nullptr;
    QPushButton *apply_button_ = nullptr;
    QPushButton *save_button_ = nullptr;
    QPushButton *close_button_ = nullptr;
    QPlainTextEdit *result_text_ = nullptr;
};

}  // namespace VaporView

#endif
