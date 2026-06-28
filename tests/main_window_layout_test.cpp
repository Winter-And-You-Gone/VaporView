#include "MainWindow.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
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

void clickWidget(QWidget *widget, int waitMs = 50)
{
    require(widget != nullptr, "click widget exists");
    const QPoint localCenter = widget->rect().center();
    const QPoint globalCenter = widget->mapToGlobal(localCenter);
    QMouseEvent press(QEvent::MouseButtonPress,
                      localCenter,
                      globalCenter,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        localCenter,
                        globalCenter,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &release);
    if (waitMs > 0)
    {
        processEventsFor(waitMs);
    }
}

void hoverWidget(QWidget *widget, bool hovered, int waitMs = 50)
{
    require(widget != nullptr, "hover widget exists");
    if (hovered)
    {
        QEvent enter(QEvent::Enter);
        QCoreApplication::sendEvent(widget, &enter);
    }
    else
    {
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(widget, &leave);
    }
    if (waitMs > 0)
    {
        processEventsFor(waitMs);
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
#ifdef Q_OS_WIN
        settings.setValue(QStringLiteral("serial/temperature_port"), QStringLiteral("COM9"));
#else
        settings.setValue(QStringLiteral("serial/temperature_port"), QStringLiteral("/dev/ttyRD105"));
#endif
        settings.setValue(QStringLiteral("serial/temperature_baud"), QStringLiteral("38400"));
        settings.setValue(QStringLiteral("rate/temperature"), QStringLiteral("5"));
        settings.sync();
    }

    MainWindow window;
    window.setWindowTitle(QStringLiteral("VaporView"));
    window.resize(1280, 800);
    window.show();
    processEventsFor(500);
    const QSize originalWindowSize = window.size();

    auto *appLayoutSplitter = window.findChild<QSplitter *>(QStringLiteral("appLayoutSplitter"));
    require(appLayoutSplitter != nullptr, "app layout splitter exists");
    require(window.centralWidget() != nullptr, "central widget exists");
    require(appLayoutSplitter->geometry().bottom() >= window.centralWidget()->contentsRect().bottom() - 1,
            "main content reaches the status bar without a bottom gap");

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
    require(checkedSidebarButton->height() == 44,
            "compact sidebar option is 4px smaller");
    require(checkedSidebarButton->width() == checkedSidebarButton->height(),
            "compact sidebar option is a strict rounded square");
    require(checkedSidebarButton->iconSize().width() >= 28 &&
                checkedSidebarButton->iconSize().height() >= 28,
            "compact sidebar lucide icon is visually larger");
    QPushButton *temperatureNavButton = nullptr;
    for (QPushButton *button : sidebarButtons)
    {
        if (button->accessibleName() == QStringLiteral("温控") ||
            button->accessibleName() == QStringLiteral("Thermal"))
        {
            temperatureNavButton = button;
            break;
        }
    }
    require(temperatureNavButton != nullptr, "temperature sidebar button exists");
    require(temperatureNavButton->property("_vv_sidebar_icon_name").toString() == QStringLiteral("thermometer"),
            "temperature sidebar button uses the home overview thermometer icon");
    const int visualLeftPadding =
        appSidebar->mapTo(&window, QPoint(0, 0)).x() + checkedSidebarButton->x();
    const int visualRightPadding =
        appSidebar->width() - checkedSidebarButton->x() - checkedSidebarButton->width();
    require(std::abs(visualLeftPadding - visualRightPadding) <= 1,
            "compact sidebar button has balanced visible left and right padding");
    auto *customLogo = window.findChild<QLabel *>(QStringLiteral("customTitleLogo"));
    require(customLogo != nullptr, "custom title logo exists");
    const int logoCenterX = customLogo->mapTo(&window, customLogo->rect().center()).x();
    const int checkedSidebarButtonCenterX =
        checkedSidebarButton->mapTo(&window, checkedSidebarButton->rect().center()).x();
    require(std::abs(logoCenterX - checkedSidebarButtonCenterX) <= 1,
            "custom title logo aligns with compact sidebar button center");
    require(customLogo->property("_vv_logo_state").toString() == QStringLiteral("logo"),
            "custom title logo starts as app logo");
    hoverWidget(customLogo, true);
    require(customLogo->property("_vv_logo_state").toString() == QStringLiteral("close-sidebar"),
            "custom title logo hover shows collapse sidebar icon");
    require(customLogo->property("titleBarHover").toBool(),
            "custom title logo hover background is active");
    clickWidget(customLogo);
    require(appSidebar->width() <= 1,
            "custom title logo click collapses left sidebar");
    require(customLogo->property("_vv_logo_state").toString() == QStringLiteral("open-sidebar"),
            "custom title logo remains hovered and changes to expand sidebar icon");
    clickWidget(customLogo);
    require(appSidebar->width() >= 44,
            "custom title logo click restores left sidebar");
    require(customLogo->property("_vv_logo_state").toString() == QStringLiteral("close-sidebar"),
            "custom title logo hover returns to collapse sidebar icon after restore");
    hoverWidget(customLogo, false);
    require(customLogo->property("_vv_logo_state").toString() == QStringLiteral("logo"),
            "custom title logo leave restores app logo");

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

    const QList<QLabel*> homeDeviceCapsules =
        window.findChildren<QLabel *>(QStringLiteral("homeDeviceStatusCapsule"));
    require(homeDeviceCapsules.size() == 6,
            "home device overview includes six status capsules");
    bool hasTemperatureHomeCapsule = false;
    for (QLabel *capsule : homeDeviceCapsules)
    {
        if (capsule->text().contains(QStringLiteral("RD105")))
        {
            hasTemperatureHomeCapsule = true;
            break;
        }
    }
    require(hasTemperatureHomeCapsule,
            "home device overview includes the RD105 laser temperature controller");

    auto *temperaturePortCombo = window.findChild<QComboBox *>(QStringLiteral("temperaturePortCombo"));
    auto *temperatureBaudCombo = window.findChild<QComboBox *>(QStringLiteral("temperatureBaudCombo"));
    auto *temperatureRateCombo = window.findChild<QComboBox *>(QStringLiteral("temperatureRateCombo"));
    require(temperaturePortCombo != nullptr && temperatureBaudCombo != nullptr && temperatureRateCombo != nullptr,
            "RD105 serial controls exist in device configuration");
#ifdef Q_OS_WIN
    require(temperaturePortCombo->currentText() == QStringLiteral("COM9"),
            "RD105 local serial port defaults to COM9");
#else
    require(temperaturePortCombo->currentText() == QStringLiteral("/dev/ttyRD105"),
            "RD105 local serial port defaults to /dev/ttyRD105");
#endif
    require(temperatureBaudCombo->currentText() == QStringLiteral("38400"),
            "RD105 local serial baud defaults to protocol-supported 38400");
    require(temperatureRateCombo->currentText() == QStringLiteral("5"),
            "RD105 local polling rate defaults to 5 Hz");

    auto *temperatureChannelButton =
        window.findChild<QToolButton *>(QStringLiteral("temperatureOverviewChannelButton"));
    require(temperatureChannelButton != nullptr, "temperature overview channel selector exists");
    require(temperatureChannelButton->toolButtonStyle() == Qt::ToolButtonTextOnly,
            "temperature overview channel selector text remains centered");
    require(temperatureChannelButton->property("available").isValid() &&
                !temperatureChannelButton->property("available").toBool(),
            "temperature overview channel selector starts unavailable without controller data");
    require(!temperatureChannelButton->isEnabled(),
            "temperature overview channel selector is disabled without controller data");
    require(qApp->styleSheet().contains(QStringLiteral("QToolButton#temperatureOverviewChannelButton[available=\"false\"]")),
            "temperature overview channel selector has a gray unavailable state");
    require(qApp->styleSheet().contains(QStringLiteral("QToolButton#temperatureOverviewChannelButton::menu-indicator")) &&
                qApp->styleSheet().contains(QStringLiteral("combo_arrow_down.xpm")),
            "temperature overview channel selector has right-side dropdown arrow");
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

    const QList<QLabel*> temperatureValuePills =
        window.findChildren<QLabel *>(QStringLiteral("temperatureOverviewValuePill"));
    require(temperatureValuePills.size() == 2,
            "temperature overview target and current value pills exist");
    require(!qApp->styleSheet().contains(QStringLiteral("QLabel#temperatureOverviewValuePill[hasData")),
            "temperature overview value pills use the default background without data-state colors");
    for (QLabel *pill : temperatureValuePills)
    {
        require(!pill->property("hasData").isValid(),
                "temperature overview value pill does not carry availability styling state");
    }
    auto *temperatureOutputSwitch =
        window.findChild<QPushButton *>(QStringLiteral("temperatureOverviewOutputSwitch"));
    require(temperatureOutputSwitch != nullptr,
            "temperature overview output enable capsule exists");
    require(!temperatureOutputSwitch->isEnabled(),
            "temperature overview output enable capsule is disabled without controller data");

    qRegisterMetaType<VaporView::TemperatureControllerData>("VaporView::TemperatureControllerData");
    VaporView::TemperatureControllerData validTemperatureData;
    validTemperatureData.valid = true;
    validTemperatureData.channels[0].target_temperature_c = 25.0;
    validTemperatureData.channels[0].measured_temperature_c = 24.75;
    validTemperatureData.channels[0].output_enabled = true;
    const bool temperatureUpdateInvoked = QMetaObject::invokeMethod(
        &window,
        "onRemoteTemperatureControllerStatusUpdated",
        Qt::DirectConnection,
        Q_ARG(VaporView::TemperatureControllerData, validTemperatureData));
    require(temperatureUpdateInvoked,
            "temperature overview can receive a valid controller data frame");
    processEventsFor(50);
    require(temperatureChannelButton->isEnabled(),
            "temperature overview channel selector is enabled with controller data");
    require(temperatureChannelButton->property("available").isValid() &&
                temperatureChannelButton->property("available").toBool(),
            "temperature overview channel selector marks valid controller data as available");
    require(temperatureOutputSwitch->isEnabled(),
            "temperature overview output enable capsule is enabled with controller data");
    require(temperatureOutputSwitch->isChecked(),
            "temperature overview output enable capsule reflects the confirmed controller output state");

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

    QPushButton *deviceConfigNavButton = nullptr;
    for (QPushButton *button : sidebarButtons)
    {
        if (button->accessibleName() == QStringLiteral("设备配置") ||
            button->accessibleName() == QStringLiteral("Device"))
        {
            deviceConfigNavButton = button;
            break;
        }
    }
    require(deviceConfigNavButton != nullptr, "device configuration sidebar button exists");
    clickWidget(deviceConfigNavButton, 150);
    activateLayouts(&window);
    auto *deviceConfigPage = window.findChild<QWidget *>(QStringLiteral("deviceConfigPage"));
    require(deviceConfigPage != nullptr && deviceConfigPage->isVisible(),
            "device configuration page can be opened");
    auto *deviceConfigScrollArea =
        deviceConfigPage->findChild<QScrollArea *>(QStringLiteral("mainCardsScrollArea"));
    require(deviceConfigScrollArea != nullptr, "device configuration scroll area exists");
    require(deviceConfigScrollArea->horizontalScrollBar() != nullptr &&
                deviceConfigScrollArea->horizontalScrollBar()->maximum() == 0,
            "device configuration page fits horizontally at default window size");

    const QStringList removedDevicePageActions = {
        QStringLiteral("刷新"),
        QStringLiteral("连接"),
        QStringLiteral("取消"),
        QStringLiteral("断开"),
        QStringLiteral("Refresh"),
        QStringLiteral("Connect"),
        QStringLiteral("Cancel"),
        QStringLiteral("Disconnect"),
    };
    for (QPushButton *button : deviceConfigPage->findChildren<QPushButton *>())
    {
        if (!button->isVisible())
        {
            continue;
        }
        require(!removedDevicePageActions.contains(button->text()),
                "device configuration page omits title-bar serial actions");
        require(button->focusPolicy() == Qt::TabFocus,
                "device configuration buttons do not take focus on mouse click");
    }

    QComboBox *devicePortCombo = nullptr;
    QComboBox *deviceRateCombo = nullptr;
    for (QComboBox *combo : deviceConfigPage->findChildren<QComboBox *>())
    {
        if (!combo->isVisible() || !combo->isEditable())
        {
            continue;
        }
        if (combo->currentText() == QStringLiteral("COM9"))
        {
            devicePortCombo = combo;
        }
        else if (combo->currentText() == QStringLiteral("5"))
        {
            deviceRateCombo = combo;
        }
    }
    require(devicePortCombo != nullptr && devicePortCombo->width() <= 112,
            "device configuration serial combo is sized for COM999");
    require(deviceRateCombo != nullptr && deviceRateCombo->width() <= 92,
            "device configuration rate combo is sized for 9999");

    QFrame *epsilonConfigCard = nullptr;
    for (QFrame *card : deviceConfigPage->findChildren<QFrame *>(QStringLiteral("epsilonSectionCard")))
    {
        const QList<QComboBox*> packetCombos = card->findChildren<QComboBox *>();
        int packetRateComboCount = 0;
        for (QComboBox *combo : packetCombos)
        {
            if (combo->property("epsilonPacketId").isValid())
            {
                ++packetRateComboCount;
            }
        }
        if (packetRateComboCount == 8)
        {
            epsilonConfigCard = card;
            break;
        }
    }
    require(epsilonConfigCard != nullptr, "device configuration page embeds all EPSILON packet-rate controls");
    require(epsilonConfigCard->isVisible(), "device EPSILON configuration card is visible in local mode");
    const QRect epsilonConfigBounds = epsilonConfigCard->rect().adjusted(-1, -1, 1, 1);
    for (QComboBox *combo : epsilonConfigCard->findChildren<QComboBox *>())
    {
        if (!combo->isVisible() || !combo->property("epsilonPacketId").isValid())
        {
            continue;
        }
        const QRect comboRect(combo->mapTo(epsilonConfigCard, QPoint(0, 0)), combo->size());
        require(epsilonConfigBounds.contains(comboRect),
                "device EPSILON packet-rate combos stay inside the embedded card");
        require(combo->width() >= combo->fontMetrics().horizontalAdvance(combo->currentText()) + 44,
                "device EPSILON packet-rate combo text is not clipped");
    }
    for (const QString& buttonText : {QStringLiteral("保存并应用"),
                                      QStringLiteral("配置RTCM串口"),
                                      QStringLiteral("重新配置输出")})
    {
        bool foundButton = false;
        for (QPushButton *button : epsilonConfigCard->findChildren<QPushButton *>())
        {
            if (button->isVisible() && button->text() == buttonText)
            {
                const QRect buttonRect(button->mapTo(epsilonConfigCard, QPoint(0, 0)), button->size());
                require(epsilonConfigBounds.contains(buttonRect),
                        "device EPSILON command buttons stay inside the embedded card");
                require(button->width() >= button->fontMetrics().horizontalAdvance(button->text()) + 32,
                        "device EPSILON command button text is not clipped");
                foundButton = true;
                break;
            }
        }
        require(foundButton, "device EPSILON configuration card exposes expected command buttons");
    }

    const QList<QFrame*> deviceSummaryCards =
        deviceConfigPage->findChildren<QFrame *>(QStringLiteral("epsilonSectionCard"));
    require(!deviceSummaryCards.isEmpty(), "device configuration telemetry summary card exists");
    for (const QFrame *summaryCard : deviceSummaryCards)
    {
        if (!summaryCard->isVisible())
        {
            continue;
        }
        const int summaryRight =
            summaryCard->mapTo(deviceConfigScrollArea->viewport(), QPoint(summaryCard->width(), 0)).x();
        require(summaryRight <= deviceConfigScrollArea->viewport()->width() + 2,
                "device configuration telemetry summary fits inside the viewport");
    }

    clickWidget(checkedSidebarButton, 150);
    activateLayouts(&window);

    auto *dataGroup = window.findChild<QGroupBox *>(QStringLiteral("sensorRowContainer"));
    require(dataGroup != nullptr, "sensor row container exists");

    auto *epsilonGroup = dataGroup->findChild<QGroupBox *>(QStringLiteral("sensorGroupBox"));
    require(epsilonGroup != nullptr, "EPSILON card exists");
    QGroupBox *environmentGroup = nullptr;
    const QList<QGroupBox*> sensorGroups =
        dataGroup->findChildren<QGroupBox *>(QStringLiteral("sensorGroupBox"));
    for (QGroupBox *group : sensorGroups)
    {
        if (group != epsilonGroup &&
            group->findChildren<QLabel *>(QStringLiteral("envStatusIcon")).size() == 3)
        {
            environmentGroup = group;
            break;
        }
    }
    require(environmentGroup != nullptr, "environment and lidar card exists");

    window.resize(1920, 1000);
    processEventsFor(300);
    activateLayouts(&window);
    const int sensorRowWidth = epsilonGroup->width() + environmentGroup->width();
    require(sensorRowWidth > 0, "sensor row has measurable width");
    const double environmentRatio =
        static_cast<double>(environmentGroup->width()) / static_cast<double>(sensorRowWidth);
    require(environmentRatio >= 0.17 && environmentRatio <= 0.23,
            "environment and lidar card stays close to one fifth of the sensor row at wide widths");
    require(epsilonGroup->width() >= environmentGroup->width() * 3.6,
            "EPSILON card keeps an approximately 4:1 width relationship against environment card");
    QList<QFrame*> wideCards = dataGroup->findChildren<QFrame *>(QStringLiteral("epsilonSectionCard"));
    require(wideCards.size() == 3, "three EPSILON section cards at wide window size");
    int wideCardsRight = 0;
    for (const QFrame *card : wideCards)
    {
        wideCardsRight = std::max(wideCardsRight,
                                  card->mapTo(epsilonGroup, QPoint(card->width(), 0)).x());
    }
    require(wideCardsRight >= epsilonGroup->contentsRect().right() - 8,
            "EPSILON section cards expand to fill the navigation card at wide window size");

    window.resize(originalWindowSize);
    processEventsFor(300);
    activateLayouts(&window);

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
