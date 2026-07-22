#include "ground/rtk/RtkConfigDialog.h"
#include "ground/widgets/SerialPortComboSupport.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

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
}

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    QSettings(QStringLiteral("VaporView"), QStringLiteral("RtkConfig"))
        .setValue(QStringLiteral("output_port"), QStringLiteral("__missing_serial_port__"));
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
    auto *outputPortCombo = dialog.findChild<QComboBox *>(QStringLiteral("rtkOutputPortCombo"));
    require(outputPortCombo != nullptr, "RTK output-port combo exists");
    require(outputPortCombo->findText(QStringLiteral("__missing_serial_port__")) < 0 &&
                outputPortCombo->currentText() == QStringLiteral("未选择"),
            "unavailable legacy RTK output port shows the Chinese unselected placeholder");
    dialog.setPreferredOutputPortAndBaud(QStringLiteral("COM77"), QStringLiteral("115200"));
    const int rememberedPortIndex = outputPortCombo->findText(QStringLiteral("COM77"));
    require(rememberedPortIndex >= 0 &&
                outputPortCombo->currentIndex() == rememberedPortIndex &&
                outputPortCombo->itemData(
                    rememberedPortIndex,
                    VaporView::kSerialPortHistoryItemRole).toBool(),
            "explicit RTK serial history is retained and marked as history");
    auto *xEdit = dialog.findChild<QLineEdit *>(QStringLiteral("rtkLeverXEdit"));
    auto *yEdit = dialog.findChild<QLineEdit *>(QStringLiteral("rtkLeverYEdit"));
    auto *zEdit = dialog.findChild<QLineEdit *>(QStringLiteral("rtkLeverZEdit"));
    auto *applyButton = dialog.findChild<QPushButton *>(QStringLiteral("rtkApplyLeverArmButton"));
    require(xEdit && yEdit && zEdit && applyButton, "lever-arm controls exist");

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
