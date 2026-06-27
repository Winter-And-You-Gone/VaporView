#include "MainWindow.h"

#include <QApplication>
#include <QAction>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <cstdlib>
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

void processEventsFor(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
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
    const QList<QWidget*> children = widget->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : children)
    {
        activateLayouts(child);
    }
}

QRect wrappedTextBounds(const QLabel *label)
{
    const int width = std::max(1, label->width());
    return label->fontMetrics().boundingRect(QRect(0, 0, width, 10000),
                                             Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignVCenter,
                                             label->text());
}

void requireMargins(const QMargins& actual, const QMargins& expected, const char *message)
{
    require(actual.left() == expected.left() &&
                actual.top() == expected.top() &&
                actual.right() == expected.right() &&
                actual.bottom() == expected.bottom(),
            message);
}

}  // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewLayoutTest"));
    app.setApplicationName(QStringLiteral("main_window_layout_test"));

    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        settings.setValue(QStringLiteral("app_sidebar_width"), 56);
        settings.sync();
    }

    MainWindow window;
    window.setWindowTitle(QStringLiteral("VaporView"));
    window.resize(1280, 800);
    window.show();
    processEventsFor(500);

    auto *appSidebar = window.findChild<QWidget *>(QStringLiteral("appSidebar"));
    require(appSidebar != nullptr, "app sidebar exists");
    QPushButton *checkedSidebarButton = nullptr;
    const QList<QPushButton*> sidebarButtons =
        window.findChildren<QPushButton *>(QStringLiteral("appSidebarButton"));
    for (QPushButton *button : sidebarButtons)
    {
        if (button->isChecked())
        {
            checkedSidebarButton = button;
            break;
        }
    }
    require(checkedSidebarButton != nullptr, "checked compact sidebar button exists");
    require(checkedSidebarButton->text().isEmpty(),
            "compact sidebar hides navigation text");
    require(checkedSidebarButton->height() >= 48,
            "compact sidebar button keeps a 48px hit target");
    require(checkedSidebarButton->width() == checkedSidebarButton->height(),
            "compact sidebar option is a strict rounded square");
    require(checkedSidebarButton->iconSize().width() >= 28 &&
                checkedSidebarButton->iconSize().height() >= 28,
            "compact sidebar lucide icon is visually larger");
    const int visualLeftPadding =
        appSidebar->mapTo(&window, QPoint(0, 0)).x() + checkedSidebarButton->x();
    const int visualRightPadding =
        appSidebar->width() - checkedSidebarButton->x() - checkedSidebarButton->width();
    require(std::abs(visualLeftPadding - visualRightPadding) <= 1,
            "compact sidebar button has balanced visible left and right padding");

    auto *homeOverviewSplitter = window.findChild<QSplitter *>(QStringLiteral("homeOverviewSplitter"));
    require(homeOverviewSplitter != nullptr, "home overview splitter exists");
    require(homeOverviewSplitter->count() == 2, "home overview splitter has device and temperature cards");

    auto *deviceOverviewCard = qobject_cast<QGroupBox *>(homeOverviewSplitter->widget(0));
    auto *temperatureOverviewCard = qobject_cast<QGroupBox *>(homeOverviewSplitter->widget(1));
    require(deviceOverviewCard != nullptr, "device overview card exists");
    require(temperatureOverviewCard != nullptr, "temperature overview card exists");
    require(deviceOverviewCard->layout() != nullptr, "device overview card layout exists");
    require(temperatureOverviewCard->layout() != nullptr, "temperature overview card layout exists");

    auto *deviceOverviewBody = deviceOverviewCard->findChild<QWidget *>(QStringLiteral("homeOverviewDeviceBody"));
    auto *temperatureOverviewBody = temperatureOverviewCard->findChild<QWidget *>(QStringLiteral("temperatureOverviewPanel"));
    require(deviceOverviewBody != nullptr, "device overview body exists");
    require(temperatureOverviewBody != nullptr, "temperature overview body exists");
    require(deviceOverviewBody->layout() != nullptr, "device overview body layout exists");
    require(temperatureOverviewBody->layout() != nullptr, "temperature overview body layout exists");

    requireMargins(deviceOverviewCard->layout()->contentsMargins(),
                   QMargins(1, 0, 1, 1),
                   "device overview card outer padding matches sensor cards");
    requireMargins(temperatureOverviewCard->layout()->contentsMargins(),
                   QMargins(1, 0, 1, 1),
                   "temperature overview card outer padding matches sensor cards");
    requireMargins(deviceOverviewBody->layout()->contentsMargins(),
                   QMargins(2, 2, 2, 2),
                   "device overview body padding matches sensor-card content rhythm");
    requireMargins(temperatureOverviewBody->layout()->contentsMargins(),
                   QMargins(2, 2, 2, 2),
                   "temperature overview body padding matches sensor-card content rhythm");

    auto *temperatureChannelButton =
        window.findChild<QToolButton *>(QStringLiteral("temperatureOverviewChannelButton"));
    require(temperatureChannelButton != nullptr, "temperature overview channel selector exists");
    require(temperatureChannelButton->toolButtonStyle() == Qt::ToolButtonTextOnly,
            "temperature overview channel selector text remains centered");
    require(temperatureChannelButton->menu() != nullptr,
            "temperature overview channel selector menu exists");
    require(temperatureChannelButton->menu()->minimumWidth() == temperatureChannelButton->width() &&
                temperatureChannelButton->menu()->maximumWidth() == temperatureChannelButton->width(),
            "temperature overview channel menu width matches capsule width");
    require(temperatureChannelButton->menu()->actions().size() == 2,
            "temperature overview channel menu has two channel options");
    temperatureChannelButton->menu()->popup(temperatureChannelButton->mapToGlobal(QPoint(0, temperatureChannelButton->height())));
    processEventsFor(50);
    for (QAction *action : temperatureChannelButton->menu()->actions())
    {
        const QRect actionRect = temperatureChannelButton->menu()->actionGeometry(action);
        require(std::abs(actionRect.width() - temperatureChannelButton->width()) <= 4,
                "temperature overview channel menu option width matches capsule width");
        require(std::abs(actionRect.height() - temperatureChannelButton->height()) <= 4,
                "temperature overview channel menu option height matches capsule height");
    }
    temperatureChannelButton->menu()->hide();
    processEventsFor(50);

    QLabel *peakTrendTitle = nullptr;
    const QList<QLabel*> sectionTitleLabels =
        window.findChildren<QLabel *>(QStringLiteral("sectionTitleLabel"));
    for (QLabel *label : sectionTitleLabels)
    {
        if (label->text() == QStringLiteral("归一化二次谐波峰值趋势"))
        {
            peakTrendTitle = label;
            break;
        }
    }
    require(peakTrendTitle != nullptr,
            "normalized second harmonic peak trend title omits frame-count suffix");

    QPushButton *peakFilterButton = nullptr;
    const QList<QPushButton*> compactTcpButtons =
        window.findChildren<QPushButton *>(QStringLiteral("compactTcpButton"));
    for (QPushButton *button : compactTcpButtons)
    {
        if (button->text().startsWith(QStringLiteral("峰值搜索:")))
        {
            peakFilterButton = button;
            break;
        }
    }
    require(peakFilterButton != nullptr, "peak search filter button exists");
    require(peakFilterButton->width() >= peakFilterButton->fontMetrics().horizontalAdvance(peakFilterButton->text()) + 48,
            "peak search filter button has enough horizontal room for its label");

    auto *dataGroup = window.findChild<QGroupBox *>(QStringLiteral("sensorRowContainer"));
    require(dataGroup != nullptr, "sensor row container exists");

    auto *epsilonGroup = dataGroup->findChild<QGroupBox *>(QStringLiteral("sensorGroupBox"));
    require(epsilonGroup != nullptr, "EPSILON card exists");
    auto *epsilonPanel = dataGroup->findChild<QWidget *>(QStringLiteral("epsilonPanel"));
    require(epsilonPanel != nullptr, "EPSILON panel exists");
    require(epsilonPanel->layout() != nullptr, "EPSILON panel layout exists");
    requireMargins(epsilonPanel->layout()->contentsMargins(),
                   QMargins(2, 2, 2, 2),
                   "EPSILON panel content rhythm remains the reference");

    QList<QFrame*> cards = dataGroup->findChildren<QFrame *>(QStringLiteral("epsilonSectionCard"));
    require(cards.size() == 3, "three EPSILON section cards");
    std::sort(cards.begin(), cards.end(), [](const QFrame *lhs, const QFrame *rhs) {
        if (std::abs(lhs->y() - rhs->y()) > 4)
        {
            return lhs->y() < rhs->y();
        }
        return lhs->x() < rhs->x();
    });

    const int rowTolerance = 4;
    require(std::abs(cards.at(0)->y() - cards.at(1)->y()) <= rowTolerance,
            "EPSILON first and second cards are on one row at default window size");
    require(std::abs(cards.at(0)->y() - cards.at(2)->y()) <= rowTolerance,
            "EPSILON third card is on the same row at default window size");
    require(cards.at(1)->x() > cards.at(0)->x(), "EPSILON second card is to the right of the first");
    require(cards.at(2)->x() > cards.at(1)->x(), "EPSILON third card is to the right of the second");

    for (QTimer *timer : window.findChildren<QTimer *>())
    {
        timer->stop();
    }

    QStringList sampleValues = {
        QStringLiteral("9999-12-31T23:59:59.999Z"),
        QStringLiteral("18446744073709551615 us"),
        QStringLiteral("原始 4294967295 / 丢帧 4294967295"),
        QStringLiteral("0xFFFF 已初始化 / 定位融合中"),
        QStringLiteral("hAcc 9999.999 m / vAcc 9999.999 m"),
        QStringLiteral("N -9999.999 / E 9999.999 / D -9999.999"),
        QStringLiteral("X -9999.999 / Y 9999.999 / Z -9999.999"),
        QStringLiteral("X -9999.9999 / Y 9999.9999 / Z -9999.9999"),
        QStringLiteral("Roll -180.00 / Pitch 90.00 / Yaw 359.99")
    };

    QList<QLabel*> valueLabels;
    for (QFrame *card : cards)
    {
        valueLabels.append(card->findChildren<QLabel *>(QStringLiteral("valueLabel")));
    }
    require(!valueLabels.isEmpty(), "EPSILON value labels exist");
    for (int i = 0; i < valueLabels.size(); ++i)
    {
        QLabel *label = valueLabels.at(i);
        label->setText(sampleValues.at(i % sampleValues.size()));
        label->setToolTip(label->text());
        label->updateGeometry();
    }
    activateLayouts(dataGroup);
    dataGroup->updateGeometry();
    processEventsFor(50);
    activateLayouts(dataGroup);

    for (const QLabel *label : valueLabels)
    {
        require(label->wordWrap(), "EPSILON value labels can wrap long values");
        require(label->sizePolicy().verticalPolicy() != QSizePolicy::Fixed,
                "EPSILON value labels can grow vertically");
        const QRect needed = wrappedTextBounds(label);
        require(needed.height() <= label->height() + 2,
                "EPSILON value label height fits wrapped worst-case content");
    }
    for (const QFrame *card : cards)
    {
        const int cardBottomInDataGroup = card->mapTo(dataGroup, QPoint(0, card->height())).y();
        require(cardBottomInDataGroup <= dataGroup->contentsRect().bottom() + 2,
                "EPSILON card remains visible after worst-case value wrapping");
    }

    window.close();
    processEventsFor(100);
    std::cout << "main_window_layout_test passed\n";
    return 0;
}
