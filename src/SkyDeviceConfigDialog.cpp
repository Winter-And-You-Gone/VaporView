#include "SkyDeviceConfigDialog.h"

#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QMessageBox>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace VaporView
{
namespace
{
constexpr int kFieldDigitCount = 20;
constexpr int kFieldHeight = 36;

QLabel *addLabeledRow(QFormLayout *layout, const QString& text, QWidget *widget)
{
    auto *label = new QLabel(text);
    layout->addRow(label, widget);
    return label;
}

int skyConfigFieldWidth(QWidget *widget)
{
    const QFontMetrics metrics(widget->font());
    return metrics.horizontalAdvance(QString(kFieldDigitCount, QLatin1Char('8'))) + 52;
}

void polishConfigField(QWidget *widget)
{
    if (!widget)
    {
        return;
    }
    const int width = skyConfigFieldWidth(widget);
    widget->setMinimumWidth(width);
    widget->setMaximumWidth(width);
    widget->setMinimumHeight(kFieldHeight);
    widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void setupFormLayout(QFormLayout *layout)
{
    layout->setContentsMargins(16, 18, 16, 14);
    layout->setSpacing(10);
    layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
}
}

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
    setMinimumSize(980, 680);
    setStyleSheet(QStringLiteral(
        "SkyDeviceConfigDialog { background-color: #f3f5f7; }"
        "QGroupBox { background-color: #ffffff; border: 1px solid #dfe4ea; border-radius: 8px; margin-top: 12px; font-size: 15px; font-weight: bold; color: #1976d2; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 14px; padding: 0 8px; background-color: #ffffff; }"
        "QLabel { color: #1f2a35; font-size: 14px; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox { background-color: #ffffff; border: 1px solid #d9dde3; border-radius: 6px; padding: 4px 28px 4px 10px; min-height: 28px; color: #111827; font-size: 14px; }"
        "QLineEdit { padding-right: 10px; }"
        "QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: #b7c0cc; }"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 2px solid #1976d2; }"
        "QSpinBox::up-button, QDoubleSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::down-button { width: 22px; border: none; background: transparent; subcontrol-origin: border; }"
        "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: 6px; }"
        "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-position: bottom right; border-bottom-right-radius: 6px; }"
        "QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background-color: #eef4fb; }"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { width: 0; height: 0; border-left: 4px solid transparent; border-right: 4px solid transparent; border-bottom: 5px solid #667085; }"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { width: 0; height: 0; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid #667085; }"
        "QCheckBox::indicator { width: 22px; height: 22px; border-radius: 5px; border: 1px solid #cbd5e1; background-color: #ffffff; }"
        "QCheckBox::indicator:checked { background-color: #1976d2; border-color: #1976d2; }"
        "QPlainTextEdit { background-color: #ffffff; border: 1px solid #dfe4ea; border-radius: 8px; padding: 8px; font-family: Consolas, \"Cascadia Mono\", monospace; font-size: 13px; }"
    ));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 16);
    root->setSpacing(12);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(12);

    auto *deviceGrid = new QGridLayout();
    deviceGrid->setContentsMargins(0, 0, 0, 0);
    deviceGrid->setHorizontalSpacing(12);
    deviceGrid->setVerticalSpacing(12);
    for (int column = 0; column < 3; ++column)
    {
        deviceGrid->setColumnStretch(column, 1);
    }
    contentLayout->addLayout(deviceGrid);

    auto addSerialGroup = [this, deviceGrid](const QString& title, QGroupBox*& group, SerialRow& row, int rowIndex, int columnIndex) {
        group = new QGroupBox(title, this);
        auto *layout = new QFormLayout(group);
        setupFormLayout(layout);
        row = createSerialRow(layout, title);
        deviceGrid->addWidget(group, rowIndex, columnIndex);
    };

    addSerialGroup(QStringLiteral("EPSILON"), epsilon_group_, epsilon_, 0, 0);
    addSerialGroup(QStringLiteral("PTB210"), ptb_group_, ptb_, 0, 1);
    addSerialGroup(QStringLiteral("HMP3"), hmp_group_, hmp_, 0, 2);
    addSerialGroup(QStringLiteral("TFA1500-L"), lidar_group_, lidar_, 1, 0);

    wave_group_ = new QGroupBox(QStringLiteral("Wave TCP"), this);
    auto *waveLayout = new QFormLayout(wave_group_);
    setupFormLayout(waveLayout);
    wave_enabled_ = new QCheckBox(this);
    wave_host_ = new QLineEdit(this);
    wave_port_ = new QSpinBox(this);
    wave_port_->setRange(1, 65535);
    wave_frequency_ = new QDoubleSpinBox(this);
    wave_frequency_->setRange(0.1, 1000.0);
    wave_frequency_->setDecimals(1);
    wave_downsample_ = new QSpinBox(this);
    wave_downsample_->setRange(1, 1000);
    for (QWidget *field : {static_cast<QWidget*>(wave_host_), static_cast<QWidget*>(wave_port_), static_cast<QWidget*>(wave_frequency_), static_cast<QWidget*>(wave_downsample_)})
    {
        polishConfigField(field);
    }
    wave_enabled_label_ = addLabeledRow(waveLayout, QStringLiteral("启用"), wave_enabled_);
    wave_host_label_ = addLabeledRow(waveLayout, QStringLiteral("主机"), wave_host_);
    wave_port_label_ = addLabeledRow(waveLayout, QStringLiteral("端口"), wave_port_);
    wave_frequency_label_ = addLabeledRow(waveLayout, QStringLiteral("频率 Hz"), wave_frequency_);
    wave_downsample_label_ = addLabeledRow(waveLayout, QStringLiteral("降采样倍率"), wave_downsample_);
    deviceGrid->addWidget(wave_group_, 1, 1);

    telemetry_group_ = new QGroupBox(QStringLiteral("数传配置"), this);
    auto *telemetryLayout = new QFormLayout(telemetry_group_);
    setupFormLayout(telemetryLayout);
    auto makeRate = [this]() {
        auto *spin = new QDoubleSpinBox(this);
        spin->setRange(0.1, 200.0);
        spin->setDecimals(1);
        polishConfigField(spin);
        return spin;
    };
    telemetry_basic_rate_ = makeRate();
    telemetry_feature_rate_ = makeRate();
    telemetry_waveform_rate_ = makeRate();
    telemetry_heartbeat_rate_ = makeRate();
    telemetry_status_rate_ = makeRate();
    telemetry_basic_label_ = addLabeledRow(telemetryLayout, QStringLiteral("基础遥测 Hz"), telemetry_basic_rate_);
    telemetry_feature_label_ = addLabeledRow(telemetryLayout, QStringLiteral("特征值 Hz"), telemetry_feature_rate_);
    telemetry_waveform_label_ = addLabeledRow(telemetryLayout, QStringLiteral("波形 Hz"), telemetry_waveform_rate_);
    telemetry_heartbeat_label_ = addLabeledRow(telemetryLayout, QStringLiteral("心跳 Hz"), telemetry_heartbeat_rate_);
    telemetry_status_label_ = addLabeledRow(telemetryLayout, QStringLiteral("状态 Hz"), telemetry_status_rate_);
    deviceGrid->addWidget(telemetry_group_, 1, 2);

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
    polishConfigField(row.port);
    polishConfigField(row.baud);
    polishConfigField(row.frequency);
    row.enabled_label = addLabeledRow(layout, QStringLiteral("启用"), row.enabled);
    row.port_label = addLabeledRow(layout, QStringLiteral("串口"), row.port);
    row.baud_label = addLabeledRow(layout, QStringLiteral("波特率"), row.baud);
    row.frequency_label = addLabeledRow(layout, QStringLiteral("频率 Hz"), row.frequency);
    return row;
}

void SkyDeviceConfigDialog::setConfig(const SkyConfig& config)
{
    current_config_ = config;
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
    SkyConfig config = current_config_;
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
    auto updateSerialLabels = [this](const SerialRow& row) {
        if (row.enabled_label) row.enabled_label->setText(is_english_ ? QStringLiteral("Enabled") : QStringLiteral("启用"));
        if (row.port_label) row.port_label->setText(is_english_ ? QStringLiteral("Port") : QStringLiteral("串口"));
        if (row.baud_label) row.baud_label->setText(is_english_ ? QStringLiteral("Baud") : QStringLiteral("波特率"));
        if (row.frequency_label) row.frequency_label->setText(is_english_ ? QStringLiteral("Frequency Hz") : QStringLiteral("频率 Hz"));
    };
    if (epsilon_group_) epsilon_group_->setTitle(QStringLiteral("EPSILON"));
    if (ptb_group_) ptb_group_->setTitle(QStringLiteral("PTB210"));
    if (hmp_group_) hmp_group_->setTitle(QStringLiteral("HMP3"));
    if (lidar_group_) lidar_group_->setTitle(QStringLiteral("TFA1500-L"));
    if (wave_group_) wave_group_->setTitle(QStringLiteral("Wave TCP"));
    if (telemetry_group_) telemetry_group_->setTitle(is_english_ ? QStringLiteral("Telemetry") : QStringLiteral("数传配置"));
    updateSerialLabels(epsilon_);
    updateSerialLabels(ptb_);
    updateSerialLabels(hmp_);
    updateSerialLabels(lidar_);
    if (wave_enabled_label_) wave_enabled_label_->setText(is_english_ ? QStringLiteral("Enabled") : QStringLiteral("启用"));
    if (wave_host_label_) wave_host_label_->setText(is_english_ ? QStringLiteral("Host") : QStringLiteral("主机"));
    if (wave_port_label_) wave_port_label_->setText(is_english_ ? QStringLiteral("Port") : QStringLiteral("端口"));
    if (wave_frequency_label_) wave_frequency_label_->setText(is_english_ ? QStringLiteral("Frequency Hz") : QStringLiteral("频率 Hz"));
    if (wave_downsample_label_) wave_downsample_label_->setText(is_english_ ? QStringLiteral("Downsample") : QStringLiteral("降采样倍率"));
    if (telemetry_basic_label_) telemetry_basic_label_->setText(is_english_ ? QStringLiteral("Basic Hz") : QStringLiteral("基础遥测 Hz"));
    if (telemetry_feature_label_) telemetry_feature_label_->setText(is_english_ ? QStringLiteral("Feature Hz") : QStringLiteral("特征值 Hz"));
    if (telemetry_waveform_label_) telemetry_waveform_label_->setText(is_english_ ? QStringLiteral("Waveform Hz") : QStringLiteral("波形 Hz"));
    if (telemetry_heartbeat_label_) telemetry_heartbeat_label_->setText(is_english_ ? QStringLiteral("Heartbeat Hz") : QStringLiteral("心跳 Hz"));
    if (telemetry_status_label_) telemetry_status_label_->setText(is_english_ ? QStringLiteral("Status Hz") : QStringLiteral("状态 Hz"));
    read_button_->setText(is_english_ ? "Read From Sky" : "读取天空端配置");
    apply_button_->setText(is_english_ ? "Apply Config" : "应用配置");
    save_button_->setText(is_english_ ? "Save To Sky" : "保存到天空端");
    close_button_->setText(is_english_ ? "Close" : "关闭");
}

}  // namespace VaporView
