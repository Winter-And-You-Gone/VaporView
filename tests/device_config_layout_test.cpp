#include "ground/main/MainWindow.h"
#include "ground/devices/RemoteSkyController.h"
#include "ground/navigation/CombinationNavigationPage.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "SkyConfig.h"
#include "TelemetryCodec.h"
#include "test_ui_helpers.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QJsonDocument>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QToolButton>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{

[[noreturn]] void fail(const char *message)
{
    std::cerr << "FAIL: " << message << '\n';
    VaporView::setSettingsWritesSuspended(false);
    std::exit(1);
}

void require(bool condition, const char *message)
{
    if (!condition)
    {
        fail(message);
    }
}

void activateLayouts(QWidget *widget)
{
    if (!widget)
    {
        return;
    }
    if (QLayout *layout = widget->layout())
    {
        layout->invalidate();
        layout->activate();
    }
    const QList<QWidget *> children = widget->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : children)
    {
        activateLayouts(child);
    }
}

QPushButton *findDeviceConfigNav(MainWindow& window)
{
    for (QPushButton *button : window.findChildren<QPushButton *>())
    {
        if (!button)
        {
            continue;
        }
        if (button->accessibleName() == QStringLiteral("设备配置") ||
            button->accessibleName() == QStringLiteral("Device"))
        {
            return button;
        }
    }
    return nullptr;
}

QFrame *findLinkStatusCard(QWidget *deviceConfigPage)
{
    for (QFrame *card : deviceConfigPage->findChildren<QFrame *>(QStringLiteral("epsilonSectionCard")))
    {
        if (!card || !card->isVisible())
        {
            continue;
        }
        for (QLabel *label : card->findChildren<QLabel *>())
        {
            const QString text = label->text();
            if (text.contains(QStringLiteral("数据源与天地链路")) ||
                text.contains(QStringLiteral("Data Source / Sky Link")))
            {
                return card;
            }
        }
    }
    return nullptr;
}

QRect rectInPage(QWidget *widget, QWidget *page)
{
    return QRect(widget->mapTo(page, QPoint(0, 0)), widget->size());
}

bool cardHasAnyLabel(QWidget *card, const QStringList& candidates)
{
    if (!card)
    {
        return false;
    }
    for (QLabel *label : card->findChildren<QLabel *>())
    {
        if (candidates.contains(label->text()))
        {
            return true;
        }
    }
    return false;
}

QLabel *findExactLabel(QWidget *parent, const QString& text)
{
    if (!parent)
    {
        return nullptr;
    }
    for (QLabel *label : parent->findChildren<QLabel *>())
    {
        if (label && label->text() == text)
        {
            return label;
        }
    }
    return nullptr;
}

QLabel *findSubsectionLabel(QWidget *parent, const QString& sectionKey)
{
    if (!parent)
    {
        return nullptr;
    }
    for (QLabel *label : parent->findChildren<QLabel *>(QStringLiteral("deviceConfigSubsectionLabel")))
    {
        if (label && label->property("deviceConfigSubsection").toString() == sectionKey)
        {
            return label;
        }
    }
    return nullptr;
}

void requireLabelFits(QLabel *label, const char *message)
{
    require(label != nullptr, message);
    const int textWidth = label->fontMetrics().horizontalAdvance(label->text());
    if (textWidth > label->width() + 1)
    {
        std::cerr << "Label clipped: text='"
                  << label->text().toStdString()
                  << "' textWidth=" << textWidth
                  << " labelWidth=" << label->width() << '\n';
    }
    require(textWidth <= label->width() + 1, message);
}

void requireHeaderAboveWidget(QWidget *container, QLabel *header, QWidget *widget, const char *message)
{
    require(container != nullptr && header != nullptr && widget != nullptr, message);
    const QRect headerRect(header->mapTo(container, QPoint(0, 0)), header->size());
    const QRect widgetRect(widget->mapTo(container, QPoint(0, 0)), widget->size());
    if (headerRect.bottom() >= widgetRect.top())
    {
        std::cerr << "Column header is not above widget: header='"
                  << header->text().toStdString()
                  << "' headerBottom=" << headerRect.bottom()
                  << " widgetTop=" << widgetRect.top() << '\n';
    }
    require(headerRect.bottom() < widgetRect.top(), message);
    require(headerRect.center().x() >= widgetRect.left() - 8 &&
                headerRect.center().x() <= widgetRect.right() + 8,
            message);
}

void selectComboData(QComboBox *combo, const QString& data, const char *message)
{
    require(combo != nullptr, message);
    const int index = combo->findData(data);
    require(index >= 0, message);
    combo->setCurrentIndex(index);
}

void selectComboText(QComboBox *combo, const QString& text, const char *message)
{
    require(combo != nullptr, message);
    int index = combo->findText(text);
    if (index < 0)
    {
        combo->addItem(text, text);
        index = combo->findText(text);
    }
    require(index >= 0, message);
    combo->setCurrentIndex(index);
}

QString pillName(QFrame *pill)
{
    QLabel *nameLabel =
        pill ? pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel")) : nullptr;
    return nameLabel ? nameLabel->text() : QString();
}

QList<QList<QFrame *>> pillRows(QFrame *subCard)
{
    QList<QFrame *> pills =
        subCard ? subCard->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill")) : QList<QFrame *>();
    std::sort(pills.begin(), pills.end(), [subCard](QFrame *left, QFrame *right) {
        const QRect leftRect(left->mapTo(subCard, QPoint(0, 0)), left->size());
        const QRect rightRect(right->mapTo(subCard, QPoint(0, 0)), right->size());
        if (std::abs(leftRect.top() - rightRect.top()) > 2)
        {
            return leftRect.top() < rightRect.top();
        }
        return leftRect.left() < rightRect.left();
    });

    QList<QList<QFrame *>> rows;
    QList<int> rowTops;
    for (QFrame *pill : pills)
    {
        const QRect rect(pill->mapTo(subCard, QPoint(0, 0)), pill->size());
        if (rows.isEmpty() || std::abs(rect.top() - rowTops.last()) > 2)
        {
            rows << QList<QFrame *>();
            rowTops << rect.top();
        }
        rows.last() << pill;
    }
    return rows;
}

