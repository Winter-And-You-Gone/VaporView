#include "ground/main/MainWindow.h"
#include "ground/main/GroundMainWindowSupport.h"
#include "ground/devices/RemoteSkyController.h"
#include "ground/navigation/CombinationNavigationPage.h"
#include "ground/widgets/SegmentedSwitchButton.h"
#include "shared/config/ApplicationConfig.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "SkyConfig.h"
#include "TelemetryCodec.h"
#include "test_ui_helpers.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRect>
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

QPushButton *findSidebarNav(MainWindow& window, const QStringList& accessibleNames)
{
    for (QPushButton *button : window.findChildren<QPushButton *>())
    {
        if (!button)
        {
            continue;
        }
        if (accessibleNames.contains(button->accessibleName()))
        {
            return button;
        }
    }
    return nullptr;
}

QPushButton *findDeviceConfigNav(MainWindow& window)
{
    return findSidebarNav(window, {
        QStringLiteral("设备配置"),
        QStringLiteral("Device"),
    });
}

QPushButton *findCombinationNavigationNav(MainWindow& window)
{
    return findSidebarNav(window, {
        QStringLiteral("组合导航"),
        QStringLiteral("Combination Navigation"),
    });
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
    historySettings.setValue(QStringLiteral("ports"),
                             QStringList{QStringLiteral("COM7"),
                                         QStringLiteral("COM11"),
                                         QStringLiteral("COM12"),
                                         QStringLiteral("COM42")});
    historySettings.sync();
    QSettings legacySettings = VaporView::applicationConfigSettings();
    legacySettings.beginGroup(QStringLiteral("MainWindow"));
    legacySettings.setValue(QStringLiteral("serial/epsilon_port"), QStringLiteral("COM7"));
    legacySettings.setValue(QStringLiteral("serial/epsilon_baud"), QStringLiteral("460800"));
    legacySettings.setValue(QStringLiteral("serial/ptb_port"), QStringLiteral("COM11"));
    legacySettings.setValue(QStringLiteral("serial/ptb_baud"), QStringLiteral("9600"));
    legacySettings.setValue(QStringLiteral("serial/hmp_port"), QStringLiteral("COM12"));
    legacySettings.setValue(QStringLiteral("serial/hmp_baud"), QStringLiteral("19200"));
    legacySettings.setValue(QStringLiteral("sensor/pressure_source"), QStringLiteral("ptb210"));
    legacySettings.setValue(QStringLiteral("sensor/humidity_source"), QStringLiteral("hmp3"));
    legacySettings.endGroup();
    legacySettings.sync();
    VaporView::setSettingsWritesSuspended(true);

    MainWindow window;
    window.resize(1280, 760);
    window.show();
    require(VaporViewTest::waitForWindowExposed(&window),
            "main window becomes exposed for device configuration layout test");

    QPushButton *deviceConfigNav = findDeviceConfigNav(window);
    require(deviceConfigNav != nullptr, "device configuration nav button exists");
    QPushButton *combinationNavigationNav = findCombinationNavigationNav(window);
    require(combinationNavigationNav != nullptr,
            "combination navigation nav button exists");
    deviceConfigNav->click();
    VaporViewTest::processEventsFor(180);
    activateLayouts(&window);

    QWidget *deviceConfigPage = window.findChild<QWidget *>(QStringLiteral("deviceConfigPage"));
    require(deviceConfigPage != nullptr && deviceConfigPage->isVisible(),
            "device configuration page is visible");
    QWidget *homeOverviewBody =
        window.findChild<QWidget *>(QStringLiteral("homeOverviewDeviceBody"));
    require(homeOverviewBody != nullptr,
            "home device overview body exists");
    require(window.findChild<QWidget *>(QStringLiteral("homeSkyTelemetryRow")) == nullptr &&
                window.findChild<QComboBox *>(QStringLiteral("skyTelemetryPortCombo")) == nullptr &&
                homeOverviewBody->findChildren<QComboBox *>().isEmpty() &&
                homeOverviewBody->findChildren<QLineEdit *>().isEmpty() &&
                homeOverviewBody->findChildren<QSpinBox *>().isEmpty(),
            "home device overview does not create Sky Link configuration widgets");
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

    const QStringList formattedDeviceLabels{
        QStringLiteral("EPSILON2-D4G 组合导航"),
        QStringLiteral("PTB210 气压计"),
        QStringLiteral("HMP3 温湿度计"),
        QStringLiteral("TFA1500-L 激光测距"),
        QStringLiteral("RD105 温控器"),
        QStringLiteral("AI-8288D92J0 八路温控器"),
    };
    for (const QString& labelText : formattedDeviceLabels)
    {
        requireLabelFits(findExactLabel(serialCard, labelText),
                         "serial configuration uses model-plus-device labels without clipping");
    }
    int widestDeviceLabelText = 0;
    int deviceLabelWidth = -1;
    for (const QString& labelText : formattedDeviceLabels)
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
            "serial configuration device column uses the widest formatted label without extra width");
    const QStringList serialColumnHeaders{
        QStringLiteral("设备"),
        QStringLiteral("串口/主机"),
        QStringLiteral("波特率/端口"),
        QStringLiteral("频率/轮询"),
        QStringLiteral("启用"),
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
                             findExactLabel(serialCard, QStringLiteral("EPSILON2-D4G 组合导航")),
                             "device column header sits above the device names");
    requireHeaderAboveWidget(serialCard,
                             findExactLabel(serialCard, QStringLiteral("串口/主机")),
                             serialCard->findChild<QWidget *>(QStringLiteral("deviceEpsilonPortCombo")),
                             "serial-port column header sits above port selectors");
    requireHeaderAboveWidget(serialCard,
                             findExactLabel(serialCard, QStringLiteral("波特率/端口")),
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
    QToolButton *temperatureActionButton = nullptr;
    QToolButton *tcpWaveActionButton = nullptr;
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
        if (button->property("deviceConfigRemoteDevice").toInt() ==
            static_cast<int>(VaporView::SkyDeviceId::TemperatureController))
        {
            temperatureActionButton = button;
        }
        if (button->property("deviceConfigRemoteDevice").toInt() ==
            static_cast<int>(VaporView::SkyDeviceId::WaveTcp))
        {
            tcpWaveActionButton = button;
        }
        ++centeredActionCount;
    }
    require(centeredActionCount == 7,
            "serial configuration exposes all seven centered link-action icons");
    require(temperatureActionButton != nullptr,
            "serial configuration exposes the RD105 link-action icon");
    require(tcpWaveActionButton != nullptr,
            "serial configuration exposes the TCP waveform link-action icon");
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
    requireLabelFits(findExactLabel(serialCard, QStringLiteral("PTB210 气压计")),
                     "pressure device row restores the model-plus-device label");
    requireLabelFits(findExactLabel(serialCard, QStringLiteral("HMP3 温湿度计")),
                     "humidity device row restores the model-plus-device label");
    QLabel *tcpWaveDeviceLabel = findExactLabel(serialCard, QStringLiteral("TCP 波形"));
    requireLabelFits(tcpWaveDeviceLabel,
                     "TCP waveform row label fits in the device configuration card");
    auto *deviceTcpWaveHostEdit =
        serialCard->findChild<QLineEdit *>(QStringLiteral("deviceTcpWaveHostEdit"));
    auto *deviceTcpWavePortSpin =
        serialCard->findChild<QSpinBox *>(QStringLiteral("deviceTcpWavePortSpin"));
    require(deviceTcpWaveHostEdit && deviceTcpWavePortSpin &&
                deviceTcpWaveHostEdit->isVisible() && deviceTcpWavePortSpin->isVisible(),
            "TCP waveform device row exposes visible host and port editors");
    auto *tcpWaveHostEdit = window.findChild<QLineEdit *>(QStringLiteral("tcpWaveHostEdit"));
    auto *tcpWavePortEdit = window.findChild<QLineEdit *>(QStringLiteral("tcpWavePortEdit"));
    require(tcpWaveHostEdit && tcpWavePortEdit,
            "home TCP wave source controls exist for endpoint synchronization");
    deviceTcpWaveHostEdit->setText(QStringLiteral("127.0.0.9"));
    deviceTcpWaveHostEdit->setFocus();
    require(QMetaObject::invokeMethod(deviceTcpWaveHostEdit,
                                      "editingFinished",
                                      Qt::DirectConnection),
            "device TCP host editor accepts the endpoint commit signal");
    deviceTcpWavePortSpin->setValue(9901);
    VaporViewTest::processEventsFor(60);
    require(tcpWaveHostEdit->text() == QStringLiteral("127.0.0.9") &&
                tcpWavePortEdit->text() == QStringLiteral("9901"),
            "local device-row endpoint edits synchronize to the home TCP wave card");

    auto *dataSourceModeSwitch =
        serialCard->findChild<QPushButton *>(QStringLiteral("deviceConfigSourceModeOverviewSwitch"));
    auto *dataSourceModeSegmentedSwitch =
        qobject_cast<VaporView::Ground::Widgets::SegmentedSwitchButton *>(dataSourceModeSwitch);
    auto *epsilonPortCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceEpsilonPortCombo"));
    auto *epsilonBaudCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceEpsilonBaudCombo"));
    auto *epsilonRateCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceEpsilonRateCombo"));
    auto *pressurePortCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("devicePressurePortCombo"));
    auto *pressureBaudCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("devicePressureBaudCombo"));
    auto *humidityPortCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceHumidityPortCombo"));
    auto *humidityBaudCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceHumidityBaudCombo"));
    auto *ai8DeviceLabel = findExactLabel(serialCard, QStringLiteral("AI-8288D92J0 八路温控器"));
    require(tcpWaveDeviceLabel && ai8DeviceLabel &&
                tcpWaveDeviceLabel->mapTo(serialCard, QPoint(0, 0)).y() >
                    ai8DeviceLabel->mapTo(serialCard, QPoint(0, 0)).y(),
            "TCP waveform row is placed below the AI-8288 row");
    auto *remoteCard =
        deviceConfigPage->findChild<QGroupBox *>(QStringLiteral("deviceRemoteSkyConfigCard"));
    auto *remoteStatus =
        deviceConfigPage->findChild<QLabel *>(QStringLiteral("deviceRemoteSkyConfigStatus"));
    auto *remoteReadButton =
        deviceConfigPage->findChild<QPushButton *>(QStringLiteral("deviceRemoteSkyReadButton"));
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
    auto *temperatureEnabledCheck =
        serialCard->findChild<QCheckBox *>(QStringLiteral("deviceRemoteTemperatureEnabledCheck"));
    auto *temperaturePortCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceTemperaturePortCombo"));
    auto *ai8PortCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceAi8TemperaturePortCombo"));
    auto *ai8BaudCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceAi8TemperatureBaudCombo"));
    auto *ai8RateCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceAi8TemperatureRateCombo"));
    auto *skyTelemetryBaudCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceSkyTelemetryBaudCombo"));
    require(dataSourceModeSwitch && dataSourceModeSegmentedSwitch &&
                epsilonPortCombo && epsilonBaudCombo && epsilonRateCombo &&
                pressurePortCombo && pressureBaudCombo &&
                humidityPortCombo && humidityBaudCombo &&
                epsilonPacketRatesButton,
            "shared device config controls exist for target switching");
    if (epsilonPortCombo->currentText() != QStringLiteral("COM7") ||
        epsilonBaudCombo->currentText() != QStringLiteral("460800"))
    {
        QSettings diagnosticSettings = VaporView::applicationConfigSettings();
        diagnosticSettings.beginGroup(QStringLiteral("MainWindow"));
        std::cerr << "Legacy load mismatch: uiPort="
                  << epsilonPortCombo->currentText().toStdString()
                  << " uiBaud=" << epsilonBaudCombo->currentText().toStdString()
                  << " savedPort="
                  << diagnosticSettings.value(QStringLiteral("serial/epsilon_port")).toString().toStdString()
                  << " savedBaud="
                  << diagnosticSettings.value(QStringLiteral("serial/epsilon_baud")).toString().toStdString()
                  << " file=" << diagnosticSettings.fileName().toStdString() << '\n';
    }
    require(epsilonPortCombo->currentText() == QStringLiteral("COM7") &&
                epsilonBaudCombo->currentText() == QStringLiteral("460800"),
            "legacy local serial settings load directly into the visible device page");
    QJsonObject localConfig = window.testLocalDeviceConfigSnapshot();
    require(localConfig.value(QStringLiteral("epsilon")).toObject().value(QStringLiteral("port")).toString() ==
                QStringLiteral("COM7") &&
                localConfig.value(QStringLiteral("epsilon")).toObject().value(QStringLiteral("baud")).toString() ==
                QStringLiteral("460800"),
            "legacy settings populate the non-UI local-device model");

    epsilonBaudCombo->setCurrentText(QStringLiteral("123457"));
    VaporViewTest::processEventsFor(40);
    localConfig = window.testLocalDeviceConfigSnapshot();
    require(epsilonBaudCombo->currentText() == QStringLiteral("123457") &&
                localConfig.value(QStringLiteral("epsilon")).toObject().value(QStringLiteral("baud")).toString() ==
                    QStringLiteral("123457"),
            "visible EPSILON custom baud edit updates the local-device model");

    window.testApplyLocalPortDetection(QStringLiteral("epsilon"),
                                       QStringLiteral("COM42"),
                                       QStringLiteral("256000"));
    VaporViewTest::processEventsFor(40);
    localConfig = window.testLocalDeviceConfigSnapshot();
    if (epsilonPortCombo->currentText() != QStringLiteral("COM42") ||
        epsilonBaudCombo->currentText() != QStringLiteral("256000") ||
        localConfig.value(QStringLiteral("epsilon")).toObject().value(QStringLiteral("port")).toString() !=
            QStringLiteral("COM42") ||
        localConfig.value(QStringLiteral("epsilon")).toObject().value(QStringLiteral("baud")).toString() !=
            QStringLiteral("256000"))
    {
        const QJsonObject epsilonConfig = localConfig.value(QStringLiteral("epsilon")).toObject();
        std::cerr << "Detection apply mismatch: uiPort="
                  << epsilonPortCombo->currentText().toStdString()
                  << " uiBaud=" << epsilonBaudCombo->currentText().toStdString()
                  << " modelPort=" << epsilonConfig.value(QStringLiteral("port")).toString().toStdString()
                  << " modelBaud=" << epsilonConfig.value(QStringLiteral("baud")).toString().toStdString()
                  << '\n';
    }
    require(epsilonPortCombo->currentText() == QStringLiteral("COM42") &&
                epsilonBaudCombo->currentText() == QStringLiteral("256000") &&
                localConfig.value(QStringLiteral("epsilon")).toObject().value(QStringLiteral("port")).toString() ==
                    QStringLiteral("COM42") &&
                localConfig.value(QStringLiteral("epsilon")).toObject().value(QStringLiteral("baud")).toString() ==
                    QStringLiteral("256000"),
            "auto-detect custom baud updates the model and refreshes the visible device page");
    selectComboText(epsilonPortCombo, QStringLiteral("COM7"),
                    "EPSILON port restores after the auto-detect model test");
    selectComboText(epsilonBaudCombo, QStringLiteral("460800"),
                    "EPSILON baud restores after the auto-detect model test");

    VaporView::setSettingsWritesSuspended(false);
    selectComboData(pressureSourceCombo, QStringLiteral("ptb210"),
                    "pressure source switches to PTB210 for profile test");
    selectComboText(pressurePortCombo, QStringLiteral("COM11"),
                    "PTB210 profile accepts its own port");
    pressureBaudCombo->setCurrentText(QStringLiteral("76800"));
    selectComboData(pressureSourceCombo, QStringLiteral("bmp390"),
                    "pressure source switches to BMP390 for profile test");
    selectComboText(pressurePortCombo, QStringLiteral("COM12"),
                    "BMP390 profile accepts its own port");
    pressureBaudCombo->setCurrentText(QStringLiteral("256000"));
    selectComboData(pressureSourceCombo, QStringLiteral("ptb210"),
                    "pressure source returns to PTB210");
    require(pressurePortCombo->currentText() == QStringLiteral("COM11") &&
                pressureBaudCombo->currentText() == QStringLiteral("76800"),
            "PTB source A-B-A restores its source-specific port and baud");

    selectComboData(humiditySourceCombo, QStringLiteral("hmp3"),
                    "humidity source switches to HMP3 for profile test");
    selectComboText(humidityPortCombo, QStringLiteral("COM12"),
                    "HMP3 profile accepts its own port");
    humidityBaudCombo->setCurrentText(QStringLiteral("128000"));
    selectComboData(humiditySourceCombo, QStringLiteral("sht45"),
                    "humidity source switches to SHT45 for profile test");
    selectComboText(humidityPortCombo, QStringLiteral("COM7"),
                    "SHT45 profile accepts its own port");
    humidityBaudCombo->setCurrentText(QStringLiteral("1000000"));
    selectComboData(humiditySourceCombo, QStringLiteral("hmp3"),
                    "humidity source returns to HMP3");
    require(humidityPortCombo->currentText() == QStringLiteral("COM12") &&
                humidityBaudCombo->currentText() == QStringLiteral("128000"),
            "HMP source A-B-A restores its source-specific port and baud");
    QSettings profileSettings = VaporView::applicationConfigSettings();
    profileSettings.beginGroup(QStringLiteral("MainWindow"));
    require(profileSettings.value(QStringLiteral("serial/ptb210_port")).toString() == QStringLiteral("COM11") &&
                profileSettings.value(QStringLiteral("serial/bmp390_port")).toString() == QStringLiteral("COM12") &&
                profileSettings.value(QStringLiteral("serial/hmp3_port")).toString() == QStringLiteral("COM12") &&
                profileSettings.value(QStringLiteral("serial/sht45_port")).toString() == QStringLiteral("COM7") &&
                profileSettings.value(QStringLiteral("serial/ptb210_baud")).toString() == QStringLiteral("76800") &&
                profileSettings.value(QStringLiteral("serial/bmp390_baud")).toString() == QStringLiteral("256000") &&
                profileSettings.value(QStringLiteral("serial/hmp3_baud")).toString() == QStringLiteral("128000") &&
                profileSettings.value(QStringLiteral("serial/sht45_baud")).toString() == QStringLiteral("1000000"),
            "source-specific pressure and humidity ports and custom bauds persist beside legacy settings");
    profileSettings.endGroup();
    VaporView::setSettingsWritesSuspended(true);
    require(dataSourceModeSwitch->property("segmentedSwitchControl").toBool() &&
                dataSourceModeSwitch->text().contains(QStringLiteral("数据源")) &&
                dataSourceModeSwitch->focusPolicy() == Qt::TabFocus,
            "device configuration target switching reuses the home segmented source switch");
    const QList<QComboBox *> deviceConnectionBaudCombos = {
        epsilonBaudCombo,
        pressureBaudCombo,
        humidityBaudCombo,
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceLidarBaudCombo")),
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceTemperatureBaudCombo")),
        ai8BaudCombo,
        skyTelemetryBaudCombo,
    };
    for (QComboBox *combo : deviceConnectionBaudCombos)
    {
        require(combo &&
                    combo->isEditable() &&
                    combo->property("_vv_serial_baud_rate_combo").toBool() &&
                    combo->height() == VaporView::Ground::MainSupport::kMainPageInputHeight &&
                    combo->minimumHeight() == VaporView::Ground::MainSupport::kMainPageInputHeight &&
                    combo->maximumHeight() == VaporView::Ground::MainSupport::kMainPageInputHeight &&
                    combo->width() == epsilonBaudCombo->width() &&
                    combo->minimumWidth() == epsilonBaudCombo->minimumWidth() &&
                    combo->maximumWidth() == epsilonBaudCombo->maximumWidth() &&
                    combo->maxVisibleItems() == 15 &&
                    combo->focusPolicy() == epsilonBaudCombo->focusPolicy(),
                "device connection baud combos share the fixed size, focus, and popup item limit");
        VaporViewTest::requireComboPopupStyled(
            combo,
                "device connection baud combo uses the shared native popup style",
            require);
    }
    const QStringList expectedSkyTelemetryBauds = {
        QStringLiteral("9600"),
        QStringLiteral("19200"),
        QStringLiteral("38400"),
        QStringLiteral("57600"),
        QStringLiteral("115200"),
        QStringLiteral("230400"),
        QStringLiteral("460800"),
        QStringLiteral("500000"),
        QStringLiteral("921600"),
    };
    require(skyTelemetryBaudCombo->count() == expectedSkyTelemetryBauds.size() &&
                skyTelemetryBaudCombo->currentText() == QStringLiteral("921600"),
            "device Sky Link baud keeps its nine original choices and 921600 default");
    for (const QString& baud : expectedSkyTelemetryBauds)
    {
        require(skyTelemetryBaudCombo->findText(baud) >= 0,
                "device Sky Link baud keeps every original choice");
    }
    const QList<QComboBox *> genericHostBaudCombos = {
        epsilonBaudCombo,
        pressureBaudCombo,
        humidityBaudCombo,
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceLidarBaudCombo")),
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceTemperatureBaudCombo")),
        skyTelemetryBaudCombo,
    };
    for (QComboBox *combo : genericHostBaudCombos)
    {
        require(combo && combo->count() == expectedSkyTelemetryBauds.size(),
                "generic host serial baud combo keeps the shared preset set");
        for (const QString& baud : expectedSkyTelemetryBauds)
        {
            require(combo->findText(baud) >= 0,
                    "generic host serial baud combo keeps every original preset");
        }
    }
    const QStringList expectedAi8HostBauds = {
        QStringLiteral("4800"),
        QStringLiteral("9600"),
        QStringLiteral("19200"),
        QStringLiteral("38400"),
        QStringLiteral("57600"),
        QStringLiteral("115200"),
    };
    require(ai8BaudCombo->count() == expectedAi8HostBauds.size(),
            "AI-8 host serial combo keeps its original preset set");
    for (const QString& baud : expectedAi8HostBauds)
    {
        require(ai8BaudCombo->findText(baud) >= 0,
                "AI-8 host serial combo keeps every original preset");
    }
    auto *ai8ParameterBaudCombo = window.findChild<QComboBox *>(QStringLiteral("ai8BaudCombo"));
    require(ai8ParameterBaudCombo &&
                !ai8ParameterBaudCombo->isEditable() &&
                ai8ParameterBaudCombo->property("usesSingleLevelPopupMenu").toBool() &&
                ai8ParameterBaudCombo->count() == expectedAi8HostBauds.size() &&
                ai8ParameterBaudCombo->currentData().toInt() == 19200,
            "AI-8 parameter bAud keeps its separate fixed-choice popup implementation");
    for (const QString& baud : expectedAi8HostBauds)
    {
        require(ai8ParameterBaudCombo->findText(baud) >= 0,
                "AI-8 parameter bAud keeps every documented fixed value");
    }
    ai8BaudCombo->setCurrentText(QStringLiteral("115200"));
    VaporViewTest::processEventsFor(40);
    localConfig = window.testLocalDeviceConfigSnapshot();
    require(localConfig.value(QStringLiteral("ai8")).toObject().value(QStringLiteral("baud")).toString() ==
                    QStringLiteral("115200") &&
                ai8ParameterBaudCombo->currentData().toInt() == 19200,
            "AI-8 host serial baud does not overwrite the fixed device bAud parameter");
    ai8BaudCombo->setCurrentText(QStringLiteral("19200"));
    VaporViewTest::processEventsFor(40);
    auto lineEditForNumericControl = [](QWidget *control) -> QLineEdit * {
        if (auto *combo = qobject_cast<QComboBox *>(control))
        {
            return combo->lineEdit();
        }
        if (auto *spin = qobject_cast<QSpinBox *>(control))
        {
            return spin->findChild<QLineEdit *>(QString(), Qt::FindDirectChildrenOnly);
        }
        return nullptr;
    };
    const QList<QWidget *> numericColumnControls = {
        epsilonBaudCombo,
        serialCard->findChild<QComboBox *>(QStringLiteral("devicePressureBaudCombo")),
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceHumidityBaudCombo")),
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceLidarBaudCombo")),
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceTemperatureBaudCombo")),
        ai8BaudCombo,
        deviceTcpWavePortSpin,
    };
    const int localNumericFontHeight = epsilonBaudCombo->fontMetrics().height();
    const int localNumericTextWidth =
        epsilonBaudCombo->fontMetrics().horizontalAdvance(QStringLiteral("8888"));
    const auto isHorizontallyCentered = [](Qt::Alignment alignment) {
        return (alignment & Qt::AlignHorizontal_Mask) == Qt::AlignHCenter;
    };
    for (QWidget *control : numericColumnControls)
    {
        require(control &&
                    control->fontMetrics().height() == localNumericFontHeight &&
                    control->fontMetrics().horizontalAdvance(QStringLiteral("8888")) ==
                        localNumericTextWidth,
                "local baud/port column controls share one font size");
        if (auto *combo = qobject_cast<QComboBox *>(control))
        {
            require(combo->count() > 0, "local baud combo has selectable values");
            for (int index = 0; index < combo->count(); ++index)
            {
                require(isHorizontallyCentered(static_cast<Qt::Alignment>(
                            combo->itemData(index, Qt::TextAlignmentRole).toUInt())),
                        "local baud combo items are horizontally centered");
            }
        }
        else if (auto *spin = qobject_cast<QSpinBox *>(control))
        {
            QLineEdit *editor = lineEditForNumericControl(control);
            require(isHorizontallyCentered(spin->alignment()) && editor &&
                        isHorizontallyCentered(editor->alignment()),
                    "local TCP port control text is horizontally centered");
        }
    }
    QPushButton *autoDetectButton = nullptr;
    for (QPushButton *button : serialCard->findChildren<QPushButton *>())
    {
        if (button->isVisible() &&
            (button->text().contains(QStringLiteral("自动识别")) ||
             button->text().contains(QStringLiteral("Auto Detect"))))
        {
            autoDetectButton = button;
            break;
        }
    }
    require(autoDetectButton != nullptr,
            "device configuration exposes the title-bar auto-detect button");
    const QRect autoDetectRect(autoDetectButton->mapTo(serialCard, QPoint(0, 0)),
                               autoDetectButton->size());
    const QRect sourceModeSwitchRect(dataSourceModeSwitch->mapTo(serialCard, QPoint(0, 0)),
                                     dataSourceModeSwitch->size());
    require(sourceModeSwitchRect.left() > autoDetectRect.right() &&
                sourceModeSwitchRect.left() - autoDetectRect.right() <= 16,
            "device configuration target switch is left-aligned beside auto-detect");
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
    require(deviceConfigNav->isChecked() && !deviceConfigNav->icon().isNull(),
            "device configuration sidebar button starts selected with an icon");
    const qint64 selectedDeviceConfigIconKey =
        deviceConfigNav->icon().pixmap(deviceConfigNav->iconSize()).cacheKey();
    epsilonPacketRatesButton->click();
    VaporViewTest::processEventsFor(120);
    require(mainPageStackForEpsilonButton->currentWidget() == combinationPageForEpsilonButton &&
                combinationPageForEpsilonButton->currentSection() ==
                    VaporView::Ground::Navigation::CombinationNavigationPage::Section::Epsilon,
            "EPSILON packet-rate button jumps to Combination Navigation EPSILON settings");
    require(!deviceConfigNav->isChecked() &&
                combinationNavigationNav->isChecked() &&
                !deviceConfigNav->icon().isNull() &&
                deviceConfigNav->icon().pixmap(deviceConfigNav->iconSize()).cacheKey() !=
                    selectedDeviceConfigIconKey,
            "EPSILON packet-rate jump refreshes the previous device-config sidebar icon");
    mainPageStackForEpsilonButton->setCurrentWidget(deviceConfigPage);
    VaporViewTest::processEventsFor(80);
    auto *skyTelemetryTransportCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceSkyTelemetryTransportCombo"));
    auto *skyTelemetryPortCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceSkyTelemetryPortCombo"));
    auto *skyTelemetryTcpHostEdit =
        deviceConfigPage->findChild<QLineEdit *>(QStringLiteral("deviceSkyTelemetryTcpHostEdit"));
    auto *skyTelemetryTcpPortSpin =
        deviceConfigPage->findChild<QSpinBox *>(QStringLiteral("deviceSkyTelemetryTcpPortSpin"));
    QWidget *skyTelemetryRow = skyTelemetryTransportCombo ? skyTelemetryTransportCombo->parentWidget() : nullptr;
    require(skyTelemetryRow && skyTelemetryBaudCombo && skyTelemetryPortCombo &&
                skyTelemetryTcpHostEdit && skyTelemetryTcpPortSpin && !skyTelemetryRow->isVisible(),
            "local mode hides sky-ground link editing controls from the unified device table");
    QWidget *serialFormRow =
        epsilonPortCombo && epsilonPortCombo->parentWidget() ? epsilonPortCombo->parentWidget()->parentWidget() : nullptr;
    QLayout *serialCardLayout = serialCard ? serialCard->layout() : nullptr;
    require(serialFormRow && serialCardLayout &&
                serialCardLayout->indexOf(skyTelemetryRow) > serialCardLayout->indexOf(serialFormRow),
            "sky-ground link fields are placed below the device serial configuration rows");
    QLabel *servicesLabel = findSubsectionLabel(remoteCard, QStringLiteral("services"));
    QLabel *syncLabel = findSubsectionLabel(remoteCard, QStringLiteral("sync"));
    QLabel *advancedLabel = findSubsectionLabel(remoteCard, QStringLiteral("advanced"));
    require(remoteCard && remoteStatus && remoteReadButton && remoteApplyButton && remoteSaveButton &&
                rawModeButton && rawJsonEdit && servicesLabel && syncLabel && advancedLabel,
            "remote sky service, sync, and diagnostics controls exist on the unified page");
    require(rd105SlaveSpin == nullptr,
            "remote RD105 address setting is removed from Device Config; use the RD105 temperature page instead");
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
    require(temperatureEnabledCheck && temperatureEnabledCheck->isVisible() &&
                temperatureEnabledCheck->isEnabled() &&
                temperatureEnabledCheck->isChecked(),
            "local mode exposes the same enabled toggle used by remote serial rows");
    require(temperaturePortCombo != nullptr,
            "local mode exposes the RD105 serial port combo");
    selectComboText(temperaturePortCombo, QStringLiteral("COM7"),
                    "local RD105 serial combo can select a retained port before disabling");
    VaporViewTest::processEventsFor(80);
    require(temperatureActionButton->isEnabled(),
            "local RD105 action is enabled before the enabled toggle is cleared");
    const QString originalTemperaturePort = temperaturePortCombo->currentText();
    temperatureEnabledCheck->setChecked(false);
    VaporViewTest::processEventsFor(80);
    require(!temperatureActionButton->isEnabled() &&
                temperaturePortCombo->currentText() == originalTemperaturePort,
            "local enabled toggle disables RD105 connection without clearing the serial selection");
    temperatureEnabledCheck->setChecked(true);
    VaporViewTest::processEventsFor(80);
    require(temperatureActionButton->isEnabled(),
            "local enabled toggle restores the RD105 connection action");

    selectComboText(epsilonPortCombo, QStringLiteral("COM7"),
                    "local EPSILON port can be set before switching targets");
    VaporViewTest::processEventsFor(120);
    require(epsilonPortCombo->currentText() == QStringLiteral("COM7"),
            "local EPSILON port is visible before remote switch");

    dataSourceModeSwitch->click();
    require(dataSourceModeSegmentedSwitch->switchAnimationRunning(),
            "device configuration source switch starts visual feedback before layout stabilization");
    require(serialCard->height() >= serialCard->sizeHint().height() - 1,
            "local-to-remote switch applies the expanded device card geometry before repaint");
    require(ai8DeviceLabel && ai8DeviceLabel->isVisible() &&
                ai8DeviceLabel->mapTo(serialCard, QPoint(0, 0)).y() + ai8DeviceLabel->height() <= serialCard->height(),
            "local-to-remote switch keeps the AI-8288 row inside the device card before repaint");
    require(!remoteCard->isVisible() ||
                remoteCard->geometry().top() >= serialCard->geometry().bottom() + 1,
            "local-to-remote switch positions the remote card below all device rows before repaint");
    VaporViewTest::processEventsFor(16);
    require(ai8DeviceLabel && ai8DeviceLabel->isVisible() &&
                ai8DeviceLabel->mapTo(serialCard, QPoint(0, 0)).y() + ai8DeviceLabel->height() <= serialCard->height(),
            "local-to-remote switch keeps the AI-8288 row inside the device card during the first repaint frame");
    require(!remoteCard->isVisible() ||
                remoteCard->geometry().top() >= serialCard->geometry().bottom() + 1,
            "first repaint frame keeps the remote card below the device rows");
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
    selectComboData(skyTelemetryTransportCombo, QStringLiteral("serial"),
                    "Sky Link transport can switch to Serial");
    VaporViewTest::processEventsFor(60);
    require(skyTelemetryBaudCombo->isVisible(),
            "Sky Link baud remains visible in Serial mode");
    skyTelemetryBaudCombo->setCurrentText(QStringLiteral("1000000"));
    VaporViewTest::processEventsFor(60);
    selectComboText(skyTelemetryPortCombo, QStringLiteral("COM42"),
                    "device Sky Link serial port accepts a retained port");
    VaporViewTest::processEventsFor(60);
    QJsonObject skyLinkConfig = window.testRemoteSkyLinkConfigSnapshot();
    require(skyLinkConfig.value(QStringLiteral("transport")).toString() == QStringLiteral("serial") &&
                skyLinkConfig.value(QStringLiteral("serial_port")).toString() == QStringLiteral("COM42") &&
                skyLinkConfig.value(QStringLiteral("serial_baud")).toInt() == 1000000 &&
                skyTelemetryBaudCombo->count() == expectedSkyTelemetryBauds.size(),
            "device Sky Link custom serial baud updates the non-UI link model without changing presets");
    selectComboData(skyTelemetryTransportCombo, QStringLiteral("tcp"),
                    "Sky Link transport can switch back to TCP");
    VaporViewTest::processEventsFor(60);
    require(!skyTelemetryBaudCombo->isVisible(),
            "Sky Link baud remains hidden in TCP mode");
    skyTelemetryTcpHostEdit->setText(QStringLiteral("10.10.0.8"));
    skyTelemetryTcpPortSpin->setValue(39201);
    VaporViewTest::processEventsFor(60);
    skyLinkConfig = window.testRemoteSkyLinkConfigSnapshot();
    require(skyLinkConfig.value(QStringLiteral("transport")).toString() == QStringLiteral("tcp") &&
                skyLinkConfig.value(QStringLiteral("tcp_host")).toString() == QStringLiteral("10.10.0.8") &&
                skyLinkConfig.value(QStringLiteral("tcp_port")).toInt() == 39201,
            "device Sky Link TCP controls update the non-UI link model");
    selectComboData(skyTelemetryTransportCombo, QStringLiteral("serial"),
                    "Sky Link transport restores Serial after TCP validation");
    VaporViewTest::processEventsFor(60);
    require(skyTelemetryBaudCombo->isVisible() &&
                skyTelemetryBaudCombo->currentText() == QStringLiteral("1000000"),
            "Sky Link preserves the custom serial baud across a TCP transport switch");
    selectComboData(skyTelemetryTransportCombo, QStringLiteral("tcp"),
                    "Sky Link transport returns to TCP before remote connection");
    VaporViewTest::processEventsFor(60);
    require(remoteCard->isVisible(), "remote sky service/config card appears in remote mode");
    QWidget *serialTitleBar =
        serialCard->findChild<QWidget *>(QStringLiteral("sectionTitleBar"), Qt::FindDirectChildrenOnly);
    QWidget *remoteTitleBar =
        remoteCard->findChild<QWidget *>(QStringLiteral("sectionTitleBar"), Qt::FindDirectChildrenOnly);
    require(serialTitleBar && remoteTitleBar,
            "device and remote sky config cards expose direct title bars");
    if (serialTitleBar->height() != remoteTitleBar->height())
    {
        std::cerr << "Title bar geometry: serial height="
                  << serialTitleBar->height()
                  << " remote height=" << remoteTitleBar->height()
                  << " serial top=" << serialTitleBar->geometry().top()
                  << " remote top=" << remoteTitleBar->geometry().top()
                  << " serial card top=" << serialCard->geometry().top()
                  << " remote card top=" << remoteCard->geometry().top()
                  << '\n';
    }
    require(remoteTitleBar->height() == serialTitleBar->height(),
            "remote sky config title bar matches the shared device-card height");
    if (std::abs(remoteTitleBar->geometry().top() - serialTitleBar->geometry().top()) > 1)
    {
        std::cerr << "Title bar top mismatch: serial top="
                  << serialTitleBar->geometry().top()
                  << " remote top=" << remoteTitleBar->geometry().top()
                  << '\n';
    }
    require(std::abs(remoteTitleBar->geometry().top() - serialTitleBar->geometry().top()) <= 1,
            "remote sky config title bar starts flush like the shared device card");
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
    require(tcpWaveDeviceLabel->isVisible() && tcpWaveActionButton->isVisible(),
            "remote mode keeps the TCP waveform row visible in the device table");
    require(!tcpWaveActionButton->isEnabled(),
            "disconnected remote mode disables the TCP waveform device action until telemetry is connected");
    require(!remoteApplyButton->isEnabled() && !remoteSaveButton->isEnabled(),
            "disconnected remote mode disables apply and save operations");
    require(!epsilonPortCombo->isEnabled() &&
                ai8PortCombo && !ai8PortCombo->isEnabled() &&
                !deviceTcpWaveHostEdit->isEnabled() &&
                !deviceTcpWavePortSpin->isEnabled(),
            "remote config fields wait for a loaded SkyConfig while the link is disconnected");
    auto *tcpWaveConnectButton = window.findChild<QPushButton *>(QStringLiteral("compactTcpStartButton"));
    auto *remoteWaveHostEdit =
        deviceConfigPage->findChild<QLineEdit *>(QStringLiteral("deviceRemoteSkyWaveHostEdit"));
    auto *remoteWavePortSpin =
        deviceConfigPage->findChild<QSpinBox *>(QStringLiteral("deviceRemoteSkyWavePortSpin"));
    require(tcpWaveHostEdit != nullptr && tcpWavePortEdit != nullptr &&
                tcpWaveConnectButton != nullptr && remoteWaveHostEdit != nullptr &&
                remoteWavePortSpin != nullptr,
            "home TCP wave source controls are reachable in remote mode");
    require(tcpWaveHostEdit->isEnabled() && tcpWavePortEdit->isEnabled(),
            "remote TCP wave disconnected mode leaves host and port editable");

    VaporView::SkyConfig remoteConfig = VaporView::SkyConfig::defaults();
    remoteConfig.epsilon = {true, QStringLiteral("/dev/ttyEPSILON"), 256000};
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
    skyTelemetryTcpHostEdit->setText(QStringLiteral("127.0.0.1"));
    skyTelemetryTcpPortSpin->setValue(static_cast<int>(fakeSkyServer.serverPort()));
    VaporViewTest::processEventsFor(60);
    skyLinkConfig = window.testRemoteSkyLinkConfigSnapshot();
    require(skyLinkConfig.value(QStringLiteral("tcp_host")).toString() == QStringLiteral("127.0.0.1") &&
                skyLinkConfig.value(QStringLiteral("tcp_port")).toInt() ==
                    static_cast<int>(fakeSkyServer.serverPort()),
            "Sky Link model supplies the fake TCP endpoint before remote connection");
    QTcpSocket *fakeSkySocket = nullptr;
    VaporView::TelemetryCodec inboundCodec;
    VaporView::TelemetryCodec outboundCodec;
    int getSkyConfigRequests = 0;
    int setSkyConfigRequests = 0;
    int saveSkyConfigRequests = 0;
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
                     command.command_id != VaporView::CommandId::SetSkyConfig &&
                     command.command_id != VaporView::CommandId::SaveSkyConfig))
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
                    if (command.command_id == VaporView::CommandId::SetSkyConfig)
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
                    else
                    {
                        ++saveSkyConfigRequests;
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
                else if (command.command_id == VaporView::CommandId::SetSkyConfig)
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
    const bool remoteConnectInvoked = QMetaObject::invokeMethod(
        &window, "onConnectClicked", Qt::DirectConnection);
    const bool remoteOpened = remoteConnectInvoked && remoteController->isOpen();
    if (!remoteOpened)
    {
        VaporView::setSettingsWritesSuspended(true);
    }
    require(remoteOpened,
            "Remote connection opens the fake Sky TCP link from the Sky Link model endpoint");
    const bool remoteConfigAutoLoaded = VaporViewTest::processEventsUntil(3000, [&]() {
                return skyConfigFrameSent &&
                    getSkyConfigRequests == 1 &&
                    remoteStatus->property("status").toString() == QStringLiteral("success") &&
                    epsilonPortCombo->isEnabled() &&
                    epsilonBaudCombo->isEnabled() &&
                    ai8PortCombo->isEnabled() &&
                    ai8BaudCombo->isEnabled() &&
                    ai8RateCombo->isEnabled() &&
                    deviceTcpWaveHostEdit->isEnabled() &&
                    deviceTcpWavePortSpin->isEnabled() &&
                    epsilonPortCombo->currentText() == QStringLiteral("/dev/ttyEPSILON") &&
                    ai8PortCombo->currentText() == QStringLiteral("/dev/ttyAI8") &&
                    deviceTcpWaveHostEdit->text() == QStringLiteral("10.0.0.2") &&
                    deviceTcpWavePortSpin->value() == 8899;
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
    require(epsilonPortCombo->currentText() == QStringLiteral("/dev/ttyEPSILON") &&
                epsilonBaudCombo->currentText() == QStringLiteral("256000"),
            "remote SkyConfig updates the same EPSILON port and custom baud controls");
    for (QComboBox *baudCombo : {epsilonBaudCombo,
                                 pressureBaudCombo,
                                 humidityBaudCombo,
                                 serialCard->findChild<QComboBox *>(QStringLiteral("deviceLidarBaudCombo")),
                                 serialCard->findChild<QComboBox *>(QStringLiteral("deviceTemperatureBaudCombo")),
                                 ai8BaudCombo,
                                 skyTelemetryBaudCombo})
    {
        require(baudCombo && baudCombo->isEditable() &&
                    baudCombo->property("_vv_serial_baud_rate_combo").toBool(),
                "remote Sky host serial baud controls accept custom positive integers");
    }
    require(epsilonPortCombo->findText(QStringLiteral("COM7")) < 0,
            "remote sky device port options do not reuse the ground PC serial list");
    require(epsilonPortCombo->findText(QStringLiteral("手动添加")) >= 0 ||
                epsilonPortCombo->findText(QStringLiteral("Add Port")) >= 0,
            "remote sky port combo keeps manual entry available");
    require(deviceConfigPage->findChild<QSpinBox *>(QStringLiteral("deviceRemoteSkyRd105SlaveSpin")) == nullptr,
            "remote-only RD105 slave address is not duplicated on the unified page");
    require(pressureSourceCombo->currentData().toString() == QStringLiteral("ptb210") &&
                humiditySourceCombo->currentData().toString() == QStringLiteral("hmp3"),
            "remote SkyConfig restores pressure and humidity source selections");
    require(ai8EnabledCheck->isChecked() &&
                ai8PortCombo->currentText() == QStringLiteral("/dev/ttyAI8") &&
                ai8BaudCombo->currentText() == QStringLiteral("19200") &&
                ai8RateCombo->currentText() == QStringLiteral("5"),
            "remote SkyConfig restores AI-8 enabled, port, baud, and polling rate");
    QJsonArray remoteDetections;
    remoteDetections.append(QJsonObject{
        {QStringLiteral("device_key"), QStringLiteral("epsilon")},
        {QStringLiteral("port"), QStringLiteral("/dev/ttyEPSILON_DETECTED")},
        {QStringLiteral("baud"), QStringLiteral("256000")},
    });
    window.testInjectRemoteSerialPortDetectionResult(QJsonObject{
        {QStringLiteral("success"), true},
        {QStringLiteral("canceled"), false},
        {QStringLiteral("detections"), remoteDetections},
    });
    VaporViewTest::processEventsFor(80);
    QString remoteDetectionError;
    const QJsonObject remoteDetectionJson =
        window.testRemoteSkyConfigFromDeviceConfigUi(&remoteDetectionError);
    require(remoteDetectionError.isEmpty() &&
                epsilonPortCombo->currentText() == QStringLiteral("/dev/ttyEPSILON_DETECTED") &&
                epsilonBaudCombo->currentText() == QStringLiteral("256000") &&
                remoteDetectionJson.value(QStringLiteral("epsilon")).toObject().value(QStringLiteral("baud")).toInt() ==
                    256000,
            "remote serial detection preserves a custom host baud in the unified UI and config model");
    for (QWidget *control : numericColumnControls)
    {
        QLineEdit *editor = lineEditForNumericControl(control);
        require(editor &&
                    control->fontMetrics().height() == localNumericFontHeight &&
                    control->fontMetrics().horizontalAdvance(QStringLiteral("8888")) ==
                        localNumericTextWidth &&
                    editor->fontMetrics().height() == localNumericFontHeight &&
                    editor->fontMetrics().horizontalAdvance(QStringLiteral("8888")) ==
                        localNumericTextWidth &&
                    isHorizontallyCentered(editor->alignment()),
                "remote baud/port column editors keep the local font size and alignment");
        if (auto *spin = qobject_cast<QSpinBox *>(control))
        {
            require(isHorizontallyCentered(spin->alignment()),
                    "remote TCP port control keeps the shared alignment");
        }
    }
    require(tcpWaveHostEdit->text() == QStringLiteral("10.0.0.2") &&
                tcpWavePortEdit->text() == QStringLiteral("8899"),
            "remote SkyConfig mirrors Wave TCP endpoint into the home TCP wave card");

    deviceTcpWaveHostEdit->setText(QStringLiteral("10.0.0.3"));
    deviceTcpWaveHostEdit->setFocus();
    require(QMetaObject::invokeMethod(deviceTcpWaveHostEdit,
                                      "editingFinished",
                                      Qt::DirectConnection),
            "remote device TCP host editor accepts the endpoint commit signal");
    deviceTcpWavePortSpin->setValue(9900);
    VaporViewTest::processEventsFor(80);
    require(remoteWaveHostEdit->text() == QStringLiteral("10.0.0.3") &&
                remoteWavePortSpin->value() == 9900 &&
                remoteApplyButton->isEnabled(),
            "remote device-row endpoint edits synchronize to the SkyConfig form");

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
    epsilonBaudCombo->setCurrentText(QStringLiteral("123457"));
    auto *lidarBaudCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceLidarBaudCombo"));
    auto *temperatureBaudCombo =
        serialCard->findChild<QComboBox *>(QStringLiteral("deviceTemperatureBaudCombo"));
    require(lidarBaudCombo && temperatureBaudCombo,
            "remote configuration exposes every remaining host serial baud control");
    pressureBaudCombo->setCurrentText(QStringLiteral("256000"));
    humidityBaudCombo->setCurrentText(QStringLiteral("1000000"));
    lidarBaudCombo->setCurrentText(QStringLiteral("1500000"));
    temperatureBaudCombo->setCurrentText(QStringLiteral("38401"));
    selectComboData(pressureSourceCombo, QStringLiteral("bmp390"),
                    "remote pressure source can be edited to BMP390");
    selectComboData(humiditySourceCombo, QStringLiteral("sht45"),
                    "remote humidity source can be edited to SHT45");
    selectComboText(ai8PortCombo, QStringLiteral("/dev/ttyAI8_ALT"),
                    "remote AI-8 port can be edited independently");
    ai8BaudCombo->setCurrentText(QStringLiteral("234567"));
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
    require(uiJson.value(QStringLiteral("epsilon")).toObject().value(QStringLiteral("baud")).toInt() == 123457,
            "remote custom host baud is serialized into SetSkyConfig JSON");
    require(uiJson.value(QStringLiteral("ptb")).toObject().value(QStringLiteral("baud")).toInt() == 256000 &&
                uiJson.value(QStringLiteral("hmp")).toObject().value(QStringLiteral("baud")).toInt() == 1000000 &&
                uiJson.value(QStringLiteral("lidar")).toObject().value(QStringLiteral("baud")).toInt() == 1500000 &&
                uiJson.value(QStringLiteral("temperature_controller")).toObject().value(QStringLiteral("baud")).toInt() == 38401,
            "every remote host serial baud control serializes its custom positive value");
    require(!uiJson.value(QStringLiteral("epsilon")).toObject().contains(QStringLiteral("frequency_hz")),
            "remote EPSILON SkyConfig omits the legacy single frequency while packet rates are edited in Combination Navigation");
    require(uiJson.value(QStringLiteral("temperature_controller")).toObject().value(QStringLiteral("slave_address")).toInt() == 9,
            "Device Config preserves the loaded RD105 slave address instead of editing it");
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
                ai8Json.value(QStringLiteral("baud")).toInt() == 234567 &&
                std::abs(ai8Json.value(QStringLiteral("frequency_hz")).toDouble() - 12.0) < 0.01,
            "remote edited AI-8 fields are serialized into SkyConfig JSON");
    require(uiJson.contains(QStringLiteral("wave_tcp")) && uiJson.contains(QStringLiteral("telemetry")),
            "remote-only Wave TCP and telemetry sections are preserved");
    require(!uiJson.contains(QStringLiteral("ai8")) &&
                !uiJson.contains(QStringLiteral("pressure_source")) &&
                !uiJson.contains(QStringLiteral("humidity_source")),
            "remote SkyConfig uses canonical nested source fields and AI-8 section");

    const int customBaudApplyRequestCount = setSkyConfigRequests;
    remoteApplyButton->click();
    require(VaporViewTest::processEventsUntil(1500, [&]() {
                return setSkyConfigRequests == customBaudApplyRequestCount + 1 &&
                    remoteConfig.epsilon.baud_rate == 123457 &&
                    remoteConfig.ptb.baud_rate == 256000 &&
                    remoteConfig.hmp.baud_rate == 1000000 &&
                    remoteConfig.lidar.baud_rate == 1500000 &&
                    remoteConfig.temperature_controller.baud_rate == 38401 &&
                    remoteConfig.ai8_temperature_controller.baud_rate == 234567 &&
                    remoteStatus->property("status").toString() == QStringLiteral("success");
            }),
            "remote apply persists every custom host serial baud through the SkyConfig endpoint");

    VaporView::setSettingsWritesSuspended(false);
    const int customBaudReadRequestCount = getSkyConfigRequests;
    remoteReadButton->click();
    const bool customBaudReadbackRestored = VaporViewTest::processEventsUntil(1500, [&]() {
                return getSkyConfigRequests == customBaudReadRequestCount + 1 &&
                    epsilonBaudCombo->currentText() == QStringLiteral("123457") &&
                    pressureBaudCombo->currentText() == QStringLiteral("256000") &&
                    humidityBaudCombo->currentText() == QStringLiteral("1000000") &&
                    lidarBaudCombo->currentText() == QStringLiteral("1500000") &&
                    temperatureBaudCombo->currentText() == QStringLiteral("38401") &&
                    ai8BaudCombo->currentText() == QStringLiteral("234567") &&
                    remoteStatus->property("status").toString() == QStringLiteral("success");
            });
    require(customBaudReadbackRestored,
            "remote SkyConfig readback restores every custom host baud in the unified UI");

    const int customBaudSaveRequestCount = saveSkyConfigRequests;
    remoteSaveButton->click();
    require(VaporViewTest::processEventsUntil(1500, [&]() {
                return saveSkyConfigRequests == customBaudSaveRequestCount + 1 &&
                    remoteStatus->property("status").toString() == QStringLiteral("success");
            }),
            "remote SkyConfig save acknowledges the applied custom host baud values");
    VaporView::setSettingsWritesSuspended(true);

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

    dataSourceModeSwitch->click();
    require(serialCard->height() >= serialCard->sizeHint().height() - 1,
            "remote-to-local switch keeps the compact device card geometry stable before repaint");
    VaporViewTest::processEventsFor(160);
    require(epsilonPortCombo->currentText() == QStringLiteral("COM7"),
            "switching back to Local restores the local EPSILON serial port");
    require(pressureSourceCombo->isVisible() && humiditySourceCombo->isVisible(),
            "local source selectors reappear after returning from remote mode");
    require(pressureSourceCombo->currentData().toString() == QStringLiteral("bmp390") &&
                humiditySourceCombo->currentData().toString() == QStringLiteral("sht45"),
            "switching back to Local restores local pressure and humidity sources");

    VaporView::setSettingsWritesSuspended(false);
    epsilonBaudCombo->setCurrentText(QStringLiteral("256000"));
    VaporViewTest::processEventsFor(80);
    localConfig = window.testLocalDeviceConfigSnapshot();
    require(epsilonBaudCombo->currentText() == QStringLiteral("256000") &&
                localConfig.value(QStringLiteral("epsilon")).toObject().value(QStringLiteral("baud")).toString() ==
                    QStringLiteral("256000"),
            "local custom host baud remains in the model before settings round-trip");
    QSettings customBaudSettings = VaporView::applicationConfigSettings();
    customBaudSettings.beginGroup(QStringLiteral("MainWindow"));
    require(customBaudSettings.value(QStringLiteral("serial/epsilon_baud")).toString() ==
                QStringLiteral("256000"),
            "local custom host baud is persisted before the next window is created");
    customBaudSettings.endGroup();
    customBaudSettings.sync();
    window.close();

    QSettings migratedSkyLinkSettings = VaporView::applicationConfigSettings();
    migratedSkyLinkSettings.beginGroup(QStringLiteral("MainWindow"));
    migratedSkyLinkSettings.setValue(QStringLiteral("source/mode"), QStringLiteral("remote"));
    migratedSkyLinkSettings.setValue(QStringLiteral("telemetry/transport"), QStringLiteral("serial"));
    migratedSkyLinkSettings.setValue(QStringLiteral("telemetry/sky_port"), QStringLiteral("COM11"));
    migratedSkyLinkSettings.setValue(QStringLiteral("telemetry/sky_baud"), QStringLiteral("1000000"));
    migratedSkyLinkSettings.setValue(QStringLiteral("telemetry/tcp_host"), QStringLiteral("172.20.0.8"));
    migratedSkyLinkSettings.setValue(QStringLiteral("telemetry/tcp_port"), 39555);
    migratedSkyLinkSettings.endGroup();
    migratedSkyLinkSettings.sync();

    MainWindow restoredWindow;
    restoredWindow.resize(1280, 760);
    restoredWindow.show();
    require(VaporViewTest::waitForWindowExposed(&restoredWindow),
            "restored Sky Link settings window becomes exposed");
    QPushButton *restoredDeviceConfigNav = findDeviceConfigNav(restoredWindow);
    require(restoredDeviceConfigNav != nullptr,
            "restored Sky Link settings window exposes the device configuration nav button");
    restoredDeviceConfigNav->click();
    VaporViewTest::processEventsFor(120);
    QJsonObject restoredSkyLink = restoredWindow.testRemoteSkyLinkConfigSnapshot();
    QWidget *restoredDeviceConfigPage =
        restoredWindow.findChild<QWidget *>(QStringLiteral("deviceConfigPage"));
    QComboBox *restoredTransport = restoredDeviceConfigPage
        ? restoredDeviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceSkyTelemetryTransportCombo"))
        : nullptr;
    QComboBox *restoredPort = restoredDeviceConfigPage
        ? restoredDeviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceSkyTelemetryPortCombo"))
        : nullptr;
    QComboBox *restoredBaud = restoredDeviceConfigPage
        ? restoredDeviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceSkyTelemetryBaudCombo"))
        : nullptr;
    QLineEdit *restoredHost = restoredDeviceConfigPage
        ? restoredDeviceConfigPage->findChild<QLineEdit *>(QStringLiteral("deviceSkyTelemetryTcpHostEdit"))
        : nullptr;
    QSpinBox *restoredTcpPort = restoredDeviceConfigPage
        ? restoredDeviceConfigPage->findChild<QSpinBox *>(QStringLiteral("deviceSkyTelemetryTcpPortSpin"))
        : nullptr;
    QComboBox *restoredEpsilonBaud = restoredDeviceConfigPage
        ? restoredDeviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceEpsilonBaudCombo"))
        : nullptr;
    QPushButton *restoredSourceModeSwitch = restoredDeviceConfigPage
        ? restoredDeviceConfigPage->findChild<QPushButton *>(QStringLiteral("deviceConfigSourceModeOverviewSwitch"))
        : nullptr;
    require(restoredSkyLink.value(QStringLiteral("transport")).toString() == QStringLiteral("serial") &&
                restoredSkyLink.value(QStringLiteral("serial_port")).toString() == QStringLiteral("COM11") &&
                restoredSkyLink.value(QStringLiteral("serial_baud")).toInt() == 1000000 &&
                restoredSkyLink.value(QStringLiteral("tcp_host")).toString() == QStringLiteral("172.20.0.8") &&
                restoredSkyLink.value(QStringLiteral("tcp_port")).toInt() == 39555 &&
                restoredTransport && restoredTransport->currentData().toString() == QStringLiteral("serial") &&
                restoredPort && restoredPort->currentText() == QStringLiteral("COM11") &&
                restoredBaud && restoredBaud->currentText() == QStringLiteral("1000000") &&
                restoredHost && restoredHost->text() == QStringLiteral("172.20.0.8") &&
                restoredTcpPort && restoredTcpPort->value() == 39555,
            "custom Sky Link settings load into the model and refresh the device configuration UI");
    require(restoredSourceModeSwitch && restoredEpsilonBaud,
            "restored device page exposes source switching and local EPSILON baud controls");
    restoredSourceModeSwitch->click();
    VaporViewTest::processEventsFor(120);
    require(restoredEpsilonBaud->currentText() == QStringLiteral("256000"),
            "local custom host baud survives a settings save and a new window instance");
    restoredWindow.close();

    std::cout << "device configuration layout test passed\n";
    return 0;
}
