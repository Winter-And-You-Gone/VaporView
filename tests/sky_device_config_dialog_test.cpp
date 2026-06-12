#include "SkyDeviceConfigDialog.h"
#include "WindowSizing.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
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
        const QPoint localCenter = button->rect().center();
        const QPoint globalCenter = button->mapToGlobal(localCenter);
        QCursor::setPos(globalCenter);
        processEventsFor(20);
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

        const QPoint outside = dialog.mapToGlobal(QPoint(2, dialog.height() - 2));
        QCursor::setPos(outside);
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(button, &leave);
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
    QGroupBox *epsilonGroup = nullptr;
    QGroupBox *ptbGroup = nullptr;
    QGroupBox *hmpGroup = nullptr;
    QGroupBox *lidarGroup = nullptr;
    QGroupBox *waveGroup = nullptr;
    QGroupBox *telemetryGroup = nullptr;
    for (QGroupBox *group : groups)
    {
        auto *titleLabel = group->findChild<QLabel *>(QStringLiteral("skyConfigGroupTitleLabel"));
        const QString title = titleLabel ? titleLabel->text() : QString();
        if (title == QStringLiteral("EPSILON")) epsilonGroup = group;
        if (title == QStringLiteral("PTB210")) ptbGroup = group;
        if (title == QStringLiteral("HMP3")) hmpGroup = group;
        if (title == QStringLiteral("TFA1500-L")) lidarGroup = group;
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
    require(waveGroup && telemetryGroup, "wave and telemetry cards exist");
    require(std::abs(waveGroup->y() - telemetryGroup->y()) <= topRowTolerance, "wide telemetry card is on second row");
    require(telemetryGroup->width() > waveGroup->width() * 3 / 2, "telemetry card spans two columns");
    require(telemetryGroup->height() <= telemetryGroup->sizeHint().height() + 12, "telemetry card is not stretched vertically");
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
