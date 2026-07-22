#include "ground/rtk/RtkConfigDialog.h"
#include "ground/widgets/SerialPortComboSupport.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/SingleLevelPopupMenu.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

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
    dialog.show();
    QApplication::processEvents();
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
    auto *leverHelpButton = dialog.findChild<QToolButton *>(QStringLiteral("rtkLeverHelpButton"));
    require(xEdit && yEdit && zEdit && applyButton && leverHelpButton, "lever-arm controls exist");
    const QString leverHelpButtonStyle = leverHelpButton->styleSheet();
    require(leverHelpButtonStyle.contains(QStringLiteral("border-radius: 6px")) &&
                leverHelpButtonStyle.contains(QStringLiteral("QToolButton:hover, QToolButton:pressed")) &&
                leverHelpButtonStyle.contains(VaporView::appThemeColorName(
                    VaporView::AppThemeColor::TitleBarHover,
                    VaporView::isDarkThemePalette(app.palette()))),
            "lever-arm help uses the title-bar gray hover and pressed background");

    leverHelpButton->click();
    QApplication::processEvents();
    auto *leverHelpPopup = dialog.findChild<VaporView::SingleLevelPopupMenu *>(QStringLiteral("rtkLeverHelpPopup"));
    require(leverHelpPopup && leverHelpPopup->isVisible() &&
                leverHelpPopup->property("floatingPanelChrome").toBool() &&
                leverHelpPopup->property("shadowMargin").toInt() == 40 &&
                leverHelpPopup->property("shadowBottomMargin").toInt() == 50 &&
                leverHelpPopup->contentsMargins().bottom() == 50 &&
                leverHelpPopup->testAttribute(Qt::WA_TranslucentBackground),
            "lever-arm help uses the shared rounded shadow popup");
    auto *leverHelpText = leverHelpPopup->findChild<QLabel *>(QStringLiteral("rtkLeverHelpPopupText"));
    require(leverHelpText && leverHelpText->mapTo(leverHelpPopup, QPoint()).x() >= 30,
            "lever-arm help keeps the text inset from the shadowed panel edge");
    leverHelpPopup->hide();

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
