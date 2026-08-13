#include "ground/main/MainWindow.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "test_ui_helpers.h"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStringList>
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
            if (text.contains(QStringLiteral("天地通信链路状态")) ||
                text.contains(QStringLiteral("Sky-ground Communication Link Status")))
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

bool cardHasAnyLabel(QFrame *card, const QStringList& candidates)
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
            "link-status summary sections use a horizontal layout");
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

    int previousRight = -1;
    int top = -1;
    for (QFrame *subCard : subCards)
    {
        const QRect subRect(subCard->mapTo(summaryContainer, QPoint(0, 0)), subCard->size());
        require(previousRight < 0 || subRect.left() > previousRight,
                "link-status subcards are arranged left-to-right");
        require(top < 0 || std::abs(subRect.top() - top) <= 2,
                "link-status subcards share a top baseline");
        require(subRect.right() <= summaryContainer->width() &&
                    subRect.bottom() <= summaryContainer->height(),
                "link-status subcards stay inside the summary container");
        previousRight = subRect.right();
        top = subRect.top();
    }

    const QStringList detailedDeviceLabels{
        QStringLiteral("EPSILON2-D4G 组合导航"),
        QStringLiteral("PTB210 气压计"),
        QStringLiteral("HMP3 温湿度计"),
        QStringLiteral("TFA1005-L 激光雷达"),
        QStringLiteral("RD105 激光驱动板温控器"),
        QStringLiteral("AI-8288D92J0 八路温控器"),
    };
    for (const QString& labelText : detailedDeviceLabels)
    {
        requireLabelFits(findExactLabel(serialCard, labelText),
                         "serial configuration uses detailed device labels without clipping");
    }
    int widestDeviceLabelText = 0;
    int deviceLabelWidth = -1;
    for (const QString& labelText : detailedDeviceLabels)
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
            "serial configuration device column uses the widest device label without extra width");
    const QStringList serialColumnHeaders{
        QStringLiteral("设备"),
        QStringLiteral("串口"),
        QStringLiteral("波特率"),
        QStringLiteral("频率/轮询"),
        QStringLiteral("来源"),
        QStringLiteral("链路操作"),
    };
    for (const QString& headerText : serialColumnHeaders)
    {
        requireLabelFits(findExactLabel(serialCard, headerText),
                         "serial configuration column headers are visible and fit");
    }
    requireHeaderAboveWidget(serialCard,
                             findExactLabel(serialCard, QStringLiteral("设备")),
                             findExactLabel(serialCard, QStringLiteral("EPSILON2-D4G 组合导航")),
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
                             serialCard->findChild<QWidget *>(QStringLiteral("deviceAi8TemperatureRateCombo")),
                             "rate column header sits above rate selectors");
    requireHeaderAboveWidget(serialCard,
                             findExactLabel(serialCard, QStringLiteral("来源")),
                             serialCard->findChild<QWidget *>(QStringLiteral("devicePressureSourceCombo")),
                             "source column header sits above source selectors");
    QLabel *actionHeader = findExactLabel(serialCard, QStringLiteral("链路操作"));
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
    requireLabelFits(findExactLabel(serialCard, QStringLiteral("BMP390 气压计")),
                     "pressure device label follows the selected source");
    requireLabelFits(findExactLabel(serialCard, QStringLiteral("SHT45 温湿度计")),
                     "humidity device label follows the selected source");

    window.close();
    VaporView::setSettingsWritesSuspended(false);
    std::cout << "device configuration layout test passed\n";
    return 0;
}