QStringList pillNames(const QList<QFrame *>& row)
{
    QStringList names;
    for (QFrame *pill : row)
    {
        names << pillName(pill);
    }
    return names;
}

void requireAlignedColumns(QFrame *subCard, int columnCount, const char *message)
{
    const QList<QList<QFrame *>> rows = pillRows(subCard);
    QVector<int> lefts(columnCount, -1);
    QVector<int> widths(columnCount, -1);
    for (const QList<QFrame *>& row : rows)
    {
        require(row.size() <= columnCount, message);
        for (int column = 0; column < row.size(); ++column)
        {
            const QRect rect(row.at(column)->mapTo(subCard, QPoint(0, 0)), row.at(column)->size());
            if (lefts.at(column) < 0)
            {
                lefts[column] = rect.left();
                widths[column] = rect.width();
                continue;
            }
            require(std::abs(rect.left() - lefts.at(column)) <= 2 &&
                        std::abs(rect.width() - widths.at(column)) <= 2,
                    message);
        }
    }
}

void requirePillsUseWidestColumnWidth(QFrame *subCard, const char *message)
{
    const QList<QList<QFrame *>> rows = pillRows(subCard);
    int widestWidth = 0;
    int pillCount = 0;
    for (const QList<QFrame *>& row : rows)
    {
        for (QFrame *pill : row)
        {
            widestWidth = std::max(widestWidth, pill->width());
            ++pillCount;
        }
    }
    require(pillCount > 0 && widestWidth > 0, message);
    for (const QList<QFrame *>& row : rows)
    {
        for (QFrame *pill : row)
        {
            require(std::abs(pill->width() - widestWidth) <= 2, message);
        }
    }
}

