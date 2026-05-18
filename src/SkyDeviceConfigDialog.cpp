#include "SkyDeviceConfigDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFontMetrics>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSizePolicy>
#include <QSvgRenderer>
#include <QVBoxLayout>
#include <QApplication>

namespace VaporView
{
namespace
{
constexpr int kFieldDigitCount = 20;
constexpr int kFieldHeight = 36;
constexpr int kEnableToggleSize = 30;
constexpr int kEnableToggleIconSize = 16;
const QColor kEnableToggleOnIcon(255, 255, 255);
const QColor kEnableToggleOffIcon(180, 35, 24);

QLabel *addLabeledRow(QFormLayout *layout, const QString& text, QWidget *widget)
{
    auto *label = new QLabel(text);
    layout->addRow(label, widget);
    return label;
}

QString findResourceFile(const QString& relativePath)
{
    const QString appDir = QApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(relativePath),
        QDir(appDir).filePath(QStringLiteral("../") + relativePath),
        QDir(appDir).filePath(QStringLiteral("../../") + relativePath)
    };

    for (const QString& path : candidates)
    {
        if (QFileInfo::exists(path))
        {
            return path;
        }
    }
    return QString();
}

QPixmap renderLucidePixmap(const QByteArray& svgData, const QColor& color)
{
    QByteArray tinted = svgData;
    tinted.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(tinted);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(2, 2, 28, 28));
    return pixmap;
}

QIcon createLucideIcon(const QString& iconName, const QColor& color)
{
    QFile file(findResourceFile(QStringLiteral("resources/lucide/%1.svg").arg(iconName)));
    if (!file.open(QIODevice::ReadOnly))
    {
        return QIcon();
    }

    QIcon icon;
    icon.addPixmap(renderLucidePixmap(file.readAll(), color), QIcon::Normal);
    return icon;
}

QIcon enableToggleIcon(bool enabled)
{
    static const QIcon onIcon = createLucideIcon(QStringLiteral("plug"), kEnableToggleOnIcon);
    static const QIcon offIcon = createLucideIcon(QStringLiteral("unplug"), kEnableToggleOffIcon);
    return enabled ? onIcon : offIcon;
}

