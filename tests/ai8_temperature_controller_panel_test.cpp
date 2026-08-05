#include "ground/widgets/Ai8TemperatureControllerPanel.h"
#include "shared/theme/SingleLevelPopupComboBox.h"
#include "shared/theme/SingleLevelPopupMenu.h"

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
    auto *temperaturePlot = panel.findChild<QWidget *>(QStringLiteral("ai8TemperatureTrendPlot"));
    auto *navigationBar = panel.findChild<QFrame *>(QStringLiteral("ai8NavigationBar"));
    auto *statusRow = panel.findChild<QWidget *>(QStringLiteral("ai8ProtocolStatusRow"));
    require(detailStack != nullptr && detailStack->count() == 4 && detailStack->currentIndex() == 0 &&
                mainContentCard != nullptr && temperaturePlot != nullptr &&
                navigationBar != nullptr && statusRow != nullptr,
            "AI-8 panel builds the side-by-side common-parameter and plot layout");
    require(temperaturePlot->property("forceWhiteBackground").toBool(),
            "AI-8 temperature plot uses the same white background as the parameter area");
    require(temperaturePlot->testAttribute(Qt::WA_OpaquePaintEvent) &&
                temperaturePlot->minimumHeight() == temperaturePlot->maximumHeight(),
            "AI-8 temperature plot has a stable opaque paint area");
    auto *manualOutputSpin = panel.findChild<QDoubleSpinBox *>(QStringLiteral("ai8ManualOutputSpin"));
    require(manualOutputSpin != nullptr, "AI-8 channel page exposes the bottom-row manual output editor");
    const int plotBottom =
        temperaturePlot->mapTo(&panel, QPoint(0, 0)).y() + temperaturePlot->height();
    const int manualOutputBottom =
        manualOutputSpin->mapTo(&panel, QPoint(0, 0)).y() + manualOutputSpin->height();
    if (std::abs(plotBottom - manualOutputBottom) > 2)
    {
        std::cerr << "PLOT_BOTTOM=" << plotBottom
                  << " MANUAL_BOTTOM=" << manualOutputBottom
                  << " PLOT_HEIGHT=" << temperaturePlot->height()
                  << " STACK_HEIGHT=" << stack->height()
                  << '\n';
    }
    require(std::abs(plotBottom - manualOutputBottom) <= 2,
            "AI-8 temperature plot bottom aligns with the bottom-row channel editor");

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
    auto *globalDetailContent =
        panel.findChild<QWidget *>(QStringLiteral("ai8GlobalDetailParametersContent"));
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
    auto *correctionEntrySpin =
        panel.findChild<QSpinBox *>(QStringLiteral("ai8CorrectionEntrySpin"));
    require(channelDetailToggle != nullptr && inputDetailToggle == nullptr &&
                outputDetailToggle != nullptr && globalDetailToggle != nullptr &&
                channelDetailContent != nullptr && globalDetailContent != nullptr &&
                channelInputGroupEdit != nullptr &&
                channelAlarmStatusEdit != nullptr && !channelInputGroupEdit->isVisible() &&
                channelCommonLayout != nullptr && channelCommonLayout->count() == 8 &&
                channelCommonLayout->columnCount() == 2 &&
                inputCommonLayout != nullptr && inputCommonLayout->count() == 8 &&
                inputCommonLayout->columnCount() == 2 &&
                correctionEntrySpin != nullptr &&
                inputCommonLayout->indexOf(correctionEntrySpin->parentWidget()) >= 0 &&
                detailStack->widget(1)->findChild<QSpinBox *>(
                    QStringLiteral("ai8CorrectionEntrySpin")) == nullptr &&
                outputCommonLayout != nullptr && outputCommonLayout->count() == 8 &&
                outputCommonLayout->columnCount() == 2 &&
                globalCommonLayout != nullptr && globalCommonLayout->count() == 8 &&
                globalCommonLayout->columnCount() == 2 &&
                !channelAlarmStatusEdit->isVisible() && !channelDetailToggle->isChecked() &&
                !outputDetailToggle->isChecked() &&
                !globalDetailToggle->isChecked() && !channelDetailContent->isVisible() &&
                channelDetailToggle->arrowType() == Qt::RightArrow,
            "AI-8 detailed parameter cards start collapsed with right arrows");
    const QRect commonStackRect(stack->mapTo(&panel, QPoint(0, 0)), stack->size());
    const QRect plotRect(temperaturePlot->mapTo(&panel, QPoint(0, 0)), temperaturePlot->size());
    const QRect detailStackRect(detailStack->mapTo(&panel, QPoint(0, 0)), detailStack->size());
    const QRect navigationRect(navigationBar->mapTo(&panel, QPoint(0, 0)), navigationBar->size());
    const QRect statusRect(statusRow->mapTo(&panel, QPoint(0, 0)), statusRow->size());
    const QRect mainContentRect(mainContentCard->mapTo(&panel, QPoint(0, 0)), mainContentCard->size());
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
    constexpr int kSerialConfigComboSpacingPx = 6;
    QWidget *channelField = channelSpin->parentWidget();
    QWidget *setpointField = setpointSpin->parentWidget();
    require(channelField != nullptr && setpointField != nullptr &&
                channelField->property("ai8CompactCommonField").toBool() &&
                setpointField->property("ai8CompactCommonField").toBool() &&
                channelField->maximumWidth() == channelSpin->minimumWidth() &&
                setpointField->maximumWidth() == setpointSpin->minimumWidth(),
            "AI-8 common parameter fields shrink to the editor width");
    QList<QWidget *> channelCommonFields;
    for (int itemIndex = 0; itemIndex < channelCommonLayout->count(); ++itemIndex)
    {
        if (auto *item = channelCommonLayout->itemAt(itemIndex))
        {
            if (auto *field = item->widget())
            {
                channelCommonFields.append(field);
            }
        }
    }
    require(channelCommonFields.size() == 8, "AI-8 channel common page has eight field frames");
    const int commonFieldHeight = channelCommonFields.first()->height();
    for (QWidget *field : channelCommonFields)
    {
        auto *label = field->findChild<QLabel *>(QStringLiteral("fieldLabel"),
                                                Qt::FindDirectChildrenOnly);
        require(field->height() == commonFieldHeight &&
                    label != nullptr &&
                    label->alignment().testFlag(Qt::AlignVCenter) &&
                    label->height() >= label->fontMetrics().height() + 2,
                "AI-8 common parameter rows share one height and center field labels");
    }
    auto *row0Field = channelCommonLayout->itemAtPosition(0, 0)->widget();
    auto *row1Field = channelCommonLayout->itemAtPosition(1, 0)->widget();
    auto *row2Field = channelCommonLayout->itemAtPosition(2, 0)->widget();
    auto *row3Field = channelCommonLayout->itemAtPosition(3, 0)->widget();
    const int firstRowPitch = row1Field->y() - row0Field->y();
    require(firstRowPitch == row2Field->y() - row1Field->y() &&
                firstRowPitch == row3Field->y() - row2Field->y(),
            "AI-8 channel common parameter row pitch stays even");
    const QRect channelControlRect(channelSpin->mapTo(mainContentCard, QPoint(0, 0)),
                                   channelSpin->size());
    const QRect setpointControlRect(setpointSpin->mapTo(mainContentCard, QPoint(0, 0)),
                                    setpointSpin->size());
    const QRect plotRectInMainContent(temperaturePlot->mapTo(mainContentCard, QPoint(0, 0)),
                                      temperaturePlot->size());
    require(channelControlRect.left() == kSerialConfigComboSpacingPx &&
                setpointControlRect.left() -
                    (channelControlRect.left() + channelControlRect.width()) ==
                    kSerialConfigComboSpacingPx &&
                plotRectInMainContent.left() -
                    (setpointControlRect.left() + setpointControlRect.width()) ==
                    kSerialConfigComboSpacingPx &&
                commonStackRect.width() ==
                    channelSpin->minimumWidth() + setpointSpin->minimumWidth() +
                        kSerialConfigComboSpacingPx,
            "AI-8 common controls use the serial config's 6px left, middle, and right spacing");
    globalButton->click();
    QApplication::processEvents();
    auto *lockCombo = panel.findChild<QComboBox *>(QStringLiteral("ai8ParameterLockCombo"));
    auto *singleLevelLockCombo =
        dynamic_cast<VaporView::SingleLevelPopupComboBox *>(lockCombo);
    require(lockCombo != nullptr && singleLevelLockCombo != nullptr,
            "AI-8 parameter lock uses the shared single-level combo");
    const QString lockGroupText = QStringLiteral("锁定组参数");
    lockCombo->setCurrentIndex(lockCombo->findText(lockGroupText));
    require(lockCombo->currentText() == lockGroupText &&
                lockCombo->width() >=
                    lockCombo->fontMetrics().horizontalAdvance(lockGroupText) + 44,
            "AI-8 parameter lock display text fits the compact combo width");
    singleLevelLockCombo->showPopup();
    QApplication::processEvents();
    const auto lockRows = singleLevelLockCombo->popupMenu()->rows();
    require(lockRows.size() >= 2 && lockRows.at(1)->textLabel() != nullptr,
            "AI-8 parameter lock popup exposes the lock row label");
    require(lockRows.at(1)->font() == lockCombo->font() &&
                lockRows.at(1)->textLabel()->font() == lockCombo->font(),
            "AI-8 parameter lock popup row uses the combo font");
    require(lockRows.at(1)->textLabel()->width() >=
                lockRows.at(1)->textLabel()->fontMetrics().horizontalAdvance(lockGroupText),
            "AI-8 parameter lock popup row fits the lock text");
    singleLevelLockCombo->hidePopup();
    channelButton->click();
    QApplication::processEvents();
    panel.resize(1180, 820);
    QApplication::processEvents();
    const int mainContentHeightBeforeDetailExpand = mainContentCard->height();
    channelDetailToggle->click();
    QApplication::processEvents();
    require(channelDetailToggle->isChecked() && channelDetailContent->isVisible() &&
                channelInputGroupEdit->isVisible() && channelAlarmStatusEdit->isVisible() &&
                channelDetailContent->property("ai8DetailContent").toBool() &&
                channelDetailContent->testAttribute(Qt::WA_StyledBackground) &&
                channelDetailToggle->arrowType() == Qt::DownArrow &&
                channelDetailContent->geometry().top() > channelDetailToggle->geometry().bottom(),
            "AI-8 detailed parameters expand below the toggle card header");
    require(mainContentCard->height() >= mainContentHeightBeforeDetailExpand,
            "AI-8 detail expansion keeps the main parameter and trend area from shrinking");
    const QList<QFrame *> channelDetailFields =
        channelDetailContent->findChildren<QFrame *>(QStringLiteral("ai8ParameterField"));
    auto *channelDetailLayout = qobject_cast<QGridLayout *>(channelDetailContent->layout());
    require(channelDetailFields.size() == 8,
            "AI-8 channel detail page exposes its eight detail field frames");
    require(channelDetailLayout != nullptr &&
                channelDetailLayout->columnCount() == 4 &&
                channelDetailLayout->rowCount() == 2,
            "AI-8 channel detail page uses four compact columns instead of leaving the middle empty");
    for (QFrame *field : channelDetailFields)
    {
        QWidget *editor = nullptr;
        const QList<QWidget *> directChildren =
            field->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget *child : directChildren)
        {
            if (child && child->objectName() != QStringLiteral("fieldLabel"))
            {
                editor = child;
                break;
            }
        }
        require(field->property("ai8CompactParameterField").toBool() &&
                    field->property("ai8CompactCommonField").toBool() &&
                    field->maximumWidth() == channelField->maximumWidth() &&
                    editor != nullptr &&
                    editor->minimumWidth() == channelSpin->minimumWidth() &&
                    editor->maximumWidth() == channelSpin->maximumWidth(),
                "AI-8 detail fields use the same compact width as common parameters");
    }
    for (int column = 0; column < 4; ++column)
    {
        require(channelDetailLayout->itemAtPosition(0, column) != nullptr &&
                    channelDetailLayout->itemAtPosition(1, column) != nullptr,
                "AI-8 channel detail fields fill each compact detail column");
    }
    const int channelExpandedDetailHeight = detailStack->height();
    require(channelDetailToggle->isChecked() && inputDetailToggle->isChecked() &&
                outputDetailToggle->isChecked() && globalDetailToggle->isChecked(),
            "AI-8 detail expansion is shared by every parameter page");
    globalButton->click();
    QApplication::processEvents();
    QApplication::processEvents();
    const int globalExpandedDetailHeight = detailStack->height();
    require(detailStack->currentIndex() == 3 && globalDetailToggle->isChecked() &&
                globalDetailContent->isVisible() &&
                globalExpandedDetailHeight > channelExpandedDetailHeight,
            "AI-8 switching pages preserves the shared expanded detail state and current-page height");
    globalDetailToggle->click();
    QApplication::processEvents();
    QApplication::processEvents();
    require(!globalDetailToggle->isChecked() && !channelDetailToggle->isChecked() &&
                !inputDetailToggle->isChecked() && !outputDetailToggle->isChecked() &&
                !globalDetailContent->isVisible(),
            "AI-8 collapsing one detail page collapses every parameter page");
    channelButton->click();
    QApplication::processEvents();
    QApplication::processEvents();
    auto *firstTopDetailField = qobject_cast<QFrame *>(
        channelDetailLayout->itemAtPosition(0, 0)->widget());
    auto *firstBottomDetailField = qobject_cast<QFrame *>(
        channelDetailLayout->itemAtPosition(1, 0)->widget());
    require(detailStack->currentIndex() == 0 &&
                !channelDetailToggle->isChecked() &&
                !channelDetailContent->isVisible() &&
                detailStack->height() < channelExpandedDetailHeight &&
                firstTopDetailField != nullptr &&
                firstBottomDetailField != nullptr &&
                firstBottomDetailField->y() - firstTopDetailField->y() <=
                    firstTopDetailField->height() + 16,
            "AI-8 switching pages preserves the shared collapsed detail state without vertical blank space");
    channelDetailToggle->click();
    QApplication::processEvents();
    channelDetailToggle->click();
    QApplication::processEvents();
    require(!channelDetailToggle->isChecked() && !channelDetailContent->isVisible() &&
                !inputDetailToggle->isChecked() && !outputDetailToggle->isChecked() &&
                !globalDetailToggle->isChecked() &&
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
