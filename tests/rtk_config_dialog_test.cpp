#include "ground/rtk/RtkConfigDialog.h"
#include "ground/widgets/SerialPortComboSupport.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/SingleLevelPopupComboBox.h"
#include "shared/theme/SingleLevelPopupMenu.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
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

    QSettings rtkSettings(QStringLiteral("VaporView"), QStringLiteral("RtkConfig"));
    rtkSettings.setValue(QStringLiteral("server"), QStringLiteral("127.0.0.1"));
    rtkSettings.setValue(QStringLiteral("port"), QStringLiteral("60844"));
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
    const int compactMountpointWidth = mountpointCombo->width();

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
    require(mountpointCombo->width() == compactMountpointWidth,
            "mountpoint combo stays compact after fetching long mountpoints");
    require(fetchMountpointsButton->width() == compactMountpointWidth,
            "mountpoint detect button stays aligned with the compact combo width");
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
            "mountpoint popup can be wider than the compact combo box");
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
