#ifndef VaporView_SKY_DEVICE_CONFIG_DIALOG_H_
#define VaporView_SKY_DEVICE_CONFIG_DIALOG_H_

#include "GroundTelemetryService.h"
#include "SkyConfig.h"

#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
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
    QPushButton *read_button_ = nullptr;
    QPushButton *apply_button_ = nullptr;
    QPushButton *save_button_ = nullptr;
    QPushButton *close_button_ = nullptr;
    QPlainTextEdit *result_text_ = nullptr;
};

}  // namespace VaporView

#endif
