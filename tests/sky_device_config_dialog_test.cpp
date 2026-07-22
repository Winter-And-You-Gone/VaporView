#include "ground/widgets/SkyDeviceConfigDialog.h"
#include "ground/widgets/SerialPortComboSupport.h"
#include "ground/widgets/VisualTextLabel.h"
#include "ground/widgets/WindowSizing.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <cstdlib>
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

void processEventsFor(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

void clickWidget(QWidget *widget, double xRatio, int waitMs = 250)
{
    require(widget != nullptr, "click widget exists");
    const QPointF localPos(std::clamp(xRatio, 0.0, 1.0) * widget->width(),
                           widget->height() / 2.0);
    const QPointF globalPos = widget->mapToGlobal(localPos.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress,
                      localPos,
                      globalPos,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        localPos,
                        globalPos,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &release);
    if (waitMs > 0)
    {
        processEventsFor(waitMs);
    }
}

bool isTitleBarButton(QToolButton *button)
{
    if (!button)
    {
        return false;
    }

    const QString name = button->objectName();
    return name == QStringLiteral("titleBarButton") ||
           name == QStringLiteral("windowMinimizeButton") ||
           name == QStringLiteral("windowMaximizeButton") ||
           name == QStringLiteral("windowCloseButton");
}

void requireTitleBarHoverStillWorks(VaporView::SkyDeviceConfigDialog& dialog)
{
    const QList<QToolButton*> buttons = dialog.findChildren<QToolButton *>();
    int checkedButtons = 0;
    for (QToolButton *button : buttons)
    {
        if (!isTitleBarButton(button))
        {
            continue;
        }

        ++checkedButtons;
        const QPoint localCenter = button->rect().center();
        const QPoint globalCenter = button->mapToGlobal(localCenter);
        QEvent enter(QEvent::Enter);
        QCoreApplication::sendEvent(button, &enter);
        QMouseEvent move(QEvent::MouseMove,
                         localCenter,
                         globalCenter,
                         Qt::NoButton,
                         Qt::NoButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(button, &move);
        require(button->property("titleBarHover").toBool(), "title bar button hover enabled");

        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(button, &leave);
        require(!button->property("titleBarHover").toBool(), "title bar button hover cleared");
    }

    require(checkedButtons >= 5, "all title bar buttons checked");
}

}  // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);

    VaporView::SkyDeviceConfigDialog dialog(nullptr);
    dialog.setEnglish(false);
    dialog.show();
    processEventsFor(250);

    require(dialog.isVisible(), "dialog visible");
    require(dialog.size().width() >= dialog.minimumSize().width(), "dialog opens at minimum width");
    require(dialog.size().height() >= dialog.minimumSize().height(), "dialog opens at minimum height");
    const QSize screenLimit = VaporView::screenFractionSize(&dialog);
    const QSize expectedMinimum = QSize(640, 420).boundedTo(screenLimit);
    require(dialog.minimumSize().height() == expectedMinimum.height(), "dialog minimum height follows child window policy");
    require(dialog.minimumSize().width() >= expectedMinimum.width(), "dialog minimum width preserves content");
    require(dialog.minimumSize().width() <= std::max(980, screenLimit.width()), "dialog minimum width fits default window policy");
    require(dialog.size().width() <= std::max(980, screenLimit.width()), "dialog width follows child window policy");
    require(dialog.size().height() <= screenLimit.height(), "dialog height follows screen fraction");
    auto *modeSwitch = dialog.findChild<QWidget *>(QStringLiteral("skyConfigModeSwitch"));
    require(modeSwitch != nullptr, "mode switch exists");
    require(modeSwitch->isVisible(), "mode switch visible");

    const QList<QPushButton*> enableButtons = dialog.findChildren<QPushButton *>(QStringLiteral("skyEnableToggle"));
    require(enableButtons.size() == 6, "six enable buttons");
    require(dialog.styleSheet().contains(QStringLiteral("QPushButton#skyEnableToggle:hover { background-color:")),
            "enable button has hover background style");
    require(!dialog.styleSheet().contains(QStringLiteral("QPushButton#skyEnableToggle:hover { background-color: transparent")),
            "enable button hover background is visible");
    for (QPushButton *button : enableButtons)
    {
        require(button->parentWidget() != nullptr, "enable button parent exists");
        require(button->parentWidget()->objectName() == QStringLiteral("skyConfigEnableTitleAction"),
                "enable button is in title action");
        require(button->parentWidget()->parentWidget() != nullptr &&
                    button->parentWidget()->parentWidget()->objectName() == QStringLiteral("skyConfigGroupTitleBar"),
                "enable button action is in card title bar");
        require(button->isFlat(), "enable button is a flat icon button");
        require(button->text().isEmpty(), "enable button has no text");
        require(!button->icon().isNull(), "enable button has an icon");
        require(button->iconSize() == QSize(22, 22), "enable button icon is larger");
    }
    const bool firstEnableChecked = enableButtons.front()->isChecked();
    const qint64 firstEnableIconKey = enableButtons.front()->icon().cacheKey();
    enableButtons.front()->setChecked(!firstEnableChecked);
    processEventsFor(50);
    require(enableButtons.front()->icon().cacheKey() != firstEnableIconKey,
            "enable button icon changes between check and x");
    enableButtons.front()->setChecked(firstEnableChecked);
    processEventsFor(50);
    const QList<QLabel*> enableLabels = dialog.findChildren<QLabel *>(QStringLiteral("skyConfigEnableTitleLabel"));
    require(enableLabels.size() == 6, "six enable labels");
    for (QLabel *label : enableLabels)
    {
        require(label->text() == QStringLiteral("启用"), "enable label text");
        require(label->parentWidget() != nullptr &&
                    label->parentWidget()->objectName() == QStringLiteral("skyConfigEnableTitleAction"),
                "enable label is in title action");
    }
    const QList<QLabel*> labels = dialog.findChildren<QLabel *>();
    for (QLabel *label : labels)
    {
        if (label->text() == QStringLiteral("启用"))
        {
            require(label->objectName() == QStringLiteral("skyConfigEnableTitleLabel"),
                    "enabled label only appears in title bar");
        }
    }
    const QList<QGroupBox*> groups = dialog.findChildren<QGroupBox *>();
    require(!groups.isEmpty(), "config groups exist");
    QGroupBox *epsilonGroup = nullptr;
    QGroupBox *ptbGroup = nullptr;
    QGroupBox *hmpGroup = nullptr;
    QGroupBox *lidarGroup = nullptr;
    QGroupBox *temperatureControllerGroup = nullptr;
    QGroupBox *waveGroup = nullptr;
    QGroupBox *telemetryGroup = nullptr;
    for (QGroupBox *group : groups)
    {
        auto *titleLabel = group->findChild<QLabel *>(QStringLiteral("skyConfigGroupTitleLabel"));
        if (titleLabel)
        {
            const bool usesCustomMouseSelection =
                dynamic_cast<VaporView::VisualTextLabel *>(titleLabel) != nullptr &&
                titleLabel->cursor().shape() == Qt::IBeamCursor;
            require((titleLabel->textInteractionFlags().testFlag(Qt::TextSelectableByMouse) ||
                        usesCustomMouseSelection) &&
                        titleLabel->textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard),
                    "sky-device card title is selectable and copyable");
        }
        const QString title = titleLabel ? titleLabel->text() : QString();
        if (title == QStringLiteral("EPSILON")) epsilonGroup = group;
        if (title == QStringLiteral("PTB210")) ptbGroup = group;
        if (title == QStringLiteral("HMP3")) hmpGroup = group;
        if (title == QStringLiteral("TFA1500-L")) lidarGroup = group;
        if (title == QStringLiteral("RD105")) temperatureControllerGroup = group;
        if (title == QStringLiteral("Wave TCP")) waveGroup = group;
        if (title == QStringLiteral("数传配置")) telemetryGroup = group;
        if (title == QStringLiteral("EPSILON") ||
            title == QStringLiteral("PTB210") ||
            title == QStringLiteral("HMP3") ||
            title == QStringLiteral("TFA1500-L"))
        {
            require(group->height() <= group->sizeHint().height() + 12, "top row cards are not stretched vertically");
        }
    }
    require(epsilonGroup && ptbGroup && hmpGroup && lidarGroup, "four sensor cards exist");
    const int topRowTolerance = 4;
    require(std::abs(epsilonGroup->y() - ptbGroup->y()) <= topRowTolerance, "EPSILON and PTB are on first row");
    require(std::abs(epsilonGroup->y() - hmpGroup->y()) <= topRowTolerance, "EPSILON and HMP are on first row");
    require(std::abs(epsilonGroup->y() - lidarGroup->y()) <= topRowTolerance, "fourth sensor card is on first row");
    require(lidarGroup->x() > hmpGroup->x(), "fourth sensor card is in fourth column");
    require(waveGroup && telemetryGroup && temperatureControllerGroup, "wave, RD105 and telemetry cards exist");
    require(std::abs(temperatureControllerGroup->y() - waveGroup->y()) <= topRowTolerance, "RD105 and wave cards are on second row");
    require(std::abs(waveGroup->y() - telemetryGroup->y()) <= topRowTolerance, "wide telemetry card is on second row");
    require(waveGroup->x() > temperatureControllerGroup->x(), "wave card is beside RD105 card");
    require(telemetryGroup->width() > waveGroup->width() * 3 / 2, "telemetry card spans two columns");
    auto findTelemetryLabel = [telemetryGroup](const QString& text) -> QLabel * {
        const QList<QLabel*> labels = telemetryGroup->findChildren<QLabel *>();
        for (QLabel *label : labels)
        {
            if (label->text() == text)
            {
                return label;
            }
        }
        return nullptr;
    };
    QLabel *basicRateLabel = findTelemetryLabel(QStringLiteral("基础遥测 Hz"));
    QLabel *featureRateLabel = findTelemetryLabel(QStringLiteral("特征值 Hz"));
    QLabel *waveformRateLabel = findTelemetryLabel(QStringLiteral("波形 Hz"));
    QLabel *heartbeatRateLabel = findTelemetryLabel(QStringLiteral("心跳 Hz"));
    QLabel *statusRateLabel = findTelemetryLabel(QStringLiteral("状态 Hz"));
    require(basicRateLabel && featureRateLabel && waveformRateLabel && heartbeatRateLabel && statusRateLabel,
            "telemetry rate labels exist");
    require(std::abs(basicRateLabel->x() - waveformRateLabel->x()) <= 4 &&
                std::abs(basicRateLabel->x() - statusRateLabel->x()) <= 4,
            "telemetry left column has three rows");
    require(waveformRateLabel->y() > basicRateLabel->y() &&
                statusRateLabel->y() > waveformRateLabel->y(),
            "telemetry left column rows are vertical");
    require(std::abs(featureRateLabel->x() - heartbeatRateLabel->x()) <= 4,
            "telemetry right column has rows");
    require(heartbeatRateLabel->y() > featureRateLabel->y(),
            "telemetry right column rows are vertical");
    require(featureRateLabel->x() > basicRateLabel->x(),
            "telemetry right column is beside left column");

    const QList<QComboBox*> serialPortCombos = dialog.findChildren<QComboBox *>();
    require(serialPortCombos.size() == 5, "five sky-device serial port combos exist");
    for (QComboBox *combo : serialPortCombos)
    {
        require(!combo->isEditable(), "sky-device serial port combo is select-only by default");
        require(combo->itemText(0) == QStringLiteral("未选择"),
                "sky-device serial port combo uses the Chinese unselected placeholder");
        require(combo->findText(QStringLiteral("手动添加")) >= 0,
                "sky-device serial port combo exposes manual add");
        require(combo->findData(QStringLiteral("__vv_manual_serial_port__")) >= 0,
                "sky-device serial port combo marks the manual option");
        require(combo->currentData().toString().isEmpty(),
                "sky-device serial port combo does not preselect a synthetic default port");
    }

    QComboBox *manualPortCombo = serialPortCombos.front();
    const int manualPortIndex = manualPortCombo->findData(QStringLiteral("__vv_manual_serial_port__"));
    manualPortCombo->setCurrentIndex(manualPortIndex);
    processEventsFor(20);
    require(manualPortCombo->isEditable() && manualPortCombo->lineEdit(),
            "manual add temporarily enables sky-device serial input");
    require(manualPortCombo->lineEdit()->placeholderText() == QStringLiteral("输入串口..."),
            "manual sky-device serial input uses the shortened Chinese placeholder");
    manualPortCombo->lineEdit()->setText(QStringLiteral("COM77"));
    QKeyEvent acceptManualPort(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(manualPortCombo->lineEdit(), &acceptManualPort);
    processEventsFor(20);
    require(!manualPortCombo->isEditable() &&
                manualPortCombo->currentData().toString() == QStringLiteral("COM77"),
            "Enter accepts a sky-device manual serial port and restores select-only mode");
    require(manualPortCombo->itemData(
                manualPortCombo->findData(QStringLiteral("COM77")),
                VaporView::kSerialPortHistoryItemRole).toBool(),
            "manually confirmed sky-device serial port is marked as history");

    manualPortCombo->setCurrentIndex(manualPortCombo->findData(QStringLiteral("__vv_manual_serial_port__")));
    processEventsFor(20);
    manualPortCombo->setCurrentIndex(0);
    processEventsFor(20);
    require(!manualPortCombo->isEditable() && manualPortCombo->currentData().toString().isEmpty(),
            "selecting unselected while entering a sky-device serial port restores select-only mode");
    manualPortCombo->setCurrentIndex(manualPortCombo->findData(QStringLiteral("__vv_manual_serial_port__")));
    processEventsFor(20);
    require(manualPortCombo->isEditable() && manualPortCombo->lineEdit(),
            "sky-device serial manual entry can restart after selecting unselected");
    manualPortCombo->lineEdit()->setText(QStringLiteral("COM101"));
    QApplication::sendEvent(manualPortCombo->lineEdit(), &acceptManualPort);
    processEventsFor(20);
    require(!manualPortCombo->isEditable() &&
                manualPortCombo->currentData().toString() == QStringLiteral("COM101"),
            "restarted sky-device serial manual entry accepts the new port");
    manualPortCombo->setCurrentIndex(manualPortCombo->findData(QStringLiteral("COM77")));
    processEventsFor(20);

    manualPortCombo->setCurrentIndex(manualPortCombo->findData(QStringLiteral("__vv_manual_serial_port__")));
    processEventsFor(20);
    manualPortCombo->lineEdit()->clear();
    QKeyEvent rejectEmptyManualPort(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(manualPortCombo->lineEdit(), &rejectEmptyManualPort);
    processEventsFor(20);
    require(!manualPortCombo->isEditable() &&
                manualPortCombo->currentData().toString() == QStringLiteral("COM77"),
            "empty sky-device manual serial input restores the previous port");

    manualPortCombo->setCurrentIndex(manualPortCombo->findData(QStringLiteral("__vv_manual_serial_port__")));
    processEventsFor(20);
    manualPortCombo->lineEdit()->setText(QStringLiteral("COM78"));
    QKeyEvent cancelManualPort(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(manualPortCombo->lineEdit(), &cancelManualPort);
    processEventsFor(20);
    require(!manualPortCombo->isEditable() &&
                manualPortCombo->currentData().toString() == QStringLiteral("COM77"),
            "Esc cancels sky-device manual serial input and restores the previous port");

    manualPortCombo->setCurrentIndex(manualPortCombo->findData(QStringLiteral("__vv_manual_serial_port__")));
    processEventsFor(20);
    manualPortCombo->lineEdit()->setText(QStringLiteral("/dev/ttyUSB0"));
    QFocusEvent acceptManualPortOnBlur(QEvent::FocusOut, Qt::OtherFocusReason);
    QApplication::sendEvent(manualPortCombo->lineEdit(), &acceptManualPortOnBlur);
    processEventsFor(60);
    require(!manualPortCombo->isEditable() &&
                manualPortCombo->currentData().toString() == QStringLiteral("/dev/ttyUSB0"),
            "focus loss accepts a Linux-style sky-device serial path");

    dialog.setEnglish(true);
    require(manualPortCombo->findText(QStringLiteral("Add Port")) >= 0,
            "sky-device manual option follows the dialog language");
    dialog.setEnglish(false);

    auto *stack = dialog.findChild<QStackedWidget *>();
    require(stack != nullptr, "mode stack exists");
    require(stack->currentWidget() != nullptr, "initial stack page exists");
    require(stack->currentWidget()->objectName() != QStringLiteral("skyConfigRawPage"),
            "initial visual page selected");
    require(modeSwitch->property("currentIndex").toInt() == 0, "initial mode switch index");

    clickWidget(modeSwitch, 0.75, 0);
    processEventsFor(5);
    require(modeSwitch->property("currentIndex").toInt() == 1, "mode switch thumb moves before page work");
    require(stack->currentWidget() && stack->currentWidget()->objectName() != QStringLiteral("skyConfigRawPage"),
            "raw page is deferred until thumb movement starts");
    processEventsFor(120);
    require(stack->currentWidget() && stack->currentWidget()->objectName() != QStringLiteral("skyConfigRawPage"),
            "raw page stays deferred during thumb animation");
    processEventsFor(160);
    require(stack->currentWidget() && stack->currentWidget()->objectName() == QStringLiteral("skyConfigRawPage"),
            "raw page selected");
    requireTitleBarHoverStillWorks(dialog);

    clickWidget(modeSwitch, 0.25, 0);
    processEventsFor(5);
    require(modeSwitch->property("currentIndex").toInt() == 0, "mode switch thumb returns before page work");
    processEventsFor(120);
    require(stack->currentWidget() && stack->currentWidget()->objectName() == QStringLiteral("skyConfigRawPage"),
            "visual page stays deferred during thumb animation");
    processEventsFor(160);
    require(stack->currentWidget() && stack->currentWidget()->objectName() != QStringLiteral("skyConfigRawPage"),
            "visual page selected");
    requireTitleBarHoverStillWorks(dialog);

    dialog.close();
    processEventsFor(100);

    std::cout << "sky_device_config_dialog_test passed\n";
    return 0;
}
