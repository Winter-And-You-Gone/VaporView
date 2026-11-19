#include "ground/main/GatewaySettingsDialog.h"
#include "shared/config/ApplicationConfig.h"
#include "shared/config/SettingsWriteBarrier.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFont>
#include <QGroupBox>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QUuid>
#include <QVBoxLayout>

namespace VaporView::Ground::Main
{
namespace
{
constexpr qint64 kMiB = 1024 * 1024;
quint16 commandValue(CommandId command) { return static_cast<quint16>(command); }
const QList<CommandId> kCommands{CommandId::RequestStatus, CommandId::StartRecording, CommandId::StopRecording};

QSet<quint16> loadAllowedCommands(const QVariant& value)
{
    QSet<quint16> result;
    const auto allowed = value.toStringList();
    for (const auto command : kCommands)
        if (allowed.contains(QString::number(commandValue(command))))
            result.insert(commandValue(command));
    return result;
}

QStringList saveAllowedCommands(const QSet<quint16>& commands)
{
    QStringList result;
    for (const auto command : kCommands)
        if (commands.contains(commandValue(command))) result.append(QString::number(commandValue(command)));
    return result;
}
}

GatewaySettings loadGatewaySettings()
{
    auto persisted = applicationConfigSettings();
    persisted.beginGroup(QStringLiteral("Gateway"));
    GatewaySettings result;
    auto& config = result.connection;
    config.enabled = persisted.value(QStringLiteral("enabled"), false).toBool();
    config.serverUrl = QUrl(persisted.value(QStringLiteral("server_url")).toString());
    config.deviceId = persisted.value(QStringLiteral("device_id"),
        QUuid::createUuid().toString(QUuid::WithoutBraces)).toString();
    config.token = persisted.value(QStringLiteral("token")).toString();
    config.allowRemoteCommands = persisted.value(QStringLiteral("allow_remote_commands"), false).toBool();
    const QStringList defaultCommands{QString::number(commandValue(CommandId::RequestStatus))};
    config.allowedCommands = loadAllowedCommands(persisted.value(QStringLiteral("allowed_commands"), defaultCommands));
    config.maxCacheBytes = qBound<qint64>(kMiB,
        persisted.value(QStringLiteral("max_cache_bytes"), config.maxCacheBytes).toLongLong(), 4096 * kMiB);
    config.retentionHours = qBound(1, persisted.value(QStringLiteral("retention_hours"), 24).toInt(), 720);
    auto& listener = config.listener;
    listener.enabled = persisted.value(QStringLiteral("listener_enabled"), false).toBool();
    listener.address = persisted.value(QStringLiteral("listener_address"), listener.address).toString();
    listener.port = quint16(qBound(1, persisted.value(QStringLiteral("listener_port"), listener.port).toInt(), 65535));
    result.closeToTray = persisted.value(QStringLiteral("close_to_tray"), false).toBool();
    return result;
}

void saveGatewaySettings(const GatewaySettings& value)
{
    auto persisted = applicationConfigSettings();
    persisted.beginGroup(QStringLiteral("Gateway"));
    const auto& config = value.connection;
    setPersistentSetting(persisted, QStringLiteral("enabled"), config.enabled);
    setPersistentSetting(persisted, QStringLiteral("server_url"), config.serverUrl.toString(QUrl::FullyEncoded));
    setPersistentSetting(persisted, QStringLiteral("device_id"), config.deviceId);
    setPersistentSetting(persisted, QStringLiteral("token"), config.token);
    setPersistentSetting(persisted, QStringLiteral("allow_remote_commands"), config.allowRemoteCommands);
    setPersistentSetting(persisted, QStringLiteral("allowed_commands"), saveAllowedCommands(config.allowedCommands));
    setPersistentSetting(persisted, QStringLiteral("max_cache_bytes"), config.maxCacheBytes);
    setPersistentSetting(persisted, QStringLiteral("retention_hours"), config.retentionHours);
    setPersistentSetting(persisted, QStringLiteral("close_to_tray"), value.closeToTray);
    const auto& listener = config.listener;
    setPersistentSetting(persisted, QStringLiteral("listener_enabled"), listener.enabled);
    setPersistentSetting(persisted, QStringLiteral("listener_address"), listener.address);
    setPersistentSetting(persisted, QStringLiteral("listener_port"), listener.port);
    // Drop settings from the former HTTPS listener when saving the LAN gateway.
    for (const auto* key : {"listener_certificate_file", "listener_private_key_file", "listener_token",
                            "listener_allow_commands", "listener_allowed_commands"})
        removePersistentSetting(persisted, QString::fromLatin1(key));
}

GatewaySettingsDialog::GatewaySettingsDialog(const GatewaySettings& value,
    Gateway::GatewayService *service, bool english, QWidget *parent)
    : QDialog(parent), original_(value), english_(english)
{
    setObjectName(QStringLiteral("gatewaySettingsDialog"));
    setWindowTitle(english ? QStringLiteral("Gateway") : QStringLiteral("Gateway 网关"));
    resize(650, 700);
    auto *layout = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *body = new QVBoxLayout(content);
    auto *description = new QLabel(english
        ? QStringLiteral("Forward the existing SkyCore data stream to your server. Uploads and cache writes run in a separate worker thread.")
        : QStringLiteral("复用现有 SkyCore 数据流上传至服务器。云端通信与缓存写入在独立工作线程运行。"), content);
    description->setWordWrap(true);
    body->addWidget(description);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    enabled_ = new QCheckBox(english ? QStringLiteral("Enable cloud forwarding") : QStringLiteral("启用云端转发"), content);
    enabled_->setObjectName(QStringLiteral("gatewayEnabled"));
    enabled_->setChecked(value.connection.enabled);
    form->addRow(enabled_);
    server_url_ = new QLineEdit(value.connection.serverUrl.toString(), content);
    server_url_->setObjectName(QStringLiteral("gatewayServerUrl"));
    server_url_->setPlaceholderText(QStringLiteral("https://cloud.example.com/"));
    form->addRow(english ? QStringLiteral("Cloud server URL") : QStringLiteral("云端服务器地址"), server_url_);
    device_id_ = new QLineEdit(value.connection.deviceId, content);
    device_id_->setObjectName(QStringLiteral("gatewayDeviceId"));
    form->addRow(english ? QStringLiteral("Device ID") : QStringLiteral("设备标识"), device_id_);
    token_ = new QLineEdit(value.connection.token, content);
    token_->setObjectName(QStringLiteral("gatewayToken"));
    token_->setEchoMode(QLineEdit::Password);
    form->addRow(english ? QStringLiteral("Cloud access token") : QStringLiteral("云端访问令牌"), token_);
    cache_mib_ = new QSpinBox(content);
    cache_mib_->setObjectName(QStringLiteral("gatewayCacheMiB"));
    cache_mib_->setRange(1, 4096);
    cache_mib_->setSuffix(QStringLiteral(" MiB"));
    cache_mib_->setValue(static_cast<int>(value.connection.maxCacheBytes / kMiB));
    form->addRow(english ? QStringLiteral("Cache limit") : QStringLiteral("缓存容量上限"), cache_mib_);
    retention_hours_ = new QSpinBox(content);
    retention_hours_->setObjectName(QStringLiteral("gatewayRetentionHours"));
    retention_hours_->setRange(1, 720);
    retention_hours_->setSuffix(english ? QStringLiteral(" h") : QStringLiteral(" 小时"));
    retention_hours_->setValue(value.connection.retentionHours);
    form->addRow(english ? QStringLiteral("Cache retention") : QStringLiteral("缓存保留时间"), retention_hours_);
    body->addLayout(form);

    auto *commands = new QGroupBox(content);
    commands->setObjectName(QStringLiteral("gatewayCommandPermissions"));
    commands->setStyleSheet(QStringLiteral("QGroupBox#gatewayCommandPermissions { border-top-width: 1px; padding-top: 8px; }"));
    auto *commandLayout = new QVBoxLayout(commands);
    auto *commandTitle = new QLabel(english ? QStringLiteral("Cloud command permissions") : QStringLiteral("云端命令权限"), commands);
    QFont titleFont = commandTitle->font();
    titleFont.setBold(true);
    commandTitle->setFont(titleFont);
    commandLayout->addWidget(commandTitle);
    remote_commands_ = new QCheckBox(english ? QStringLiteral("Allow remote commands") : QStringLiteral("允许云端下发命令"), commands);
    remote_commands_->setObjectName(QStringLiteral("gatewayRemoteCommands"));
    remote_commands_->setChecked(value.connection.allowRemoteCommands);
    commandLayout->addWidget(remote_commands_);
    auto addCommand = [&](CommandId command, const QString& name, const QString& objectName) {
        auto *check = new QCheckBox(name, commands);
        check->setObjectName(objectName);
        check->setChecked(value.connection.allowedCommands.contains(commandValue(command)));
        check->setEnabled(remote_commands_->isChecked());
        connect(remote_commands_, &QCheckBox::toggled, check, &QWidget::setEnabled);
        commandLayout->addWidget(check);
        return check;
    };
    status_command_ = addCommand(CommandId::RequestStatus, english ? QStringLiteral("Read device status") : QStringLiteral("读取设备状态"), QStringLiteral("gatewayAllowStatus"));
    start_command_ = addCommand(CommandId::StartRecording, english ? QStringLiteral("Start / resume SkyCore recording") : QStringLiteral("启动 / 恢复 SkyCore 记录"), QStringLiteral("gatewayAllowStartRecording"));
    stop_command_ = addCommand(CommandId::StopRecording, english ? QStringLiteral("Stop SkyCore recording") : QStringLiteral("停止 SkyCore 记录"), QStringLiteral("gatewayAllowStopRecording"));
    body->addWidget(commands);

    const auto& listenerConfig = value.connection.listener;
    auto *listener = new QGroupBox(english ? QStringLiteral("Inbound HTTP / WebSocket") : QStringLiteral("入站 HTTP / WebSocket"), content);
    auto *listenerForm = new QFormLayout(listener);
    listener_enabled_ = new QCheckBox(english ? QStringLiteral("Enable inbound listener") : QStringLiteral("启用入站监听"), listener);
    listener_enabled_->setObjectName(QStringLiteral("gatewayListenerEnabled"));
    listener_enabled_->setChecked(listenerConfig.enabled);
    listenerForm->addRow(listener_enabled_);
    listener_address_ = new QLineEdit(listenerConfig.address, listener);
    listener_address_->setObjectName(QStringLiteral("gatewayListenerAddress"));
    listenerForm->addRow(english ? QStringLiteral("Bind IP") : QStringLiteral("监听 IP"), listener_address_);
    listener_port_ = new QSpinBox(listener);
    listener_port_->setObjectName(QStringLiteral("gatewayListenerPort"));
    listener_port_->setRange(1, 65535);
    listener_port_->setValue(listenerConfig.port);
    listenerForm->addRow(english ? QStringLiteral("Port") : QStringLiteral("端口"), listener_port_);
    auto *listenerStatus = new QLabel(service ? service->listenerStatusText() : QString(), listener);
    listenerStatus->setObjectName(QStringLiteral("gatewayListenerStatus"));
    listenerStatus->setWordWrap(true);
    listenerForm->addRow(listenerStatus);
    if (service) connect(service, &Gateway::GatewayService::listenerStatusChanged, listenerStatus, &QLabel::setText);
    body->addWidget(listener);

    close_to_tray_ = new QCheckBox(english
        ? QStringLiteral("Keep running in the system tray when the main window is closed")
        : QStringLiteral("关闭主窗口时留在系统托盘，继续运行"), content);
    close_to_tray_->setObjectName(QStringLiteral("gatewayCloseToTray"));
    close_to_tray_->setChecked(value.closeToTray);
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        close_to_tray_->setToolTip(english ? QStringLiteral("The system tray is currently unavailable; closing will exit normally.")
            : QStringLiteral("当前系统托盘不可用；关闭窗口时将正常退出。"));
    body->addWidget(close_to_tray_);

    auto *status = new QLabel(service ? service->statusText() : QString(), content);
    status->setObjectName(QStringLiteral("gatewayStatus"));
    status->setWordWrap(true);
    body->addWidget(status);
    if (service) connect(service, &Gateway::GatewayService::statusChanged, status, &QLabel::setText);
    auto *sync = new QPushButton(english ? QStringLiteral("Sync a completed local Session…") : QStringLiteral("同步已结束的本地 Session…"), content);
    sync->setObjectName(QStringLiteral("gatewaySyncSession"));
    sync->setEnabled(service && service->config().enabled);
    connect(sync, &QPushButton::clicked, this, [this] {
        const QString directory = QFileDialog::getExistingDirectory(this, english_
            ? QStringLiteral("Select a completed local Session") : QStringLiteral("选择已结束的本地 Session"));
        if (!directory.isEmpty()) emit sessionSyncRequested(directory);
    });
    body->addWidget(sync);
    auto *sessionHint = new QLabel(english
        ? QStringLiteral("Save and enable Gateway before syncing. After editing the connection, save and reopen this dialog. Select one completed local Session directory; download SkyCore Sessions to this computer first.")
        : QStringLiteral("请先保存并启用 Gateway。修改连接配置后，保存并重新打开本窗口再同步。请选择一个已结束的本地 Session 目录；SkyCore 端 Session 需先下载到本机。"), content);
    sessionHint->setWordWrap(true);
    body->addWidget(sessionHint);
    for (auto *input : {server_url_, device_id_, token_})
        connect(input, &QLineEdit::textChanged, sync, [sync] { sync->setEnabled(false); });
    connect(enabled_, &QCheckBox::toggled, sync, [sync] { sync->setEnabled(false); });
    validation_ = new QLabel(content);
    validation_->setObjectName(QStringLiteral("gatewayValidationError"));
    validation_->setWordWrap(true);
    validation_->hide();
    body->addWidget(validation_);
    body->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("gatewayDialogButtons"));
    buttons->button(QDialogButtonBox::Save)->setText(english ? QStringLiteral("Save") : QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(english ? QStringLiteral("Cancel") : QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &GatewaySettingsDialog::acceptSettings);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

GatewaySettings GatewaySettingsDialog::settings() const
{
    auto result = original_;
    auto& config = result.connection;
    config.enabled = enabled_->isChecked();
    config.serverUrl = QUrl(server_url_->text().trimmed());
    config.deviceId = device_id_->text().trimmed();
    config.token = token_->text();
    config.allowRemoteCommands = remote_commands_->isChecked();
    config.allowedCommands.clear();
    if (status_command_->isChecked()) config.allowedCommands.insert(commandValue(CommandId::RequestStatus));
    if (start_command_->isChecked()) config.allowedCommands.insert(commandValue(CommandId::StartRecording));
    if (stop_command_->isChecked()) config.allowedCommands.insert(commandValue(CommandId::StopRecording));
    config.maxCacheBytes = cache_mib_->value() * kMiB;
    config.retentionHours = retention_hours_->value();
    auto& listener = config.listener;
    listener.enabled = listener_enabled_->isChecked();
    listener.address = listener_address_->text().trimmed();
    listener.port = quint16(listener_port_->value());
    result.closeToTray = close_to_tray_->isChecked();
    return result;
}

void GatewaySettingsDialog::acceptSettings()
{
    const auto config = settings().connection;
    const auto url = config.serverUrl;
    const bool loopback = url.host().compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0 ||
        QHostAddress(url.host()).isLoopback();
    if (config.enabled && (!url.isValid() || url.host().isEmpty() ||
        (url.scheme() != QStringLiteral("https") && !(url.scheme() == QStringLiteral("http") && loopback)) ||
        !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()))
    {
        validation_->setText(english_ ? QStringLiteral("Enter an HTTPS server URL without credentials, query or fragment. HTTP is allowed only for local testing.")
            : QStringLiteral("请输入 HTTPS 服务器地址，不含账号密码、查询参数或片段。本机测试可使用 HTTP。"));
        validation_->show();
        server_url_->setFocus();
        return;
    }
    if (config.enabled && (config.deviceId.isEmpty() || config.token.trimmed().isEmpty()))
    {
        validation_->setText(english_ ? QStringLiteral("Device ID and access token are required.")
            : QStringLiteral("设备标识和访问令牌不能为空。"));
        validation_->show();
        (config.deviceId.isEmpty() ? device_id_ : token_)->setFocus();
        return;
    }
    if (config.listener.enabled)
    {
        const QString error = config.listener.validationError();
        if (!error.isEmpty() || config.deviceId.isEmpty() || config.deviceId.size() > 128)
        {
            validation_->setText(english_
                ? QStringLiteral("Enter a valid bind IP and a device ID of 1 to 128 characters.")
                : (error.isEmpty() ? QStringLiteral("请输入 1 至 128 个字符的设备标识") : error));
            validation_->show();
            return;
        }
    }
    accept();
}

} // namespace VaporView::Ground::Main
