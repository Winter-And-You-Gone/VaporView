#include "ground/widgets/SegmentedSwitchButton.h"
#include "ground/widgets/TemperatureControllerWidgets.h"
#include "shared/theme/AppTheme.h"

#include <QAbstractAnimation>
#include <QApplication>
#include <QFocusEvent>
#include <QFrame>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>
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

void sendMouseClick(QWidget *widget, const QPointF& position)
{
    const QPointF globalPosition = widget->mapToGlobal(position.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress,
                      position,
                      globalPosition,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        position,
                        globalPosition,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(widget, &release);
    QApplication::processEvents();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    using VaporView::Ground::Widgets::SegmentedSwitchButton;

    SegmentedSwitchButton button;
    button.resize(128, 36);
    button.setSegmentTexts(QStringLiteral("Local"), QStringLiteral("Remote"));
    button.setStateDescription(QStringLiteral("Source"), QStringLiteral(": "));
    button.setAccentMode(SegmentedSwitchButton::AccentMode::Primary);
    button.setAutoToggle(false);
    button.setSwitchChecked(false, false);
    button.show();
    QApplication::processEvents();

    require(button.property("segmentedSwitchControl").toBool(),
            "segmented switch exposes its shared control identity");
    require(button.focusPolicy() == Qt::TabFocus,
            "segmented switch accepts keyboard focus without click focus");
    require(button.leftSegmentText() == QStringLiteral("Local") &&
                button.rightSegmentText() == QStringLiteral("Remote") &&
                button.text() == QStringLiteral("Source: Local"),
            "segmented switch uses configurable labels and accessible state text");
    require(button.accentMode() == SegmentedSwitchButton::AccentMode::Primary &&
                button.animationDuration() == 480 &&
                button.sizeHint() == QSize(250, 62) &&
                button.minimumSizeHint() == QSize(96, 32),
            "segmented switch uses configurable theme accents and responsive sizing");

    const QVariant previousDarkTheme = app.property(VaporView::kAppDarkThemeProperty);
    app.setProperty(VaporView::kAppDarkThemeProperty, true);
    button.repaint();
    QApplication::processEvents();
    const QImage darkSwitchImage = button.grab().toImage();
    const qreal imageScale = darkSwitchImage.devicePixelRatio();
    const qreal darkTrackTop = 0.75 + std::clamp(button.height() * 0.08, 2.5, 5.0);
    const QColor darkTrackEdgePixel = darkSwitchImage.pixelColor(
        qRound(button.width() * imageScale / 2.0),
        qRound(darkTrackTop * imageScale));
    require(darkTrackEdgePixel.lightness() < 190,
            "dark segmented switch uses a subdued semantic track outline instead of white");
    app.setProperty(VaporView::kAppDarkThemeProperty, previousDarkTheme);
    button.repaint();
    QApplication::processEvents();

    QList<bool> requestedSelections;
    QObject::connect(&button,
                     &SegmentedSwitchButton::selectionRequested,
                     &button,
                     [&button, &requestedSelections](bool rightSelected) {
                         requestedSelections.append(rightSelected);
                         button.setSwitchChecked(rightSelected, true);
                     });
    const QPointF repeatedClickPosition(button.width() * 0.25, button.height() / 2.0);
    for (int clickIndex = 0; clickIndex < 6; ++clickIndex)
    {
        sendMouseClick(&button, repeatedClickPosition);
    }
    require(requestedSelections == QList<bool>({true, false, true, false, true, false}) &&
                !button.switchChecked() &&
                button.text() == QStringLiteral("Source: Local"),
            "repeated clicks at one position toggle every time through controlled feedback");

    requestedSelections.clear();
    button.setSwitchChecked(true, false);

    QKeyEvent leftKey(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QApplication::sendEvent(&button, &leftKey);
    require(requestedSelections == QList<bool>{false} && !button.switchChecked(),
            "left arrow requests the left segment in controlled mode");
    QKeyEvent enterKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&button, &enterKey);
    require(requestedSelections == QList<bool>({false, true}) && button.switchChecked(),
            "Enter toggles once through the controlled callback");
    QKeyEvent spacePress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QKeyEvent spaceRelease(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&button, &spacePress);
    QApplication::sendEvent(&button, &spaceRelease);
    require(requestedSelections == QList<bool>({false, true, false}) && !button.switchChecked(),
            "Space toggles once through the controlled callback");

    button.setEnabled(false);
    QKeyEvent disabledRightKey(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(&button, &disabledRightKey);
    require(requestedSelections.size() == 3,
            "disabled segmented switch ignores keyboard selection requests");
    button.setEnabled(true);

    button.setAutoToggle(true);
    button.setSwitchChecked(false, false);
    button.click();
    require(button.switchChecked() && button.text() == QStringLiteral("Source: Remote") &&
                requestedSelections.constLast(),
            "self-controlled segmented switch toggles and emits its selected value together");

    auto *thumbAnimation =
        button.findChild<QVariantAnimation *>(QStringLiteral("segmentedSwitchThumbAnimation"));
    require(thumbAnimation != nullptr && thumbAnimation->duration() == 480,
            "segmented switch exposes one reusable 480 ms thumb animation");
    button.setSwitchChecked(false, false);
    button.setSwitchChecked(true, true);
    thumbAnimation->setCurrentTime(qRound(thumbAnimation->duration() * 0.16));
    require(button.thumbHorizontalScale() >= 1.08 &&
                button.thumbVerticalScale() <= 0.96,
            "jelly launch stretches horizontally and compresses vertically");
    thumbAnimation->setCurrentTime(qRound(thumbAnimation->duration() * 0.66));
    require(button.thumbPosition() > 1.0 && button.thumbHorizontalScale() < 1.0,
            "jelly arrival overshoots the target and briefly compresses");
    thumbAnimation->setCurrentTime(qRound(thumbAnimation->duration() * 0.82));
    require(button.thumbPosition() < 1.0 && button.thumbHorizontalScale() > 1.0,
            "jelly rebound crosses back once before settling");

    thumbAnimation->setCurrentTime(qRound(thumbAnimation->duration() * 0.45));
    const qreal interruptedPosition = button.thumbPosition();
    button.setSwitchChecked(false, true);
    thumbAnimation->setCurrentTime(qRound(thumbAnimation->duration() * 0.16));
    require(button.thumbPosition() < interruptedPosition &&
                button.thumbHorizontalScale() >= 1.08,
            "rapid reverse input restarts smoothly from the current visual state");
    thumbAnimation->setCurrentTime(thumbAnimation->duration());
    require(!button.switchChecked() &&
                std::abs(button.thumbPosition()) < 0.001 &&
                std::abs(button.thumbHorizontalScale() - 1.0) < 0.001 &&
                std::abs(button.thumbVerticalScale() - 1.0) < 0.001,
            "rapid reversal settles on exactly one left-side thumb");

    button.setReducedMotionEnabled(true);
    button.setSwitchChecked(true, true);
    require(button.reducedMotionEnabled() &&
                button.property("reducedMotionEnabled").toBool() &&
                thumbAnimation->duration() == 150,
            "reduced motion replaces the jelly sequence with a short slide");
    thumbAnimation->setCurrentTime(thumbAnimation->duration() / 2);
    require(std::abs(button.thumbHorizontalScale() - 1.0) < 0.001 &&
                std::abs(button.thumbVerticalScale() - 1.0) < 0.001,
            "reduced motion never deforms the selected thumb");
    thumbAnimation->setCurrentTime(thumbAnimation->duration());
    button.setReducedMotionEnabled(false);

    QFocusEvent activeWindowFocus(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
    QApplication::sendEvent(&button, &activeWindowFocus);
    require(!button.keyboardFocusIndicatorVisible(),
            "window activation does not create a segmented-switch focus ring");

    QFocusEvent tabFocus(QEvent::FocusIn, Qt::TabFocusReason);
    QApplication::sendEvent(&button, &tabFocus);
    require(button.keyboardFocusIndicatorVisible() &&
                button.property("keyboardFocusIndicatorVisible").toBool(),
            "keyboard tab focus creates the segmented-switch focus ring");

    sendMouseClick(&button, QPointF(10.0, 10.0));
    require(!button.keyboardFocusIndicatorVisible(),
            "pointer interaction clears the segmented-switch keyboard focus ring");

    button.setAccentMode(SegmentedSwitchButton::AccentMode::BinaryState);
    button.setSegmentTexts(QStringLiteral("Off"), QStringLiteral("On"));
    button.setStateDescription(QStringLiteral("Output Enable"), QStringLiteral(": "));
    button.setSwitchChecked(true, false);
    require(button.accentMode() == SegmentedSwitchButton::AccentMode::BinaryState &&
                button.text() == QStringLiteral("Output Enable: On"),
            "the same segmented switch supports binary output-state configuration");

    auto *sourceModeSwitch =
        VaporView::Ground::Widgets::createSourceModeOverviewSwitchButton();
    sourceModeSwitch->setEnglish(false);
    require(sourceModeSwitch->leftSegmentText() == QStringLiteral("本地") &&
                sourceModeSwitch->rightSegmentText() == QStringLiteral("远程") &&
                !sourceModeSwitch->switchChecked() &&
                sourceModeSwitch->animationDuration() == 480,
            "configuration page source switch uses the shared jelly component with local selected by default");
    delete sourceModeSwitch;

    auto *temperatureOverview =
        VaporView::Ground::Widgets::createTemperatureControllerOverviewPanel();
    temperatureOverview->resize(420, 220);
    temperatureOverview->setEnglish(false);
    auto *overviewOutputCapsule =
        temperatureOverview->findChild<QFrame *>(QStringLiteral("temperatureOverviewOutputCapsule"));
    auto *overviewOutputLabel =
        temperatureOverview->findChild<QLabel *>(QStringLiteral("temperatureOverviewOutputLabel"));
    auto *overviewOutputSwitch =
        temperatureOverview->findChild<SegmentedSwitchButton *>(QStringLiteral("temperatureOverviewOutputSwitch"));
    require(overviewOutputCapsule != nullptr &&
                overviewOutputLabel != nullptr &&
                overviewOutputSwitch != nullptr,
            "temperature overview exposes the output capsule, label, and switch");
    require(overviewOutputLabel->parentWidget() == overviewOutputCapsule &&
                overviewOutputSwitch->parentWidget() == overviewOutputCapsule &&
                overviewOutputLabel->text() == QStringLiteral("输出使能"),
            "temperature overview places the output-enable label above the switch in one capsule");
    require(overviewOutputSwitch->property("segmentedSwitchControl").toBool() &&
                overviewOutputSwitch->height() == 34 &&
                overviewOutputSwitch->animationDuration() == 480 &&
                overviewOutputCapsule->height() == 60,
            "temperature overview uses the shared 480 ms jelly switch inside a compact capsule");
    temperatureOverview->show();
    QApplication::processEvents();
    const int overviewSwitchLeftGap = overviewOutputSwitch->geometry().left();
    const int overviewSwitchRightGap =
        overviewOutputCapsule->width() - overviewOutputSwitch->geometry().right() - 1;
    require(std::abs(overviewSwitchLeftGap - overviewSwitchRightGap) <= 1,
            "temperature overview segmented switch is horizontally centered in its capsule");

    auto *overviewValueOverlay =
        temperatureOverview->findChild<QWidget *>(QStringLiteral("temperatureOverviewValueOverlay"));
    const QList<QLabel *> overviewValuePills =
        temperatureOverview->findChildren<QLabel *>(QStringLiteral("temperatureOverviewValuePill"));
    QWidget *overviewPlot = nullptr;
    for (QWidget *plot : temperatureOverview->findChildren<QWidget *>(QStringLiteral("temperatureTrendPlot")))
    {
        if (plot->property("temperatureOverviewPlot").toBool())
        {
            overviewPlot = plot;
            break;
        }
    }
    require(overviewValueOverlay != nullptr && overviewValuePills.size() == 2 &&
                overviewPlot != nullptr &&
                overviewValueOverlay->parentWidget() == overviewPlot &&
                overviewValueOverlay->testAttribute(Qt::WA_TransparentForMouseEvents),
            "temperature overview moves the target and current value pills into a pointer-transparent plot overlay");
    const QString targetLegendNumberColor = VaporView::appThemeColor(
        VaporView::AppThemeColor::ToolbarGreen,
        VaporView::isDarkThemeEnabled()).name(QColor::HexRgb);
    const QString currentLegendNumberColor = VaporView::appThemeColor(
        VaporView::AppThemeColor::PlotSeriesTemperature,
        VaporView::isDarkThemeEnabled()).name(QColor::HexRgb);
    require(overviewValuePills.at(0)->textFormat() == Qt::RichText &&
                overviewValuePills.at(1)->textFormat() == Qt::RichText &&
                overviewValuePills.at(0)->parentWidget() == overviewValueOverlay &&
                overviewValuePills.at(1)->parentWidget() == overviewValueOverlay,
            "temperature overview value pills use one-line rich text inside the plot overlay");
    require(overviewValuePills.at(0)->property("displayText").toString() == QStringLiteral("目标 --- ℃") &&
                overviewValuePills.at(1)->property("displayText").toString() == QStringLiteral("当前 --- ℃") &&
                overviewValuePills.at(0)->property("legendNumberColor").toString() == targetLegendNumberColor &&
                overviewValuePills.at(1)->property("legendNumberColor").toString() == currentLegendNumberColor &&
                overviewValuePills.at(0)->text().contains(
                    QStringLiteral("<span style=\"color: %1;\">---</span>").arg(targetLegendNumberColor)) &&
                overviewValuePills.at(1)->text().contains(
                    QStringLiteral("<span style=\"color: %1;\">---</span>").arg(currentLegendNumberColor)),
            "temperature overview initializes concise target and current rows with legend colors");
    VaporView::TemperatureControllerData overviewData;
    overviewData.valid = true;
    overviewData.channels[0].target_temperature_c = 25.0;
    overviewData.channels[0].measured_temperature_c = 24.75;
    temperatureOverview->updateData(overviewData);
    QApplication::processEvents();
    require(overviewValuePills.at(0)->property("displayText").toString() == QStringLiteral("目标 25.00000 ℃") &&
                overviewValuePills.at(1)->property("displayText").toString() == QStringLiteral("当前 24.75000 ℃") &&
                overviewValuePills.at(0)->accessibleName() == QStringLiteral("目标 25.00000 ℃") &&
                overviewValuePills.at(1)->accessibleName() == QStringLiteral("当前 24.75000 ℃") &&
                overviewValuePills.at(0)->text().contains(
                    QStringLiteral("<span style=\"color: %1;\">25.00000</span>").arg(targetLegendNumberColor)) &&
                overviewValuePills.at(1)->text().contains(
                    QStringLiteral("<span style=\"color: %1;\">24.75000</span>").arg(currentLegendNumberColor)),
            "temperature overview value pills format five-decimal values with matching legend colors");
    overviewPlot->repaint();
    const QRect overlayRect = overviewValueOverlay->geometry();
    require(overlayRect.left() > overviewPlot->property("plotAreaLeft").toDouble() &&
                overlayRect.top() > overviewPlot->property("plotAreaTop").toDouble() &&
                overlayRect.right() <= std::floor(overviewPlot->property("plotAreaRight").toDouble()) &&
                overlayRect.bottom() <= std::floor(overviewPlot->property("plotAreaBottom").toDouble()),
            "temperature overview value overlay stays clear of the plot axes");
    delete temperatureOverview;

    std::cout << "segmented_switch_button_test passed\n";
    return 0;
}
