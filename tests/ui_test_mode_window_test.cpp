#include "ground/main/MainWindow.h"
#include "ground/rtk/RtkConfigDialog.h"
#include "ground/wave/TcpWavePanel.h"
#include "ground/widgets/SkyDeviceConfigDialog.h"
#include "shared/config/SettingsWriteBarrier.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDialog>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QMap>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariant>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

using SettingsSnapshot = QMap<QString, QVariant>;

SettingsSnapshot snapshot(const QString& application)
{
    QSettings settings(QStringLiteral("VaporView"), application);
    SettingsSnapshot result;
    for (const QString& key : settings.allKeys())
    {
        result.insert(key, settings.value(key));
    }
    return result;
}

QMap<QString, SettingsSnapshot> snapshotAll()
{
    QMap<QString, SettingsSnapshot> result;
    for (const QString& application : {
             QStringLiteral("MainWindow"), QStringLiteral("SerialPortHistory"),
             QStringLiteral("RtkConfig"), QStringLiteral("TcpWavePanel"),
             QStringLiteral("SessionViewer"), QStringLiteral("TrajectoryViewer"),
             QStringLiteral("Map3D")})
    {
        result.insert(application, snapshot(application));
    }
    return result;
}

void processEvents()
{
    QApplication::processEvents(QEventLoop::AllEvents, 100);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QTemporaryDir settingsDirectory;
    require(settingsDirectory.isValid(), "temporary settings directory created");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());

    {
        QSettings mainSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        mainSettings.setValue(QStringLiteral("serial/epsilon_port"), QStringLiteral("NORMAL-COM7"));
        mainSettings.setValue(QStringLiteral("dark_theme_enabled"), false);
        mainSettings.setValue(QStringLiteral("font_scale_percent"), 100);
        mainSettings.setValue(QStringLiteral("recording_directory"), settingsDirectory.filePath(QStringLiteral("business-output")));
        mainSettings.sync();
        QSettings history(QStringLiteral("VaporView"), QStringLiteral("SerialPortHistory"));
        history.setValue(QStringLiteral("ports"), QStringList{QStringLiteral("NORMAL-COM7")});
        history.sync();
    }

    auto *window = new MainWindow();
    window->show();
    processEvents();
    QComboBox *epsilonPort = window->findChild<QComboBox *>(QStringLiteral("epsilonPortCombo"));
    QLineEdit *rtkServer = window->findChild<QLineEdit *>(QStringLiteral("rtkServerEdit"));
    require(epsilonPort && rtkServer,
            "main and RTK configuration controls exist");
    epsilonPort->addItem(QStringLiteral("UNSAVED-COM42"), QStringLiteral("UNSAVED-COM42"));
    epsilonPort->setCurrentIndex(epsilonPort->count() - 1);
    rtkServer->setText(QStringLiteral("unsaved.normal.caster"));
    QStringList epsilonItemsBefore;
    for (int index = 0; index < epsilonPort->count(); ++index)
    {
        epsilonItemsBefore.push_back(epsilonPort->itemText(index));
    }
    const auto before = snapshotAll();

    QAction *modeAction = window->findChild<QAction *>(QStringLiteral("uiTestModeAction"));
    QLabel *badge = window->findChild<QLabel *>(QStringLiteral("uiTestModeBadge"));
    QMenu *scenarioMenu = window->findChild<QMenu *>(QStringLiteral("uiTestScenarioMenu"));
    require(modeAction && badge && scenarioMenu, "UI test menu actions and title badge exist");
    modeAction->trigger();
    processEvents();
    require(modeAction->isChecked(), "UI test mode action becomes checked");
    require(!badge->isHidden(), "persistent UI test badge is visible");
    require(scenarioMenu->isEnabled(), "scenario menu is enabled in UI test mode");
    QDialog testCreatedAuxiliary;
    testCreatedAuxiliary.show();
    processEvents();
    require(testCreatedAuxiliary.isVisible(), "test-created auxiliary window is visible during UI test mode");
    TcpWavePanel *wavePanel = window->findChild<TcpWavePanel *>();
    RtkConfigDialog *rtkDialog = window->findChild<RtkConfigDialog *>();
    require(wavePanel && rtkDialog, "TCP waveform and RTK test-session participants exist");
    require(wavePanel->isConnected(), "TCP waveform panel starts connected in UI test mode");
    wavePanel->toggleConnection();
    require(!wavePanel->isConnected(), "TCP waveform disconnect is simulated in memory");
    wavePanel->toggleConnection();
    require(wavePanel->isConnected(), "TCP waveform reconnect is simulated in memory");
    require(QMetaObject::invokeMethod(rtkDialog, "onFetchMountpointsClicked", Qt::DirectConnection),
            "RTK fixed mountpoint action invoked");
    require(QMetaObject::invokeMethod(rtkDialog, "onTestClicked", Qt::DirectConnection),
            "RTK no-signal validation action invoked");
    require(QMetaObject::invokeMethod(rtkDialog, "onStartClicked", Qt::DirectConnection),
            "RTK simulated start action invoked");
    require(rtkDialog->isRunning(), "RTK simulated service enters running state");
    require(QMetaObject::invokeMethod(rtkDialog, "onStopClicked", Qt::DirectConnection),
            "RTK simulated stop action invoked");

    VaporView::SkyDeviceConfigDialog skyDialog(nullptr);
    skyDialog.setUiTestMode(true);
    require(QMetaObject::invokeMethod(&skyDialog, "onReadClicked", Qt::DirectConnection),
            "Sky fixed configuration read action invoked");
    require(QMetaObject::invokeMethod(&skyDialog, "onApplyClicked", Qt::DirectConnection),
            "Sky configuration validation action invoked");
    require(QMetaObject::invokeMethod(&skyDialog, "onSaveClicked", Qt::DirectConnection),
            "Sky simulated save action invoked");

    QAction *partialFailureAction = nullptr;
    QAction *stalledAction = nullptr;
    for (QAction *action : scenarioMenu->actions())
    {
        if (action->data().toInt() == 1) partialFailureAction = action;
        if (action->data().toInt() == 2) stalledAction = action;
    }
    require(partialFailureAction && stalledAction, "all UI test scenarios are present");
    partialFailureAction->trigger();
    stalledAction->trigger();
    processEvents();

    require(QMetaObject::invokeMethod(window, "onStartRecordingClicked", Qt::DirectConnection),
            "simulated recording start slot invoked");
    require(QMetaObject::invokeMethod(window, "onPauseRecordingClicked", Qt::DirectConnection),
            "simulated recording pause slot invoked");
    require(QMetaObject::invokeMethod(window, "onStopRecordingClicked", Qt::DirectConnection),
            "simulated recording stop slot invoked");
    require(QMetaObject::invokeMethod(window, "onDisconnectClicked", Qt::DirectConnection),
            "simulated disconnect slot invoked");
    require(QMetaObject::invokeMethod(window, "onConnectClicked", Qt::DirectConnection),
            "simulated connect slot invoked");
    require(QMetaObject::invokeMethod(window, "onCancelConnectClicked", Qt::DirectConnection),
            "simulated cancel slot invoked");
    require(QMetaObject::invokeMethod(window, "onRefreshPortsClicked", Qt::DirectConnection),
            "fixed-port refresh slot invoked");
    processEvents();
    require(snapshotAll() == before, "all settings namespaces remain byte-for-byte equivalent during UI test mode");
    require(!QDir(settingsDirectory.filePath(QStringLiteral("business-output"))).exists(),
            "simulated recording did not create its configured business directory");

    modeAction->trigger();
    processEvents();
    require(!modeAction->isChecked(), "UI test mode action clears after exit");
    require(badge->isHidden(), "UI test badge hides after exit");
    require(!scenarioMenu->isEnabled(), "scenario menu is disabled after exit");
    require(!testCreatedAuxiliary.isVisible(), "test-created auxiliary window closes on UI test exit");
    require(snapshotAll() == before, "all settings namespaces remain unchanged after normal UI test exit");
    require(epsilonPort->currentText() == QStringLiteral("UNSAVED-COM42") &&
                rtkServer->text() == QStringLiteral("unsaved.normal.caster"),
            "unsaved normal-mode control values are restored after UI test mode");
    QStringList epsilonItemsAfter;
    for (int index = 0; index < epsilonPort->count(); ++index)
    {
        epsilonItemsAfter.push_back(epsilonPort->itemText(index));
    }
    require(epsilonItemsAfter == epsilonItemsBefore,
            "normal-mode serial choices are restored without UI-test entries");
    window->close();
    delete window;
    const auto beforeDirectClose = snapshotAll();

    auto *directCloseWindow = new MainWindow();
    directCloseWindow->show();
    processEvents();
    QAction *directCloseModeAction = directCloseWindow->findChild<QAction *>(QStringLiteral("uiTestModeAction"));
    require(directCloseModeAction, "UI test action exists after recreating main window");
    directCloseModeAction->trigger();
    processEvents();
    directCloseWindow->close();
    delete directCloseWindow;
    require(VaporView::settingsWritesSuspended(), "direct close keeps the write barrier active through destruction");
    require(snapshotAll() == beforeDirectClose, "direct close from UI test mode does not persist destructor state");
    VaporView::setSettingsWritesSuspended(false);

    std::cout << "ui_test_mode_window_test passed\n";
    return 0;
}
