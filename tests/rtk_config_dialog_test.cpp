#include "ground/rtk/RtkConfigDialog.h"
#include "LogService.h"
#include "ground/widgets/SerialPortComboSupport.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/SingleLevelPopupComboBox.h"
#include "shared/theme/SingleLevelPopupMenu.h"
#include "test_ui_helpers.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QGroupBox>
#include <QHostAddress>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>

class RtkStatusLogTestAccess
{
public:
    static void handle(RtkConfigDialog& dialog, const RtkStreamStats& stats)
    {
        dialog.handleRtkServiceStatus(stats, true);
    }
    static bool hasSink(const RtkConfigDialog& dialog)
    {
        return static_cast<bool>(dialog.rtcm_correction_sink_server_);
    }
};

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

QGroupBox *ancestorCard(QWidget *widget)
{
    QWidget *current = widget;
    while (current)
    {
        if (auto *card = qobject_cast<QGroupBox *>(current))
        {
            return card;
        }
        current = current->parentWidget();
    }
    return nullptr;
}
}

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

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
    qputenv("VAPORVIEW_ENABLE_STARTUP_RTK_MOUNTPOINT_FETCH_IN_TESTS", QByteArrayLiteral("1"));
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
        require(testResidueServer->text() == QStringLiteral("203.107.45.154") &&
                    testResiduePort->text() == QStringLiteral("8002") &&
                    testResidueUsername->text() == QStringLiteral("saved-user") &&
                    testResiduePassword->text() == QStringLiteral("saved-password"),
                "UI test mode uses the real RTK profile instead of placeholder credentials");
        testResidueServer->setText(QStringLiteral("sandbox-only.caster"));
    }
    require(rtkSettings.value(QStringLiteral("server")).toString() == QStringLiteral("203.107.45.154") &&
                rtkSettings.value(QStringLiteral("port")).toString() == QStringLiteral("8002"),
            "saved UI-test caster residue is persisted as the WGS84 default caster");
    require(rtkSettings.value(QStringLiteral("username")).toString() == QStringLiteral("saved-user") &&
                rtkSettings.value(QStringLiteral("password")).toString() == QStringLiteral("saved-password"),
            "direct UI-test dialog teardown cannot overwrite the isolated RTK profile");
    rtkSettings.setValue(QStringLiteral("server"), QStringLiteral("127.0.0.1"));
    rtkSettings.setValue(QStringLiteral("port"), QStringLiteral("60844"));
    rtkSettings.setValue(QStringLiteral("mountpoint"), QStringLiteral("AUTO"));
    rtkSettings.setValue(QStringLiteral("mountpoint_confirmed"), true);
    rtkSettings.sync();
    {
        RtkConfigDialog loopbackResidueDialog(nullptr, false);
        auto *loopbackResidueServer =
            loopbackResidueDialog.findChild<QLineEdit *>(QStringLiteral("rtkServerEdit"));
        auto *loopbackResiduePort =
            loopbackResidueDialog.findChild<QLineEdit *>(QStringLiteral("rtkPortEdit"));
        auto *loopbackResidueMountpoint =
            loopbackResidueDialog.findChild<QComboBox *>(QStringLiteral("rtkMountpointCombo"));
        require(loopbackResidueServer && loopbackResiduePort &&
                    loopbackResidueServer->text() == QStringLiteral("203.107.45.154") &&
                    loopbackResiduePort->text() == QStringLiteral("8002"),
                "saved loopback caster test residue is cleared back to the WGS84 default caster");
        require(loopbackResidueMountpoint &&
                    loopbackResidueMountpoint->currentText() == QStringLiteral("请先检测") &&
                    loopbackResidueMountpoint->findText(QStringLiteral("AUTO")) < 0,
                "saved AUTO mountpoint shows the detect-first prompt until mountpoints are detected");
    }

    {
        RtkConfigDialog remoteSinkDialog(nullptr, false);
        remoteSinkDialog.setRtcmCorrectionSink(
            [](const QByteArray&) { return true; }, QStringLiteral("/dev/ttyRTCM"));
        auto *remoteOutputPortCombo =
            remoteSinkDialog.findChild<QComboBox *>(QStringLiteral("rtkOutputPortCombo"));
        auto *remoteGgaPortCombo =
            remoteSinkDialog.findChild<QComboBox *>(QStringLiteral("rtkGgaPortCombo"));
        auto *remoteRefreshPortsButton =
            remoteSinkDialog.findChild<QPushButton *>(QStringLiteral("rtkRefreshPortsButton"));
        auto *remoteAutoDetectPortsButton =
            remoteSinkDialog.findChild<QPushButton *>(QStringLiteral("rtkAutoDetectPortsButton"));
        auto *remoteGgaToggleButton =
            remoteSinkDialog.findChild<QPushButton *>(QStringLiteral("rtkGgaToggleButton"));
        require(remoteOutputPortCombo && remoteOutputPortCombo->count() == 1 &&
                    remoteOutputPortCombo->currentText() == QStringLiteral("/dev/ttyRTCM") &&
                    !remoteOutputPortCombo->isEnabled(),
                "Remote RTCM sink shows only the Sky endpoint instead of enumerating local output ports");
        require(remoteGgaPortCombo && remoteGgaPortCombo->count() == 1 &&
                    !remoteGgaPortCombo->isEnabled(),
                "Remote RTCM sink keeps GGA on the generated EPSILON source");
        require(remoteRefreshPortsButton && remoteAutoDetectPortsButton && remoteGgaToggleButton &&
                    !remoteRefreshPortsButton->isEnabled() &&
                    !remoteAutoDetectPortsButton->isEnabled() &&
                    !remoteGgaToggleButton->isEnabled(),
                "Remote RTCM sink disables local serial refresh, auto-detect, and GGA port controls");
    }

    QTcpServer caster;
    require(caster.listen(QHostAddress::LocalHost, 0), "local sourcetable test server starts");
    int mountpointRequestCount = 0;
    QObject::connect(&caster, &QTcpServer::newConnection, [&caster, &mountpointRequestCount]() {
        while (QTcpSocket *socket = caster.nextPendingConnection())
        {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &mountpointRequestCount]() {
                socket->readAll();
                ++mountpointRequestCount;
                const QByteArray body =
                    "STR;AUTO;Auto mountpoint;RTCM 3;1004(1);2;GPS;NONE;B;N;0;0;VaporView;none;B;N;0;\r\n"
                    "STR;PERSISTED_MOUNTPOINT;Saved mountpoint;RTCM 3;1004(1);2;GPS;NONE;B;N;0;0;VaporView;none;B;N;0;\r\n"
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
    rtkSettings.setValue(QStringLiteral("server"), QStringLiteral("127.0.0.1"));
    rtkSettings.setValue(QStringLiteral("port"), QString::number(caster.serverPort()));
    rtkSettings.setValue(QStringLiteral("output_port"), QStringLiteral("__missing_serial_port__"));
    rtkSettings.setValue(QStringLiteral("mountpoint"), QStringLiteral("PERSISTED_MOUNTPOINT"));
    rtkSettings.setValue(QStringLiteral("mountpoint_confirmed"), true);
    rtkSettings.sync();

    RtkConfigDialog dialog(nullptr, false);
    dialog.show();
    QApplication::processEvents();
    auto *outputPortCombo = dialog.findChild<QComboBox *>(QStringLiteral("rtkOutputPortCombo"));
    require(outputPortCombo != nullptr, "RTK output-port combo exists");
    VaporViewTest::requireComboPopupStyled(outputPortCombo,
                                           "RTK output-port selector has the shared popup highlight",
                                           require);
    require(outputPortCombo->findText(QStringLiteral("__missing_serial_port__")) < 0 &&
                outputPortCombo->currentText() == QStringLiteral("未选择"),
            "unavailable legacy RTK output port shows the Chinese unselected placeholder");
    auto *mountpointCombo = dialog.findChild<QComboBox *>(QStringLiteral("rtkMountpointCombo"));
    require(mountpointCombo != nullptr, "RTK mountpoint combo exists");
    require(mountpointCombo->property("usesSingleLevelPopupMenu").toBool(),
            "RTK mountpoint combo uses the single-level popup implementation");
    auto *serverEdit = dialog.findChild<QLineEdit *>(QStringLiteral("rtkServerEdit"));
    auto *portEdit = dialog.findChild<QLineEdit *>(QStringLiteral("rtkPortEdit"));
    require(serverEdit && portEdit, "RTK server and port edits exist");
    require(serverEdit->text() == QStringLiteral("127.0.0.1") &&
                portEdit->text() == QString::number(caster.serverPort()),
            "saved real caster settings are loaded before startup mountpoint detection");
    auto *usernameEdit = dialog.findChild<QLineEdit *>(QStringLiteral("rtkUsernameEdit"));
    auto *passwordEdit = dialog.findChild<QLineEdit *>(QStringLiteral("rtkPasswordEdit"));
    auto *timeoutCombo = dialog.findChild<QComboBox *>(QStringLiteral("rtkTimeoutCombo"));
    auto *reconnectCombo = dialog.findChild<QComboBox *>(QStringLiteral("rtkReconnectCombo"));
    auto *testConnectionButton =
        dialog.findChild<QPushButton *>(QStringLiteral("rtkTestConnectionButton"));
    auto *startButton = dialog.findChild<QPushButton *>(QStringLiteral("rtkStartButton"));
    auto *stopButton = dialog.findChild<QPushButton *>(QStringLiteral("rtkStopButton"));
    auto *clearLogButton = dialog.findChild<QPushButton *>(QStringLiteral("rtkClearLogButton"));
    auto *ntripPanel = dialog.findChild<QWidget *>(QStringLiteral("rtkNtripConfigPanel"));
    auto *streamPanel = dialog.findChild<QWidget *>(QStringLiteral("rtkStreamStatusPanel"));
    auto *epsilonPanel = dialog.findChild<QWidget *>(QStringLiteral("rtkEpsilonDataPathPanel"));
    auto *messageArea = dialog.findChild<QWidget *>(QStringLiteral("rtkStatusMessageArea"));
    auto *serviceOperationsPanel =
        dialog.findChild<QWidget *>(QStringLiteral("rtkServiceOperationsPanel"));
    auto *streamInputValue =
        dialog.findChild<QLabel *>(QStringLiteral("rtkStreamInputStatusValue"));
    auto *rtcmOutputValue =
        dialog.findChild<QLabel *>(QStringLiteral("rtkRtcmOutputStatusValue"));
    auto *scrollArea = dialog.findChild<QScrollArea *>(QStringLiteral("rtkConfigScrollArea"));
    require(usernameEdit && passwordEdit && timeoutCombo && reconnectCombo &&
                testConnectionButton && startButton && stopButton && clearLogButton &&
                ntripPanel && streamPanel && epsilonPanel && messageArea && serviceOperationsPanel &&
                streamInputValue && rtcmOutputValue && scrollArea,
            "Stage 5 RTK panels, controls, status values, and scroll area exist");
    require(dialog.findChildren<QGroupBox *>(QStringLiteral("sensorGroupBox")).size() == 5,
            "differential-positioning page keeps five business cards including service operations");
    require(ntripPanel->isAncestorOf(serverEdit) && ntripPanel->isAncestorOf(portEdit) &&
                ntripPanel->isAncestorOf(mountpointCombo) && ntripPanel->isAncestorOf(usernameEdit) &&
                ntripPanel->isAncestorOf(passwordEdit) &&
                epsilonPanel->isAncestorOf(outputPortCombo) &&
                epsilonPanel->isAncestorOf(timeoutCombo) &&
                epsilonPanel->isAncestorOf(reconnectCombo),
            "NTRIP credentials stay in the top panel while output timing stays with RTCM output");
    require(serviceOperationsPanel->isAncestorOf(startButton) &&
                serviceOperationsPanel->isAncestorOf(stopButton) &&
                serviceOperationsPanel->isAncestorOf(testConnectionButton) &&
                serviceOperationsPanel->isAncestorOf(clearLogButton),
            "start, stop, test, and clear actions belong to the bottom service-operations panel");
    require(streamPanel->isAncestorOf(streamInputValue) &&
                epsilonPanel->isAncestorOf(outputPortCombo) &&
                epsilonPanel->isAncestorOf(rtcmOutputValue),
            "reliable stream and RTCM output status belong to their business panels");
    require(streamInputValue->text() == QStringLiteral("--") &&
                rtcmOutputValue->text() == QStringLiteral("--"),
            "inactive stream statistics stay unavailable instead of being inferred");
    QGroupBox *configCard = ancestorCard(ntripPanel);
    QGroupBox *streamCard = ancestorCard(streamPanel);
    QGroupBox *epsilonCard = ancestorCard(epsilonPanel);
    QGroupBox *messageCard = ancestorCard(messageArea);
    require(scrollArea->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff &&
                scrollArea->horizontalScrollBar()->maximum() == 0,
            "embedded-capable RTK content never exposes a horizontal scrollbar");
    require(passwordEdit->echoMode() == QLineEdit::Normal,
            "Stage 5 preserves the existing password echo behavior");

    QGroupBox *operationsCard = ancestorCard(serviceOperationsPanel);
    require(configCard && streamCard && epsilonCard && messageCard && operationsCard,
            "each differential-positioning business region has a top-level card");
    const QRect configBounds(configCard->mapTo(&dialog, QPoint(0, 0)), configCard->size());
    const QRect streamBounds(streamCard->mapTo(&dialog, QPoint(0, 0)), streamCard->size());
    const QRect epsilonBounds(epsilonCard->mapTo(&dialog, QPoint(0, 0)), epsilonCard->size());
    const QRect messageBounds(messageCard->mapTo(&dialog, QPoint(0, 0)), messageCard->size());
    const QRect operationsBounds(operationsCard->mapTo(&dialog, QPoint(0, 0)), operationsCard->size());
    require(std::abs(configBounds.top() - streamBounds.top()) <= 1 &&
                configBounds.left() < streamBounds.left() &&
                configBounds.width() > streamBounds.width() &&
                std::abs(epsilonBounds.top() - messageBounds.top()) <= 1 &&
                epsilonBounds.left() < messageBounds.left() &&
                messageBounds.width() > epsilonBounds.width() &&
                epsilonBounds.top() > std::max(configBounds.bottom(), streamBounds.bottom()) &&
                operationsBounds.top() > std::max(epsilonBounds.bottom(), messageBounds.bottom()),
            "page follows NTRIP/GGA, RTCM/log, and full-width operations rows");

    const QList<QWidget *> accessibleControls = {
        serverEdit, portEdit, mountpointCombo, usernameEdit, passwordEdit,
        timeoutCombo, reconnectCombo, testConnectionButton, startButton, stopButton,
        outputPortCombo, streamInputValue, rtcmOutputValue,
    };
    for (QWidget *control : accessibleControls)
    {
        require(!control->accessibleName().trimmed().isEmpty(),
                "Stage 5 key controls expose accessible names");
    }

    const QList<QWidget *> initialTabOrder = {
        serverEdit, portEdit, mountpointCombo, usernameEdit, passwordEdit,
        dialog.findChild<QPushButton *>(QStringLiteral("rtkFetchMountpointsButton")),
    };
    serverEdit->setFocus(Qt::TabFocusReason);
    for (int index = 1; index < initialTabOrder.size(); ++index)
    {
        QWidget *focused = QApplication::focusWidget();
        QKeyEvent tabPress(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QKeyEvent tabRelease(QEvent::KeyRelease, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(focused, &tabPress);
        QApplication::sendEvent(QApplication::focusWidget(), &tabRelease);
        QApplication::processEvents();
        QWidget *expected = initialTabOrder.at(index);
        QWidget *actual = QApplication::focusWidget();
        require(actual == expected || expected->isAncestorOf(actual) || expected->focusProxy() == actual,
                "NTRIP tab order follows the visual field order");
    }
    dialog.setPreferredOutputPortAndBaud(QStringLiteral("COM77"), QStringLiteral("115200"));
    auto *baudrateCombo = dialog.findChild<QComboBox *>(QStringLiteral("rtkBaudrateCombo"));
    const int rememberedPortIndex = outputPortCombo->findText(QStringLiteral("COM77"));
    require(baudrateCombo && baudrateCombo->isEditable() &&
                baudrateCombo->property("_vv_serial_baud_rate_combo").toBool(),
            "RTK output baud selector accepts custom host serial values");
    dialog.setPreferredOutputPortAndBaud(QStringLiteral("COM77"), QStringLiteral("123457"));
    require(rememberedPortIndex >= 0 &&
                outputPortCombo->currentIndex() == rememberedPortIndex &&
                outputPortCombo->itemData(
                    rememberedPortIndex,
                    VaporView::kSerialPortHistoryItemRole).toBool(),
            "explicit RTK serial history is retained and marked as history");
    require(baudrateCombo->currentText() == QStringLiteral("123457"),
            "RTK custom host baud round-trips through the visible output configuration");
    dialog.setPreferredOutputPortAndBaud(QStringLiteral("COM77"), QStringLiteral("115200"));
    auto *fetchMountpointsButton = dialog.findChild<QPushButton *>(QStringLiteral("rtkFetchMountpointsButton"));
    require(serverEdit && portEdit && fetchMountpointsButton, "mountpoint fetch controls exist");
    const int mountpointComboWidth = mountpointCombo->width();
    const int detectButtonWidth = fetchMountpointsButton->width();
    require(processEventsUntil(5000, [&dialog, mountpointCombo, &mountpointRequestCount]() {
                return mountpointRequestCount >= 1 && !dialog.hasActiveExternalOperation() &&
                    mountpointCombo->findText(QStringLiteral("RTCM32_GPS_LONG_WIDEST")) >= 0 &&
                    mountpointCombo->currentText() == QStringLiteral("PERSISTED_MOUNTPOINT");
            }),
            "startup mountpoint detection loads the sourcetable and selects the saved mountpoint");
    rtkSettings.sync();
    require(rtkSettings.value(QStringLiteral("mountpoint")).toString() ==
                QStringLiteral("PERSISTED_MOUNTPOINT") &&
                rtkSettings.value(QStringLiteral("mountpoint_confirmed")).toBool(),
            "startup mountpoint detection persists the matched mountpoint");
    require(mountpointComboWidth >= 150 && mountpointComboWidth > detectButtonWidth,
            "mountpoint field receives more horizontal space than its adjacent detect action");
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
    auto *ggaSourceCombo = dialog.findChild<QComboBox *>(QStringLiteral("rtkGgaPortCombo"));
    auto *ggaClearLogButton =
        dialog.findChild<QToolButton *>(QStringLiteral("rtkGgaClearLogButton"));
    auto *ggaMonitorLog = dialog.findChild<QTextEdit *>(QStringLiteral("rtkGgaTextEdit"));
    QGroupBox *ggaCard = ancestorCard(ggaMonitorLog);
    require(ggaToggleButton && ggaSourceCombo && ggaClearLogButton && ggaMonitorLog && ggaCard,
            "GGA monitor controls, internal log, and card exist");
    VaporViewTest::requireComboPopupStyled(ggaSourceCombo,
                                           "RTK GGA source selector has the shared popup highlight",
                                           require);
    require(ancestorCard(ggaSourceCombo) == ggaCard &&
                ancestorCard(ggaClearLogButton) == ggaCard &&
                streamPanel->isAncestorOf(ggaToggleButton) &&
                streamPanel->isAncestorOf(ggaMonitorLog),
            "GGA source, controls, and reminder output stay inside the GGA monitor card");
    const QFontMetrics ggaSourceMetrics(ggaSourceCombo->font());
    const int ggaSourceExtraWidth =
        ggaSourceCombo->width() - ggaSourceMetrics.horizontalAdvance(ggaSourceCombo->currentText());
    require(ggaSourceExtraWidth >= 32,
            "GGA source combo leaves usable room for its generated source label");
    const QPoint ggaSourceTopLeft = ggaSourceCombo->mapTo(&dialog, QPoint(0, 0));
    const QPoint ggaClearTopLeft = ggaClearLogButton->mapTo(&dialog, QPoint(0, 0));
    require(std::abs((ggaClearTopLeft.y() + ggaClearLogButton->height() / 2) -
                     (ggaSourceTopLeft.y() + ggaSourceCombo->height() / 2)) <= 2 &&
                ggaClearTopLeft.x() >= ggaSourceTopLeft.x() + ggaSourceCombo->width(),
            "GGA clear-log action remains at the title bar's upper-right edge");
    const int clearButtonRightGap = ggaClearLogButton->parentWidget()->width() -
        ggaClearLogButton->geometry().right() - 1;
    require(clearButtonRightGap >= 0 && clearButtonRightGap <= 12,
            "GGA clear-log action follows the card-title right margin");
    require(!ggaClearLogButton->icon().isNull() &&
                ggaClearLogButton->iconSize() == QSize(24, 24) &&
                ggaClearLogButton->size() == QSize(34, 34) &&
                ggaClearLogButton->toolTip() == QStringLiteral("清空 GGA 日志"),
            "GGA clear-log action matches the main log-card icon-button presentation");
    const int ggaControlsCenter =
        ggaToggleButton->parentWidget()->mapTo(&dialog, QPoint(0, 0)).y() +
        ggaToggleButton->parentWidget()->height() / 2;
    const int ggaLogCenter =
        ggaMonitorLog->mapTo(&dialog, QPoint(0, 0)).y() + ggaMonitorLog->height() / 2;
    require(std::abs(ggaControlsCenter - ggaLogCenter) <= 2,
            "GGA read controls are vertically centered beside the GGA log");
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
    require(processEventsUntil(2000, [ggaMonitorLog]() {
                return ggaMonitorLog->toPlainText().split(
                    QLatin1Char('\n'), Qt::SkipEmptyParts).size() >= 2;
            }),
            "GGA monitor adds its first counted reminder after two seconds without data");
    QStringList repeatedGgaLogLines =
        ggaMonitorLog->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    require(repeatedGgaLogLines.size() == 2 &&
                (repeatedGgaLogLines.constLast().contains(QStringLiteral("第 1 次提醒")) ||
                 repeatedGgaLogLines.constLast().contains(QStringLiteral("Reminder 1"))) &&
                (repeatedGgaLogLines.constLast().contains(QStringLiteral("秒未收到有效 GGA")) ||
                 repeatedGgaLogLines.constLast().contains(QStringLiteral("no valid GGA for"))) &&
                (repeatedGgaLogLines.constLast().contains(QStringLiteral("正在等待有效的 EPSILON 主串口定位")) ||
                 repeatedGgaLogLines.constLast().contains(QStringLiteral("Waiting for valid EPSILON main-port position"))),
            "GGA waiting heartbeat includes a reminder count, elapsed time, and the current reason");
    const QRegularExpression elapsedSecondsPattern(
        QStringLiteral("(?:已连续\\s*(\\d+)\\s*秒未收到有效 GGA|no valid GGA for\\s*(\\d+)\\s*s)"));
    const auto elapsedSecondsFromLine = [&elapsedSecondsPattern](const QString& line) {
        const QRegularExpressionMatch match = elapsedSecondsPattern.match(line);
        if (!match.hasMatch())
        {
            return -1;
        }
        return (match.captured(1).isEmpty() ? match.captured(2) : match.captured(1)).toInt();
    };
    const int firstReminderSeconds = elapsedSecondsFromLine(repeatedGgaLogLines.constLast());
    require(firstReminderSeconds >= 2,
            "the first GGA waiting reminder reports at least two elapsed seconds");
    require(processEventsUntil(2500, [ggaMonitorLog]() {
                return ggaMonitorLog->toPlainText().split(
                    QLatin1Char('\n'), Qt::SkipEmptyParts).size() >= 3;
            }),
            "GGA monitor adds a second counted reminder when data is still absent");
    repeatedGgaLogLines =
        ggaMonitorLog->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const int secondReminderSeconds = elapsedSecondsFromLine(repeatedGgaLogLines.constLast());
    require(repeatedGgaLogLines.size() == 3 &&
                (repeatedGgaLogLines.constLast().contains(QStringLiteral("第 2 次提醒")) ||
                 repeatedGgaLogLines.constLast().contains(QStringLiteral("Reminder 2"))) &&
                secondReminderSeconds > firstReminderSeconds,
            "successive GGA waiting reminders increment both count and elapsed time");
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
    require(stoppedGgaLogLines.size() == 4 &&
                stoppedGgaLogLines.constLast().startsWith(QLatin1Char('[')) &&
                stoppedGgaLogLines.constLast().contains(QStringLiteral("] ")),
            "GGA stop status is appended once with a timestamp");
    require(ggaCard->height() == ggaCardHeightBeforeReading,
            "GGA stop status log does not change the monitor card height");
    ggaClearLogButton->click();
    require(ggaMonitorLog->toPlainText().isEmpty(),
            "GGA clear-log action clears only the monitor output");

    mountpointCombo->setCurrentText(QStringLiteral("MISSING_MOUNTPOINT"));
    const int expectedMountpointRequestCount = mountpointRequestCount + 1;
    fetchMountpointsButton->click();
    require(processEventsUntil(4000, [&dialog, &mountpointRequestCount, expectedMountpointRequestCount]() {
                return mountpointRequestCount >= expectedMountpointRequestCount &&
                    !dialog.hasActiveExternalOperation();
            }),
            "mountpoint detection populates the dropdown from the sourcetable");
    require(mountpointCombo->findText(QStringLiteral("AUTO")) >= 0,
            "detected AUTO is kept as a real mountpoint option");
    require(mountpointCombo->currentText() == QStringLiteral("请选择挂载点"),
            "detected mountpoints fall back to the select prompt when the saved mountpoint is absent");
    rtkSettings.sync();
    require(rtkSettings.value(QStringLiteral("mountpoint")).toString().isEmpty() &&
                !rtkSettings.value(QStringLiteral("mountpoint_confirmed")).toBool(),
            "missing mountpoint selection is persisted as an empty unconfirmed mountpoint");
    require(mountpointCombo->width() == mountpointComboWidth,
            "mountpoint combo keeps its widened width after fetching long mountpoints");
    require(fetchMountpointsButton->width() == detectButtonWidth,
            "mountpoint detect button keeps its stable action width after fetching long names");
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
                "mountpoint popup opens without a default selected-row highlight");
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
    auto *serviceLogClearButton =
        dialog.findChild<QToolButton *>(QStringLiteral("rtkServiceLogClearButton"));
    require(testButton && serviceLog && serviceLogClearButton,
            "RTK test button, service log, and title-bar clear action exist");
    require(messageArea->isAncestorOf(serviceLog) &&
                ancestorCard(serviceLogClearButton) == ancestorCard(serviceLog),
            "RTK service messages and their clear action belong to the status/message area");
    require(serviceLog->styleSheet().contains(expectedLogBackground, Qt::CaseInsensitive),
            "RTK service log uses the raised log-panel background");
    require(!serviceLogClearButton->icon().isNull() &&
                serviceLogClearButton->iconSize() == QSize(24, 24) &&
                serviceLogClearButton->size() == QSize(34, 34) &&
                serviceLogClearButton->toolTip() == QStringLiteral("清空 RTK 服务日志"),
            "RTK service-log clear action matches the main log-card icon-button presentation");
    const int serviceClearButtonRightGap = serviceLogClearButton->parentWidget()->width() -
        serviceLogClearButton->geometry().right() - 1;
    require(serviceClearButtonRightGap >= 0 && serviceClearButtonRightGap <= 12,
            "RTK service-log clear action follows the card title-bar right margin");
    serviceLog->setPlainText(QStringLiteral("service log sentinel"));
    serviceLogClearButton->click();
    require(serviceLog->toPlainText().isEmpty(),
            "RTK service-log title action clears the service log");
    dialog.appendLog(QStringLiteral("RTK 服务日志格式验证"));
    const QString serviceLogText = serviceLog->toPlainText();
    const QStringList serviceLogLines =
        serviceLogText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    require(serviceLogLines.size() == 1 &&
                serviceLogLines.constFirst().startsWith(QLatin1Char('[')) &&
                serviceLogLines.constFirst().contains(QStringLiteral("] RTK 服务日志格式验证")),
            "RTK service log keeps the timestamp and message on one line");
    serviceLogClearButton->click();
    require(serviceLog->toPlainText().isEmpty(),
            "RTK service-log clear action resets the formatting probe");
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
    require(epsilonPanel->isAncestorOf(xEdit) && epsilonPanel->isAncestorOf(yEdit) &&
                epsilonPanel->isAncestorOf(zEdit) && epsilonPanel->isAncestorOf(applyButton),
            "existing RTCM target and lever-arm controls stay inside the EPSILON data-path panel");
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

    {
        QStackedWidget pageHost;
        pageHost.resize(960, 720);
        auto *embeddedDialog = new RtkConfigDialog(&pageHost, true);
        embeddedDialog->setUiTestMode(true);
        auto *siblingPage = new QWidget(&pageHost);
        pageHost.addWidget(embeddedDialog);
        pageHost.addWidget(siblingPage);
        pageHost.setCurrentWidget(embeddedDialog);
        pageHost.show();
        QApplication::processEvents();

        QPointer<RtkConfigDialog> originalDialog(embeddedDialog);
        require(QMetaObject::invokeMethod(
                    embeddedDialog, "onStartClicked", Qt::DirectConnection),
                "embedded RTK service can start in UI test mode");
        require(embeddedDialog->isRunning(),
                "embedded RTK service reports running before an internal page switch");

        pageHost.setCurrentWidget(siblingPage);
        QApplication::processEvents();
        require(!originalDialog.isNull() && originalDialog == embeddedDialog &&
                    !embeddedDialog->isVisible() && embeddedDialog->isRunning(),
                "hiding the embedded RTK page preserves its instance and running service");

        pageHost.setCurrentWidget(embeddedDialog);
        QApplication::processEvents();
        require(embeddedDialog->isVisible() && embeddedDialog->isRunning(),
                "returning to the embedded RTK page preserves its running service");
        require(QMetaObject::invokeMethod(
                    embeddedDialog, "onStopClicked", Qt::DirectConnection),
                "embedded RTK service can stop after an internal page switch");
        require(!embeddedDialog->isRunning(),
                "embedded RTK service stops only when explicitly requested");
    }

    {
        QTemporaryDir logDir;
        require(logDir.isValid(), "temporary RTK log directory");
        VaporView::LogService logs(QStringLiteral("RtkCorrelationTest"), &app, logDir.path());
        QList<VaporView::LogRecord> records;
        QList<VaporView::LogRecord> rawRecords;
        QObject::connect(&logs, &VaporView::LogService::recordPublished, &logs,
                         [&records, &rawRecords](const VaporView::LogRecord& record) {
            if (record.category == QStringLiteral("rtcm.lifecycle")) records.append(record);
            if (record.category == QStringLiteral("service.raw")) rawRecords.append(record);
        }, Qt::DirectConnection);
        RtkConfigDialog isolated(nullptr, false);
        isolated.setUiTestMode(true);
        for (int i = 0; i < 2; ++i) {
            require(QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection), "start simulated RTCM");
            require(QMetaObject::invokeMethod(&isolated, "onStopClicked", Qt::DirectConnection), "stop simulated RTCM");
        }
        require(records.size() == 6, "RTCM emits lifecycle records only at operation boundaries");
        for (int i = 0; i < 2; ++i) {
            require(records[i * 3].fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_start_requested") &&
                    records[i * 3 + 1].fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_started") &&
                    records[i * 3 + 2].fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_stopped"),
                    "RTCM lifecycle records preserve stable event names");
        }
        require(!records[0].correlation_id.isEmpty() && records[0].correlation_id == records[1].correlation_id &&
                records[1].correlation_id == records[2].correlation_id, "first RTCM lifecycle retains ID");
        require(!records[3].correlation_id.isEmpty() && records[3].correlation_id != records[0].correlation_id &&
                records[3].correlation_id == records[4].correlation_id && records[4].correlation_id == records[5].correlation_id,
                "next RTCM lifecycle gets a fresh ID");
        require(records[0].session_id.isEmpty(), "no invented session ID for RTCM stream");
        records.clear();
        bool nestedStart = false;
        const auto startReentry = QObject::connect(&logs, &VaporView::LogService::recordPublished, &isolated,
            [&](const VaporView::LogRecord& record) {
                if (!nestedStart && record.fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_start_requested")) {
                    nestedStart = true;
                    QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection);
                }
            }, Qt::DirectConnection);
        require(QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection), "start with synchronous log reentry");
        QObject::disconnect(startReentry);
        require(nestedStart && isolated.isRunning() && records.size() == 2 &&
                records[0].correlation_id == records[1].correlation_id,
                "recursive start is suppressed without changing the operation identity");
        const QString oldId = records[0].correlation_id;
        bool restarted = false;
        const auto stopReentry = QObject::connect(&isolated, &RtkConfigDialog::rtkRunningChanged, &isolated,
            [&](bool running) {
                if (!running && !restarted) {
                    restarted = true;
                    QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection);
                }
            }, Qt::DirectConnection);
        require(QMetaObject::invokeMethod(&isolated, "onStopClicked", Qt::DirectConnection), "stop with synchronous restart");
        QObject::disconnect(stopReentry);
        require(restarted && isolated.isRunning() && isolated.rtcmCorrelationId() != oldId &&
                records.last().fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_stopped") &&
                records.last().correlation_id == oldId,
                "old stop retains its identity and leaves the restarted stream intact");
        require(QMetaObject::invokeMethod(&isolated, "onStopClicked", Qt::DirectConnection), "stop restarted stream");
        records.clear();
        require(QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection), "start status reentry fixture");
        const QString failedId = isolated.rtcmCorrelationId();
        bool statusRestarted = false;
        const auto statusReentry = QObject::connect(&isolated, &RtkConfigDialog::rtkRunningChanged, &isolated,
            [&](bool running) {
                if (!running && !statusRestarted) {
                    statusRestarted = true;
                    QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection);
                }
            }, Qt::DirectConnection);
        RtkStreamStats failedStatus;
        failedStatus.running = false;
        failedStatus.message = QStringLiteral("previous stream failed");
        RtkStatusLogTestAccess::handle(isolated, failedStatus);
        QObject::disconnect(statusReentry);
        require(statusRestarted && isolated.isRunning() && isolated.rtcmCorrelationId() != failedId &&
                records.last().fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_unexpected_stop") &&
                records.last().correlation_id == failedId,
                "old status failure retains its identity without clearing the restarted stream");
        require(QMetaObject::invokeMethod(&isolated, "onStopClicked", Qt::DirectConnection), "stop status reentry fixture");
        VaporView::LogRecord located;
        const auto locationConnection = QObject::connect(&logs, &VaporView::LogService::recordPublished, &isolated,
            [&](const VaporView::LogRecord& record) {
                if (record.fields.value(QStringLiteral("event")) == QStringLiteral("rtk_service_log")) located = record;
            }, Qt::DirectConnection);
        const int expectedLine = __LINE__ + 1;
        isolated.appendLog(QStringLiteral("context location test"), VaporView::LogContext{oldId, {}});
        QObject::disconnect(locationConnection);
        require(located.correlation_id == oldId && located.source_location.line == expectedLine &&
                located.source_location.file.endsWith(QStringLiteral("rtk_config_dialog_test.cpp")),
                "RTCM forwarding log retains the actual caller location");
        records.clear();
        isolated.setUiTestMode(false);
        auto *serverEdit = isolated.findChild<QLineEdit *>(QStringLiteral("rtkServerEdit"));
        require(serverEdit, "RTCM server input exists for validation failure");
        serverEdit->clear();
        QString validationMessage;
        QTimer closeValidation;
        QObject::connect(&closeValidation, &QTimer::timeout, &isolated, [&validationMessage] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                validationMessage = box->text();
                box->accept();
            }
        });
        closeValidation.start(10);
        require(QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection),
                "invoke real RTCM start with invalid configuration");
        closeValidation.stop();
        require(records.size() == 2 &&
                records[0].fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_start_requested") &&
                records[1].fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_start_failed"),
                "invalid RTCM configuration emits requested and failed lifecycle records");
        require(!records[0].correlation_id.isEmpty() && records[0].correlation_id == records[1].correlation_id &&
                !validationMessage.isEmpty() && records[1].fields.value(QStringLiteral("reason")).toString() == validationMessage,
                "RTCM failure record preserves the operation identity and actual validation reason");

        records.clear();
        isolated.setUiTestMode(true);
        require(QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection), "start RTCM status fixture");
        require(records.size() == 2 && !records[0].correlation_id.isEmpty(), "status fixture gets a stream identity");
        const QString streamId = records[0].correlation_id;
        rawRecords.clear();
        RtkStreamStats running;
        running.running = true;
        running.message = QStringLiteral("caster retry");
        RtkStatusLogTestAccess::handle(isolated, running);
        isolated.appendRawLogLine(QStringLiteral("independent no-signal task"));
        require(rawRecords.size() == 2 && rawRecords[0].correlation_id == streamId &&
                rawRecords[1].correlation_id.isEmpty(), "stream status retains identity and independent diagnostics do not inherit it");
        RtkStreamStats stopped;
        stopped.running = false;
        stopped.message = QStringLiteral("caster authentication rejected");
        RtkStatusLogTestAccess::handle(isolated, stopped);
        require(rawRecords.size() == 3 && rawRecords[2].correlation_id == streamId &&
                records.last().fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_unexpected_stop") &&
                records.last().fields.value(QStringLiteral("reason")).toString() == stopped.message &&
                records.last().correlation_id == streamId, "unexpected stop retains stream identity and actual reason");
        isolated.appendRawLogLine(QStringLiteral("after stop"));
        require(rawRecords.last().correlation_id.isEmpty(), "post-stop diagnostic has no stale stream identity");

        QString recordingSession;
        isolated.setRecordingSessionProvider([&] { return recordingSession; });
        records.clear(); rawRecords.clear();
        require(QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection), "restart RTCM with session provider");
        const QString restartId = records.first().correlation_id;
        require(!restartId.isEmpty() && records.first().session_id.isEmpty(), "stream starts without an invented session");
        recordingSession = QStringLiteral("actual-local-session");
        RtkStatusLogTestAccess::handle(isolated, running);
        require(rawRecords.last().correlation_id == restartId && rawRecords.last().session_id == recordingSession,
                "running stream status captures the current session");
        RtkStatusLogTestAccess::handle(isolated, running);
        require(rawRecords.last().session_id == recordingSession, "paused session remains associated while open");
        recordingSession.clear();
        RtkStatusLogTestAccess::handle(isolated, running);
        require(rawRecords.last().session_id.isEmpty(), "stream status after session stop has no stale session");
        isolated.appendRawLogLine(QStringLiteral("independent no-signal task"));
        require(rawRecords.last().correlation_id.isEmpty() && rawRecords.last().session_id.isEmpty(),
                "independent status does not inherit stream or session");
        isolated.appendLog(QStringLiteral("delayed failure"), {restartId, {}});
        require(QMetaObject::invokeMethod(&isolated, "onStopClicked", Qt::DirectConnection), "stop RTCM provider fixture");

        recordingSession = QStringLiteral("cancel-session");
        records.clear();
        const auto cancelRequested = QObject::connect(&logs, &VaporView::LogService::recordPublished, &isolated,
            [&](const VaporView::LogRecord& record) {
                if (record.fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_start_requested"))
                    QMetaObject::invokeMethod(&isolated, "onStopClicked", Qt::DirectConnection);
            }, Qt::DirectConnection);
        require(QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection), "cancel simulated RTCM request");
        QObject::disconnect(cancelRequested);
        require(!isolated.isRunning() && isolated.rtcmCorrelationId().isEmpty() && records.size() == 2 &&
                records[1].fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_start_cancelled") &&
                !records[0].correlation_id.isEmpty() && records[0].correlation_id == records[1].correlation_id &&
                records[1].session_id == recordingSession, "cancelled request retains operation and actual session");
        const QString cancelledId = records[0].correlation_id;

        bool transportDisabled = false;
        const auto disableDuringStart = QObject::connect(&logs, &VaporView::LogService::recordPublished, &isolated,
            [&](const VaporView::LogRecord& record) {
                if (!transportDisabled && record.fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_start_requested")) {
                    transportDisabled = true;
                    isolated.setTransportSupported(false, QStringLiteral("transport unavailable"));
                }
            }, Qt::DirectConnection);
        records.clear();
        require(QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection), "disable transport during RTCM start");
        QObject::disconnect(disableDuringStart);
        require(transportDisabled && !isolated.isRunning() && isolated.rtcmCorrelationId().isEmpty() &&
                records.size() == 2 && records[1].fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_start_cancelled") &&
                !records[0].correlation_id.isEmpty() && records[0].correlation_id == records[1].correlation_id &&
                records[1].session_id == recordingSession,
                "transport loss cancels the starting operation with its original context");
        isolated.setTransportSupported(true);

        isolated.setUiTestMode(false);
        isolated.setRtcmCorrectionSink([](const QByteArray&) { return true; }, QStringLiteral("Remote Sky"));
        auto *cancelServer = isolated.findChild<QLineEdit *>(QStringLiteral("rtkServerEdit"));
        auto *cancelMountpoint = isolated.findChild<QComboBox *>(QStringLiteral("rtkMountpointCombo"));
        require(cancelServer && cancelMountpoint, "RTCM cancellation configuration controls exist");
        cancelServer->setText(QStringLiteral("127.0.0.1"));
        cancelMountpoint->setEditText(QStringLiteral("TEST"));
        bool providerCancelled = false;
        isolated.setEpsilonDataProvider([&] {
            providerCancelled = true;
            QMetaObject::invokeMethod(&isolated, "onStopClicked", Qt::DirectConnection);
            return VaporView::EpsilonData{};
        });
        records.clear();
        require(QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection), "cancel RTCM config callback");
        require(providerCancelled && !isolated.isRunning() && !RtkStatusLogTestAccess::hasSink(isolated) &&
                records.size() == 2 && records[1].fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_start_cancelled") &&
                records[0].correlation_id == records[1].correlation_id && records[1].session_id == recordingSession,
                "configuration callback cancellation preserves context and never opens sink");

        isolated.setEpsilonDataProvider([] { return VaporView::EpsilonData{}; });
        bool sinkWasOpen = false;
        const auto cancelAfterSink = QObject::connect(&logs, &VaporView::LogService::recordPublished, &isolated,
            [&](const VaporView::LogRecord& record) {
                if (!sinkWasOpen && record.fields.value(QStringLiteral("event")) == QStringLiteral("rtk_service_log") &&
                    RtkStatusLogTestAccess::hasSink(isolated)) {
                    sinkWasOpen = true;
                    QMetaObject::invokeMethod(&isolated, "onStopClicked", Qt::DirectConnection);
                }
            }, Qt::DirectConnection);
        records.clear();
        require(QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection), "cancel RTCM after sink opens");
        QObject::disconnect(cancelAfterSink);
        require(sinkWasOpen && !isolated.isRunning() && !RtkStatusLogTestAccess::hasSink(isolated) &&
                isolated.rtcmCorrelationId().isEmpty() && records.size() == 2 &&
                records[1].fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_start_cancelled") &&
                records[0].correlation_id == records[1].correlation_id && records[1].session_id == recordingSession,
                "cancelling after sink creation closes it and retains context");

        bool sinkChanged = false;
        const auto changeSinkDuringStart = QObject::connect(&logs, &VaporView::LogService::recordPublished, &isolated,
            [&](const VaporView::LogRecord& record) {
                if (!sinkChanged && record.fields.value(QStringLiteral("event")) == QStringLiteral("rtk_service_log") &&
                    RtkStatusLogTestAccess::hasSink(isolated)) {
                    sinkChanged = true;
                    isolated.setRtcmCorrectionSink([](const QByteArray&) { return true; }, QStringLiteral("New Sky"));
                }
            }, Qt::DirectConnection);
        records.clear();
        require(QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection), "change sink during RTCM start");
        QObject::disconnect(changeSinkDuringStart);
        require(sinkChanged && !isolated.isRunning() && !RtkStatusLogTestAccess::hasSink(isolated) &&
                isolated.rtcmCorrelationId().isEmpty() && records.size() == 2 &&
                records[1].fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_stream_start_cancelled") &&
                !records[0].correlation_id.isEmpty() && records[0].correlation_id == records[1].correlation_id &&
                records[1].session_id == recordingSession,
                "sink change cancels the starting operation and closes its listener");
        isolated.setUiTestMode(true);
        records.clear();
        require(QMetaObject::invokeMethod(&isolated, "onStartClicked", Qt::DirectConnection) && isolated.isRunning() &&
                records.first().correlation_id != cancelledId, "normal start after cancellation gets a fresh identity");
        require(QMetaObject::invokeMethod(&isolated, "onStopClicked", Qt::DirectConnection), "stop post-cancellation stream");
    }

    std::cout << "rtk_config_dialog_test passed\n";
    return 0;
}
