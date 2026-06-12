#include "SkyDeviceConfigDialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGroupBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
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

void clickWidget(QWidget *widget, double xRatio)
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
    processEventsFor(250);
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
        QEvent enter(QEvent::Enter);
        QCoreApplication::sendEvent(button, &enter);
        processEventsFor(20);
        require(button->property("titleBarHover").toBool(), "title bar button hover enabled");

        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(button, &leave);
        processEventsFor(20);
        require(!button->property("titleBarHover").toBool(), "title bar button hover cleared");
    }

    require(checkedButtons >= 5, "all title bar buttons checked");
}

}  // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    VaporView::SkyDeviceConfigDialog dialog(nullptr);
    dialog.setEnglish(false);
    dialog.show();
    processEventsFor(250);

    require(dialog.isVisible(), "dialog visible");
    auto *modeSwitch = dialog.findChild<QWidget *>(QStringLiteral("skyConfigModeSwitch"));
    require(modeSwitch != nullptr, "mode switch exists");
    require(modeSwitch->isVisible(), "mode switch visible");

    const QList<QPushButton*> enableButtons = dialog.findChildren<QPushButton *>(QStringLiteral("skyEnableToggle"));
    require(enableButtons.size() == 5, "five enable buttons");
    for (QPushButton *button : enableButtons)
    {
        require(button->parentWidget() != nullptr, "enable button parent exists");
        require(button->parentWidget()->objectName() == QStringLiteral("skyConfigEnableTitleAction"),
                "enable button is in title action");
        require(button->parentWidget()->parentWidget() != nullptr &&
                    button->parentWidget()->parentWidget()->objectName() == QStringLiteral("skyConfigGroupTitleBar"),
                "enable button action is in card title bar");
    }
    const QList<QLabel*> enableLabels = dialog.findChildren<QLabel *>(QStringLiteral("skyConfigEnableTitleLabel"));
    require(enableLabels.size() == 5, "five enable labels");
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
    for (QGroupBox *group : groups)
    {
        auto *titleLabel = group->findChild<QLabel *>(QStringLiteral("skyConfigGroupTitleLabel"));
        const QString title = titleLabel ? titleLabel->text() : QString();
        if (title == QStringLiteral("EPSILON") ||
            title == QStringLiteral("PTB210") ||
            title == QStringLiteral("HMP3"))
        {
            require(group->height() <= group->sizeHint().height() + 12, "top row cards are not stretched vertically");
        }
    }

    auto *stack = dialog.findChild<QStackedWidget *>();
    require(stack != nullptr, "mode stack exists");
    require(stack->currentWidget() != nullptr, "initial stack page exists");
    require(stack->currentWidget()->objectName() != QStringLiteral("skyConfigRawPage"),
            "initial visual page selected");

    clickWidget(modeSwitch, 0.75);
    require(stack->currentWidget() && stack->currentWidget()->objectName() == QStringLiteral("skyConfigRawPage"),
            "raw page selected");
    requireTitleBarHoverStillWorks(dialog);

    clickWidget(modeSwitch, 0.25);
    require(stack->currentWidget() && stack->currentWidget()->objectName() != QStringLiteral("skyConfigRawPage"),
            "visual page selected");
    requireTitleBarHoverStillWorks(dialog);

    dialog.close();
    processEventsFor(100);

    std::cout << "sky_device_config_dialog_test passed\n";
    return 0;
}
