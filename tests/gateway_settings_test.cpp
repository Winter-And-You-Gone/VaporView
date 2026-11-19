#include "test_settings_sandbox.h"
#include "test_ui_helpers.h"
#include "ground/main/GatewaySettingsDialog.h"
#include "ground/main/MainWindow.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "shared/config/ApplicationConfig.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <cstdlib>
#include <iostream>

using namespace VaporView::Ground::Main;

namespace
{
void require(bool value, const char *message)
{
    if (!value)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template<class T> T *field(QObject& dialog, const char *name)
{
    auto *result = dialog.findChild<T *>(QString::fromLatin1(name));
    require(result != nullptr, name);
    return result;
}

void testPermissionsAndPersistence()
{
    const auto defaults = loadGatewaySettings();
    require(!defaults.connection.enabled && !defaults.connection.allowRemoteCommands && !defaults.closeToTray,
            "Cloud forwarding, cloud commands and tray background mode are opt-in");
    require(defaults.connection.allowedCommands == QSet<quint16>{static_cast<quint16>(VaporView::CommandId::RequestStatus)},
            "default command permission is status only");
    require(defaults.connection.listener.enabled && defaults.connection.listener.address == QStringLiteral("127.0.0.1") &&
            defaults.connection.listener.port == 8443, "ground listener defaults to loopback port 8443");
    GatewaySettings saved = defaults;
    saved.connection.deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    saved.connection.enabled = true;
    saved.connection.serverUrl = QUrl(QStringLiteral("https://cloud.example.invalid/device/"));
    saveGatewaySettings(saved);
    auto persisted = VaporView::applicationConfigSettings();
    const QString obsoleteTokenKey = QStringLiteral("Gateway/token");
    persisted.setValue(obsoleteTokenKey, QUuid::createUuid().toString(QUuid::WithoutBraces));
    persisted.sync();
    VaporView::setSettingsWritesSuspended(true);
    const auto migrated = loadGatewaySettings();
    VaporView::setSettingsWritesSuspended(false);
    persisted.sync();
    require(persisted.contains(obsoleteTokenKey), "settings write barrier covers obsolete token migration");
    require(migrated.connection.enabled && migrated.connection.deviceId == saved.connection.deviceId &&
            migrated.connection.serverUrl == saved.connection.serverUrl, "legacy HTTPS settings load without credentials");
    loadGatewaySettings();
    persisted.sync();
    require(!persisted.contains(obsoleteTokenKey), "loading old settings removes the obsolete token");
    GatewaySettingsDialog dialog(migrated, nullptr, false);
    require(dialog.findChild<QLineEdit *>(QStringLiteral("gatewayToken")) == nullptr,
            "cloud forwarding has no access token field");
    auto *allow = field<QCheckBox>(dialog, "gatewayRemoteCommands");
    auto *start = field<QCheckBox>(dialog, "gatewayAllowStartRecording");
    auto *stop = field<QCheckBox>(dialog, "gatewayAllowStopRecording");
    require(!start->isEnabled() && !start->isChecked() && !stop->isChecked(),
            "recording controls require explicit permission");
    allow->setChecked(true);
    require(start->isEnabled() && stop->isEnabled(), "command permission controls follow authorization");
    start->setChecked(true);
    const auto chosen = dialog.settings();
    require(chosen.connection.allowedCommands.contains(static_cast<quint16>(VaporView::CommandId::StartRecording)) &&
            !chosen.connection.allowedCommands.contains(static_cast<quint16>(VaporView::CommandId::StopRecording)),
            "starting and stopping recording are independently authorized");
    dialog.reject();
    require(!loadGatewaySettings().connection.allowRemoteCommands,
            "canceling the dialog does not persist permissions");
    VaporView::setSettingsWritesSuspended(true);
    saveGatewaySettings(chosen);
    VaporView::setSettingsWritesSuspended(false);
    require(!loadGatewaySettings().connection.allowRemoteCommands, "settings write barrier covers Gateway");
    saveGatewaySettings(chosen);
    const auto restored = loadGatewaySettings();
    require(restored.connection.deviceId == saved.connection.deviceId &&
            restored.connection.allowRemoteCommands &&
            restored.connection.allowedCommands == chosen.connection.allowedCommands,
            "Gateway settings restore the selected identity and permissions");
    require(restored.connection.listener == chosen.connection.listener, "inbound settings restore independently");
    saved.connection.enabled = defaults.connection.enabled;
    saveGatewaySettings(saved);
}

void testLanListenerSettings()
{
    auto stored = VaporView::applicationConfigSettings();
    stored.setValue(QStringLiteral("Gateway/listener_certificate_file"), QStringLiteral("missing.pem"));
    auto saved = loadGatewaySettings();
    saved.connection.enabled = false;
    GatewaySettingsDialog dialog(saved, nullptr, true);
    auto *enabled = field<QCheckBox>(dialog, "gatewayListenerEnabled");
    require(enabled->isChecked(), "LAN listener is selected by default");
    field<QLineEdit>(dialog, "gatewayListenerAddress")->setText(QStringLiteral("192.0.2.10"));
    field<QSpinBox>(dialog, "gatewayListenerPort")->setValue(8844);
    field<QDialogButtonBox>(dialog, "gatewayDialogButtons")->button(QDialogButtonBox::Save)->click();
    require(dialog.result() == QDialog::Accepted, "LAN listener accepts just address and port");
    saveGatewaySettings(dialog.settings());
    const auto listener = loadGatewaySettings().connection.listener;
    require(listener.enabled && listener.address == QStringLiteral("192.0.2.10") && listener.port == 8844,
            "LAN listener settings survive reopening");
    require(!stored.contains(QStringLiteral("Gateway/listener_certificate_file")), "obsolete listener setting removed");
    enabled->setChecked(false);
    saveGatewaySettings(dialog.settings());
    require(!loadGatewaySettings().connection.listener.enabled, "explicitly disabling the listener survives restart");
}

void testValidation()
{
    auto saved = loadGatewaySettings();
    GatewaySettingsDialog dialog(saved, nullptr, true);
    dialog.show();
    VaporViewTest::processEventsFor(40);
    field<QCheckBox>(dialog, "gatewayEnabled")->setChecked(true);
    auto *url = field<QLineEdit>(dialog, "gatewayServerUrl");
    auto *buttons = field<QDialogButtonBox>(dialog, "gatewayDialogButtons");
    for (const auto* invalidUrl : {"ftp://192.0.2.20/", "http:///gateway", "http://device@192.0.2.20/",
                                  "http://192.0.2.20/?device=1", "https://192.0.2.20/#fragment"})
    {
        url->setText(QString::fromLatin1(invalidUrl));
        buttons->button(QDialogButtonBox::Save)->click();
        require(dialog.isVisible() && field<QLabel>(dialog, "gatewayValidationError")->isVisible(),
                "malformed outbound endpoint is rejected before saving");
    }
    const auto lanUrl = QStringLiteral("http://192.0.2.20:8080/gateway/");
    url->setText(lanUrl);
    field<QLineEdit>(dialog, "gatewayDeviceId")->clear();
    buttons->button(QDialogButtonBox::Save)->click();
    require(dialog.isVisible(), "enabled Gateway requires a device identity");
    field<QLineEdit>(dialog, "gatewayDeviceId")->setText(saved.connection.deviceId);
    buttons->button(QDialogButtonBox::Save)->click();
    require(dialog.result() == QDialog::Accepted && dialog.settings().connection.serverUrl == QUrl(lanUrl),
            "LAN HTTP forwarding can be configured without credentials");
}

void testMenuAndUiTestIsolation()
{
    MainWindow window;
    auto *action = field<QAction>(window, "gatewaySettingsAction");
    require(window.findChild<QSystemTrayIcon *>(QStringLiteral("gatewaySystemTray")) == nullptr,
            "tray icon is absent until opted in");
    window.show();
    VaporViewTest::processEventsFor(100);
    for (bool english : {false, true})
    {
        if (english)
        {
            require(QMetaObject::invokeMethod(&window, "onSwitchLanguage", Qt::DirectConnection),
                    "language switch slot is available");
            require(VaporViewTest::processEventsUntil(2000, [&] {
                return action->text() == QStringLiteral("Gateway…");
            }), "language change updates the Gateway menu action");
        }
        bool opened = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<GatewaySettingsDialog *>();
            require(dialog && dialog->isVisible(), "data menu opens Gateway settings");
            opened = true;
            dialog->reject();
        });
        action->trigger();
        require(opened, "Gateway settings action is connected");
    }
    auto *uiTest = field<QAction>(window, "uiTestModeAction");
    uiTest->setChecked(true);
    require(uiTest->isChecked() && !action->isEnabled(), "UI Test Mode disables Gateway configuration");
    uiTest->setChecked(false);
    require(action->isEnabled(), "leaving UI Test Mode restores Gateway configuration");
    require(!VaporView::settingsWritesSuspended(), "UI test restores the settings write barrier");
    window.close();
}

void testSyncUsesAppliedConnection()
{
    QTemporaryDir cache;
    require(cache.isValid(), "temporary Gateway cache is available");
    auto saved = loadGatewaySettings();
    saved.connection.enabled = true;
    saved.connection.serverUrl = QUrl(QStringLiteral("http://127.0.0.1:1/"));
    saved.connection.cacheDirectory = cache.path();
    VaporView::Ground::Gateway::GatewayService service;
    service.configure(saved.connection);
    GatewaySettingsDialog dialog(saved, &service, true);
    auto *sync = field<QPushButton>(dialog, "gatewaySyncSession");
    require(sync->isEnabled(), "Session sync uses an enabled applied configuration");
    field<QLineEdit>(dialog, "gatewayServerUrl")->setText(QStringLiteral("https://cloud.example.invalid/"));
    require(!sync->isEnabled(), "editing the destination prevents sync to the previous destination");
}
}

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    testPermissionsAndPersistence();
    testValidation();
    testSyncUsesAppliedConnection();
    testMenuAndUiTestIsolation();
    testLanListenerSettings();
    std::cout << "Gateway settings tests passed\n";
    return 0;
}