void requirePillLabelsFit(QFrame *subCard, const char *message)
{
    const QList<QFrame *> pills =
        subCard ? subCard->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill")) : QList<QFrame *>();
    require(!pills.isEmpty(), message);
    for (QFrame *pill : pills)
    {
        const QList<QLabel *> labels = pill->findChildren<QLabel *>();
        for (QLabel *label : labels)
        {
            if (!label || label->text().isEmpty())
            {
                continue;
            }
            const int textWidth = label->fontMetrics().horizontalAdvance(label->text());
            if (textWidth > label->width() + 1)
            {
                std::cerr << "Telemetry pill label clipped: text='"
                          << label->text().toStdString()
                          << "' textWidth=" << textWidth
                          << " labelWidth=" << label->width()
                          << " pillWidth=" << pill->width() << '\n';
            }
            require(textWidth <= label->width() + 1, message);
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDirectory;
    require(settingsDirectory.isValid(), "temporary settings directory created");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDirectory.path());

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("VaporView"));
    app.setOrganizationName(QStringLiteral("VaporView"));
    QSettings historySettings(QStringLiteral("VaporView"), QStringLiteral("SerialPortHistory"));
    historySettings.setValue(QStringLiteral("ports"), QStringList{QStringLiteral("COM7")});
    historySettings.sync();
    VaporView::setSettingsWritesSuspended(true);

    MainWindow window;
    window.resize(1280, 760);
    window.show();
    require(VaporViewTest::waitForWindowExposed(&window),
            "main window becomes exposed for device configuration layout test");

    QPushButton *deviceConfigNav = findDeviceConfigNav(window);
    require(deviceConfigNav != nullptr, "device configuration nav button exists");
    deviceConfigNav->click();
    VaporViewTest::processEventsFor(180);
    activateLayouts(&window);

    QWidget *deviceConfigPage = window.findChild<QWidget *>(QStringLiteral("deviceConfigPage"));
    require(deviceConfigPage != nullptr && deviceConfigPage->isVisible(),
            "device configuration page is visible");
    QScrollArea *scrollArea =
        deviceConfigPage->findChild<QScrollArea *>(QStringLiteral("mainCardsScrollArea"));
    require(scrollArea != nullptr && scrollArea->widget() != nullptr,
            "device configuration scroll area exists");
    const int horizontalMaximum = scrollArea->horizontalScrollBar()
        ? scrollArea->horizontalScrollBar()->maximum()
        : -1;
    if (horizontalMaximum != 0)
    {
        std::cerr << "Device configuration horizontal overflow: max=" << horizontalMaximum
                  << " viewport=" << scrollArea->viewport()->width()
                  << " content=" << scrollArea->widget()->width()
                  << " contentMin=" << scrollArea->widget()->minimumWidth()
                  << '\n';
    }
    require(scrollArea->horizontalScrollBar() != nullptr &&
                horizontalMaximum == 0,
            "device configuration page has no horizontal overflow");

    QFrame *linkStatusCard = findLinkStatusCard(deviceConfigPage);
    QGroupBox *serialCard = deviceConfigPage->findChild<QGroupBox *>(QStringLiteral("sensorGroupBox"));
    require(linkStatusCard != nullptr, "link-status card can be identified");
    require(serialCard != nullptr, "serial configuration card can be identified");
    require(linkStatusCard->parentWidget() == serialCard->parentWidget(),
            "link-status and serial configuration cards are siblings");

    const QMargins margins = scrollArea->widget()->layout()
        ? scrollArea->widget()->layout()->contentsMargins()
        : QMargins();
    const int expectedCardWidth = scrollArea->viewport()->width() - margins.left() - margins.right();
    const QRect linkRect = rectInPage(linkStatusCard, deviceConfigPage);
    const QRect serialRect = rectInPage(serialCard, deviceConfigPage);
    require(linkRect.top() < serialRect.top(),
            "link-status card is on the first row");
    require(serialRect.top() > linkRect.bottom(),
            "serial configuration card is on the second row");
    require(std::abs(linkRect.left() - serialRect.left()) <= 2 &&
                std::abs(linkRect.right() - serialRect.right()) <= 2,
            "both device configuration cards share the same horizontal span");
    require(std::abs(linkStatusCard->width() - expectedCardWidth) <= 4 &&
                std::abs(serialCard->width() - expectedCardWidth) <= 4,
            "both device configuration cards fill the main content width");
    require(cardHasAnyLabel(linkStatusCard,
                            QStringList() << QStringLiteral("记录") << QStringLiteral("Record")),
            "link-status card includes the remote recording state field");
    require(cardHasAnyLabel(linkStatusCard,
                            QStringList() << QStringLiteral("磁盘") << QStringLiteral("Disk")),
            "link-status card includes the remaining sky disk field");
    require(cardHasAnyLabel(linkStatusCard, QStringList() << QStringLiteral("CRC")),
            "link-status card includes the CRC error field");

    QWidget *summaryContainer =
        linkStatusCard->findChild<QWidget *>(QStringLiteral("homeTelemetrySummaryContainer"));
    require(summaryContainer != nullptr, "link-status card exposes its summary container");
    require(qobject_cast<QHBoxLayout *>(summaryContainer->layout()) != nullptr,
            "link-status summary sections are arranged horizontally");
    QList<QFrame *> subCards =
        summaryContainer->findChildren<QFrame *>(QStringLiteral("homeTelemetrySectionCard"));
    require(subCards.size() == 3,
            "link-status card keeps the three telemetry summary subcards");
    std::sort(subCards.begin(), subCards.end(), [summaryContainer](QFrame *left, QFrame *right) {
        return left->mapTo(summaryContainer, QPoint(0, 0)).x() <
               right->mapTo(summaryContainer, QPoint(0, 0)).x();
    });
    const QList<QList<QFrame *>> rateRows = pillRows(subCards.at(0));
    require(rateRows.size() == 3 &&
                rateRows.at(0).size() == 2 &&
                rateRows.at(1).size() == 2 &&
                rateRows.at(2).size() == 2,
            "data-rate subcard arranges its pills as two aligned columns");
    requireAlignedColumns(subCards.at(0), 2,
                          "data-rate subcard keeps all rows aligned to two columns");
    requirePillsUseWidestColumnWidth(subCards.at(0),
                                     "data-rate subcard uses its widest pill as every column width");
    requirePillLabelsFit(subCards.at(0),
                         "data-rate subcard pill labels fit without clipping");
    requirePillsUseWidestColumnWidth(subCards.at(1),
                                     "link-rate subcard uses its widest pill as every column width");
    requirePillLabelsFit(subCards.at(1),
                         "link-rate subcard pill labels fit without clipping");

    const QList<QList<QFrame *>> dataRows = pillRows(subCards.at(2));
    const QStringList firstDataRow = dataRows.isEmpty() ? QStringList() : pillNames(dataRows.first());
    const bool chineseStatusRow =
        firstDataRow == (QStringList() << QStringLiteral("磁盘") << QStringLiteral("记录") << QStringLiteral("CRC"));
    const bool englishStatusRow =
        firstDataRow == (QStringList() << QStringLiteral("Disk") << QStringLiteral("Record") << QStringLiteral("CRC"));
    require(dataRows.size() == 3 &&
                dataRows.at(0).size() == 3 &&
                dataRows.at(1).size() == 3 &&
                dataRows.at(2).size() == 2 &&
                (chineseStatusRow || englishStatusRow),
            "data subcard places disk, recording state, and CRC on the first row");
    requireAlignedColumns(subCards.at(2), 3,
                          "data subcard keeps all rows aligned to the same three columns");
    requirePillsUseWidestColumnWidth(subCards.at(2),
                                     "data subcard uses its widest pill as every column width");
    requirePillLabelsFit(subCards.at(2),
                         "data subcard pill labels fit without clipping");

    const int summaryTop = summaryContainer->layout()->contentsMargins().top();
    int previousRight = -1;
    for (QFrame *subCard : subCards)
    {
        const QRect subRect(subCard->mapTo(summaryContainer, QPoint(0, 0)), subCard->size());
        require(previousRight < 0 || subRect.left() > previousRight,
                "link-status subcards are arranged left-to-right");
        require(std::abs(subRect.top() - summaryTop) <= 2,
                "link-status subcards share a top edge");
        require(subRect.right() <= summaryContainer->width() &&
                    subRect.bottom() <= summaryContainer->height(),
                "link-status subcards stay inside the summary container");
        require(subCard->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed &&
                    std::abs(subRect.width() - subCard->sizeHint().width()) <= 2,
                "link-status subcard width follows its widest content row");
        previousRight = subRect.right();
    }

    const QStringList compactDeviceLabels{
        QStringLiteral("EPSILON"),
        QStringLiteral("气压"),
        QStringLiteral("温湿度"),
        QStringLiteral("TFA1500-L"),
        QStringLiteral("激光温控"),
        QStringLiteral("系统温控"),
    };
    for (const QString& labelText : compactDeviceLabels)
    {
        requireLabelFits(findExactLabel(serialCard, labelText),
                         "serial configuration uses compact target-neutral device labels without clipping");
    }
    int widestDeviceLabelText = 0;
    int deviceLabelWidth = -1;
    for (const QString& labelText : compactDeviceLabels)
    {
        QLabel *label = findExactLabel(serialCard, labelText);
        require(label != nullptr, "serial configuration device label exists for width measurement");
        widestDeviceLabelText = std::max(widestDeviceLabelText,
                                         label->fontMetrics().horizontalAdvance(labelText));
        if (deviceLabelWidth < 0)
        {
            deviceLabelWidth = label->width();
        }
        require(std::abs(label->width() - deviceLabelWidth) <= 1,
                "serial configuration device labels share one content-driven column width");
    }
    require(deviceLabelWidth >= widestDeviceLabelText &&
                deviceLabelWidth <= widestDeviceLabelText + 2,
            "serial configuration device column uses the widest compact label without extra width");
    const QStringList serialColumnHeaders{
        QStringLiteral("设备"),
        QStringLiteral("串口"),
        QStringLiteral("波特率"),
        QStringLiteral("频率/轮询"),
        QStringLiteral("来源"),
        QStringLiteral("操作"),
    };
    for (const QString& headerText : serialColumnHeaders)
    {
        requireLabelFits(findExactLabel(serialCard, headerText),
                         "serial configuration column headers are visible and fit");
    }
    auto *epsilonPacketRatesButton =
        serialCard->findChild<QPushButton *>(QStringLiteral("deviceEpsilonPacketRatesButton"));
    requireHeaderAboveWidget(serialCard,
                             findExactLabel(serialCard, QStringLiteral("设备")),
                             findExactLabel(serialCard, QStringLiteral("EPSILON")),
                             "device column header sits above the device names");
    requireHeaderAboveWidget(serialCard,
                             findExactLabel(serialCard, QStringLiteral("串口")),
                             serialCard->findChild<QWidget *>(QStringLiteral("deviceEpsilonPortCombo")),
                             "serial-port column header sits above port selectors");
    requireHeaderAboveWidget(serialCard,
                             findExactLabel(serialCard, QStringLiteral("波特率")),
                             serialCard->findChild<QWidget *>(QStringLiteral("deviceEpsilonBaudCombo")),
                             "baud-rate column header sits above baud selectors");
    requireHeaderAboveWidget(serialCard,
                             findExactLabel(serialCard, QStringLiteral("频率/轮询")),
                             epsilonPacketRatesButton,
                             "rate column header sits above the EPSILON packet-rate entry");
    requireHeaderAboveWidget(serialCard,
                             findExactLabel(serialCard, QStringLiteral("来源")),
                             serialCard->findChild<QWidget *>(QStringLiteral("devicePressureSourceCombo")),
                             "source column header sits above source selectors");
    QLabel *actionHeader = findExactLabel(serialCard, QStringLiteral("操作"));
    require(actionHeader != nullptr, "link-action column header exists");
    int centeredActionCount = 0;
    const QRect actionHeaderRect(actionHeader->mapTo(serialCard, QPoint(0, 0)), actionHeader->size());
    for (QToolButton *button : serialCard->findChildren<QToolButton *>())
    {
        if (!button->isVisible() || button->property("deviceConfigRemoteAction").toString().isEmpty())
        {
            continue;
        }
        const QRect buttonRect(button->mapTo(serialCard, QPoint(0, 0)), button->size());
        if (std::abs(buttonRect.center().x() - actionHeaderRect.center().x()) > 1)
        {
            std::cerr << "Link-action icon is not centered: headerCenter="
                      << actionHeaderRect.center().x()
                      << " buttonCenter=" << buttonRect.center().x() << '\n';
        }
        require(std::abs(buttonRect.center().x() - actionHeaderRect.center().x()) <= 1,
                "link-action icons are centered under the link-action column header");
        ++centeredActionCount;
    }
    require(centeredActionCount == 6,
            "serial configuration exposes all six centered link-action icons");
    auto *pressureSourceCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("devicePressureSourceCombo"));
    auto *humiditySourceCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceHumiditySourceCombo"));
    selectComboData(pressureSourceCombo, QStringLiteral("ptb210"),
                    "pressure source combo exposes PTB210");
    selectComboData(humiditySourceCombo, QStringLiteral("hmp3"),
                    "humidity source combo exposes HMP3");
    VaporViewTest::processEventsFor(120);
    require(pressureSourceCombo->toolTip().contains(QStringLiteral("PTB210")) &&
                !pressureSourceCombo->toolTip().contains(QStringLiteral("BMP390")),
            "pressure source tooltip follows PTB210 selection");
    require(humiditySourceCombo->toolTip().contains(QStringLiteral("HMP3")) &&
                !humiditySourceCombo->toolTip().contains(QStringLiteral("SHT45")),
            "humidity source tooltip follows HMP3 selection");
    selectComboData(pressureSourceCombo, QStringLiteral("bmp390"),
                    "pressure source combo exposes BMP390");
    selectComboData(humiditySourceCombo, QStringLiteral("sht45"),
                    "humidity source combo exposes SHT45");
    VaporViewTest::processEventsFor(120);
    activateLayouts(&window);
    require(pressureSourceCombo->toolTip().contains(QStringLiteral("BMP390")) &&
                !pressureSourceCombo->toolTip().contains(QStringLiteral("PTB210")),
            "pressure source tooltip follows BMP390 selection");
    require(humiditySourceCombo->toolTip().contains(QStringLiteral("SHT45")) &&
                !humiditySourceCombo->toolTip().contains(QStringLiteral("HMP3")),
            "humidity source tooltip follows SHT45 selection");
    requireLabelFits(findExactLabel(serialCard, QStringLiteral("气压")),
                     "pressure device row remains target-neutral while the source field carries the model");
    requireLabelFits(findExactLabel(serialCard, QStringLiteral("温湿度")),
                     "humidity device row remains target-neutral while the source field carries the model");

    auto *dataSourceModeCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceDataSourceModeCombo"));
    auto *epsilonPortCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceEpsilonPortCombo"));
    auto *epsilonBaudCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceEpsilonBaudCombo"));
    auto *epsilonRateCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceEpsilonRateCombo"));
    auto *remoteCard =
        deviceConfigPage->findChild<QGroupBox *>(QStringLiteral("deviceRemoteSkyConfigCard"));
    auto *remoteStatus =
        deviceConfigPage->findChild<QLabel *>(QStringLiteral("deviceRemoteSkyConfigStatus"));
    auto *remoteApplyButton =
        deviceConfigPage->findChild<QPushButton *>(QStringLiteral("deviceRemoteSkyApplyButton"));
    auto *remoteSaveButton =
        deviceConfigPage->findChild<QPushButton *>(QStringLiteral("deviceRemoteSkySaveButton"));
    auto *rawModeButton =
        deviceConfigPage->findChild<QPushButton *>(QStringLiteral("deviceRemoteSkyRawModeButton"));
    auto *rawJsonEdit =
        deviceConfigPage->findChild<QPlainTextEdit *>(QStringLiteral("deviceRemoteSkyRawJsonEdit"));
    auto *rd105SlaveSpin =
        deviceConfigPage->findChild<QSpinBox *>(QStringLiteral("deviceRemoteSkyRd105SlaveSpin"));
    auto *ai8EnabledCheck =
        serialCard->findChild<QCheckBox *>(QStringLiteral("deviceRemoteAi8TemperatureEnabledCheck"));
    auto *ai8PortCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceAi8TemperaturePortCombo"));
    auto *ai8BaudCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceAi8TemperatureBaudCombo"));
    auto *ai8RateCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceAi8TemperatureRateCombo"));
    require(dataSourceModeCombo && epsilonPortCombo && epsilonBaudCombo && epsilonRateCombo &&
                epsilonPacketRatesButton,
            "shared device config controls exist for target switching");
    require(!epsilonRateCombo->isVisible() &&
                !epsilonRateCombo->isEnabled() &&
                epsilonPacketRatesButton->isVisible() &&
                epsilonPacketRatesButton->text() == QStringLiteral("包频率设置"),
            "EPSILON frequency column uses a packet-rate navigation button instead of a single rate selector");
    require(epsilonRateCombo->findText(QStringLiteral("No Set")) < 0 &&
                epsilonRateCombo->findText(QStringLiteral("不设定")) < 0,
            "EPSILON compatibility rate combo does not expose an unspecified option");
    auto *mainPageStackForEpsilonButton =
        window.findChild<QStackedWidget *>(QStringLiteral("mainPageStack"));
    auto *combinationPageForEpsilonButton =
        window.findChild<VaporView::Ground::Navigation::CombinationNavigationPage *>();
    require(mainPageStackForEpsilonButton && combinationPageForEpsilonButton,
            "combination navigation page is available for the EPSILON packet-rate jump");
    epsilonPacketRatesButton->click();
    VaporViewTest::processEventsFor(120);
    require(mainPageStackForEpsilonButton->currentWidget() == combinationPageForEpsilonButton &&
                combinationPageForEpsilonButton->currentSection() ==
                    VaporView::Ground::Navigation::CombinationNavigationPage::Section::Epsilon,
            "EPSILON packet-rate button jumps to Combination Navigation EPSILON settings");
    mainPageStackForEpsilonButton->setCurrentWidget(deviceConfigPage);
    VaporViewTest::processEventsFor(80);
    auto *skyTelemetryTransportCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceSkyTelemetryTransportCombo"));
    QWidget *skyTelemetryRow = skyTelemetryTransportCombo ? skyTelemetryTransportCombo->parentWidget() : nullptr;
    require(skyTelemetryRow && !skyTelemetryRow->isVisible(),
            "local mode hides sky-ground link editing controls from the unified device table");
    QLabel *servicesLabel = findSubsectionLabel(remoteCard, QStringLiteral("services"));
    QLabel *syncLabel = findSubsectionLabel(remoteCard, QStringLiteral("sync"));
    QLabel *advancedLabel = findSubsectionLabel(remoteCard, QStringLiteral("advanced"));
    require(remoteCard && remoteStatus && remoteApplyButton && remoteSaveButton &&
                rawModeButton && rawJsonEdit && rd105SlaveSpin &&
                servicesLabel && syncLabel && advancedLabel,
            "remote sky service, sync, and diagnostics controls exist on the unified page");
    QLabel *remoteTitleIcon = nullptr;
    for (QLabel *iconLabel : remoteCard->findChildren<QLabel *>(QStringLiteral("sectionTitleIcon")))
    {
        if (iconLabel->property("_vv_section_title_icon_name").toString() == QStringLiteral("server-cog"))
        {
            remoteTitleIcon = iconLabel;
            break;
        }
    }
    require(remoteTitleIcon && !remoteTitleIcon->pixmap().isNull(),
            "remote sky service/config card renders its server configuration icon");
    require(!remoteCard->isVisible(),
            "local mode hides the remote-only Sky services/config card");
    require(!rawJsonEdit->isVisible(),
            "SkyConfig JSON starts hidden instead of acting as a primary configuration area");

    selectComboText(epsilonPortCombo, QStringLiteral("COM7"),
                    "local EPSILON port can be set before switching targets");
    VaporViewTest::processEventsFor(120);
    require(epsilonPortCombo->currentText() == QStringLiteral("COM7"),
            "local EPSILON port is visible before remote switch");

    dataSourceModeCombo->setCurrentIndex(1);
    VaporViewTest::processEventsFor(160);
    activateLayouts(&window);
    require(scrollArea->horizontalScrollBar() &&
                scrollArea->horizontalScrollBar()->maximum() == 0 &&
                !scrollArea->horizontalScrollBar()->isVisible(),
            "remote device configuration page has no horizontal overflow");
    require(cardHasAnyLabel(serialCard,
                            QStringList() << QStringLiteral("设备配置 [远程]")
                                          << QStringLiteral("Device Configuration [Remote]")),
            "remote mode retitles the shared device configuration card for the Remote target");
    require(skyTelemetryRow->isVisible(),
            "remote mode shows sky-ground link editing controls in the target section");
    require(remoteCard->isVisible(), "remote sky service/config card appears in remote mode");
    require(servicesLabel->isVisible() && syncLabel->isVisible() && advancedLabel->isVisible(),
            "remote mode separates Sky services, config sync, and advanced diagnostics");
    require(!rawJsonEdit->isVisible(),
            "advanced SkyConfig JSON remains collapsed until requested");
    require(remoteStatus->property("status").toString() == QStringLiteral("disabled"),
            "disconnected remote mode explains the config state with a disabled status chip");
    require(pressureSourceCombo->isVisible() && humiditySourceCombo->isVisible(),
            "remote mode keeps PTB/BMP390 and HMP/SHT45 source selectors visible");
    require(ai8EnabledCheck && ai8EnabledCheck->isVisible() &&
                ai8PortCombo && ai8PortCombo->isVisible() &&
                ai8BaudCombo && ai8BaudCombo->isVisible() &&
                ai8RateCombo && ai8RateCombo->isVisible(),
            "remote mode exposes AI-8 SkyConfig serial fields");
    require(!remoteApplyButton->isEnabled() && !remoteSaveButton->isEnabled(),
            "disconnected remote mode disables apply and save operations");
    require(!epsilonPortCombo->isEnabled() &&
                ai8PortCombo && !ai8PortCombo->isEnabled(),
            "remote config fields wait for a loaded SkyConfig while the link is disconnected");
    auto *tcpWaveHostEdit = window.findChild<QLineEdit *>(QStringLiteral("tcpWaveHostEdit"));
    auto *tcpWavePortEdit = window.findChild<QLineEdit *>(QStringLiteral("tcpWavePortEdit"));
    auto *tcpWaveConnectButton = window.findChild<QPushButton *>(QStringLiteral("compactTcpStartButton"));
    require(tcpWaveHostEdit != nullptr && tcpWavePortEdit != nullptr && tcpWaveConnectButton != nullptr,
            "home TCP wave source controls are reachable in remote mode");
    require(tcpWaveHostEdit->isEnabled() && tcpWavePortEdit->isEnabled(),
            "remote TCP wave disconnected mode leaves host and port editable");

    VaporView::SkyConfig remoteConfig = VaporView::SkyConfig::defaults();
    remoteConfig.epsilon = {true, QStringLiteral("/dev/ttyEPSILON"), 921600};
    remoteConfig.ptb = {true, QStringLiteral("/dev/ttyPTB210"), 9600, 20.0};
    remoteConfig.ptb.source = QStringLiteral("ptb210");
    remoteConfig.hmp = {true, QStringLiteral("/dev/ttyHMP3"), 19200, 20.0};
    remoteConfig.hmp.source = QStringLiteral("hmp3");
    remoteConfig.lidar = {true, QStringLiteral("/dev/ttyLIDAR"), 500000, 100.0};
    remoteConfig.temperature_controller = {true, QStringLiteral("/dev/ttyRD105"), 38400, 5.0, 9};
    remoteConfig.ai8_temperature_controller = {true, QStringLiteral("/dev/ttyAI8"), 19200, 5.0, 5};
    remoteConfig.wave_tcp = {true, QStringLiteral("10.0.0.2"), 8899, 12, 0, 0};
    remoteConfig.telemetry = {11.0, 12.0, 2.0, 1.0, 3.0};

    auto *remoteController = qobject_cast<VaporView::Ground::Devices::RemoteSkyController *>(
        window.property("remoteSkyController").value<QObject *>());
    require(remoteController != nullptr,
            "normal-mode device config test can access the Remote Sky controller");
    QTcpServer fakeSkyServer;
    require(fakeSkyServer.listen(QHostAddress::LocalHost),
            "fake SkyConfig TCP server starts after the remote page is already open");
    QTcpSocket *fakeSkySocket = nullptr;
    VaporView::TelemetryCodec inboundCodec;
    VaporView::TelemetryCodec outboundCodec;
    int getSkyConfigRequests = 0;
    int setSkyConfigRequests = 0;
    bool skyConfigFrameSent = false;
    bool skyConfigApplyResultSent = false;
    QObject::connect(&fakeSkyServer, &QTcpServer::newConnection, [&]() {
        fakeSkySocket = fakeSkyServer.nextPendingConnection();
        require(fakeSkySocket != nullptr, "fake SkyConfig server accepts the Ground link");
        QObject::connect(fakeSkySocket, &QTcpSocket::readyRead, [&]() {
            const QVector<VaporView::TelemetryFrame> frames =
                inboundCodec.feedBytes(fakeSkySocket->readAll());
            for (const VaporView::TelemetryFrame& frame : frames)
            {
                if (frame.type != VaporView::MsgType::Command)
                {
                    continue;
                }
                VaporView::CommandMessage command;
                if (!VaporView::TelemetryCodec::parseCommand(frame.payload, command) ||
                    (command.command_id != VaporView::CommandId::GetSkyConfig &&
                     command.command_id != VaporView::CommandId::SetSkyConfig))
                {
                    continue;
                }
                VaporView::CommandAck ack;
                ack.command_id = command.command_id;
                ack.command_seq = command.command_seq;
                ack.error_code = VaporView::CommandErrorCode::Ok;
                if (command.command_id == VaporView::CommandId::GetSkyConfig)
                {
                    ++getSkyConfigRequests;
                }
                else
                {
                    ++setSkyConfigRequests;
                    VaporView::SkyConfig parsedConfig;
                    QString parseError;
                    const QJsonDocument document = QJsonDocument::fromJson(command.payload);
                    if (document.isObject() &&
                        VaporView::SkyConfig::fromJson(document.object(), parsedConfig, &parseError))
                    {
                        remoteConfig = parsedConfig;
                    }
                }
                fakeSkySocket->write(outboundCodec.encodeFrame(
                    VaporView::MsgType::CommandAck,
                    VaporView::TelemetryCodec::serializeCommandAck(ack),
                    static_cast<quint16>(100 + getSkyConfigRequests * 2),
                    1));
                if (command.command_id == VaporView::CommandId::GetSkyConfig)
                {
                    fakeSkySocket->write(outboundCodec.encodeFrame(
                        VaporView::MsgType::SkyConfig,
                        QJsonDocument(remoteConfig.toJson()).toJson(QJsonDocument::Compact),
                        static_cast<quint16>(101 + getSkyConfigRequests * 2),
                        2));
                    skyConfigFrameSent = true;
                }
                else
                {
                    fakeSkySocket->write(outboundCodec.encodeFrame(
                        VaporView::MsgType::SkyConfigApplyResult,
                        QJsonDocument(QJsonObject{{QStringLiteral("success"), true}})
                            .toJson(QJsonDocument::Compact),
                        static_cast<quint16>(180 + setSkyConfigRequests),
                        3));
                    skyConfigApplyResultSent = true;
                }
            }
        });
    });
    VaporView::setSettingsWritesSuspended(false);
    const bool remoteOpened = remoteController->openTcp(
        QStringLiteral("127.0.0.1"), fakeSkyServer.serverPort());
    if (!remoteOpened)
    {
        VaporView::setSettingsWritesSuspended(true);
    }
    require(remoteOpened,
            "Ground opens the fake Sky TCP link after entering the Remote Device Config page");
    const bool remoteConfigAutoLoaded = VaporViewTest::processEventsUntil(3000, [&]() {
                return skyConfigFrameSent &&
                    getSkyConfigRequests == 1 &&
                    remoteStatus->property("status").toString() == QStringLiteral("success") &&
                    epsilonPortCombo->isEnabled() &&
                    epsilonBaudCombo->isEnabled() &&
                    ai8PortCombo->isEnabled() &&
                    ai8BaudCombo->isEnabled() &&
                    ai8RateCombo->isEnabled() &&
                    epsilonPortCombo->currentText() == QStringLiteral("/dev/ttyEPSILON") &&
                    ai8PortCombo->currentText() == QStringLiteral("/dev/ttyAI8");
            });
    VaporView::setSettingsWritesSuspended(true);
    if (!remoteConfigAutoLoaded)
    {
        std::cerr << "remote lifecycle state: opened=" << remoteController->isOpen()
                  << " getRequests=" << getSkyConfigRequests
                  << " frameSent=" << skyConfigFrameSent
                  << " status=" << remoteStatus->property("status").toString().toStdString()
                  << " text=" << remoteStatus->text().toStdString()
                  << " epsilonEnabled=" << epsilonPortCombo->isEnabled()
                  << " epsilonText=" << epsilonPortCombo->currentText().toStdString()
                  << " ai8Enabled=" << ai8PortCombo->isEnabled()
                  << " ai8Text=" << ai8PortCombo->currentText().toStdString()
                  << "\n";
    }
    require(remoteConfigAutoLoaded,
            "page-open-before-Sky lifecycle auto-reads SkyConfig and enables remote serial fields");
    VaporViewTest::processEventsFor(160);
    require(getSkyConfigRequests == 1,
            "Remote link-open lifecycle sends one automatic GetSkyConfig request");
    activateLayouts(&window);
    require(scrollArea->horizontalScrollBar() &&
                scrollArea->horizontalScrollBar()->maximum() == 0 &&
                !scrollArea->horizontalScrollBar()->isVisible(),
            "loaded remote SkyConfig still fits horizontally");
    require(remoteStatus->property("status").toString() == QStringLiteral("success"),
            "loaded remote SkyConfig is marked successful while the sky link is connected");
    require(epsilonPortCombo->currentText() == QStringLiteral("/dev/ttyEPSILON"),
            "remote SkyConfig updates the same EPSILON port combo");
    require(epsilonPortCombo->findText(QStringLiteral("COM7")) < 0,
            "remote sky device port options do not reuse the ground PC serial list");
    require(epsilonPortCombo->findText(QStringLiteral("手动添加")) >= 0 ||
                epsilonPortCombo->findText(QStringLiteral("Add Port")) >= 0,
            "remote sky port combo keeps manual entry available");
    require(rd105SlaveSpin->value() == 9,
            "remote-only RD105 slave address is loaded into the unified page");
    require(pressureSourceCombo->currentData().toString() == QStringLiteral("ptb210") &&
                humiditySourceCombo->currentData().toString() == QStringLiteral("hmp3"),
            "remote SkyConfig restores pressure and humidity source selections");
    require(ai8EnabledCheck->isChecked() &&
                ai8PortCombo->currentText() == QStringLiteral("/dev/ttyAI8") &&
                ai8BaudCombo->currentText() == QStringLiteral("19200") &&
                ai8RateCombo->currentText() == QStringLiteral("5"),
            "remote SkyConfig restores AI-8 enabled, port, baud, and polling rate");
    require(tcpWaveHostEdit->text() == QStringLiteral("10.0.0.2") &&
                tcpWavePortEdit->text() == QStringLiteral("8899"),
            "remote SkyConfig mirrors Wave TCP endpoint into the home TCP wave card");

    tcpWaveHostEdit->setText(QStringLiteral("10.0.0.9"));
    tcpWavePortEdit->setText(QStringLiteral("9901"));
    tcpWaveConnectButton->click();
    require(VaporViewTest::processEventsUntil(1500, [&]() {
                return skyConfigApplyResultSent &&
                    remoteStatus->property("status").toString() == QStringLiteral("success");
            }),
            "remote TCP wave endpoint edit clears SetSkyConfig pending state");
    QString endpointError;
    const QJsonObject endpointJson = window.testRemoteSkyConfigFromDeviceConfigUi(&endpointError);
    const QJsonObject endpointWaveJson = endpointJson.value(QStringLiteral("wave_tcp")).toObject();
    require(endpointError.isEmpty() &&
                endpointWaveJson.value(QStringLiteral("enabled")).toBool(false) &&
                endpointWaveJson.value(QStringLiteral("host")).toString() == QStringLiteral("10.0.0.9") &&
                endpointWaveJson.value(QStringLiteral("port")).toInt() == 9901,
            "remote TCP wave connect applies the home card endpoint to SkyConfig");

    const int manualIndex = std::max(epsilonPortCombo->findText(QStringLiteral("手动添加")),
                                     epsilonPortCombo->findText(QStringLiteral("Add Port")));
    require(manualIndex >= 0, "manual remote sky port option exists");
    epsilonPortCombo->setCurrentIndex(manualIndex);
    VaporViewTest::processEventsFor(40);
    require(epsilonPortCombo->isEditable() && epsilonPortCombo->lineEdit(),
            "remote sky manual port entry enables inline editing");
    epsilonPortCombo->lineEdit()->setText(QStringLiteral("/dev/ttyEPSILON_ALT"));
    QKeyEvent acceptManualPort(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(epsilonPortCombo->lineEdit(), &acceptManualPort);
    VaporViewTest::processEventsFor(80);
    epsilonBaudCombo->setCurrentText(QStringLiteral("115200"));
    rd105SlaveSpin->setValue(17);
    selectComboData(pressureSourceCombo, QStringLiteral("bmp390"),
                    "remote pressure source can be edited to BMP390");
    selectComboData(humiditySourceCombo, QStringLiteral("sht45"),
                    "remote humidity source can be edited to SHT45");
    selectComboText(ai8PortCombo, QStringLiteral("/dev/ttyAI8_ALT"),
                    "remote AI-8 port can be edited independently");
    ai8BaudCombo->setCurrentText(QStringLiteral("115200"));
    ai8RateCombo->setCurrentText(QStringLiteral("12"));
    VaporViewTest::processEventsFor(120);
    require(remoteStatus->property("status").toString() == QStringLiteral("dirty"),
            "editing remote SkyConfig fields uses the dirty status vocabulary");

    QString remoteError;
    const QJsonObject uiJson = window.testRemoteSkyConfigFromDeviceConfigUi(&remoteError);
    require(remoteError.isEmpty(), "remote SkyConfig can be serialized from unified UI");
    require(uiJson.value(QStringLiteral("epsilon")).toObject().value(QStringLiteral("port")).toString() ==
                QStringLiteral("/dev/ttyEPSILON_ALT"),
            "remote edited port is serialized into SetSkyConfig JSON");
    require(uiJson.value(QStringLiteral("epsilon")).toObject().value(QStringLiteral("baud")).toInt() == 115200,
            "remote edited baud is serialized into SetSkyConfig JSON");
    require(!uiJson.value(QStringLiteral("epsilon")).toObject().contains(QStringLiteral("frequency_hz")),
            "remote EPSILON SkyConfig omits the legacy single frequency while packet rates are edited in Combination Navigation");
    require(uiJson.value(QStringLiteral("temperature_controller")).toObject().value(QStringLiteral("slave_address")).toInt() == 17,
            "remote-only RD105 field is serialized into SkyConfig JSON");
    require(uiJson.value(QStringLiteral("ptb")).toObject().value(QStringLiteral("source")).toString() ==
                QStringLiteral("bmp390"),
            "remote edited pressure source is serialized into SkyConfig JSON");
    require(uiJson.value(QStringLiteral("hmp")).toObject().value(QStringLiteral("source")).toString() ==
                QStringLiteral("sht45"),
            "remote edited humidity source is serialized into SkyConfig JSON");
    const QJsonObject ai8Json =
        uiJson.value(QStringLiteral("ai8_temperature_controller")).toObject();
    require(ai8Json.value(QStringLiteral("enabled")).toBool(false) &&
                ai8Json.value(QStringLiteral("port")).toString() == QStringLiteral("/dev/ttyAI8_ALT") &&
                ai8Json.value(QStringLiteral("baud")).toInt() == 115200 &&
                std::abs(ai8Json.value(QStringLiteral("frequency_hz")).toDouble() - 12.0) < 0.01,
            "remote edited AI-8 fields are serialized into SkyConfig JSON");
    require(uiJson.contains(QStringLiteral("wave_tcp")) && uiJson.contains(QStringLiteral("telemetry")),
            "remote-only Wave TCP and telemetry sections are preserved");
    require(!uiJson.contains(QStringLiteral("ai8")) &&
                !uiJson.contains(QStringLiteral("pressure_source")) &&
                !uiJson.contains(QStringLiteral("humidity_source")),
            "remote SkyConfig uses canonical nested source fields and AI-8 section");

    rawModeButton->click();
    VaporViewTest::processEventsFor(120);
    require(rawJsonEdit->isVisible() &&
                rawJsonEdit->toPlainText().contains(QStringLiteral("/dev/ttyEPSILON_ALT")) &&
                rawJsonEdit->toPlainText().contains(QStringLiteral("\"wave_tcp\"")),
            "remote raw JSON mode is hosted in the unified Device Config page");
    window.testInjectRemoteSkyApplyResult(QJsonObject{
        {QStringLiteral("success"), false},
        {QStringLiteral("error"), QStringLiteral("mock reject")}
    });
    VaporViewTest::processEventsFor(80);
    require(window.testRemoteSkyConfigStatusText().contains(QStringLiteral("mock reject")),
            "remote apply failure is displayed inline");
    require(remoteStatus->property("status").toString() == QStringLiteral("error"),
            "remote apply failure uses the error status vocabulary");
    require(remoteApplyButton->isEnabled() &&
                epsilonPortCombo->isEnabled() &&
                ai8PortCombo->isEnabled(),
            "remote apply failure restores editing controls for dirty retry");
    QString afterFailureError;
    const QJsonObject afterFailureJson = window.testRemoteSkyConfigFromDeviceConfigUi(&afterFailureError);
    require(afterFailureError.isEmpty() &&
                afterFailureJson.value(QStringLiteral("epsilon")).toObject().value(QStringLiteral("port")).toString() ==
                    QStringLiteral("/dev/ttyEPSILON_ALT"),
            "remote apply failure preserves the user's edited remote values");
    rawModeButton->click();
    VaporViewTest::processEventsFor(120);

    remoteController->close();
    require(VaporViewTest::processEventsUntil(1500, [&]() {
                return !remoteController->isOpen() &&
                    remoteStatus->property("status").toString() == QStringLiteral("disabled") &&
                    !epsilonPortCombo->isEnabled() &&
                    !ai8PortCombo->isEnabled();
            }),
            "remote link close keeps loaded values visible but makes remote fields non-writable");

    dataSourceModeCombo->setCurrentIndex(0);
    VaporViewTest::processEventsFor(160);
    require(epsilonPortCombo->currentText() == QStringLiteral("COM7"),
            "switching back to Local restores the local EPSILON serial port");
    require(pressureSourceCombo->isVisible() && humiditySourceCombo->isVisible(),
            "local source selectors reappear after returning from remote mode");
    require(pressureSourceCombo->currentData().toString() == QStringLiteral("bmp390") &&
                humiditySourceCombo->currentData().toString() == QStringLiteral("sht45"),
            "switching back to Local restores local pressure and humidity sources");

    window.close();
    VaporView::setSettingsWritesSuspended(false);
    std::cout << "device configuration layout test passed\n";
    return 0;
}
