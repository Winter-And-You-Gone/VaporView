#include "ground/widgets/SegmentedSwitchButton.h"
#include "ground/widgets/TemperatureControllerWidgets.h"

#include <QApplication>
#include <QFocusEvent>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>

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
    button.setAnimationDuration(160);
    button.setAutoToggle(false);
    button.setSwitchChecked(false, false);

    require(button.property("segmentedSwitchControl").toBool(),
            "segmented switch exposes its shared control identity");
    require(button.focusPolicy() == Qt::TabFocus,
            "segmented switch accepts keyboard focus without click focus");
    require(button.leftSegmentText() == QStringLiteral("Local") &&
                button.rightSegmentText() == QStringLiteral("Remote") &&
                button.text() == QStringLiteral("Source: Local"),
            "segmented switch uses configurable labels and accessible state text");
    require(button.accentMode() == SegmentedSwitchButton::AccentMode::Primary &&
                button.animationDuration() == 160,
            "segmented switch uses configurable accent and animation timing");

    button.click();
    require(!button.switchChecked(),
            "externally controlled segmented switch does not pre-toggle on click");
    button.setSwitchChecked(true, false);
    require(button.switchChecked() && button.text() == QStringLiteral("Source: Remote"),
            "segmented switch updates its selected segment and accessible state together");

    button.setAutoToggle(true);
    button.click();
    require(!button.switchChecked() && button.text() == QStringLiteral("Source: Local"),
            "self-controlled segmented switch can toggle through the shared implementation");

    QFocusEvent activeWindowFocus(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
    QApplication::sendEvent(&button, &activeWindowFocus);
    require(!button.keyboardFocusIndicatorVisible(),
            "window activation does not create a segmented-switch focus ring");

    QFocusEvent tabFocus(QEvent::FocusIn, Qt::TabFocusReason);
    QApplication::sendEvent(&button, &tabFocus);
    require(button.keyboardFocusIndicatorVisible() &&
                button.property("keyboardFocusIndicatorVisible").toBool(),
            "keyboard tab focus creates the segmented-switch focus ring");

    QMouseEvent mousePress(QEvent::MouseButtonPress,
                           QPointF(10.0, 10.0),
                           QPointF(10.0, 10.0),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    QApplication::sendEvent(&button, &mousePress);
    require(!button.keyboardFocusIndicatorVisible(),
            "pointer interaction clears the segmented-switch keyboard focus ring");

    button.setAccentMode(SegmentedSwitchButton::AccentMode::BinaryState);
    button.setSegmentTexts(QStringLiteral("Off"), QStringLiteral("On"));
    button.setStateDescription(QStringLiteral("Output Enable"), QStringLiteral(": "));
    button.setSwitchChecked(true, false);
    require(button.accentMode() == SegmentedSwitchButton::AccentMode::BinaryState &&
                button.text() == QStringLiteral("Output Enable: On"),
            "the same segmented switch supports binary output-state configuration");

    auto *temperatureOverview =
        VaporView::Ground::Widgets::createTemperatureControllerOverviewPanel();
    temperatureOverview->resize(420, 220);
    temperatureOverview->setEnglish(false);
    auto *overviewOutputCapsule =
        temperatureOverview->findChild<QFrame *>(QStringLiteral("temperatureOverviewOutputCapsule"));
    auto *overviewOutputLabel =
        temperatureOverview->findChild<QLabel *>(QStringLiteral("temperatureOverviewOutputLabel"));
    auto *overviewOutputSwitch =
        temperatureOverview->findChild<QPushButton *>(QStringLiteral("temperatureOverviewOutputSwitch"));
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
                overviewOutputCapsule->height() == 60,
            "temperature overview uses the shared 34 px segmented switch inside a compact capsule");
    delete temperatureOverview;

    std::cout << "segmented_switch_button_test passed\n";
    return 0;
}
