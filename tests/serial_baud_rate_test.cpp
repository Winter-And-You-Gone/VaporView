#include "test_settings_sandbox.h"
#include "BaudRateComboSupport.h"
#include "SkyConfig.h"

#include <QApplication>
#include <QComboBox>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMetaObject>
#include <QValidator>

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

}  // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    require(VaporView::parseSerialBaudRate(QStringLiteral("76800")) == 76800 &&
                VaporView::parseSerialBaudRate(QStringLiteral("128000")) == 128000 &&
                VaporView::parseSerialBaudRate(QStringLiteral("123457")) == 123457 &&
                VaporView::parseSerialBaudRate(QStringLiteral("256000")) == 256000 &&
                VaporView::parseSerialBaudRate(QStringLiteral("1000000")) == 1000000 &&
                VaporView::parseSerialBaudRate(QStringLiteral("1500000")) == 1500000,
            "arbitrary positive baud values parse");
    require(VaporView::normalizedSerialBaudRateText(QStringLiteral("000123457")) ==
                QStringLiteral("123457"),
            "baud text canonicalizes leading zeroes");
    require(!VaporView::parseSerialBaudRate(QString()) &&
                !VaporView::parseSerialBaudRate(QStringLiteral("   ")) &&
                !VaporView::parseSerialBaudRate(QStringLiteral("0")) &&
                !VaporView::parseSerialBaudRate(QStringLiteral("-1")) &&
                !VaporView::parseSerialBaudRate(QStringLiteral("abc")) &&
                !VaporView::parseSerialBaudRate(QStringLiteral("115200x")) &&
                !VaporView::parseSerialBaudRate(QStringLiteral("9600.5")) &&
                !VaporView::parseSerialBaudRate(QStringLiteral("2147483648")),
            "invalid or overflowing baud text is rejected");

    const auto& hc2aCapabilities = VaporView::hc2aBaudCapabilities();
    require(hc2aCapabilities.inputMode == VaporView::BaudRateInputMode::PresetAndCustom &&
                hc2aCapabilities.customMinimum == 1200 &&
                hc2aCapabilities.customMaximum == 115200 &&
                hc2aCapabilities.presets.contains(QStringLiteral("19200")) &&
                VaporView::isBaudRateSupported(hc2aCapabilities, 19200) &&
                !VaporView::isBaudRateSupported(hc2aCapabilities, 750000) &&
                !VaporView::isBaudRateSupported(hc2aCapabilities, 1199),
            "HC2A keeps the RO-ASCII default and rejects out-of-range baud rates");

    const auto& lidarCapabilities = VaporView::lidarBaudCapabilities();
    require(lidarCapabilities.inputMode == VaporView::BaudRateInputMode::PresetOnly &&
                lidarCapabilities.presets.size() == 7,
            "SDFM300 accepts only its seven documented rates");
    for (int baud : {9600, 19200, 38400, 115200, 230400, 256000, 460800})
        require(VaporView::isBaudRateSupported(lidarCapabilities, baud), "documented SDFM300 baud accepted");
    for (int baud : {57600, 500000, 750000, 921600})
        require(!VaporView::isBaudRateSupported(lidarCapabilities, baud), "unsupported SDFM300 baud rejected");

    VaporView::SkyConfig skyDefaults = VaporView::SkyConfig::defaults();
    QString skyConfigError;
    require(skyDefaults.hc2a.baud_rate == 19200 && skyDefaults.lidar.baud_rate == 460800 &&
                skyDefaults.validate(&skyConfigError),
            "HC2A 19200 and SDFM300 460800 defaults remain valid");
    skyDefaults.lidar.baud_rate = 500000;
    require(!skyDefaults.validate(&skyConfigError) &&
                skyConfigError == QStringLiteral("lidar baud is unsupported"),
            "SkyConfig rejects a persisted SDFM300 baud outside the device list");
    skyDefaults.lidar.baud_rate = 256000;
    require(skyDefaults.validate(&skyConfigError),
            "SkyConfig accepts the documented 256000 SDFM300 baud");
    QJsonObject invalidSkyJson = skyDefaults.toJson();
    QJsonObject invalidLidar = invalidSkyJson.value(QStringLiteral("lidar")).toObject();
    invalidLidar[QStringLiteral("baud")] = 500000;
    invalidSkyJson[QStringLiteral("lidar")] = invalidLidar;
    VaporView::SkyConfig parsedSkyConfig;
    require(!VaporView::SkyConfig::fromJson(invalidSkyJson, parsedSkyConfig, &skyConfigError) &&
                skyConfigError == QStringLiteral("lidar baud is unsupported"),
            "remote SkyConfig JSON rejects the same unsupported SDFM300 baud");

    QComboBox combo;
    VaporView::configureSerialBaudRateCombo(
        &combo,
        VaporView::hostSerialLinkBaudCapabilities(),
        QStringLiteral("115200"));
    require(combo.isEditable() && VaporView::isSerialBaudRateCombo(&combo),
            "host baud combo is explicitly editable");
    require(combo.count() == 9 && combo.currentText() == QStringLiteral("115200"),
            "preset baud values remain intact");

    require(VaporView::setSerialBaudRateComboText(&combo, QStringLiteral("123457")) &&
                combo.currentText() == QStringLiteral("123457") &&
                combo.count() == 9,
            "custom baud remains selected without polluting preset items");
    require(VaporView::serialBaudRateComboValue(&combo) == 123457,
            "custom baud round-trips through combo parsing");

    combo.lineEdit()->setText(QStringLiteral("256000"));
    QMetaObject::invokeMethod(combo.lineEdit(), "editingFinished", Qt::DirectConnection);
    require(combo.currentText() == QStringLiteral("256000") && combo.count() == 9,
            "typed custom baud commits without adding a preset item");
    combo.lineEdit()->setText(QStringLiteral("1000000"));
    QMetaObject::invokeMethod(combo.lineEdit(), "editingFinished", Qt::DirectConnection);
    require(VaporView::serialBaudRateComboValue(&combo) == 1000000 && combo.count() == 9,
            "large custom baud commits through the editable control");

    const QValidator *validator = combo.lineEdit()->validator();
    QString zero = QStringLiteral("0");
    QString nonNumeric = QStringLiteral("12k");
    int cursor = 0;
    require(validator && validator->validate(zero, cursor) != QValidator::Acceptable &&
                validator->validate(nonNumeric, cursor) != QValidator::Acceptable,
            "host baud editor does not accept zero or non-numeric input");
    combo.lineEdit()->setText(QStringLiteral("0"));
    QMetaObject::invokeMethod(combo.lineEdit(), "editingFinished", Qt::DirectConnection);
    require(combo.currentText() == QStringLiteral("1000000"),
            "zero baud restores the last valid value when editing finishes");
    combo.lineEdit()->setText(QStringLiteral("not-a-baud"));
    QMetaObject::invokeMethod(combo.lineEdit(), "editingFinished", Qt::DirectConnection);
    require(combo.currentText() == QStringLiteral("1000000"),
            "invalid edit restores the last valid baud");
    combo.lineEdit()->setText(QStringLiteral("123x"));
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(combo.lineEdit(), &escape);
    require(combo.currentText() == QStringLiteral("1000000") && combo.count() == 9,
            "Escape restores the last valid baud without changing presets");

    QComboBox lidarCombo;
    VaporView::configureSerialBaudRateCombo(&lidarCombo, lidarCapabilities, QStringLiteral("460800"));
    require(!lidarCombo.isEditable() && lidarCombo.currentText() == QStringLiteral("460800") &&
                lidarCombo.count() == 7, "SDFM300 baud combo exposes only documented presets");
    require(!VaporView::setSerialBaudRateComboText(&lidarCombo, QStringLiteral("500000")) &&
                lidarCombo.currentText() == QStringLiteral("460800") &&
                VaporView::setSerialBaudRateComboText(&lidarCombo, QStringLiteral("9600")) &&
                VaporView::setSerialBaudRateComboText(&lidarCombo, QStringLiteral("256000")),
            "SDFM300 combo rejects old 500000 and accepts documented low and intermediate rates");

    QComboBox ai8Combo;
    VaporView::configureSerialBaudRateCombo(
        &ai8Combo,
        VaporView::ai8TemperatureControllerBaudCapabilities(),
        QStringLiteral("19200"));
    require(!ai8Combo.isEditable() && ai8Combo.count() == 6 &&
                ai8Combo.currentText() == QStringLiteral("19200"),
            "AI-8288 connection baud is a non-editable documented preset list");
    require(!VaporView::setSerialBaudRateComboText(&ai8Combo, QStringLiteral("256000")) &&
                ai8Combo.currentText() == QStringLiteral("19200") &&
                VaporView::serialBaudRateComboValue(&ai8Combo) == 19200,
            "AI-8288 connection rejects unsupported custom baud without changing its value");
    for (const QString& baud : VaporView::ai8TemperatureControllerBaudCapabilities().presets)
    {
        require(ai8Combo.findText(baud) >= 0,
                "AI-8288 connection exposes every protocol-supported baud");
    }

    QComboBox skyLinkCombo;
    VaporView::configureSerialBaudRateCombo(
        &skyLinkCombo,
        VaporView::skyLinkBaudCapabilities(),
        QStringLiteral("921600"));
    require(skyLinkCombo.isEditable() &&
                VaporView::setSerialBaudRateComboText(&skyLinkCombo, QStringLiteral("1000000")) &&
                VaporView::serialBaudRateComboValue(&skyLinkCombo) == 1000000,
            "Sky Link remains an editable custom baud host link");

    QComboBox rtkOutputCombo;
    VaporView::configureSerialBaudRateCombo(
        &rtkOutputCombo,
        VaporView::rtkOutputBaudCapabilities(),
        QStringLiteral("115200"));
    require(rtkOutputCombo.isEditable() &&
                VaporView::setSerialBaudRateComboText(&rtkOutputCombo, QStringLiteral("123457")) &&
                VaporView::serialBaudRateComboValue(&rtkOutputCombo) == 123457,
            "RTK output remains an editable custom baud host link");

    std::cout << "serial baud rate tests passed\n";
    return 0;
}
