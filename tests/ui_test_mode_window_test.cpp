#include "ground/main/MainWindow.h"
#include "ground/rtk/RtkConfigDialog.h"
#include "ground/wave/TcpWavePanel.h"
#include "ground/widgets/SkyDeviceConfigDialog.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "test_ui_helpers.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QEventLoop>
#include <QFrame>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QMap>
#include <QPushButton>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
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
    QTemporaryDir settingsDirectory;
    require(settingsDirectory.isValid(), "temporary settings directory created");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QApplication application(argc, argv);

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
        QSettings rtkSettings(QStringLiteral("VaporView"), QStringLiteral("RtkConfig"));
        rtkSettings.setValue(QStringLiteral("server"), QStringLiteral("persisted.ui-test.caster"));
        rtkSettings.setValue(QStringLiteral("port"), QStringLiteral("2201"));
        rtkSettings.setValue(QStringLiteral("username"), QStringLiteral("persisted-user"));
        rtkSettings.setValue(QStringLiteral("password"), QStringLiteral("persisted-password"));
        rtkSettings.setValue(QStringLiteral("mountpoint"), QStringLiteral("PERSISTED_MOUNTPOINT"));
        rtkSettings.setValue(QStringLiteral("mountpoint_confirmed"), true);
        rtkSettings.setValue(QStringLiteral("timeout"), QStringLiteral("8000"));
        rtkSettings.setValue(QStringLiteral("reconnect"), QStringLiteral("2000"));
        rtkSettings.sync();
    }

    auto *window = new MainWindow();
    window->show();
    processEvents();
    QComboBox *epsilonPort = window->findChild<QComboBox *>(QStringLiteral("epsilonPortCombo"));
    QLineEdit *rtkServer = window->findChild<QLineEdit *>(QStringLiteral("rtkServerEdit"));
    QLineEdit *rtkPort = window->findChild<QLineEdit *>(QStringLiteral("rtkPortEdit"));
    QLineEdit *rtkUsername = window->findChild<QLineEdit *>(QStringLiteral("rtkUsernameEdit"));
    QLineEdit *rtkPassword = window->findChild<QLineEdit *>(QStringLiteral("rtkPasswordEdit"));
    QComboBox *rtkMountpoint = window->findChild<QComboBox *>(QStringLiteral("rtkMountpointCombo"));
    require(epsilonPort && rtkServer && rtkPort && rtkUsername && rtkPassword && rtkMountpoint,
            "main and RTK configuration controls exist");
    epsilonPort->addItem(QStringLiteral("UNSAVED-COM42"), QStringLiteral("UNSAVED-COM42"));
    epsilonPort->setCurrentIndex(epsilonPort->count() - 1);
    rtkServer->setText(QStringLiteral("normal.unpersisted.caster"));
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
    require(rtkServer->text() == QStringLiteral("persisted.ui-test.caster") &&
                rtkPort->text() == QStringLiteral("2201") &&
                rtkUsername->text() == QStringLiteral("persisted-user") &&
                rtkPassword->text() == QStringLiteral("persisted-password") &&
                rtkMountpoint->currentText() == QStringLiteral("PERSISTED_MOUNTPOINT"),
            "UI test mode reloads the real RTK profile as its sandbox baseline");
    QToolButton *epsilonAction = nullptr;
    for (QToolButton *button : window->findChildren<QToolButton *>(QStringLiteral("homeDeviceActionButton")))
    {
        if (!button->property("deviceConfigAction").toBool() &&
            button->toolTip().contains(QStringLiteral("EPSILON")))
        {
            epsilonAction = button;
            break;
        }
    }
    require(epsilonAction, "EPSILON home connection action exists");
    require(epsilonAction->isEnabled() &&
                epsilonAction->property("state").toString() == QStringLiteral("connected"),
            "EPSILON home action starts enabled and connected");
    epsilonAction->click();
    processEvents();
    require(epsilonAction->isEnabled() &&
                epsilonAction->property("state").toString() == QStringLiteral("disconnected"),
            "a disconnected UI-test device remains available for reconnect");
    epsilonAction->click();
    processEvents();
    require(!epsilonAction->isEnabled() &&
                epsilonAction->property("state").toString() == QStringLiteral("connecting"),
            "UI-test reconnect enters the temporary connecting state");
    require(VaporViewTest::processEventsUntil(2000, [epsilonAction]() {
                return epsilonAction->isEnabled() &&
                    epsilonAction->property("state").toString() == QStringLiteral("connected");
            }),
            "UI-test reconnect finishes and restores the connected action style");
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

    QTcpServer mountpointCaster;
    require(mountpointCaster.listen(QHostAddress::LocalHost, 0),
            "UI-test real mountpoint caster starts");
    bool mountpointRequestReceived = false;
    QObject::connect(&mountpointCaster, &QTcpServer::newConnection,
                     [&mountpointCaster, &mountpointRequestReceived]() {
        while (QTcpSocket *socket = mountpointCaster.nextPendingConnection())
        {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &mountpointRequestReceived]() {
                if (socket->readAll().isEmpty())
                {
                    return;
                }
                mountpointRequestReceived = true;
                const QByteArray body =
                    "STR;PERSISTED_MOUNTPOINT;Saved mountpoint;RTCM 3;1004(1);2;GPS;NONE;B;N;0;0;VaporView;none;B;N;0;\r\n"
                    "STR;REAL_UI_TEST_MOUNTPOINT;UI test mountpoint;RTCM 3;1004(1);2;GPS;NONE;B;N;0;0;VaporView;none;B;N;0;\r\n"
                    "ENDSOURCETABLE\r\n";
                const QByteArray response =
                    "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\n\r\n" + body;
                socket->write(response);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    auto *rtkServiceLog = rtkDialog->findChild<QTextEdit *>(QStringLiteral("rtkServiceLogTextEdit"));
    require(rtkServiceLog, "RTK service log exists");
    rtkServer->setText(QStringLiteral("127.0.0.1"));
    rtkPort->setText(QString::number(mountpointCaster.serverPort()));
    require(QMetaObject::invokeMethod(rtkDialog, "onFetchMountpointsClicked", Qt::DirectConnection),
            "RTK real mountpoint request invoked in UI test mode");
    require(VaporViewTest::processEventsUntil(5000, [rtkDialog, rtkMountpoint, rtkServiceLog, &mountpointRequestReceived]() {
                const QString log = rtkServiceLog->toPlainText();
                return mountpointRequestReceived && !rtkDialog->hasActiveExternalOperation() &&
                    rtkMountpoint->findText(QStringLiteral("REAL_UI_TEST_MOUNTPOINT")) >= 0 &&
                    (log.contains(QStringLiteral("[界面测试] 已从真实源表获取")) ||
                     log.contains(QStringLiteral("[界面测试] Fetched")));
            }),
            "UI-test mountpoint detection sends a real sourcetable request");
    auto *ggaMonitorLog = rtkDialog->findChild<QTextEdit *>(QStringLiteral("rtkGgaTextEdit"));
    require(ggaMonitorLog, "RTK GGA monitor output exists");
    ggaMonitorLog->clear();
    require(QMetaObject::invokeMethod(rtkDialog, "onGgaToggleClicked", Qt::DirectConnection),
            "RTK simulated GGA monitor starts");
    require(VaporViewTest::processEventsUntil(2500, [ggaMonitorLog]() {
                return ggaMonitorLog->toPlainText().count(QStringLiteral("$GPGGA,")) >= 3;
            }),
            "RTK simulated GGA monitor continuously appends one-hertz data");
    require(QMetaObject::invokeMethod(rtkDialog, "onGgaToggleClicked", Qt::DirectConnection),
            "RTK simulated GGA monitor stops");
    const int stoppedGgaCount = ggaMonitorLog->toPlainText().count(QStringLiteral("$GPGGA,"));
    require(!VaporViewTest::processEventsUntil(1200, [ggaMonitorLog, stoppedGgaCount]() {
                return ggaMonitorLog->toPlainText().count(QStringLiteral("$GPGGA,")) > stoppedGgaCount;
            }),
            "RTK simulated GGA monitor stops appending after the user stops it");

    QTcpServer ntripValidationCaster;
    require(ntripValidationCaster.listen(QHostAddress::LocalHost, 0),
            "UI-test real NTRIP validation caster starts");
    bool ntripRequestReceived = false;
    QObject::connect(&ntripValidationCaster, &QTcpServer::newConnection,
                     [&ntripValidationCaster, &ntripRequestReceived]() {
        while (QTcpSocket *socket = ntripValidationCaster.nextPendingConnection())
        {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &ntripRequestReceived]() {
                if (socket->readAll().isEmpty())
                {
                    return;
                }
                ntripRequestReceived = true;
                if (socket->property("sentHeader").toBool())
                {
                    return;
                }
                socket->setProperty("sentHeader", true);
                socket->write("ICY 200 OK\r\n\r\n");
                auto *burstTimer = new QTimer(socket);
                burstTimer->setInterval(50);
                QObject::connect(burstTimer, &QTimer::timeout, socket, [socket, burstTimer]() {
                    const int count = socket->property("burstCount").toInt();
                    if (count >= 12)
                    {
                        burstTimer->stop();
                        socket->disconnectFromHost();
                        return;
                    }
                    socket->write(QByteArray(48, '\xD3'));
                    socket->flush();
                    socket->setProperty("burstCount", count + 1);
                });
                burstTimer->start();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    require(rtkServer && rtkPort && rtkMountpoint && rtkServiceLog,
            "RTK sandbox fields and service log exist");
    rtkServer->setText(QStringLiteral("127.0.0.1"));
    rtkPort->setText(QString::number(ntripValidationCaster.serverPort()));
    rtkMountpoint->setCurrentText(QStringLiteral("REAL_UI_TEST_MOUNTPOINT"));
    require(QMetaObject::invokeMethod(rtkDialog, "onTestClicked", Qt::DirectConnection),
            "RTK real NTRIP validation action invoked in UI test mode");
    require(VaporViewTest::processEventsUntil(6000, [rtkDialog, rtkServiceLog, &ntripRequestReceived]() {
                const QString log = rtkServiceLog->toPlainText();
                return ntripRequestReceived && !rtkDialog->hasActiveExternalOperation() &&
                    (log.contains(QStringLiteral("[界面测试] 真实 NTRIP 验证成功")) ||
                     log.contains(QStringLiteral("[界面测试] Real NTRIP validation succeeded")));
            }),
            "UI-test NTRIP validation sends a real request and receives RTCM locally");
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

    auto *recordingCard = window->findChild<QFrame *>(QStringLiteral("recordingStatusCard"));
    auto *recordingStatus = window->findChild<QLabel *>(QStringLiteral("recordingStatusLabel"));
    require(recordingCard && recordingStatus, "recording status card exists");
    QLabel *recordingTitle = nullptr;
    for (QLabel *label : recordingCard->findChildren<QLabel *>())
    {
        if (label->text().contains(QStringLiteral("记录状态")))
        {
            recordingTitle = label;
            break;
        }
    }
    require(recordingTitle && recordingTitle->text() == QStringLiteral("记录状态（界面测试）"),
            "recording card title identifies UI test mode");
    require(QMetaObject::invokeMethod(window, "onStartRecordingClicked", Qt::DirectConnection),
            "simulated recording start slot invoked");
    require(VaporViewTest::processEventsUntil(1500, [recordingStatus]() {
                return recordingStatus->text().contains(QStringLiteral("记录：进行中（界面测试）")) &&
                    recordingStatus->text().contains(QStringLiteral("会话：UI-TEST-SESSION")) &&
                    recordingStatus->text().contains(QStringLiteral("设备行数：")) &&
                    !recordingStatus->text().contains(QStringLiteral("设备行数：0\n")) &&
                    recordingStatus->text().contains(QStringLiteral("文件写入：无（仅内存模拟）"));
            }),
            "simulated recording displays deterministic in-memory counters");
    require(QMetaObject::invokeMethod(window, "onPauseRecordingClicked", Qt::DirectConnection),
            "simulated recording pause slot invoked");
    processEvents();
    const QString pausedRecordingText = recordingStatus->text();
    require(pausedRecordingText.contains(QStringLiteral("记录：已暂停（界面测试）")),
            "simulated recording displays the paused UI-test state");
    require(!VaporViewTest::processEventsUntil(400, [recordingStatus, pausedRecordingText]() {
                return recordingStatus->text() != pausedRecordingText;
            }),
            "simulated recording counters freeze while paused");
    require(QMetaObject::invokeMethod(window, "onStartRecordingClicked", Qt::DirectConnection),
            "simulated recording resume slot invoked");
    require(VaporViewTest::processEventsUntil(1500, [recordingStatus, pausedRecordingText]() {
                return recordingStatus->text().contains(QStringLiteral("记录：进行中（界面测试）")) &&
                    recordingStatus->text() != pausedRecordingText;
            }),
            "simulated recording counters resume without starting the real recorder");
    require(QMetaObject::invokeMethod(window, "onStopRecordingClicked", Qt::DirectConnection),
            "simulated recording stop slot invoked");
    processEvents();
    require(recordingStatus->text().contains(QStringLiteral("记录：未记录（界面测试）")) &&
                recordingStatus->text().contains(QStringLiteral("设备行数：0")) &&
                recordingStatus->text().contains(QStringLiteral("文件写入：无（仅内存模拟）")),
            "stopping simulated recording clears only its in-memory counters");
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
                rtkServer->text() == QStringLiteral("normal.unpersisted.caster"),
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
