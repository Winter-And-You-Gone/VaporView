#include "test_settings_sandbox.h"
#include "test_ui_helpers.h"
#include "ground/main/GatewaySettingsDialog.h"
#include "ground/main/MainWindow.h"
#include "shared/config/SettingsWriteBarrier.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
            "Gateway, commands and tray background mode are opt-in");
    require(defaults.connection.allowedCommands == QSet<quint16>{static_cast<quint16>(VaporView::CommandId::RequestStatus)},
            "default command permission is status only");
    GatewaySettings saved = defaults;
    saved.connection.deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    saved.connection.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    saveGatewaySettings(saved);
    GatewaySettingsDialog dialog(saved, nullptr, false);
    require(field<QLineEdit>(dialog, "gatewayToken")->echoMode() == QLineEdit::Password,
            "access token is masked");
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
            restored.connection.token == saved.connection.token &&
            restored.connection.allowRemoteCommands &&
            restored.connection.allowedCommands == chosen.connection.allowedCommands,
            "Gateway settings restore the selected identity and permissions");
    saveGatewaySettings(saved);
}

void testValidation()
{
    auto saved = loadGatewaySettings();
    GatewaySettingsDialog dialog(saved, nullptr, true);
    dialog.show();
    VaporViewTest::processEventsFor(40);
    field<QCheckBox>(dialog, "gatewayEnabled")->setChecked(true);
    auto *url = field<QLineEdit>(dialog, "gatewayServerUrl");
    url->setText(QStringLiteral("http://cloud.example.invalid/"));
    auto *buttons = field<QDialogButtonBox>(dialog, "gatewayDialogButtons");
    buttons->button(QDialogButtonBox::Save)->click();
    require(dialog.isVisible() && field<QLabel>(dialog, "gatewayValidationError")->isVisible(),
            "nonlocal cleartext endpoint is rejected before saving");
    url->setText(QStringLiteral("https://cloud.example.invalid/"));
    field<QLineEdit>(dialog, "gatewayDeviceId")->clear();
    buttons->button(QDialogButtonBox::Save)->click();
    require(dialog.isVisible(), "enabled Gateway requires a device identity");
    field<QLineEdit>(dialog, "gatewayDeviceId")->setText(saved.connection.deviceId);
    field<QLineEdit>(dialog, "gatewayToken")->clear();
    buttons->button(QDialogButtonBox::Save)->click();
    require(dialog.isVisible(), "enabled Gateway requires an access token");
    field<QLineEdit>(dialog, "gatewayToken")->setText(saved.connection.token);
    buttons->button(QDialogButtonBox::Save)->click();
    require(dialog.result() == QDialog::Accepted, "valid Gateway settings can be saved");
}

void testMenuAndUiTestIsolation()
{
    auto settings = loadGatewaySettings();
    settings.connection.token.clear();
    saveGatewaySettings(settings);
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
                return action->text() == QStringLiteral("Cloud Gateway…");
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
    std::cout << "Gateway settings tests passed\n";
    return 0;
}
