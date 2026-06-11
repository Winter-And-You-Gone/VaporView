#include "SkyDeviceConfigDialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
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
        require(button->parentWidget()->objectName() == QStringLiteral("skyConfigGroupTitleBar"),
                "enable button is in card title bar");
    }
    const QList<QLabel*> labels = dialog.findChildren<QLabel *>();
    for (QLabel *label : labels)
    {
        require(label->text() != QStringLiteral("启用"), "enabled label removed from form body");
    }

    auto *stack = dialog.findChild<QStackedWidget *>();
    require(stack != nullptr, "mode stack exists");
    require(stack->currentWidget() != nullptr, "initial stack page exists");
    require(stack->currentWidget()->objectName() != QStringLiteral("skyConfigRawPage"),
            "initial visual page selected");

    clickWidget(modeSwitch, 0.75);
    require(stack->currentWidget() && stack->currentWidget()->objectName() == QStringLiteral("skyConfigRawPage"),
            "raw page selected");

    clickWidget(modeSwitch, 0.25);
    require(stack->currentWidget() && stack->currentWidget()->objectName() != QStringLiteral("skyConfigRawPage"),
            "visual page selected");

    dialog.close();
    processEventsFor(100);

    std::cout << "sky_device_config_dialog_test passed\n";
    return 0;
}
