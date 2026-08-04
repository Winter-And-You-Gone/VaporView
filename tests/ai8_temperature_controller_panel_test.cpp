#include "ground/widgets/Ai8TemperatureControllerPanel.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QMetaObject>
#include <QLineEdit>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>

#include <cstdlib>
#include <iostream>
#include <limits>

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

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    VaporView::Ground::Widgets::Ai8TemperatureControllerPanel panel;
    panel.resize(1180, 560);
    panel.show();
    QApplication::processEvents();

    auto *stack = panel.findChild<QStackedWidget *>(QStringLiteral("ai8ParameterStack"));
    require(stack != nullptr && stack->count() == 4 && stack->currentIndex() == 0,
            "AI-8 panel starts on one of four documented parameter groups");
    auto *detailStack = panel.findChild<QStackedWidget *>(QStringLiteral("ai8DetailParametersStack"));
    auto *mainContentCard = panel.findChild<QFrame *>(QStringLiteral("ai8MainContentCard"));
    auto *mainContentDivider = panel.findChild<QFrame *>(QStringLiteral("ai8MainContentDivider"));
    auto *temperaturePlot = panel.findChild<QWidget *>(QStringLiteral("ai8TemperatureTrendPlot"));
    auto *navigationBar = panel.findChild<QFrame *>(QStringLiteral("ai8NavigationBar"));
    auto *statusRow = panel.findChild<QWidget *>(QStringLiteral("ai8ProtocolStatusRow"));
    require(detailStack != nullptr && detailStack->count() == 4 && detailStack->currentIndex() == 0 &&
                mainContentCard != nullptr && mainContentDivider != nullptr && temperaturePlot != nullptr &&
                navigationBar != nullptr && statusRow != nullptr,
            "AI-8 panel builds the side-by-side common-parameter and plot layout");
    require(temperaturePlot->property("forceWhiteBackground").toBool(),
            "AI-8 temperature plot uses the same white background as the parameter area");

    auto *globalButton = panel.findChild<QPushButton *>(QStringLiteral("ai8PageSelectorButton4"));
    auto *outputButton = panel.findChild<QPushButton *>(QStringLiteral("ai8PageSelectorButton3"));
    auto *channelButton = panel.findChild<QPushButton *>(QStringLiteral("ai8PageSelectorButton1"));
    require(globalButton != nullptr && outputButton != nullptr &&
                channelButton != nullptr && channelButton->isChecked(),
            "AI-8 parameter page selectors exist and channel is selected");
    globalButton->click();
    QApplication::processEvents();
    require(stack->currentIndex() == 3 && detailStack->currentIndex() == 3 && globalButton->isChecked(),
            "AI-8 global page selector changes the visible page");
    channelButton->click();
    QApplication::processEvents();
    require(stack->currentIndex() == 0 && detailStack->currentIndex() == 0 && channelButton->isChecked(),
            "AI-8 channel page selector restores the visible page");

    auto *channelSpin = panel.findChild<QSpinBox *>(QStringLiteral("ai8ChannelSpin"));
    auto *setpointSpin = panel.findChild<QDoubleSpinBox *>(QStringLiteral("ai8SetpointSpin"));
    auto *addressSpin = panel.findChild<QSpinBox *>(QStringLiteral("ai8DeviceAddressSpin"));
    auto *baudCombo = panel.findChild<QComboBox *>(QStringLiteral("ai8BaudCombo"));
    require(channelSpin != nullptr && channelSpin->minimum() == 1 && channelSpin->maximum() == 8 &&
                setpointSpin != nullptr,
            "AI-8288 channel range and setpoint control are available");

    auto *channelDetailToggle =
        panel.findChild<QToolButton *>(QStringLiteral("ai8ChannelDetailParametersToggle"));
    auto *inputDetailToggle =
        panel.findChild<QToolButton *>(QStringLiteral("ai8InputDetailParametersToggle"));
    auto *outputDetailToggle =
        panel.findChild<QToolButton *>(QStringLiteral("ai8OutputDetailParametersToggle"));
    auto *globalDetailToggle =
        panel.findChild<QToolButton *>(QStringLiteral("ai8GlobalDetailParametersToggle"));
    auto *channelDetailContent =
        panel.findChild<QWidget *>(QStringLiteral("ai8ChannelDetailParametersContent"));
    auto *channelCommonLayout =
        panel.findChild<QGridLayout *>(QStringLiteral("ai8ChannelCommonParametersLayout"));
    auto *inputCommonLayout =
        panel.findChild<QGridLayout *>(QStringLiteral("ai8InputCommonParametersLayout"));
    auto *outputCommonLayout =
        panel.findChild<QGridLayout *>(QStringLiteral("ai8OutputCommonParametersLayout"));
    auto *globalCommonLayout =
        panel.findChild<QGridLayout *>(QStringLiteral("ai8GlobalCommonParametersLayout"));
    auto *channelInputGroupEdit =
        panel.findChild<QLineEdit *>(QStringLiteral("ai8ChannelInputGroupEdit"));
    auto *channelAlarmStatusEdit =
        panel.findChild<QLineEdit *>(QStringLiteral("ai8ChannelAlarmStatusEdit"));
    require(channelDetailToggle != nullptr && inputDetailToggle != nullptr &&
                outputDetailToggle != nullptr && globalDetailToggle != nullptr &&
                channelDetailContent != nullptr && channelInputGroupEdit != nullptr &&
                channelAlarmStatusEdit != nullptr && !channelInputGroupEdit->isVisible() &&
                channelCommonLayout != nullptr && channelCommonLayout->count() == 8 &&
                channelCommonLayout->columnCount() == 2 &&
                inputCommonLayout != nullptr && inputCommonLayout->count() == 7 &&
                inputCommonLayout->columnCount() == 2 &&
                outputCommonLayout != nullptr && outputCommonLayout->count() == 8 &&
                outputCommonLayout->columnCount() == 2 &&
                globalCommonLayout != nullptr && globalCommonLayout->count() == 8 &&
                globalCommonLayout->columnCount() == 2 &&
                !channelAlarmStatusEdit->isVisible() && !channelDetailToggle->isChecked() &&
                !inputDetailToggle->isChecked() && !outputDetailToggle->isChecked() &&
                !globalDetailToggle->isChecked() && !channelDetailContent->isVisible() &&
                channelDetailToggle->arrowType() == Qt::RightArrow,
            "AI-8 detailed parameter cards start collapsed with right arrows");
    const QRect commonStackRect(stack->mapTo(&panel, QPoint(0, 0)), stack->size());
    const QRect plotRect(temperaturePlot->mapTo(&panel, QPoint(0, 0)), temperaturePlot->size());
    const QRect detailStackRect(detailStack->mapTo(&panel, QPoint(0, 0)), detailStack->size());
    const QRect navigationRect(navigationBar->mapTo(&panel, QPoint(0, 0)), navigationBar->size());
    const QRect statusRect(statusRow->mapTo(&panel, QPoint(0, 0)), statusRow->size());
    require(commonStackRect.left() < plotRect.left() &&
                commonStackRect.right() < plotRect.left() &&
                plotRect.width() > commonStackRect.width() &&
                std::abs(commonStackRect.top() - plotRect.top()) <= 2 &&
                detailStackRect.top() > plotRect.bottom(),
            "AI-8 common parameters sit left of a wider plot and details expand below");
    require(statusRect.left() > navigationRect.right() &&
                statusRect.right() <= panel.rect().right() &&
                statusRect.bottom() < commonStackRect.top(),
            "AI-8 backend status and page actions sit right of the page selectors");
    const QWidget *setpointField = setpointSpin->parentWidget();
    const int commonColumnWidth = std::max(0, (commonStackRect.width() - 8) / 2);
    const int compactFieldWidth = std::max(138, qRound(commonColumnWidth * 0.6));
    require(setpointField != nullptr &&
                setpointField->property("ai8CompactCommonField").toBool() &&
                setpointField->maximumWidth() <= compactFieldWidth + 2 &&
                setpointField->width() <= compactFieldWidth + 2,
            "AI-8 common parameter field frames are narrowed to three-fifths of their columns");
    const int setpointContentWidth = std::max(0, setpointField->width() - 20);
    const int compactSetpointWidth = std::max(118, qRound(setpointContentWidth * 0.6));
    require(std::abs(setpointSpin->maximumWidth() - compactSetpointWidth) <= 2 &&
                setpointSpin->width() <= compactSetpointWidth + 2,
            "AI-8 common parameter editors are narrowed to three-fifths of the field width");
    channelDetailToggle->click();
    QApplication::processEvents();
    require(channelDetailToggle->isChecked() && channelDetailContent->isVisible() &&
                channelInputGroupEdit->isVisible() && channelAlarmStatusEdit->isVisible() &&
                channelDetailToggle->arrowType() == Qt::DownArrow &&
                channelDetailContent->geometry().top() > channelDetailToggle->geometry().bottom(),
            "AI-8 detailed parameters expand below the toggle card header");
    channelDetailToggle->click();
    QApplication::processEvents();
    require(!channelDetailToggle->isChecked() && !channelDetailContent->isVisible() &&
                channelDetailToggle->arrowType() == Qt::RightArrow,
            "AI-8 detailed parameters collapse without changing their values");

    require(addressSpin != nullptr && addressSpin->minimum() == 1 && addressSpin->maximum() == 88 &&
                addressSpin->value() == 1,
            "AI-8 address range and default match the register table");
    require(baudCombo != nullptr && baudCombo->currentData().toInt() == 19200,
            "AI-8 baud rate defaults to 19.2K");
    auto *channelOutputGroupSpin = panel.findChild<QSpinBox *>(QStringLiteral("ai8ChannelOutputGroupSpin"));
    auto *programNumberSpin = panel.findChild<QSpinBox *>(QStringLiteral("ai8ProgramNumberSpin"));
    auto *highAlarmSpin = panel.findChild<QDoubleSpinBox *>(QStringLiteral("ai8HighAlarmSpin"));
    auto *deviationHighSpin = panel.findChild<QDoubleSpinBox *>(QStringLiteral("ai8DeviationHighAlarmSpin"));
    auto *setpointHighLimitSpin = panel.findChild<QDoubleSpinBox *>(QStringLiteral("ai8SetpointHighLimitSpin"));
    auto *modelFeatureEdit = panel.findChild<QLineEdit *>(QStringLiteral("ai8ModelFeatureEdit"));
    auto *serialNumberEdit = panel.findChild<QLineEdit *>(QStringLiteral("ai8SerialNumberEdit"));
    auto *outputStartChannelEdit = panel.findChild<QLineEdit *>(QStringLiteral("ai8OutputStartChannelEdit"));
    auto *atFunctionEdit = panel.findChild<QLineEdit *>(QStringLiteral("ai8AtFunctionEdit"));
    auto *p1tiOpsnEdit = panel.findChild<QLineEdit *>(QStringLiteral("ai8P1tiOpsnEdit"));
    require(channelOutputGroupSpin != nullptr && channelOutputGroupSpin->minimum() == 0 &&
                channelOutputGroupSpin->maximum() == 4 &&
                programNumberSpin != nullptr && programNumberSpin->maximum() == 9999 &&
                highAlarmSpin != nullptr,
            "AI-8288 extended channel parameters are visible");
    require(deviationHighSpin != nullptr && setpointHighLimitSpin != nullptr &&
                modelFeatureEdit != nullptr && serialNumberEdit != nullptr &&
                outputStartChannelEdit != nullptr && atFunctionEdit != nullptr &&
                p1tiOpsnEdit != nullptr,
            "AI-8288 output and global diagnostic parameters are visible");

    VaporView::Ai8TemperatureControllerProtocol::PageData channelData;
    channelData.page = VaporView::Ai8TemperatureControllerProtocol::Page::Channel;
    channelData.selection.channel = 1;
    channelData.channel.setpointC = 31.0;
    channelData.channel.measuredC = std::numeric_limits<double>::quiet_NaN();
    channelData.channel.channelInputGroup = 2;
    channelData.channel.correctionEntry = 11;
    channelData.channel.measurementOffset = -1.2;
    channelData.channel.channelOutputGroupRaw = 3;
    channelData.channel.programNumber = 4321;
    channelData.channel.highAlarmC = 320.0;
    channelData.channel.lowAlarmC = -20.0;
    channelData.channel.displayedSetpointC = 30.5;
    channelData.channel.alarmStatusRaw = 0x02;
    channelData.channel.alarmStatusValid = true;
    panel.applyPageData(channelData);
    QApplication::processEvents();
    require(channelOutputGroupSpin->value() == 3 &&
                programNumberSpin->value() == 4321 &&
                highAlarmSpin->value() == 320.0 &&
                panel.findChild<QLineEdit *>(QStringLiteral("ai8ChannelInputGroupEdit"))->text().contains(QStringLiteral("entry 11")) &&
                panel.findChild<QLineEdit *>(QStringLiteral("ai8ChannelAlarmStatusEdit"))->text().contains(QStringLiteral("0x02")),
            "AI-8288 channel read-back fills extended channel controls");
    auto channelRequest = panel.currentPageData();
    require(channelRequest.channel.channelOutputGroupRaw == 3 &&
                channelRequest.channel.programNumber == 4321 &&
                channelRequest.channel.highAlarmC == 320.0 &&
                channelRequest.channel.lowAlarmC == -20.0,
            "AI-8288 channel write request includes extended channel controls");
    outputButton->click();
    QApplication::processEvents();
    deviationHighSpin->setValue(35.0);
    setpointHighLimitSpin->setValue(280.0);
    auto outputRequest = panel.currentPageData();
    require(outputRequest.page == VaporView::Ai8TemperatureControllerProtocol::Page::OutputGroup &&
                outputRequest.output.deviationHighAlarm == 35.0 &&
                outputRequest.output.setpointHighLimit == 280.0,
            "AI-8288 output write request includes extended output controls");
    channelButton->click();
    QApplication::processEvents();

    auto *runCombo = panel.findChild<QComboBox *>(QStringLiteral("ai8RunModeCombo"));
    require(runCombo != nullptr && runCombo->count() == 3 &&
                runCombo->objectName() == QStringLiteral("ai8RunModeCombo") &&
                runCombo->accessibleName() == QStringLiteral("运行状态 Srun"),
            "AI-8 Srun selector exposes the documented choices and an accessible name");
    VaporView::Ai8TemperatureControllerProtocol::PageData unknownRunState;
    unknownRunState.page = VaporView::Ai8TemperatureControllerProtocol::Page::Global;
    unknownRunState.global.runStateRaw = 1;
    unknownRunState.global.runStateIsDocumented = false;
    unknownRunState.global.controlCycleS = 0.4;
    unknownRunState.global.modelFeature = 0x218A;
    unknownRunState.global.serialNumber = 0x20001919u;
    unknownRunState.global.mainStatusRaw = 0x0200;
    unknownRunState.global.commonAlarmOutput = 0x0018;
    unknownRunState.global.outputStartChannel = 1;
    unknownRunState.global.highResolutionFilter = 20;
    unknownRunState.global.aif1 = 150;
    unknownRunState.global.aif2 = 15;
    unknownRunState.global.p1faAif3 = 9999;
    unknownRunState.global.difa = 2;
    unknownRunState.global.spsr = 100;
    unknownRunState.global.atFunction = 55;
    panel.applyPageData(unknownRunState);
    QApplication::processEvents();
    require(runCombo->count() == 4 && runCombo->currentData().toUInt() == 1 &&
                runCombo->currentText().contains(QStringLiteral("1")) &&
                runCombo->currentText().contains(QStringLiteral("0x0001")) &&
                !runCombo->currentText().contains(QStringLiteral("自动运行")) &&
                runCombo->itemData(3, Qt::ToolTipRole).toString().contains(QStringLiteral("不会写入 Srun")),
            "AI-8 unknown Srun raw value is displayed and documented as preserved");
    auto unknownData = panel.currentPageData();
    require(unknownData.global.runStateRaw == 1 &&
                !unknownData.global.runStateIsDocumented &&
                !unknownData.global.runStateWriteRequested &&
                unknownData.global.runStateWriteValue == 0,
            "AI-8 unknown Srun value is not converted into a write request");
    require(modelFeatureEdit->text() == QStringLiteral("0x218A") &&
                serialNumberEdit->text() == QStringLiteral("0x20001919") &&
                panel.findChild<QLineEdit *>(QStringLiteral("ai8MainStatusEdit"))->text().contains(QStringLiteral("0x0200")) &&
                panel.findChild<QLineEdit *>(QStringLiteral("ai8CommonAlarmOutputEdit"))->text().contains(QStringLiteral("0x0018")) &&
                outputStartChannelEdit->text().contains(QStringLiteral("0x0001")) &&
                atFunctionEdit->text().contains(QStringLiteral("0x0037")) &&
                p1tiOpsnEdit->text().contains(QStringLiteral("0x0000")),
            "AI-8288 global diagnostics are displayed as read-only values");

    panel.setEnglish(true);
    QApplication::processEvents();
    require(runCombo->currentText() == QStringLiteral("Keep Device Value (Unknown: 1 / 0x0001)") &&
                runCombo->itemData(3, Qt::ToolTipRole).toString().contains(QStringLiteral("does not write Srun")),
            "AI-8 unknown Srun item follows the language switch");
    require(!panel.currentPageData().global.runStateWriteRequested,
            "language switching does not create an Srun write request");
    panel.setEnglish(false);
    const int autoRunIndex = runCombo->findData(0);
    require(autoRunIndex >= 0, "AI-8 documented automatic Srun item remains addressable");
    runCombo->setCurrentIndex(autoRunIndex);
    QApplication::processEvents();
    require(!panel.currentPageData().global.runStateWriteRequested,
            "programmatic Srun selection does not create a write request");
    require(QMetaObject::invokeMethod(runCombo,
                                      "activated",
                                      Qt::DirectConnection,
                                      Q_ARG(int, autoRunIndex)),
            "AI-8 Srun user activation signal can be exercised");
    auto explicitData = panel.currentPageData();
    require(explicitData.global.runStateRaw == 1 &&
                explicitData.global.runStateWriteRequested &&
                explicitData.global.runStateWriteValue == 0,
            "only explicit user activation requests a documented Srun write");
    const int unknownIndex = runCombo->findData(1);
    require(unknownIndex >= 0 &&
                QMetaObject::invokeMethod(runCombo,
                                          "activated",
                                          Qt::DirectConnection,
                                          Q_ARG(int, unknownIndex)) &&
                !panel.currentPageData().global.runStateWriteRequested,
            "reselecting the unknown raw-value item clears the Srun write request");
    panel.applyPageData(unknownRunState);
    require(!panel.currentPageData().global.runStateWriteRequested,
            "refreshing global page data clears a pending Srun write request");
    globalButton->click();
    channelButton->click();
    globalButton->click();
    require(!panel.currentPageData().global.runStateWriteRequested,
            "page navigation does not create an Srun write request");

    auto *readButton = panel.findChild<QPushButton *>(QStringLiteral("ai8ReadParametersButton"));
    auto *writeButton = panel.findChild<QPushButton *>(QStringLiteral("ai8WriteParametersButton"));
    auto *statusLabel = panel.findChild<QLabel *>(QStringLiteral("ai8ProtocolStatus"));
    require(readButton != nullptr && writeButton != nullptr && statusLabel != nullptr &&
                !readButton->isEnabled() && !writeButton->isEnabled() &&
                !statusLabel->property("protocolReady").toBool(),
            "AI-8 read and write stay unavailable before connection");

    panel.setBackendConnected(true, QStringLiteral("COM8 @ 19200"));
    QApplication::processEvents();
    require(readButton->isEnabled() && writeButton->isEnabled() &&
                statusLabel->property("protocolReady").toBool(),
            "AI-8 read and write become available after connection");

    setpointSpin->setValue(23.0);
    VaporView::Ai8TemperatureControllerProtocol::LiveData liveData;
    liveData.valid = true;
    liveData.controlStatesValid = true;
    liveData.controlStates.fill(
        VaporView::Ai8TemperatureControllerProtocol::ChannelControlState::ApidOutput);
    liveData.measuredC.fill(std::numeric_limits<double>::quiet_NaN());
    liveData.measuredC[0] = 23.4;
    liveData.alarmStatusValid = true;
    liveData.alarmStatusRegisters[0] = 0x0203;
    panel.applyLiveData(liveData);
    auto *pvEdit = panel.findChild<QLineEdit *>(QStringLiteral("ai8MeasuredTemperatureEdit"));
    auto *alarmStatusEdit = panel.findChild<QLineEdit *>(QStringLiteral("ai8ChannelAlarmStatusEdit"));
    require(pvEdit != nullptr && pvEdit->text().contains(QStringLiteral("23.4")),
            "AI-8 selected-channel PV is updated from live polling");
    require(alarmStatusEdit != nullptr && alarmStatusEdit->text().contains(QStringLiteral("0x02")),
            "AI-8 selected-channel alarm status is updated from live polling");
    require(temperaturePlot != nullptr && temperaturePlot->property("ai8TemperaturePlot").toBool() &&
                temperaturePlot->property("sampleCount").toInt() == 1,
            "AI-8 temperature plot receives the selected channel PV");
    require(temperaturePlot->property("yAxisMinC").toDouble() == 22.0 &&
                temperaturePlot->property("yAxisMaxC").toDouble() == 24.0,
            "AI-8 temperature plot uses a one-degree target-centered axis range");
    require(panel.currentOutputStatusText() == QStringLiteral("通道1：APID输出"),
            "AI-8 title status reports the selected channel output state");

    liveData.measuredC[1] = 24.1;
    liveData.controlStates[1] =
        VaporView::Ai8TemperatureControllerProtocol::ChannelControlState::Stopped;
    panel.applyLiveData(liveData);
    channelSpin->setValue(2);
    QApplication::processEvents();
    require(temperaturePlot->property("sampleCount").toInt() == 1,
            "AI-8 temperature plot follows the selected channel history");
    require(panel.currentOutputStatusText() == QStringLiteral("通道2：停止输出"),
            "AI-8 title status follows the selected channel");
    require(alarmStatusEdit->text().contains(QStringLiteral("0x03")),
            "AI-8 selected-channel alarm status follows the selected channel");
    panel.setEnglish(true);
    QApplication::processEvents();
    require(channelButton->text() == QStringLiteral("Channel") &&
                globalButton->text() == QStringLiteral("Global") &&
                runCombo->currentText() == QStringLiteral("Keep Device Value (Unknown: 1 / 0x0001)") &&
                statusLabel->text().contains(QStringLiteral("Modbus backend connected")) &&
                baudCombo->currentData().toInt() == 19200,
            "AI-8 panel translates labels without changing parameter values");

    const QImage snapshot = panel.grab().toImage();
    require(!snapshot.isNull() && snapshot.width() > 0 && snapshot.height() > 0,
            "AI-8 panel renders to a QWidget snapshot");

    std::cout << "ai8 temperature controller panel tests passed\n";
    return 0;
}