void configureEnableToggleButton(QPushButton *button)
{
    if (!button)
    {
        return;
    }
    button->setCheckable(true);
    button->setFixedSize(kEnableToggleSize, kEnableToggleSize);
    button->setIconSize(QSize(kEnableToggleIconSize, kEnableToggleIconSize));
    button->setCursor(Qt::PointingHandCursor);
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

void applyComboText(QComboBox *combo, const QString& value)
{
    const int index = combo->findText(value);
    if (index >= 0)
    {
        combo->setCurrentIndex(index);
    }
    else
    {
        combo->setCurrentText(value);
    }
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
    refreshSerialPortOptions();
    applyDynamicMetrics();
}

void SkyDeviceConfigDialog::setEnglish(bool english)
{
    is_english_ = english;
    updateTexts();
}

void SkyDeviceConfigDialog::setFontScale(int percent)
{
    if (percent < 70 || percent > 150)
    {
        return;
    }
    font_scale_percent_ = percent;
    applyDynamicMetrics();
}

void SkyDeviceConfigDialog::onReadClicked()
{
    refreshSerialPortOptions();
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
    setObjectName(QStringLiteral("skyDeviceConfigDialog"));
    setStyleSheet(QStringLiteral(
        "QDialog#skyDeviceConfigDialog { background-color: #f3f5f7; }"
        "QDialog#skyDeviceConfigDialog QGroupBox { background-color: #ffffff; border: 1px solid #dfe4ea; border-radius: 8px; margin-top: 12px; font-weight: bold; color: #1976d2; }"
        "QDialog#skyDeviceConfigDialog QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 14px; padding: 0 8px; background-color: #ffffff; }"
        "QDialog#skyDeviceConfigDialog QLabel { color: #1f2a35; }"
        "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle { background-color: #ffffff; color: #b42318; border: 1px solid #cbd5e1; border-radius: 5px; padding: 0; min-width: 30px; max-width: 30px; min-height: 30px; max-height: 30px; }"
        "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle:hover { background-color: #f8fafc; border-color: #94a3b8; }"
        "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle:checked { background-color: #1976d2; color: #ffffff; border-color: #1976d2; }"
        "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle:checked:hover { background-color: #1565c0; border-color: #1565c0; }"
        "QDialog#skyDeviceConfigDialog QPlainTextEdit { background-color: #ffffff; border: 1px solid #dfe4ea; border-radius: 8px; padding: 8px; font-family: Consolas, \"Cascadia Mono\", monospace; }"
    ));
    setFont(qApp->font());
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
    wave_enabled_ = new QPushButton(this);
    wave_enabled_->setObjectName(QStringLiteral("skyEnableToggle"));
    configureEnableToggleButton(wave_enabled_);
    connect(wave_enabled_, &QPushButton::toggled, this, [this](bool) {
        updateEnableButton(wave_enabled_);
    });
    wave_host_ = new QLineEdit(this);
    wave_port_ = new QSpinBox(this);
    wave_port_->setRange(1, 65535);
    wave_downsample_ = new QSpinBox(this);
    wave_downsample_->setRange(1, 1000);
    for (QWidget *field : {static_cast<QWidget*>(wave_host_), static_cast<QWidget*>(wave_port_), static_cast<QWidget*>(wave_downsample_)})
    {
        polishConfigField(field);
    }
    wave_enabled_label_ = addLabeledRow(waveLayout, QStringLiteral("启用"), wave_enabled_);
    wave_host_label_ = addLabeledRow(waveLayout, QStringLiteral("主机"), wave_host_);
    wave_port_label_ = addLabeledRow(waveLayout, QStringLiteral("端口"), wave_port_);
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
    row.enabled = new QPushButton(this);
    row.enabled->setObjectName(QStringLiteral("skyEnableToggle"));
    configureEnableToggleButton(row.enabled);
    connect(row.enabled, &QPushButton::toggled, this, [this, button = row.enabled](bool) {
        updateEnableButton(button);
    });
    row.port = new QComboBox(this);
    row.port->setEditable(true);
    row.port->setInsertPolicy(QComboBox::NoInsert);
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
    updateEnableButton(wave_enabled_);
    wave_host_->setText(config.wave_tcp.host);
    wave_port_->setValue(config.wave_tcp.port);
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
    updateEnableButton(row.enabled);
    applyComboText(row.port, config.port);
    row.baud->setValue(config.baud_rate);
    row.frequency->setValue(config.frequency_hz);
}

SerialDeviceConfig SkyDeviceConfigDialog::serialConfigFromRow(const SerialRow& row) const
{
    SerialDeviceConfig config;
    config.enabled = row.enabled->isChecked();
    config.port = row.port->currentText().trimmed();
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
    if (wave_downsample_label_) wave_downsample_label_->setText(is_english_ ? QStringLiteral("Downsample") : QStringLiteral("降采样倍率"));
    if (telemetry_basic_label_) telemetry_basic_label_->setText(is_english_ ? QStringLiteral("Basic Hz") : QStringLiteral("基础遥测 Hz"));
    if (telemetry_feature_label_) telemetry_feature_label_->setText(is_english_ ? QStringLiteral("Feature Hz") : QStringLiteral("特征值 Hz"));
    if (telemetry_waveform_label_) telemetry_waveform_label_->setText(is_english_ ? QStringLiteral("Waveform Hz") : QStringLiteral("波形 Hz"));
    if (telemetry_heartbeat_label_) telemetry_heartbeat_label_->setText(is_english_ ? QStringLiteral("Heartbeat Hz") : QStringLiteral("心跳 Hz"));
    if (telemetry_status_label_) telemetry_status_label_->setText(is_english_ ? QStringLiteral("Status Hz") : QStringLiteral("状态 Hz"));
    updateEnableButton(epsilon_.enabled);
    updateEnableButton(ptb_.enabled);
    updateEnableButton(hmp_.enabled);
    updateEnableButton(lidar_.enabled);
    updateEnableButton(wave_enabled_);
    read_button_->setText(is_english_ ? "Read From Sky" : "读取天空端配置");
    apply_button_->setText(is_english_ ? "Apply Config" : "应用配置");
    save_button_->setText(is_english_ ? "Save To Sky" : "保存到天空端");
    close_button_->setText(is_english_ ? "Close" : "关闭");
}

void SkyDeviceConfigDialog::refreshSerialPortOptions()
{
    QStringList ports;
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts())
    {
        ports.push_back(info.portName());
    }
    ports.removeDuplicates();
    ports.sort(Qt::CaseInsensitive);

    auto refreshCombo = [&ports](QComboBox *combo) {
        if (!combo)
        {
            return;
        }
        const QString current = combo->currentText();
        combo->blockSignals(true);
        combo->clear();
        combo->addItems(ports);
        combo->setCurrentText(current);
        combo->blockSignals(false);
    };

    refreshCombo(epsilon_.port);
    refreshCombo(ptb_.port);
    refreshCombo(hmp_.port);
    refreshCombo(lidar_.port);
}

void SkyDeviceConfigDialog::updateEnableButton(QPushButton *button)
{
    if (!button)
    {
        return;
    }
    button->setText(QString());
    button->setIcon(enableToggleIcon(button->isChecked()));
    button->setIconSize(QSize(kEnableToggleIconSize, kEnableToggleIconSize));
    button->setToolTip(button->isChecked()
                           ? (is_english_ ? QStringLiteral("Enabled") : QStringLiteral("已启用"))
                           : (is_english_ ? QStringLiteral("Disabled") : QStringLiteral("已禁用")));
}

void SkyDeviceConfigDialog::applyDynamicMetrics()
{
    setFont(qApp->font());
    const QList<QWidget*> fields = {
        epsilon_.port, epsilon_.baud, epsilon_.frequency,
        ptb_.port, ptb_.baud, ptb_.frequency,
        hmp_.port, hmp_.baud, hmp_.frequency,
        lidar_.port, lidar_.baud, lidar_.frequency,
        wave_host_, wave_port_, wave_downsample_,
        telemetry_basic_rate_, telemetry_feature_rate_, telemetry_waveform_rate_,
        telemetry_heartbeat_rate_, telemetry_status_rate_
    };
    for (QWidget *field : fields)
    {
        polishConfigField(field);
    }
}

}  // namespace VaporView
