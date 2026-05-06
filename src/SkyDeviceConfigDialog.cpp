#include "SkyDeviceConfigDialog.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QMessageBox>
#include <QScrollArea>
#include <QVBoxLayout>

namespace VaporView
{

SkyDeviceConfigDialog::SkyDeviceConfigDialog(GroundTelemetryService *service, QWidget *parent)
    : QDialog(parent)
    , service_(service)
{
    setupUi();
    if (service_)
    {
        connect(service_, &GroundTelemetryService::skyConfigReceived, this, &SkyDeviceConfigDialog::onSkyConfigReceived);
        connect(service_, &GroundTelemetryService::skyConfigApplyResultReceived, this, &SkyDeviceConfigDialog::onApplyResultReceived);
    }
    setConfig(SkyConfig::defaults());
    setEnglish(false);
}

void SkyDeviceConfigDialog::setEnglish(bool english)
{
    is_english_ = english;
    updateTexts();
}

void SkyDeviceConfigDialog::onReadClicked()
{
    if (service_)
    {
        service_->requestSkyConfig();
    }
}

void SkyDeviceConfigDialog::onApplyClicked()
{
    if (!service_)
    {
        return;
    }
    const SkyConfig config = currentConfigFromUi();
    QString error;
    if (!config.validate(&error))
    {
        QMessageBox::warning(this, is_english_ ? "Invalid Config" : "配置无效", error);
        return;
    }
    service_->setSkyConfig(config.toJson());
}

void SkyDeviceConfigDialog::onSaveClicked()
{
    if (service_)
    {
        service_->saveSkyConfig();
    }
}

void SkyDeviceConfigDialog::onSkyConfigReceived(const QJsonObject& object)
{
    SkyConfig config;
    QString error;
    if (!SkyConfig::fromJson(object, config, &error))
    {
        result_text_->setPlainText(QStringLiteral("Invalid config from sky: %1").arg(error));
        return;
    }
    setConfig(config);
    result_text_->setPlainText(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

void SkyDeviceConfigDialog::onApplyResultReceived(const QJsonObject& result)
{
    result_text_->setPlainText(QJsonDocument(result).toJson(QJsonDocument::Indented));
}

void SkyDeviceConfigDialog::setupUi()
{
    setMinimumSize(720, 680);
    auto *root = new QVBoxLayout(this);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);

    auto addSerialGroup = [this, contentLayout](const QString& title, SerialRow& row) {
        auto *group = new QGroupBox(title, this);
        auto *layout = new QFormLayout(group);
        row = createSerialRow(layout, title);
        contentLayout->addWidget(group);
    };

    addSerialGroup(QStringLiteral("EPSILON"), epsilon_);
    addSerialGroup(QStringLiteral("PTB210"), ptb_);
    addSerialGroup(QStringLiteral("HMP3"), hmp_);
    addSerialGroup(QStringLiteral("TFA1500-L"), lidar_);

    auto *waveGroup = new QGroupBox(QStringLiteral("Wave TCP"), this);
    auto *waveLayout = new QFormLayout(waveGroup);
    wave_enabled_ = new QCheckBox(this);
    wave_host_ = new QLineEdit(this);
    wave_port_ = new QSpinBox(this);
    wave_port_->setRange(1, 65535);
    wave_frequency_ = new QDoubleSpinBox(this);
    wave_frequency_->setRange(0.1, 1000.0);
    wave_frequency_->setDecimals(1);
    wave_downsample_ = new QSpinBox(this);
    wave_downsample_->setRange(1, 1000);
    waveLayout->addRow(QStringLiteral("Enabled"), wave_enabled_);
    waveLayout->addRow(QStringLiteral("Host"), wave_host_);
    waveLayout->addRow(QStringLiteral("Port"), wave_port_);
    waveLayout->addRow(QStringLiteral("Frequency Hz"), wave_frequency_);
    waveLayout->addRow(QStringLiteral("Downsample"), wave_downsample_);
    contentLayout->addWidget(waveGroup);

    auto *telemetryGroup = new QGroupBox(QStringLiteral("Telemetry"), this);
    auto *telemetryLayout = new QFormLayout(telemetryGroup);
    auto makeRate = [this]() {
        auto *spin = new QDoubleSpinBox(this);
        spin->setRange(0.1, 200.0);
        spin->setDecimals(1);
        return spin;
    };
    telemetry_basic_rate_ = makeRate();
    telemetry_feature_rate_ = makeRate();
    telemetry_waveform_rate_ = makeRate();
    telemetry_heartbeat_rate_ = makeRate();
    telemetry_status_rate_ = makeRate();
    telemetryLayout->addRow(QStringLiteral("Basic Hz"), telemetry_basic_rate_);
    telemetryLayout->addRow(QStringLiteral("Feature Hz"), telemetry_feature_rate_);
    telemetryLayout->addRow(QStringLiteral("Waveform Hz"), telemetry_waveform_rate_);
    telemetryLayout->addRow(QStringLiteral("Heartbeat Hz"), telemetry_heartbeat_rate_);
    telemetryLayout->addRow(QStringLiteral("Status Hz"), telemetry_status_rate_);
    contentLayout->addWidget(telemetryGroup);

    result_text_ = new QPlainTextEdit(this);
    result_text_->setReadOnly(true);
    result_text_->setMinimumHeight(160);
    contentLayout->addWidget(result_text_);
    scroll->setWidget(content);
    root->addWidget(scroll);

    auto *buttonLayout = new QHBoxLayout();
    read_button_ = new QPushButton(this);
    apply_button_ = new QPushButton(this);
    save_button_ = new QPushButton(this);
    close_button_ = new QPushButton(this);
    connect(read_button_, &QPushButton::clicked, this, &SkyDeviceConfigDialog::onReadClicked);
    connect(apply_button_, &QPushButton::clicked, this, &SkyDeviceConfigDialog::onApplyClicked);
    connect(save_button_, &QPushButton::clicked, this, &SkyDeviceConfigDialog::onSaveClicked);
    connect(close_button_, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(read_button_);
    buttonLayout->addWidget(apply_button_);
    buttonLayout->addWidget(save_button_);
    buttonLayout->addStretch();
    buttonLayout->addWidget(close_button_);
    root->addLayout(buttonLayout);
}

SkyDeviceConfigDialog::SerialRow SkyDeviceConfigDialog::createSerialRow(QFormLayout *layout, const QString&)
{
    SerialRow row;
    row.enabled = new QCheckBox(this);
    row.port = new QLineEdit(this);
    row.baud = new QSpinBox(this);
    row.baud->setRange(1200, 4000000);
    row.frequency = new QDoubleSpinBox(this);
    row.frequency->setRange(0.1, 1000.0);
    row.frequency->setDecimals(1);
    layout->addRow(QStringLiteral("Enabled"), row.enabled);
    layout->addRow(QStringLiteral("Port"), row.port);
    layout->addRow(QStringLiteral("Baud"), row.baud);
    layout->addRow(QStringLiteral("Frequency Hz"), row.frequency);
    return row;
}

void SkyDeviceConfigDialog::setConfig(const SkyConfig& config)
{
    setSerialRow(epsilon_, config.epsilon);
    setSerialRow(ptb_, config.ptb);
    setSerialRow(hmp_, config.hmp);
    setSerialRow(lidar_, config.lidar);
    wave_enabled_->setChecked(config.wave_tcp.enabled);
    wave_host_->setText(config.wave_tcp.host);
    wave_port_->setValue(config.wave_tcp.port);
    wave_frequency_->setValue(config.wave_tcp.frequency_hz);
    wave_downsample_->setValue(config.wave_tcp.downsample_ratio);
    telemetry_basic_rate_->setValue(config.telemetry.basic_rate_hz);
    telemetry_feature_rate_->setValue(config.telemetry.feature_rate_hz);
    telemetry_waveform_rate_->setValue(config.telemetry.waveform_rate_hz);
    telemetry_heartbeat_rate_->setValue(config.telemetry.heartbeat_rate_hz);
    telemetry_status_rate_->setValue(config.telemetry.status_rate_hz);
}

SkyConfig SkyDeviceConfigDialog::currentConfigFromUi() const
{
    SkyConfig config;
    config.epsilon = serialConfigFromRow(epsilon_);
    config.ptb = serialConfigFromRow(ptb_);
    config.hmp = serialConfigFromRow(hmp_);
    config.lidar = serialConfigFromRow(lidar_);
    config.wave_tcp.enabled = wave_enabled_->isChecked();
    config.wave_tcp.host = wave_host_->text().trimmed();
    config.wave_tcp.port = wave_port_->value();
    config.wave_tcp.frequency_hz = wave_frequency_->value();
    config.wave_tcp.downsample_ratio = wave_downsample_->value();
    config.telemetry.basic_rate_hz = telemetry_basic_rate_->value();
    config.telemetry.feature_rate_hz = telemetry_feature_rate_->value();
    config.telemetry.waveform_rate_hz = telemetry_waveform_rate_->value();
    config.telemetry.heartbeat_rate_hz = telemetry_heartbeat_rate_->value();
    config.telemetry.status_rate_hz = telemetry_status_rate_->value();
    return config;
}

void SkyDeviceConfigDialog::setSerialRow(const SerialRow& row, const SerialDeviceConfig& config)
{
    row.enabled->setChecked(config.enabled);
    row.port->setText(config.port);
    row.baud->setValue(config.baud_rate);
    row.frequency->setValue(config.frequency_hz);
}

SerialDeviceConfig SkyDeviceConfigDialog::serialConfigFromRow(const SerialRow& row) const
{
    SerialDeviceConfig config;
    config.enabled = row.enabled->isChecked();
    config.port = row.port->text().trimmed();
    config.baud_rate = row.baud->value();
    config.frequency_hz = row.frequency->value();
    return config;
}

void SkyDeviceConfigDialog::updateTexts()
{
    setWindowTitle(is_english_ ? "Sky Device Config" : "天空端设备配置");
    read_button_->setText(is_english_ ? "Read From Sky" : "读取天空端配置");
    apply_button_->setText(is_english_ ? "Apply Config" : "应用配置");
    save_button_->setText(is_english_ ? "Save To Sky" : "保存到天空端");
    close_button_->setText(is_english_ ? "Close" : "关闭");
}

}  // namespace VaporView
