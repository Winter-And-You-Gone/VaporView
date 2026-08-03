#include "ground/widgets/Ai8TemperatureControllerPanel.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>

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
    panel.resize(1180, 300);
    panel.show();
    QApplication::processEvents();

    auto *stack = panel.findChild<QStackedWidget *>(QStringLiteral("ai8ParameterStack"));
    require(stack != nullptr && stack->count() == 4 && stack->currentIndex() == 0,
            "AI-8 panel starts on one of four documented parameter groups");

    auto *globalButton = panel.findChild<QPushButton *>(QStringLiteral("ai8PageSelectorButton4"));
    auto *channelButton = panel.findChild<QPushButton *>(QStringLiteral("ai8PageSelectorButton1"));
    require(globalButton != nullptr && channelButton != nullptr && channelButton->isChecked(),
            "AI-8 parameter page selectors exist and channel is selected");
    globalButton->click();
    QApplication::processEvents();
    require(stack->currentIndex() == 3 && globalButton->isChecked(),
            "AI-8 global page selector changes the visible page");
    channelButton->click();
    QApplication::processEvents();
    require(stack->currentIndex() == 0 && channelButton->isChecked(),
            "AI-8 channel page selector restores the visible page");

    auto *channelSpin = panel.findChild<QSpinBox *>(QStringLiteral("ai8ChannelSpin"));
    auto *setpointSpin = panel.findChild<QDoubleSpinBox *>(QStringLiteral("ai8SetpointSpin"));
    auto *addressSpin = panel.findChild<QSpinBox *>(QStringLiteral("ai8DeviceAddressSpin"));
    auto *baudCombo = panel.findChild<QComboBox *>(QStringLiteral("ai8BaudCombo"));
    require(channelSpin != nullptr && channelSpin->minimum() == 1 && channelSpin->maximum() == 8 &&
                setpointSpin != nullptr,
            "AI-8288 channel range and setpoint control are available");
    require(addressSpin != nullptr && addressSpin->minimum() == 1 && addressSpin->maximum() == 88 &&
                addressSpin->value() == 1,
            "AI-8 address range and default match the register table");
    require(baudCombo != nullptr && baudCombo->currentData().toInt() == 19200,
            "AI-8 baud rate defaults to 19.2K");

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
    panel.applyLiveData(liveData);
    auto *pvEdit = panel.findChild<QLineEdit *>(QStringLiteral("ai8MeasuredTemperatureEdit"));
    auto *temperaturePlot = panel.findChild<QWidget *>(QStringLiteral("ai8TemperatureTrendPlot"));
    require(pvEdit != nullptr && pvEdit->text().contains(QStringLiteral("23.4")),
            "AI-8 selected-channel PV is updated from live polling");
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
    panel.setEnglish(true);
    QApplication::processEvents();
    require(channelButton->text() == QStringLiteral("Channel") &&
                globalButton->text() == QStringLiteral("Global") &&
                statusLabel->text().contains(QStringLiteral("Modbus backend connected")) &&
                baudCombo->currentData().toInt() == 19200,
            "AI-8 panel translates labels without changing parameter values");

    const QImage snapshot = panel.grab().toImage();
    require(!snapshot.isNull() && snapshot.width() > 0 && snapshot.height() > 0,
            "AI-8 panel renders to a QWidget snapshot");

    std::cout << "ai8 temperature controller panel tests passed\n";
    return 0;
}
