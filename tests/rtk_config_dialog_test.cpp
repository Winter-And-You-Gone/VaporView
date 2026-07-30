#include "ground/rtk/RtkConfigDialog.h"
#include "ground/widgets/SerialPortComboSupport.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/SingleLevelPopupComboBox.h"
#include "shared/theme/SingleLevelPopupMenu.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QHostAddress>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>

#include <cstdlib>
#include <functional>
#include <iostream>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template <typename Predicate>
bool processEventsUntil(int timeoutMs, Predicate predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if (predicate())
        {
            return true;
        }
    }
    return predicate();
}
}

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    QSettings rtkSettings(
        QSettings::IniFormat,
        QSettings::UserScope,
        QStringLiteral("VaporView"),
        QStringLiteral("RtkConfig"));
    require(QFileInfo(rtkSettings.fileName()).absoluteFilePath().startsWith(
                QDir(settingsDir.path()).absolutePath(), Qt::CaseInsensitive),
            "RTK test profile is isolated under the temporary settings directory");
    rtkSettings.setValue(QStringLiteral("server"), QStringLiteral("unsaved.normal.caster"));
    rtkSettings.setValue(QStringLiteral("port"), QStringLiteral("8002"));
    rtkSettings.setValue(QStringLiteral("username"), QStringLiteral("saved-user"));
    rtkSettings.setValue(QStringLiteral("password"), QStringLiteral("saved-password"));
    rtkSettings.setValue(QStringLiteral("output_port"), QStringLiteral("__missing_serial_port__"));
    rtkSettings.setValue(QStringLiteral("mountpoint"), QStringLiteral("AUTO"));
    rtkSettings.setValue(QStringLiteral("mountpoint_confirmed"), true);
    QSettings(QStringLiteral("VaporView"), QStringLiteral("SerialPortHistory"))
        .setValue(QStringLiteral("ports"), QStringList{QStringLiteral("COM77")});

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    {
#ifdef Q_OS_WIN
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("windows"));
#else
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
#endif
    }
    QApplication app(argc, argv);

    {
        RtkConfigDialog testResidueDialog(nullptr, false);
        auto *testResidueServer =
            testResidueDialog.findChild<QLineEdit *>(QStringLiteral("rtkServerEdit"));
        auto *testResiduePort =
            testResidueDialog.findChild<QLineEdit *>(QStringLiteral("rtkPortEdit"));
        auto *testResidueUsername =
            testResidueDialog.findChild<QLineEdit *>(QStringLiteral("rtkUsernameEdit"));
        auto *testResiduePassword =
            testResidueDialog.findChild<QLineEdit *>(QStringLiteral("rtkPasswordEdit"));
        require(testResidueServer && testResiduePort &&
                    testResidueServer->text() == QStringLiteral("203.107.45.154") &&
                    testResiduePort->text() == QStringLiteral("8002"),
                "saved UI-test caster residue is cleared back to the WGS84 default caster");
        require(testResidueUsername && testResiduePassword &&
                    testResidueUsername->text() == QStringLiteral("saved-user") &&
                    testResiduePassword->text() == QStringLiteral("saved-password"),
                "saved RTK credentials survive caster residue migration");
        testResidueDialog.setUiTestMode(true);
    }
    require(rtkSettings.value(QStringLiteral("server")).toString() == QStringLiteral("203.107.45.154") &&
                rtkSettings.value(QStringLiteral("port")).toString() == QStringLiteral("8002"),
            "saved UI-test caster residue is persisted as the WGS84 default caster");
    require(rtkSettings.value(QStringLiteral("username")).toString() == QStringLiteral("saved-user") &&
                rtkSettings.value(QStringLiteral("password")).toString() == QStringLiteral("saved-password"),
            "direct UI-test dialog teardown cannot overwrite the isolated RTK profile");
    rtkSettings.setValue(QStringLiteral("server"), QStringLiteral("127.0.0.1"));
    rtkSettings.setValue(QStringLiteral("port"), QStringLiteral("60844"));
    rtkSettings.sync();

    RtkConfigDialog dialog(nullptr, false);
    dialog.show();
    QApplication::processEvents();
    auto *outputPortCombo = dialog.findChild<QComboBox *>(QStringLiteral("rtkOutputPortCombo"));
    require(outputPortCombo != nullptr, "RTK output-port combo exists");
    require(outputPortCombo->findText(QStringLiteral("__missing_serial_port__")) < 0 &&
                outputPortCombo->currentText() == QStringLiteral("未选择"),
            "unavailable legacy RTK output port shows the Chinese unselected placeholder");
    auto *mountpointCombo = dialog.findChild<QComboBox *>(QStringLiteral("rtkMountpointCombo"));
    require(mountpointCombo != nullptr, "RTK mountpoint combo exists");
    require(mountpointCombo->currentText() == QStringLiteral("请先检测") &&
                mountpointCombo->findText(QStringLiteral("AUTO")) < 0,
            "saved AUTO mountpoint shows the detect-first prompt until mountpoints are detected");
    require(mountpointCombo->property("usesSingleLevelPopupMenu").toBool(),
            "RTK mountpoint combo uses the single-level popup implementation");
    auto *serverEdit = dialog.findChild<QLineEdit *>(QStringLiteral("rtkServerEdit"));
    auto *portEdit = dialog.findChild<QLineEdit *>(QStringLiteral("rtkPortEdit"));
    require(serverEdit && portEdit, "RTK server and port edits exist");
    require(serverEdit->text() == QStringLiteral("203.107.45.154") &&
                portEdit->text() == QStringLiteral("8002"),
            "saved loopback caster test residue is cleared back to the WGS84 default caster");
    dialog.setPreferredOutputPortAndBaud(QStringLiteral("COM77"), QStringLiteral("115200"));
    const int rememberedPortIndex = outputPortCombo->findText(QStringLiteral("COM77"));
    require(rememberedPortIndex >= 0 &&
                outputPortCombo->currentIndex() == rememberedPortIndex &&
                outputPortCombo->itemData(
                    rememberedPortIndex,
                    VaporView::kSerialPortHistoryItemRole).toBool(),
            "explicit RTK serial history is retained and marked as history");
    auto *fetchMountpointsButton = dialog.findChild<QPushButton *>(QStringLiteral("rtkFetchMountpointsButton"));
    require(serverEdit && portEdit && fetchMountpointsButton, "mountpoint fetch controls exist");
    const int mountpointComboWidth = mountpointCombo->width();
    const int detectButtonWidth = fetchMountpointsButton->width();
    require(std::abs(mountpointComboWidth - detectButtonWidth) <= 2,
            "mountpoint combo and detect button have the same width");
    require(!fetchMountpointsButton->icon().isNull(),
            "mountpoint detect button uses the lucide radar icon");
    require(fetchMountpointsButton->iconSize() == QSize(20, 20),
            "mountpoint detect button uses the enlarged 20 px radar icon");
    const QImage mountpointIconImage =
        fetchMountpointsButton->icon().pixmap(fetchMountpointsButton->iconSize()).toImage();
    bool mountpointIconHasWhitePixel = false;
    for (int y = 0; y < mountpointIconImage.height() && !mountpointIconHasWhitePixel; ++y)
    {
        for (int x = 0; x < mountpointIconImage.width(); ++x)
        {
            const QColor pixel = mountpointIconImage.pixelColor(x, y);
            if (pixel.alpha() > 0 && pixel.red() >= 250 && pixel.green() >= 250 && pixel.blue() >= 250)
            {
                mountpointIconHasWhitePixel = true;
                break;
            }
        }
    }
    require(mountpointIconHasWhitePixel,
            "mountpoint detect button renders the lucide radar icon in white");

    auto *ggaToggleButton = dialog.findChild<QPushButton *>(QStringLiteral("rtkGgaToggleButton"));
    auto *ggaMonitorLog = dialog.findChild<QTextEdit *>(QStringLiteral("rtkGgaTextEdit"));
    QWidget *ggaCard = ggaMonitorLog && ggaMonitorLog->parentWidget()
        ? ggaMonitorLog->parentWidget()->parentWidget()
        : nullptr;
    require(ggaToggleButton && ggaMonitorLog && ggaCard,
            "GGA monitor button, internal log, and card exist");
    require(dialog.findChild<QLabel *>(QStringLiteral("rtkGgaStatusLabel")) == nullptr,
            "GGA monitor does not create a separate status label");
    require(ggaMonitorLog->document()->documentMargin() <= 3.0,
            "GGA monitor document uses compact inner margins");
    require(ggaMonitorLog->styleSheet().contains(QStringLiteral("padding:")),
            "GGA monitor overrides the generic text-edit padding");
    const QString expectedLogBackground = VaporView::appThemeColorName(
        VaporView::AppThemeColor::SurfaceRaised,
        VaporView::isDarkThemePalette(ggaMonitorLog->palette()));
    require(ggaMonitorLog->styleSheet().contains(expectedLogBackground, Qt::CaseInsensitive),
            "GGA monitor uses the raised log-panel background");
    dialog.setEpsilonDataProvider([]() { return VaporView::EpsilonData{}; });
    const int ggaCardHeightBeforeReading = ggaCard->height();
    ggaToggleButton->click();
    require(ggaToggleButton->text() == QStringLiteral("停止") ||
                ggaToggleButton->text() == QStringLiteral("Stop"),
            "active GGA monitor uses the compact stop label");
    require(processEventsUntil(1000, [ggaMonitorLog]() {
                const QString text = ggaMonitorLog->toPlainText();
                return text.contains(QStringLiteral("状态:")) ||
                    text.contains(QStringLiteral("Status:"));
            }),
            "GGA reading status is appended inside the GGA monitor log");
    const QStringList ggaLogLines =
        ggaMonitorLog->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    require(!ggaLogLines.isEmpty() && ggaLogLines.constFirst().startsWith(QLatin1Char('[')) &&
                ggaLogLines.constFirst().contains(QStringLiteral("] ")),
            "GGA status log entry includes a timestamp");
    require(ggaLogLines.size() == 1 &&
                (ggaLogLines.constFirst().contains(QStringLiteral("正在等待有效的 EPSILON 主串口定位")) ||
                 ggaLogLines.constFirst().contains(QStringLiteral("Waiting for valid EPSILON main-port position"))),
            "GGA monitor logs only the precise initial EPSILON position status");
    require(!processEventsUntil(1500, [ggaMonitorLog]() {
                return ggaMonitorLog->toPlainText().split(
                    QLatin1Char('\n'), Qt::SkipEmptyParts).size() > 1;
            }),
            "GGA waiting heartbeat does not spam the log before two seconds");
    require(processEventsUntil(1000, [ggaMonitorLog]() {
                return ggaMonitorLog->toPlainText().split(
                    QLatin1Char('\n'), Qt::SkipEmptyParts).size() >= 2;
            }),
            "GGA monitor repeats its waiting status every two seconds without data");
    const QStringList repeatedGgaLogLines =
        ggaMonitorLog->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    require(repeatedGgaLogLines.size() == 2 &&
                (repeatedGgaLogLines.constLast().contains(QStringLiteral("正在等待有效的 EPSILON 主串口定位")) ||
                 repeatedGgaLogLines.constLast().contains(QStringLiteral("Waiting for valid EPSILON main-port position"))),
            "GGA waiting heartbeat repeats the current no-data reason");
    QApplication::processEvents();
    require(ggaCard->height() == ggaCardHeightBeforeReading,
            "GGA status log does not change the monitor card height");
    ggaToggleButton->click();
    require(ggaToggleButton->text() == QStringLiteral("读取") ||
                ggaToggleButton->text() == QStringLiteral("Read"),
            "stopped GGA monitor restores the compact read label");
    require(processEventsUntil(1000, [ggaMonitorLog]() {
                const QString text = ggaMonitorLog->toPlainText();
                return text.contains(QStringLiteral("状态: 已停止读取 GGA")) ||
                    text.contains(QStringLiteral("Status: GGA reading stopped"));
            }),
            "stopping GGA reading appends a status entry inside the monitor log");
    const QStringList stoppedGgaLogLines =
        ggaMonitorLog->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    require(stoppedGgaLogLines.size() == 3 &&
                stoppedGgaLogLines.constLast().startsWith(QLatin1Char('[')) &&
                stoppedGgaLogLines.constLast().contains(QStringLiteral("] ")),
            "GGA stop status is appended once with a timestamp");
    require(ggaCard->height() == ggaCardHeightBeforeReading,
            "GGA stop status log does not change the monitor card height");

    QTcpServer caster;
    require(caster.listen(QHostAddress::LocalHost, 0), "local sourcetable test server starts");
    QObject::connect(&caster, &QTcpServer::newConnection, [&caster]() {
        while (QTcpSocket *socket = caster.nextPendingConnection())
        {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                socket->readAll();
                const QByteArray body =
                    "STR;AUTO;Auto mountpoint;RTCM 3;1004(1);2;GPS;NONE;B;N;0;0;VaporView;none;B;N;0;\r\n"
                    "STR;RTCM32_GPS_LONG_WIDEST;Long mountpoint;RTCM 3;1004(1);2;GPS;NONE;B;N;0;0;VaporView;none;B;N;0;\r\n"
                    "STR;RTCM30_GG;Short mountpoint;RTCM 3;1004(1);2;GPS;NONE;B;N;0;0;VaporView;none;B;N;0;\r\n"
                    "ENDSOURCETABLE\r\n";
                const QByteArray response =
                    "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                    QByteArray::number(body.size()) +
                    "\r\n\r\n" +
                    body;
                socket->write(response);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    serverEdit->setText(QStringLiteral("127.0.0.1"));
    portEdit->setText(QString::number(caster.serverPort()));
    fetchMountpointsButton->click();
    require(processEventsUntil(4000, [mountpointCombo]() {
                return mountpointCombo->findText(QStringLiteral("RTCM32_GPS_LONG_WIDEST")) >= 0;
            }),
            "mountpoint detection populates the dropdown from the sourcetable");
    require(mountpointCombo->findText(QStringLiteral("AUTO")) >= 0,
            "detected AUTO is kept as a real mountpoint option");
    require(mountpointCombo->currentText() == QStringLiteral("请选择挂载点"),
            "detected mountpoints require an explicit user selection");
    require(mountpointCombo->width() == mountpointComboWidth,
            "mountpoint combo keeps its widened width after fetching long mountpoints");
    require(fetchMountpointsButton->width() == detectButtonWidth &&
                std::abs(fetchMountpointsButton->width() - mountpointCombo->width()) <= 2,
            "mountpoint detect button stays aligned with the widened combo width");
    auto *singleLevelMountpointCombo =
        dynamic_cast<VaporView::SingleLevelPopupComboBox *>(mountpointCombo);
    require(singleLevelMountpointCombo != nullptr,
            "mountpoint combo can expose the single-level popup for hover-state checks");
    mountpointCombo->setCurrentText(QStringLiteral("AUTO"));
    singleLevelMountpointCombo->showPopup();
    QApplication::processEvents();
    const int widestPopupContentWidth =
        mountpointCombo->fontMetrics().horizontalAdvance(QStringLiteral("RTCM32_GPS_LONG_WIDEST")) + 32;
    require(singleLevelMountpointCombo->popupMenu()->width() >= widestPopupContentWidth,
            "mountpoint popup expands to fit the widest fetched mountpoint");
    require(singleLevelMountpointCombo->popupMenu()->width() > mountpointCombo->width(),
            "mountpoint popup can still be wider than the combo box");
    singleLevelMountpointCombo->hidePopup();
    singleLevelMountpointCombo->showPopup();
    QApplication::processEvents();
    const auto popupRows = singleLevelMountpointCombo->popupMenu()->rows();
    require(!popupRows.isEmpty(), "mountpoint popup builds rows");
    for (const VaporView::SingleLevelPopupMenuRow *row : popupRows)
    {
        require(!row->property("hovered").toBool(),
                "mountpoint popup clears stale hover highlight when reopened");
        require(!row->property("selected").toBool(),
                "mountpoint popup clears stale selected highlight when reopened");
    }
    singleLevelMountpointCombo->hidePopup();

    QTcpServer ntripTestCaster;
    require(ntripTestCaster.listen(QHostAddress::LocalHost, 0), "local NTRIP response test server starts");
    QObject::connect(&ntripTestCaster, &QTcpServer::newConnection, [&ntripTestCaster]() {
        while (QTcpSocket *socket = ntripTestCaster.nextPendingConnection())
        {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                socket->readAll();
                if (socket->property("sentHeader").toBool())
                {
                    return;
                }
                socket->setProperty("sentHeader", true);
                socket->write("ICY 200 OK\r\n\r\n");
                auto *burstTimer = new QTimer(socket);
                burstTimer->setInterval(50);
                QObject::connect(burstTimer, &QTimer::timeout, socket, [socket, burstTimer]() {
                    int count = socket->property("burstCount").toInt();
                    if (count >= 12)
                    {
                        burstTimer->stop();
                        socket->disconnectFromHost();
                        return;
                    }
                    QByteArray burst(48, '\xD3');
                    socket->write(burst);
                    socket->flush();
                    socket->setProperty("burstCount", count + 1);
                });
                burstTimer->start();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    outputPortCombo->setCurrentIndex(0);
    serverEdit->setText(QStringLiteral("127.0.0.1"));
    portEdit->setText(QString::number(ntripTestCaster.serverPort()));
    mountpointCombo->setCurrentText(QStringLiteral("AUTO"));
    auto *testButton = dialog.findChild<QPushButton *>();
    for (QPushButton *button : dialog.findChildren<QPushButton *>())
    {
        if (button->text() == QStringLiteral("测试连接") || button->text() == QStringLiteral("Test Connection"))
        {
            testButton = button;
            break;
        }
    }
    auto *serviceLog = dialog.findChild<QTextEdit *>(QStringLiteral("rtkServiceLogTextEdit"));
    require(testButton && serviceLog, "RTK test button and service log exist");
    require(serviceLog->styleSheet().contains(expectedLogBackground, Qt::CaseInsensitive),
            "RTK service log uses the raised log-panel background");
    QTimer testMessageBoxCloser;
    QObject::connect(&testMessageBoxCloser, &QTimer::timeout, []() {
        for (QWidget *widget : QApplication::topLevelWidgets())
        {
            if (auto *messageBox = qobject_cast<QMessageBox *>(widget))
            {
                messageBox->accept();
            }
        }
    });
    testMessageBoxCloser.start(10);
    testButton->click();
    const bool rtkConnectionTestSucceeded = processEventsUntil(5000, [serviceLog, testButton]() {
                return serviceLog->toPlainText().contains(QStringLiteral("不需要输出串口")) &&
                    serviceLog->toPlainText().contains(QStringLiteral("模拟 GGA 测试成功")) &&
                    testButton->isEnabled();
            });
    if (!rtkConnectionTestSucceeded)
    {
        std::cerr << serviceLog->toPlainText().toLocal8Bit().constData() << '\n';
    }
    require(rtkConnectionTestSucceeded,
            "RTK connection test succeeds with simulated GGA and no selected output port");
    testMessageBoxCloser.stop();

    auto *xEdit = dialog.findChild<QLineEdit *>(QStringLiteral("rtkLeverXEdit"));
    auto *yEdit = dialog.findChild<QLineEdit *>(QStringLiteral("rtkLeverYEdit"));
    auto *zEdit = dialog.findChild<QLineEdit *>(QStringLiteral("rtkLeverZEdit"));
    auto *applyButton = dialog.findChild<QPushButton *>(QStringLiteral("rtkApplyLeverArmButton"));
    auto *leverHelpButton = dialog.findChild<QToolButton *>(QStringLiteral("rtkLeverHelpButton"));
    require(xEdit && yEdit && zEdit && applyButton && leverHelpButton, "lever-arm controls exist");
    require(leverHelpButton->iconSize() == QSize(24, 24),
            "lever-arm help icon matches the standard title-bar icon size");
    const QString leverHelpButtonStyle = leverHelpButton->styleSheet();
    require(leverHelpButtonStyle.contains(QStringLiteral("border-radius: 6px")) &&
                leverHelpButtonStyle.contains(QStringLiteral("QToolButton:hover, QToolButton:pressed")) &&
                leverHelpButtonStyle.contains(VaporView::appThemeColorName(
                    VaporView::AppThemeColor::TitleBarHover,
                    VaporView::isDarkThemePalette(app.palette()))),
            "lever-arm help uses the title-bar gray hover and pressed background");

    leverHelpButton->click();
    QApplication::processEvents();
    auto *leverHelpPopup = dialog.findChild<VaporView::SingleLevelPopupMenu *>(QStringLiteral("rtkLeverHelpPopup"));
    require(leverHelpPopup && leverHelpPopup->isVisible() &&
                leverHelpPopup->property("floatingPanelChrome").toBool() &&
                leverHelpPopup->property("shadowMargin").toInt() == 40 &&
                leverHelpPopup->property("shadowBottomMargin").toInt() == 50 &&
                leverHelpPopup->contentsMargins().bottom() == 50 &&
                leverHelpPopup->testAttribute(Qt::WA_TranslucentBackground),
            "lever-arm help uses the shared rounded shadow popup");
    auto *leverHelpText = leverHelpPopup->findChild<QLabel *>(QStringLiteral("rtkLeverHelpPopupText"));
    require(leverHelpText && leverHelpText->mapTo(leverHelpPopup, QPoint()).x() >= 30,
            "lever-arm help keeps the text inset from the shadowed panel edge");
    leverHelpPopup->hide();

    xEdit->setText(QStringLiteral("1.25"));
    yEdit->setText(QStringLiteral("-0.50"));
    zEdit->setText(QStringLiteral("0.75"));

    RtkConfigDialog::EpsilonLeverArmCompletion pendingCompletion;
    dialog.setEpsilonMainAntennaLeverArmApplier(
        [&pendingCompletion](double, double, double, RtkConfigDialog::EpsilonLeverArmCompletion completion) {
            pendingCompletion = std::move(completion);
        });

    applyButton->click();
    QApplication::processEvents();
    require(static_cast<bool>(pendingCompletion), "lever-arm command starts asynchronously");
    require(!applyButton->isEnabled(), "apply button stays disabled while command is pending");

    QTimer messageBoxCloser;
    QObject::connect(&messageBoxCloser, &QTimer::timeout, []() {
        for (QWidget *widget : QApplication::topLevelWidgets())
        {
            if (auto *messageBox = qobject_cast<QMessageBox *>(widget))
            {
                messageBox->accept();
            }
        }
    });
    messageBoxCloser.start(10);
    pendingCompletion(true, QString());
    QApplication::processEvents();
    QApplication::processEvents();
    messageBoxCloser.stop();
    require(applyButton->isEnabled(), "apply button is restored after asynchronous completion");

    std::cout << "rtk_config_dialog_test passed\n";
    return 0;
}
