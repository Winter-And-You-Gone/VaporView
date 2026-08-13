#include "ground/main/MainWindow.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "test_ui_helpers.h"

#include <QApplication>
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

    window.close();
    VaporView::setSettingsWritesSuspended(false);
    std::cout << "device configuration layout test passed\n";
    return 0;
}
