#include "BaudRateComboSupport.h"

#include <QApplication>
#include <QComboBox>
#include <QMetaObject>

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

}  // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    require(VaporView::parseSerialBaudRate(QStringLiteral("123457")) == 123457,
            "arbitrary positive baud parses");
    require(VaporView::normalizedSerialBaudRateText(QStringLiteral("000123457")) ==
                QStringLiteral("123457"),
            "baud text canonicalizes leading zeroes");
    require(!VaporView::parseSerialBaudRate(QStringLiteral("0")) &&
                !VaporView::parseSerialBaudRate(QStringLiteral("-9600")) &&
                !VaporView::parseSerialBaudRate(QStringLiteral("9600.5")) &&
                !VaporView::parseSerialBaudRate(QStringLiteral("2147483648")),
            "invalid or overflowing baud text is rejected");

    QComboBox combo;
    VaporView::configureSerialBaudRateCombo(
        &combo,
        {QStringLiteral("9600"), QStringLiteral("115200")},
        QStringLiteral("115200"));
    require(combo.isEditable() && VaporView::isSerialBaudRateCombo(&combo),
            "host baud combo is explicitly editable");
    require(combo.count() == 2 && combo.currentText() == QStringLiteral("115200"),
            "preset baud values remain intact");

    require(VaporView::setSerialBaudRateComboText(&combo, QStringLiteral("123457")) &&
                combo.currentText() == QStringLiteral("123457") &&
                combo.count() == 2,
            "custom baud remains selected without polluting preset items");
    require(VaporView::serialBaudRateComboValue(&combo) == 123457,
            "custom baud round-trips through combo parsing");

    combo.lineEdit()->setText(QStringLiteral("not-a-baud"));
    QMetaObject::invokeMethod(combo.lineEdit(), "editingFinished", Qt::DirectConnection);
    require(combo.currentText() == QStringLiteral("123457"),
            "invalid edit restores the last valid baud");

    std::cout << "serial baud rate tests passed\n";
    return 0;
}
