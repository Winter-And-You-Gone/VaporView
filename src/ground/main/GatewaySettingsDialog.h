#pragma once

#include "ground/gateway/GatewayService.h"
#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace VaporView::Ground::Main
{

struct GatewaySettings
{
    Gateway::GatewayConfig connection;
    bool closeToTray = false;
};

GatewaySettings loadGatewaySettings();
void saveGatewaySettings(const GatewaySettings& settings);

class GatewaySettingsDialog final : public QDialog
{
    Q_OBJECT
public:
    GatewaySettingsDialog(const GatewaySettings& settings, Gateway::GatewayService *service,
                          bool english, QWidget *parent = nullptr);
    GatewaySettings settings() const;

signals:
    void sessionSyncRequested(const QString& directory);

private:
    void acceptSettings();
    GatewaySettings original_;
    bool english_;
    QCheckBox *enabled_;
    QLineEdit *server_url_;
    QLineEdit *device_id_;
    QLineEdit *token_;
    QCheckBox *remote_commands_;
    QCheckBox *status_command_;
    QCheckBox *start_command_;
    QCheckBox *stop_command_;
    QSpinBox *cache_mib_;
    QSpinBox *retention_hours_;
    QCheckBox *listener_enabled_;
    QLineEdit *listener_address_;
    QSpinBox *listener_port_;
    QLineEdit *listener_certificate_;
    QLineEdit *listener_key_;
    QLineEdit *listener_token_;
    QCheckBox *listener_commands_;
    QCheckBox *listener_status_command_;
    QCheckBox *listener_start_command_;
    QCheckBox *listener_stop_command_;
    QCheckBox *close_to_tray_;
    QLabel *validation_;
};

} // namespace VaporView::Ground::Main
