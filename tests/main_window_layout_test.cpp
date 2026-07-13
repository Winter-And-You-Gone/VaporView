#include "AppTheme.h"
#include "MainWindow.h"
#include "RtkConfigDialog.h"
#include "SingleLevelPopupMenu.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QAction>
#include <QColor>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QFrame>
#include <QGroupBox>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMetaObject>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QPixmap>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextOption>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

namespace
{

struct SkyTelemetryRowWidgets
{
    QWidget *row = nullptr;
    QComboBox *transportCombo = nullptr;
    QLineEdit *tcpHostEdit = nullptr;
    QSpinBox *tcpPortSpin = nullptr;
    QComboBox *serialPortCombo = nullptr;
    QComboBox *serialBaudCombo = nullptr;
};

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}


void processEventsFor(int timeoutMs);

void requireComboPopupStyled(QComboBox *combo, const char *message)
{
    require(combo != nullptr, message);
    require(combo->property("vaporViewComboPopupStyled").toBool(),
            "combo carries the shared popup style marker");
    require(combo->view() != nullptr, "combo has a popup view");
    require(combo->view()->property("vaporViewComboPopupStyled").toBool(),
            "combo popup view carries the shared style marker");
    require(!combo->view()->property("vaporViewComboPopupRoundedMaskEnabled").isValid(),
            "combo popup view leaves rounded masking to the outer popup container");
    require(!combo->view()->property("vaporViewComboPopupViewportMargin").isValid(),
            "combo popup view does not inset its viewport for an inner border");
    require(!combo->view()->testAttribute(Qt::WA_TranslucentBackground) &&
                !combo->view()->testAttribute(Qt::WA_NoSystemBackground),
            "combo popup view uses an opaque backing store to avoid transparent edge artifacts");
    require(combo->view()->viewport() != nullptr &&
                !combo->view()->viewport()->testAttribute(Qt::WA_TranslucentBackground) &&
                !combo->view()->viewport()->testAttribute(Qt::WA_NoSystemBackground),
            "combo popup viewport avoids transparent backing-store attributes");
    require(combo->view()->viewport()->styleSheet().contains(QStringLiteral("background-color:")) &&
                combo->view()->viewport()->styleSheet().contains(QStringLiteral("border: none")),
            "combo popup viewport has an explicit filled background without drawing its own border");
    require(combo->view()->findChild<QWidget *>(QStringLiteral("vaporViewComboPopupBorderOverlay")) == nullptr,
            "combo popup does not create a redundant child border overlay");
    require(!combo->view()->property("vaporViewComboPopupShadowEnabled").toBool(),
            "combo popup view does not request unsafe external shadow chrome");
    require(!combo->view()->property("floatingPanelChrome").toBool(),
            "combo popup view does not use floating panel chrome");
    require(combo->view()->objectName() == QStringLiteral("vaporViewComboPopupView"),
            "combo popup view uses the shared object name");
    const QString popupStyle = combo->view()->styleSheet();
    const QString hoverColor = VaporView::appThemeColorName(VaporView::AppThemeColor::MenuHover,
                                                            VaporView::isDarkThemeEnabled());
    require(popupStyle.contains(QStringLiteral("border: none")) &&
                !popupStyle.contains(QStringLiteral("border: 1px solid")) &&
                !popupStyle.contains(QStringLiteral("border-bottom: 1px solid")) &&
                popupStyle.contains(QStringLiteral("border-radius: 0px")) &&
                popupStyle.contains(QStringLiteral("padding: 12px 0px")) &&
                popupStyle.contains(QStringLiteral("padding: 7px 14px")) &&
                popupStyle.contains(QStringLiteral("min-height: 30px")) &&
                popupStyle.contains(QStringLiteral("background-color: %1").arg(hoverColor)) &&
                !popupStyle.contains(QStringLiteral("padding: 12px 4px")),
            "combo popup stylesheet matches the shared rounded full-width-highlight menu style");
}

void requireComboPopupFloatingContainer(QComboBox *combo, const char *message)
{
    requireComboPopupStyled(combo, message);

    QAbstractItemView *view = combo->view();
    require(view != nullptr, "combo popup view exists before opening");
    combo->showPopup();
    processEventsFor(120);
    QWidget *container = view->window();
    require(container != nullptr, "combo popup has a native popup container");
    require(container->objectName() == QStringLiteral("vaporViewComboPopupContainer"),
            "combo popup styles the native outer container directly");
    require(container->property("vaporViewComboPopupRoundedMaskEnabled").toBool(),
            "combo popup container enables safe rounded masking");
    require(container->property("cornerRadius").toInt() == 10,
            "combo popup container uses the shared corner radius");
    require(container->property("vaporViewComboPopupBorderWidth").toInt() == 1,
            "combo popup container owns the one-pixel border");
    const QString borderColor = VaporView::appThemeColorName(VaporView::AppThemeColor::Border,
                                                             VaporView::isDarkThemeEnabled());
    require(container->styleSheet().contains(QStringLiteral("border: none")) &&
                container->styleSheet().contains(QStringLiteral("border-radius: 10px")),
            "combo popup container QSS provides the rounded opaque panel without drawing the border");
    QWidget *borderLayer = container->findChild<QWidget *>(QStringLiteral("vaporViewComboPopupBorderLayer"),
                                                          Qt::FindDirectChildrenOnly);
    require(borderLayer != nullptr,
            "combo popup container owns a direct child border layer");
    require(borderLayer->property("vaporViewComboPopupBorderLayer").toBool() &&
                borderLayer->property("vaporViewComboPopupBorderWidth").toInt() == 1 &&
                borderLayer->property("cornerRadius").toInt() == 10,
            "combo popup border layer carries the shared border metadata");
    require(borderLayer->geometry() == container->rect(),
            "combo popup border layer covers the full native popup container");
    require(borderLayer->styleSheet().contains(QStringLiteral("border: 1px solid %1").arg(borderColor)) &&
                borderLayer->styleSheet().contains(QStringLiteral("border-radius: 10px")),
            "combo popup border layer QSS draws the complete rounded gray border");
    require(view->geometry().left() == container->contentsRect().left() &&
                view->geometry().right() == container->contentsRect().right(),
            "combo popup view fills the frame contents without an extra white inset");
    const QRegion containerMask = container->mask();
    require(containerMask.contains(QPoint(container->width() / 2, 0)) &&
                containerMask.contains(QPoint(container->width() / 2, container->height() - 1)) &&
                containerMask.contains(QPoint(0, container->height() / 2)) &&
                containerMask.contains(QPoint(container->width() - 1, container->height() / 2)),
            "combo popup rounded mask preserves all four border midpoints");
    require(container->property("vaporViewComboPopupAnchorGap").toInt() == 0,
            "combo popup container does not add extra anchor gap");
    require(container->property("vaporViewComboPopupNativeDropShadowDisabled").toBool(),
            "combo popup container disables native drop shadow for a clean rounded popup");
    require(!container->property("vaporViewComboPopupShadowEnabled").toBool(),
            "combo popup container does not request unsafe external shadow chrome");
    require(!container->property("floatingPanelChrome").toBool(),
            "combo popup container does not use floating panel chrome");
    require(container->property("shadowMargin").toInt() == 0,
            "combo popup container does not reserve an unsafe transparent shadow margin");
    require(container->findChild<QWidget *>(QStringLiteral("vaporViewComboPopupShadowHost"),
                                           Qt::FindDirectChildrenOnly) == nullptr,
            "combo popup container does not create an unsafe shadow host");
    require(container->findChild<QWidget *>(QStringLiteral("vaporViewComboPopupShadowWindow"),
                                           Qt::FindDirectChildrenOnly) == nullptr,
            "combo popup container does not create a transparent custom shadow window");
    require(view->findChild<QWidget *>(QStringLiteral("vaporViewComboPopupBorderOverlay")) == nullptr,
            "opened combo popup keeps the gray border in QSS instead of an overlay widget");
    combo->hidePopup();
    processEventsFor(40);
}

void requireComboPopupsStyledIn(QWidget *scope, const char *message)
{
    require(scope != nullptr, message);
    const QList<QComboBox*> combos = scope->findChildren<QComboBox *>();
    require(!combos.isEmpty(), message);
    for (QComboBox *combo : combos)
    {
        requireComboPopupStyled(combo, message);
    }
}

void requireLabelTextOneOf(const QLabel *label, const QStringList& expected, const char *message);

QColor averageVisibleIconColor(const QIcon& icon)
{
    const QImage image = icon.pixmap(QSize(32, 32)).toImage().convertToFormat(QImage::Format_ARGB32);
    int count = 0;
    int red = 0;
    int green = 0;
    int blue = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() < 16)
            {
                continue;
            }
            red += pixel.red();
            green += pixel.green();
            blue += pixel.blue();
            ++count;
        }
    }

    require(count > 0, "icon has visible pixels for color sampling");
    return QColor(red / count, green / count, blue / count);
}

void requireColorNear(const QColor& actual, const QColor& expected, int tolerance, const char *message)
{
    require(std::abs(actual.red() - expected.red()) <= tolerance &&
                std::abs(actual.green() - expected.green()) <= tolerance &&
                std::abs(actual.blue() - expected.blue()) <= tolerance,
            message);
}

void requireCardTitleBar(QWidget *card,
                         const QStringList& expectedTitles,
                         const QString& expectedIconName,
                         const char *message)
{
    require(card != nullptr, message);
    QWidget *matchedTitleBar = nullptr;
    const QList<QWidget*> titleBars = card->findChildren<QWidget *>(QStringLiteral("sectionTitleBar"));
    for (QWidget *titleBar : titleBars)
    {
        bool titleMatched = false;
        const QList<QLabel*> titleLabels = titleBar->findChildren<QLabel *>(QStringLiteral("sectionTitleLabel"));
        for (QLabel *titleLabel : titleLabels)
        {
            if (expectedTitles.contains(titleLabel->text()))
            {
                titleMatched = true;
                break;
            }
        }
        if (!titleMatched)
        {
            continue;
        }

        bool iconMatched = false;
        const QList<QLabel*> iconLabels = titleBar->findChildren<QLabel *>(QStringLiteral("sectionTitleIcon"));
        for (QLabel *iconLabel : iconLabels)
        {
            if (iconLabel->property("_vv_section_title_icon_name").toString() == expectedIconName)
            {
                iconMatched = true;
                break;
            }
        }
        if (iconMatched)
        {
            matchedTitleBar = titleBar;
            break;
        }
    }

    require(matchedTitleBar != nullptr, message);
    require(matchedTitleBar->height() >= 36 && matchedTitleBar->height() <= 44,
            "device configuration card title bar uses the standard compact height");
    require(matchedTitleBar->y() <= 2,
            "card title bar sits flush with the top of the card");
}

QGroupBox *findCardByTitle(QWidget *root, const QStringList& expectedTitles)
{
    if (!root)
    {
        return nullptr;
    }

    const QList<QGroupBox*> cards = root->findChildren<QGroupBox *>();
    for (QGroupBox *card : cards)
    {
        const QList<QLabel*> titleLabels = card->findChildren<QLabel *>(QStringLiteral("sectionTitleLabel"));
        for (QLabel *titleLabel : titleLabels)
        {
            if (expectedTitles.contains(titleLabel->text()))
            {
                return card;
            }
        }
    }
    return nullptr;
}

QLabel *findLabelByText(QWidget *root, const QStringList& expectedTexts)
{
    if (!root)
    {
        return nullptr;
    }

    const QList<QLabel*> labels = root->findChildren<QLabel *>();
    for (QLabel *label : labels)
    {
        if (expectedTexts.contains(label->text()))
        {
            return label;
        }
    }
    return nullptr;
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

void clickWidgetAt(QWidget *widget, const QPoint& localPoint, int waitMs = 50)
{
    require(widget != nullptr, "click widget exists");
    const QPoint globalPoint = widget->mapToGlobal(localPoint);
    QMouseEvent press(QEvent::MouseButtonPress,
                      localPoint,
                      globalPoint,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        localPoint,
                        globalPoint,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &release);
    if (waitMs > 0)
    {
        processEventsFor(waitMs);
    }
}

void clickWidget(QWidget *widget, int waitMs = 50)
{
    require(widget != nullptr, "click widget exists");
    clickWidgetAt(widget, widget->rect().center(), waitMs);
}

void moveMouseOverWidgetAt(QWidget *widget, const QPoint& localPoint, int waitMs = 50)
{
    require(widget != nullptr, "mouse-move widget exists");
    const QPoint globalPoint = widget->mapToGlobal(localPoint);
    QMouseEvent move(QEvent::MouseMove,
                     localPoint,
                     globalPoint,
                     Qt::NoButton,
                     Qt::NoButton,
                     Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &move);
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

void requireTitleMenuFloatingPanel(QFrame *panel, const char *message)
{
    require(panel != nullptr, message);
    require(panel->property("floatingPanelChrome").toBool(), message);
    require(panel->property("shadowMargin").toInt() == 22, message);
    require(panel->property("cornerRadius").toInt() == 10, message);
    require(panel->testAttribute(Qt::WA_TranslucentBackground), message);
    const bool dark = qApp && qApp->property(VaporView::kAppDarkThemeProperty).toBool();
    const QString menuHover = VaporView::appThemeColorName(VaporView::AppThemeColor::MenuHover, dark);
    require(panel->styleSheet().contains(QStringLiteral("QFrame#titleApplicationMainMenu")) &&
                panel->styleSheet().contains(QStringLiteral("background-color: transparent")) &&
                panel->styleSheet().contains(QStringLiteral("border: none")) &&
                panel->styleSheet().contains(QStringLiteral("background-color: %1").arg(menuHover)) &&
                !panel->styleSheet().contains(QStringLiteral("border: 1px solid")),
            "title menu leaves chrome to the floating panel painter");
}

void requireMenuRowsRespectRoundedVerticalPadding(QFrame *panel,
                                                  QFrame *menu,
                                                  const QList<QFrame *>& rows,
                                                  const char *message)
{
    require(panel != nullptr, message);
    require(menu != nullptr, message);
    require(!rows.isEmpty(), message);

    const int minVerticalGap = panel->property("cornerRadius").toInt() + 2;
    const QRect menuGlobal(menu->mapToGlobal(QPoint(0, 0)), menu->size());
    int topGap = std::numeric_limits<int>::max();
    int bottomGap = std::numeric_limits<int>::max();
    int visibleRows = 0;
    for (QFrame *row : rows)
    {
        if (!row || !row->isVisibleTo(menu))
        {
            continue;
        }
        const QRect rowGlobal(row->mapToGlobal(QPoint(0, 0)), row->size());
        if (!menuGlobal.intersects(rowGlobal))
        {
            continue;
        }
        topGap = std::min(topGap, rowGlobal.top() - menuGlobal.top());
        bottomGap = std::min(bottomGap, menuGlobal.bottom() - rowGlobal.bottom());
        ++visibleRows;
    }

    require(visibleRows > 0, message);
    require(topGap >= minVerticalGap && bottomGap >= minVerticalGap, message);
}

void requireRtkSidebarPage(MainWindow& window, QLabel *customTitleLabel)
{
    QPushButton *homeButton = nullptr;
    QPushButton *temperatureButton = nullptr;
    QPushButton *rtkButton = nullptr;
    QPushButton *deviceButton = nullptr;
    const QList<QPushButton*> sidebarButtons =
        window.findChildren<QPushButton *>(QStringLiteral("appSidebarButton"));
    for (QPushButton *button : sidebarButtons)
    {
        const QString accessibleName = button->accessibleName();
        if (accessibleName == QStringLiteral("首页") || accessibleName == QStringLiteral("Home"))
        {
            homeButton = button;
        }
        else if (accessibleName == QStringLiteral("温控") || accessibleName == QStringLiteral("Thermal"))
        {
            temperatureButton = button;
        }
        else if (accessibleName == QStringLiteral("RTK配置") || accessibleName == QStringLiteral("RTK Config"))
        {
            rtkButton = button;
        }
        else if (accessibleName == QStringLiteral("设备配置") || accessibleName == QStringLiteral("Device"))
        {
            deviceButton = button;
        }
    }
    require(temperatureButton != nullptr, "temperature sidebar button exists for RTK order check");
    require(rtkButton != nullptr, "RTK sidebar button exists");
    require(deviceButton != nullptr, "device configuration sidebar button exists for RTK order check");
    require(homeButton != nullptr, "home sidebar button exists after RTK check");
    require(rtkButton->property("_vv_sidebar_icon_name").toString() == QStringLiteral("satellite"),
            "RTK sidebar button uses satellite icon");
    require(homeButton->y() < deviceButton->y() &&
                deviceButton->y() < temperatureButton->y() &&
                temperatureButton->y() < rtkButton->y(),
            "sidebar order is home, device configuration, thermal, RTK configuration");
    require(rtkButton->toolTip().contains(QStringLiteral("未启动")) ||
                rtkButton->toolTip().contains(QStringLiteral("stopped")),
            "RTK sidebar button starts with stopped status text");

    const QList<QToolButton*> titleButtons = window.findChildren<QToolButton *>(QStringLiteral("titleBarButton"));
    for (QToolButton *button : titleButtons)
    {
        require(!button->toolTip().contains(QStringLiteral("RTK")),
                "RTK config is not duplicated in the title bar");
    }

    const QSize iconSize(32, 32);
    const qint64 stoppedIconKey = rtkButton->icon().pixmap(iconSize).cacheKey();

    clickWidget(rtkButton);
    processEventsFor(150);
    auto *pageStack = window.findChild<QStackedWidget *>(QStringLiteral("mainPageStack"));
    require(pageStack != nullptr, "main page stack exists");
    auto *dialog = qobject_cast<RtkConfigDialog *>(pageStack->currentWidget());
    require(dialog != nullptr, "RTK config opens as an embedded sidebar page");
    require(dialog->isVisible(), "embedded RTK config page is visible after sidebar click");
    activateLayouts(dialog);
    processEventsFor(100);
    requireLabelTextOneOf(customTitleLabel,
                          {QStringLiteral("RTK配置"), QStringLiteral("RTK Config")},
                          "custom title bar follows the selected RTK page");
    auto *rtkScrollArea = dialog->findChild<QScrollArea *>(QStringLiteral("rtkConfigScrollArea"));
    require(rtkScrollArea != nullptr, "RTK config page uses a scroll area");
    require(rtkScrollArea->horizontalScrollBar() != nullptr &&
                rtkScrollArea->horizontalScrollBar()->maximum() == 0,
            "RTK config page avoids a horizontal scrollbar at the default window size");
    require(rtkScrollArea->verticalScrollBar() != nullptr &&
                rtkScrollArea->verticalScrollBar()->maximum() == 0,
            "RTK config page avoids a vertical scrollbar at the default window size");
    const std::vector<std::pair<QString, int>> compactCombos = {
        {QStringLiteral("rtkMountpointCombo"), 135},
        {QStringLiteral("rtkOutputPortCombo"), 100},
        {QStringLiteral("rtkBaudrateCombo"), 130},
        {QStringLiteral("rtkTimeoutCombo"), 115},
        {QStringLiteral("rtkReconnectCombo"), 125},
        {QStringLiteral("rtkGgaPortCombo"), 260},
    };
    for (const auto& [objectName, maxWidth] : compactCombos)
    {
        auto *combo = dialog->findChild<QComboBox *>(objectName);
        require(combo != nullptr, "compact RTK combo exists");
        require(combo->width() <= maxWidth,
                "RTK combo width stays compact");
    }
    const std::vector<std::pair<QString, int>> compactLineEdits = {
        {QStringLiteral("rtkServerEdit"), 170},
        {QStringLiteral("rtkUsernameEdit"), 170},
        {QStringLiteral("rtkPasswordEdit"), 170},
    };
    for (const auto& [objectName, maxWidth] : compactLineEdits)
    {
        auto *lineEdit = dialog->findChild<QLineEdit *>(objectName);
        require(lineEdit != nullptr, "compact RTK line edit exists");
        require(lineEdit->width() <= maxWidth,
                "RTK server/account line edit width stays compact");
    }
    auto widgetX = [dialog](QWidget *widget) {
        return widget->mapTo(dialog, QPoint(0, 0)).x();
    };
    auto widgetY = [dialog](QWidget *widget) {
        return widget->mapTo(dialog, QPoint(0, 0)).y();
    };
    auto widgetRect = [dialog](QWidget *widget) {
        return QRect(widget->mapTo(dialog, QPoint(0, 0)), widget->size());
    };
    auto requireSameRect = [](const QRect& actual, const QRect& expected, const char *message) {
        require(std::abs(actual.x() - expected.x()) <= 2 &&
                    std::abs(actual.y() - expected.y()) <= 2 &&
                    std::abs(actual.width() - expected.width()) <= 2 &&
                    std::abs(actual.height() - expected.height()) <= 2,
                message);
    };
    const std::vector<std::tuple<QStringList, QString, const char*>> rtkCards = {
        {{QStringLiteral("NTRIP 服务器配置"), QStringLiteral("NTRIP Server Configuration")},
         QStringLiteral("satellite"),
         "RTK NTRIP card uses the standard icon title bar"},
        {{QStringLiteral("RTCM 输出配置"), QStringLiteral("RTCM Output Configuration")},
         QStringLiteral("usb"),
         "RTK RTCM output card uses the standard icon title bar"},
        {{QStringLiteral("GGA 监视"), QStringLiteral("GGA Monitor")},
         QStringLiteral("activity"),
         "RTK GGA monitor card uses the standard icon title bar"},
        {{QStringLiteral("RTK 服务日志"), QStringLiteral("RTK Service Log")},
         QStringLiteral("scroll-text"),
         "RTK service log card uses the standard icon title bar"},
        {{QStringLiteral("服务操作"), QStringLiteral("Service Actions")},
         QStringLiteral("play"),
         "RTK service action card uses the standard icon title bar"},
    };
    for (const auto& [titles, iconName, message] : rtkCards)
    {
        QGroupBox *card = findCardByTitle(dialog, titles);
        require(card != nullptr, message);
        require(card->objectName() == QStringLiteral("sensorGroupBox"),
                "RTK card reuses the home page sensor card style");
        requireCardTitleBar(card, titles, iconName, message);
    }
    auto *ntripCard = findCardByTitle(dialog,
                                      {QStringLiteral("NTRIP 服务器配置"),
                                       QStringLiteral("NTRIP Server Configuration")});
    auto *rtcmCard = findCardByTitle(dialog,
                                     {QStringLiteral("RTCM 输出配置"),
                                      QStringLiteral("RTCM Output Configuration")});
    require(ntripCard != nullptr && rtcmCard != nullptr,
            "RTK NTRIP and RTCM cards exist for compact width checks");
    auto *ggaCard = findCardByTitle(dialog,
                                    {QStringLiteral("GGA 监视"),
                                     QStringLiteral("GGA Monitor")});
    auto *logCard = findCardByTitle(dialog,
                                    {QStringLiteral("RTK 服务日志"),
                                     QStringLiteral("RTK Service Log")});
    auto *actionCard = findCardByTitle(dialog,
                                       {QStringLiteral("服务操作"),
                                        QStringLiteral("Service Actions")});
    require(ggaCard != nullptr && logCard != nullptr && actionCard != nullptr,
            "RTK GGA, log, and service action cards exist for compact stacking checks");
    require(ntripCard->width() <= ntripCard->sizeHint().width() + 4 &&
                ntripCard->width() <= 760,
            "RTK NTRIP card width hugs its compact form contents");
    require(rtcmCard->width() <= rtcmCard->sizeHint().width() + 4,
            "RTK RTCM output card width hugs its compact form contents");
    require(std::abs(widgetY(ggaCard) - widgetY(ntripCard)) <= 2 &&
                widgetX(ggaCard) >= widgetX(ntripCard) + ntripCard->width(),
            "RTK GGA monitor card sits in the first row to the right of NTRIP");
    require(widgetY(rtcmCard) >= widgetY(ntripCard) + ntripCard->height() - 2,
            "RTK RTCM output card sits below the first row");
    if (!(std::abs(widgetY(logCard) - widgetY(rtcmCard)) <= 2 &&
          widgetX(logCard) >= widgetX(rtcmCard) + rtcmCard->width()))
    {
        std::cerr << "RTCM card: x=" << widgetX(rtcmCard) << " y=" << widgetY(rtcmCard)
                  << " w=" << rtcmCard->width() << " h=" << rtcmCard->height()
                  << " sizeHint=" << rtcmCard->sizeHint().width() << 'x' << rtcmCard->sizeHint().height()
                  << " log card: x=" << widgetX(logCard) << " y=" << widgetY(logCard)
                  << " w=" << logCard->width() << " h=" << logCard->height()
                  << " sizeHint=" << logCard->sizeHint().width() << 'x' << logCard->sizeHint().height()
                  << " dialog=" << dialog->width() << 'x' << dialog->height() << '\n';
    }
    require(std::abs(widgetY(logCard) - widgetY(rtcmCard)) <= 2 &&
                widgetX(logCard) >= widgetX(rtcmCard) + rtcmCard->width(),
            "RTK service log card sits to the right of the RTCM card");
    require(std::abs(logCard->height() - rtcmCard->height()) <= 2,
            "RTK service log card matches the RTCM output card height");
    auto *rtkServiceLogText = dialog->findChild<QTextEdit *>(QStringLiteral("rtkServiceLogTextEdit"));
    require(rtkServiceLogText != nullptr, "RTK service log text area exists");
    require(rtkServiceLogText->lineWrapMode() == QTextEdit::WidgetWidth,
            "RTK service log wraps long lines to the widget width");
    require(rtkServiceLogText->wordWrapMode() == QTextOption::WrapAtWordBoundaryOrAnywhere,
            "RTK service log can wrap long diagnostic tokens");
    dialog->appendLog(QStringLiteral("无信号 RTK 测试成功: 输入 6406 B, 输出 6406 B, loopback 6406 B"));
    dialog->appendRawLogLine(QStringLiteral(
        "2026/07/01 17:51:37 [CC---]\n"
        "  输入: 5500 B    速率: 7248 bps\n"
        "  状态: 127.0.0.1\n"
        "  RTCM诊断:\n"
        "    - 已检查 5500 B\n"
        "    - RTCM3/D3帧 38\n"
        "    - 首字节 0D 0A D3 00 13"));
    const QString logPlainText = rtkServiceLogText->toPlainText();
    require(logPlainText.contains(QStringLiteral("[17")) ||
                logPlainText.contains(QStringLiteral("[0")) ||
                logPlainText.contains(QStringLiteral("[1")) ||
                logPlainText.contains(QStringLiteral("[2")),
            "RTK service log prepends a timestamp line");
    require(logPlainText.contains(QStringLiteral("无信号 RTK 测试成功:")) &&
                logPlainText.contains(QStringLiteral("  - 输入 6406 B")) &&
                logPlainText.contains(QStringLiteral("  - 输出 6406 B")) &&
                logPlainText.contains(QStringLiteral("  - loopback 6406 B")),
            "RTK service log formats comma-separated success details as bullet lines");
    require(logPlainText.contains(QStringLiteral("RTCM诊断:")) &&
                logPlainText.contains(QStringLiteral("    - 已检查 5500 B")) &&
                logPlainText.contains(QStringLiteral("    - 首字节 0D 0A D3 00 13")),
            "RTK service log keeps RTCM diagnostic details on separate indented lines");
    require(actionCard->width() >= dialog->width() - 40,
            "RTK bottom actions are collected in a full-width service card");
    auto *rtkActionStatusLabel = dialog->findChild<QLabel *>(QStringLiteral("rtkStatusLabel"));
    auto *rtkActionStatusIcon = dialog->findChild<QLabel *>(QStringLiteral("rtkStatusIcon"));
    QLabel *actionTitleLabel = findLabelByText(actionCard,
                                               {QStringLiteral("服务操作"),
                                                QStringLiteral("Service Actions")});
    QWidget *actionTitleBar = nullptr;
    for (QWidget *titleBar : actionCard->findChildren<QWidget *>(QStringLiteral("sectionTitleBar")))
    {
        if (actionTitleLabel && titleBar->isAncestorOf(actionTitleLabel))
        {
            actionTitleBar = titleBar;
            break;
        }
    }
    require(rtkActionStatusLabel != nullptr && rtkActionStatusIcon != nullptr &&
                actionTitleLabel != nullptr && actionTitleBar != nullptr,
            "RTK service action title bar contains status text and icon");
    require(actionTitleBar->isAncestorOf(rtkActionStatusLabel) &&
                actionTitleBar->isAncestorOf(rtkActionStatusIcon),
            "RTK service status is placed in the service action title bar");
    require(widgetX(rtkActionStatusIcon) > widgetX(actionTitleLabel) + actionTitleLabel->width() &&
                widgetX(rtkActionStatusLabel) > widgetX(rtkActionStatusIcon),
            "RTK service status sits to the right of the service action title");
    require(rtkActionStatusLabel->text().startsWith(QStringLiteral("状态:")) ||
                rtkActionStatusLabel->text().startsWith(QStringLiteral("Status:")),
            "RTK service status text remains visible in the title bar");
    auto *serverEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkServerEdit"));
    auto *usernameEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkUsernameEdit"));
    auto *portEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkPortEdit"));
    auto *passwordEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkPasswordEdit"));
    auto *mountpointCombo = dialog->findChild<QComboBox *>(QStringLiteral("rtkMountpointCombo"));
    auto *fetchMountpointsButton = dialog->findChild<QPushButton *>(QStringLiteral("rtkFetchMountpointsButton"));
    require(serverEdit != nullptr && usernameEdit != nullptr && portEdit != nullptr &&
                passwordEdit != nullptr && mountpointCombo != nullptr && fetchMountpointsButton != nullptr,
            "RTK NTRIP compact fields exist for alignment checks");
    requireComboPopupStyled(mountpointCombo,
                            "RTK mountpoint combo uses the shared popup styling helper");
    const int rtkInputHeight = serverEdit->height();
    const QList<QPushButton*> rtkPushButtons = dialog->findChildren<QPushButton *>();
    const QStringList manualConfigButtonTexts = {
        QStringLiteral("保存配置"),
        QStringLiteral("加载配置"),
        QStringLiteral("Save Config"),
        QStringLiteral("Load Config"),
    };
    for (QPushButton *button : rtkPushButtons)
    {
        require(!manualConfigButtonTexts.contains(button->text()),
                "RTK page does not expose manual config import/export buttons");
        require(std::abs(button->height() - rtkInputHeight) <= 1,
                "RTK push buttons match the input field height");
    }
    const QList<QToolButton*> rtkToolButtons = dialog->findChildren<QToolButton *>();
    for (QToolButton *button : rtkToolButtons)
    {
        require(std::abs(button->height() - rtkInputHeight) <= 1,
                "RTK tool buttons match the input field height");
    }
    require(std::abs(widgetX(serverEdit) - widgetX(usernameEdit)) <= 2,
            "RTK NTRIP server and username fields align vertically");
    require(std::abs(serverEdit->width() - usernameEdit->width()) <= 1,
            "RTK NTRIP server address field matches the username field width");
    require(std::abs(widgetX(portEdit) - widgetX(passwordEdit)) <= 2,
            "RTK NTRIP port and password fields align vertically");
    require(passwordEdit->width() > portEdit->width() + 40,
            "RTK NTRIP staggered password field keeps a usable width");
    require(std::abs((widgetX(passwordEdit) + passwordEdit->width()) - widgetX(mountpointCombo)) <= 10,
            "RTK NTRIP staggered password field spans under the port field and mountpoint label");
    require(widgetX(mountpointCombo) > widgetX(portEdit) &&
                widgetX(mountpointCombo) - (widgetX(portEdit) + portEdit->width()) <= 90,
            "RTK NTRIP mountpoint field follows closely after the port field");
    require(std::abs(widgetX(fetchMountpointsButton) - widgetX(mountpointCombo)) <= 2 &&
                widgetY(fetchMountpointsButton) > widgetY(mountpointCombo),
            "RTK NTRIP mountpoint detection button sits below and aligns with the mountpoint combo");
    require(std::abs(fetchMountpointsButton->width() - mountpointCombo->width()) <= 2,
            "RTK NTRIP mountpoint combo matches the detect button width");
    auto *ggaSourceCombo = dialog->findChild<QComboBox *>(QStringLiteral("rtkGgaPortCombo"));
    auto *ggaToggleButton = dialog->findChild<QPushButton *>(QStringLiteral("rtkGgaToggleButton"));
    auto *ggaOutputText = dialog->findChild<QTextEdit *>(QStringLiteral("rtkGgaTextEdit"));
    QLabel *ggaSourceLabel = findLabelByText(dialog,
                                             {QStringLiteral("GGA来源:"),
                                              QStringLiteral("GGA Source:")});
    QLabel *ggaFrequencyLabel = nullptr;
    for (QLabel *label : dialog->findChildren<QLabel *>())
    {
        if (label->text().startsWith(QStringLiteral("频率:")) ||
            label->text().startsWith(QStringLiteral("Rate:")))
        {
            ggaFrequencyLabel = label;
            break;
        }
    }
    require(ggaSourceCombo != nullptr && ggaToggleButton != nullptr && ggaOutputText != nullptr &&
                ggaSourceLabel != nullptr && ggaFrequencyLabel != nullptr,
            "RTK GGA source controls exist");
    requireComboPopupStyled(ggaSourceCombo,
                            "RTK GGA source combo uses the shared popup styling helper");
    require(ggaSourceCombo->currentText() == QStringLiteral("Epsilon生成") ||
                ggaSourceCombo->currentText() == QStringLiteral("Epsilon generated"),
            "RTK GGA source defaults to the compact Epsilon generated label");
    require(ggaSourceCombo->itemText(0) == QStringLiteral("Epsilon生成") ||
                ggaSourceCombo->itemText(0) == QStringLiteral("Epsilon generated"),
            "RTK GGA source first option uses the compact Epsilon generated label");
    const int compactGgaSourceWidth =
        ggaSourceCombo->fontMetrics().horizontalAdvance(ggaSourceCombo->currentText()) + 84;
    require(ggaSourceCombo->width() <= compactGgaSourceWidth,
            "RTK GGA source combo width hugs the Epsilon generated label");
    require(ggaSourceCombo->lineEdit() != nullptr,
            "RTK GGA source combo exposes its editable text field");
    const int ggaSourceTextWidth =
        ggaSourceCombo->lineEdit()->fontMetrics().horizontalAdvance(ggaSourceCombo->currentText());
    require(ggaSourceCombo->lineEdit()->contentsRect().width() >= ggaSourceTextWidth + 4,
            "RTK GGA source combo text field fully shows the Epsilon generated label");
    require(ggaToggleButton->text() == QStringLiteral("读取") ||
                ggaToggleButton->text() == QStringLiteral("Read"),
            "RTK GGA idle action uses a compact read label");
    require(widgetX(ggaSourceLabel) - widgetX(ggaCard) <= 40,
            "RTK GGA source label is aligned near the left edge of its card");
    require(widgetY(ggaToggleButton) > widgetY(ggaSourceLabel) &&
                std::abs(widgetX(ggaToggleButton) - widgetX(ggaSourceLabel)) <= 8,
            "RTK GGA read button sits below the source label");
    require(widgetY(ggaFrequencyLabel) > widgetY(ggaSourceCombo) &&
                std::abs(widgetX(ggaFrequencyLabel) - widgetX(ggaSourceCombo)) <= 8,
            "RTK GGA frequency readout sits below the source combo");
    require(widgetX(ggaOutputText) >= widgetX(ggaSourceCombo) + ggaSourceCombo->width(),
            "RTK GGA output text area sits to the right of the source controls");
    require(std::abs(ggaCard->height() - ntripCard->height()) <= 24,
            "RTK GGA monitor card height matches the NTRIP card height closely");
    require(findLabelByText(dialog,
                            {QStringLiteral("状态: 点击按钮开始读取GGA"),
                             QStringLiteral("Status: Click button to read GGA")}) == nullptr,
            "RTK GGA monitor does not show the idle status prompt");
    auto *outputPortCombo = dialog->findChild<QComboBox *>(QStringLiteral("rtkOutputPortCombo"));
    auto *baudrateCombo = dialog->findChild<QComboBox *>(QStringLiteral("rtkBaudrateCombo"));
    auto *timeoutCombo = dialog->findChild<QComboBox *>(QStringLiteral("rtkTimeoutCombo"));
    auto *reconnectCombo = dialog->findChild<QComboBox *>(QStringLiteral("rtkReconnectCombo"));
    auto *applyLeverButton = dialog->findChild<QPushButton *>(QStringLiteral("rtkApplyLeverArmButton"));
    auto *refreshPortsButton = dialog->findChild<QPushButton *>(QStringLiteral("rtkRefreshPortsButton"));
    auto *autoDetectPortsButton = dialog->findChild<QPushButton *>(QStringLiteral("rtkAutoDetectPortsButton"));
    auto *leverHelpButton = dialog->findChild<QToolButton *>(QStringLiteral("rtkLeverHelpButton"));
    QLabel *outputPortLabel = findLabelByText(dialog,
                                              {QStringLiteral("输出串口:"),
                                               QStringLiteral("Output Port:")});
    require(outputPortCombo != nullptr && baudrateCombo != nullptr && timeoutCombo != nullptr &&
                reconnectCombo != nullptr && applyLeverButton != nullptr && refreshPortsButton != nullptr &&
                autoDetectPortsButton != nullptr && leverHelpButton != nullptr && outputPortLabel != nullptr,
            "RTK RTCM output controls exist");
    requireComboPopupStyled(outputPortCombo,
                            "RTK output port combo uses the shared popup styling helper");
    requireComboPopupStyled(baudrateCombo,
                            "RTK baudrate combo uses the shared popup styling helper");
    requireComboPopupStyled(timeoutCombo,
                            "RTK timeout combo uses the shared popup styling helper");
    requireComboPopupStyled(reconnectCombo,
                            "RTK reconnect combo uses the shared popup styling helper");
    require(widgetX(outputPortCombo) - (widgetX(outputPortLabel) + outputPortLabel->width()) <= 12,
            "RTK RTCM output port combo sits close to its label");
    require(outputPortCombo->width() <= 100 &&
                outputPortCombo->width() >= outputPortCombo->fontMetrics().horizontalAdvance(QStringLiteral("COM999")) + 34,
            "RTK RTCM output port combo is fixed around COM999 width");
    require(std::abs(widgetY(baudrateCombo) - widgetY(outputPortCombo)) <= 2 &&
                widgetX(baudrateCombo) > widgetX(outputPortCombo),
            "RTK RTCM output port and baudrate share the first row");
    require(widgetY(timeoutCombo) > widgetY(outputPortCombo) &&
                std::abs(widgetY(reconnectCombo) - widgetY(timeoutCombo)) <= 2 &&
                widgetX(reconnectCombo) > widgetX(timeoutCombo),
            "RTK RTCM timeout and reconnect interval share the second row");
    auto *leverXEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkLeverXEdit"));
    auto *leverYEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkLeverYEdit"));
    auto *leverZEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkLeverZEdit"));
    require(leverXEdit != nullptr && leverYEdit != nullptr && leverZEdit != nullptr,
            "RTK RTCM lever-arm XYZ edits exist");
    require(widgetY(leverXEdit) > widgetY(timeoutCombo) &&
                widgetX(leverXEdit) > widgetX(outputPortLabel),
            "RTK RTCM lever-arm XYZ controls sit on the third row");
    require(widgetX(leverYEdit) - (widgetX(leverXEdit) + leverXEdit->width()) <= 42 &&
                widgetX(leverZEdit) - (widgetX(leverYEdit) + leverYEdit->width()) <= 42,
            "RTK RTCM lever-arm XYZ controls stay tightly grouped");
    require(widgetY(applyLeverButton) > widgetY(leverXEdit) &&
                std::abs(widgetY(refreshPortsButton) - widgetY(applyLeverButton)) <= 2 &&
                std::abs(widgetY(autoDetectPortsButton) - widgetY(applyLeverButton)) <= 2 &&
                widgetX(refreshPortsButton) > widgetX(applyLeverButton) &&
                widgetX(autoDetectPortsButton) > widgetX(refreshPortsButton),
            "RTK RTCM lever-arm, refresh and auto-detect buttons share the fourth row");
    requireColorNear(averageVisibleIconColor(leverHelpButton->icon()),
                     VaporView::appThemeColor(VaporView::AppThemeColor::Primary, false),
                     6,
                     "RTK lever-arm help icon uses the light theme primary color");
    clickWidget(leverHelpButton, 100);
    auto *leverHelpPopup = dialog->findChild<QFrame *>(QStringLiteral("rtkLeverHelpPopup"));
    require(leverHelpPopup != nullptr && leverHelpPopup->isVisible(),
            "RTK lever-arm help opens a menu-like popup");
    auto *leverHelpText = leverHelpPopup->findChild<QLabel *>(QStringLiteral("rtkLeverHelpPopupText"));
    require(leverHelpText != nullptr &&
                (leverHelpText->text().contains(QStringLiteral("主天线杆臂")) ||
                 leverHelpText->text().contains(QStringLiteral("Main antenna lever arm"))),
            "RTK lever-arm help popup contains the lever-arm guidance");
    leverHelpPopup->hide();
    processEventsFor(50);

    const QRect ntripRectBeforeTheme = widgetRect(ntripCard);
    const QRect ggaRectBeforeTheme = widgetRect(ggaCard);
    const QRect rtcmRectBeforeTheme = widgetRect(rtcmCard);
    const QRect logRectBeforeTheme = widgetRect(logCard);
    const QRect actionRectBeforeTheme = widgetRect(actionCard);
    require(outputPortLabel->width() >= outputPortLabel->fontMetrics().horizontalAdvance(outputPortLabel->text()) + 4,
            "RTK RTCM output port label has enough width before theme switch");
    require(QMetaObject::invokeMethod(&window, "onToggleTheme", Qt::DirectConnection),
            "main window can switch to dark theme from the RTK page");
    processEventsFor(250);
    activateLayouts(dialog);
    processEventsFor(100);
    require(qApp->property(VaporView::kAppDarkThemeProperty).toBool(),
            "main window is in dark theme for RTK layout stability checks");
    requireSameRect(widgetRect(ntripCard), ntripRectBeforeTheme,
                    "RTK NTRIP card geometry stays stable after switching to dark theme");
    requireSameRect(widgetRect(ggaCard), ggaRectBeforeTheme,
                    "RTK GGA card geometry stays stable after switching to dark theme");
    requireSameRect(widgetRect(rtcmCard), rtcmRectBeforeTheme,
                    "RTK RTCM card geometry stays stable after switching to dark theme");
    requireSameRect(widgetRect(logCard), logRectBeforeTheme,
                    "RTK service log card geometry stays stable after switching to dark theme");
    requireSameRect(widgetRect(actionCard), actionRectBeforeTheme,
                    "RTK service action card geometry stays stable after switching to dark theme");
    require(std::abs(logCard->height() - rtcmCard->height()) <= 2,
            "RTK service log card remains equal-height with RTCM in dark theme");
    requireColorNear(averageVisibleIconColor(leverHelpButton->icon()),
                     VaporView::appThemeColor(VaporView::AppThemeColor::Primary, true),
                     6,
                     "RTK lever-arm help icon uses the dark theme primary color");
    require(outputPortLabel->width() >= outputPortLabel->fontMetrics().horizontalAdvance(outputPortLabel->text()) + 4,
            "RTK RTCM output port label has enough width after switching to dark theme");
    require(rtkScrollArea->horizontalScrollBar()->maximum() == 0 &&
                rtkScrollArea->verticalScrollBar()->maximum() == 0,
            "RTK config page remains scrollbar-free after switching to dark theme");
    require(QMetaObject::invokeMethod(&window, "onToggleTheme", Qt::DirectConnection),
            "main window can switch back to light theme from the RTK page");
    processEventsFor(250);
    activateLayouts(dialog);
    processEventsFor(100);
    require(!qApp->property(VaporView::kAppDarkThemeProperty).toBool(),
            "main window returns to light theme after RTK layout stability checks");
    requireSameRect(widgetRect(ntripCard), ntripRectBeforeTheme,
                    "RTK NTRIP card geometry returns unchanged after switching back to light theme");
    requireSameRect(widgetRect(ggaCard), ggaRectBeforeTheme,
                    "RTK GGA card geometry returns unchanged after switching back to light theme");
    requireSameRect(widgetRect(rtcmCard), rtcmRectBeforeTheme,
                    "RTK RTCM card geometry returns unchanged after switching back to light theme");
    requireSameRect(widgetRect(logCard), logRectBeforeTheme,
                    "RTK service log card geometry returns unchanged after switching back to light theme");
    requireSameRect(widgetRect(actionCard), actionRectBeforeTheme,
                    "RTK service action card geometry returns unchanged after switching back to light theme");
    requireColorNear(averageVisibleIconColor(leverHelpButton->icon()),
                     VaporView::appThemeColor(VaporView::AppThemeColor::Primary, false),
                     6,
                     "RTK lever-arm help icon returns to the light theme primary color");
    for (QWidget *topLevel : QApplication::topLevelWidgets())
    {
        require(qobject_cast<RtkConfigDialog *>(topLevel) == nullptr,
                "RTK config is not opened as a top-level dialog");
    }

    QMetaObject::invokeMethod(dialog, "rtkRunningChanged", Qt::DirectConnection, Q_ARG(bool, true));
    processEventsFor(50);
    require(rtkButton->toolTip().contains(QStringLiteral("运行中")) ||
                rtkButton->toolTip().contains(QStringLiteral("running")),
            "RTK sidebar button shows running status text");
    const qint64 runningIconKey = rtkButton->icon().pixmap(iconSize).cacheKey();
    require(runningIconKey != stoppedIconKey, "RTK sidebar icon changes when service starts");

    QMetaObject::invokeMethod(dialog, "rtkRunningChanged", Qt::DirectConnection, Q_ARG(bool, false));
    processEventsFor(50);
    require(rtkButton->toolTip().contains(QStringLiteral("未启动")) ||
                rtkButton->toolTip().contains(QStringLiteral("stopped")),
            "RTK sidebar button returns to stopped status text");
    require(rtkButton->icon().pixmap(iconSize).cacheKey() != runningIconKey,
            "RTK sidebar icon changes away from running color when service stops");

    clickWidget(homeButton);
    processEventsFor(150);
    requireLabelTextOneOf(customTitleLabel,
                          {QStringLiteral("首页"), QStringLiteral("Home")},
                          "custom title bar returns to home page after RTK sidebar check");
}

void requireLabelTextOneOf(const QLabel *label, const QStringList& expected, const char *message)
{
    require(label != nullptr, "label exists");
    require(expected.contains(label->text()), message);
}

void requireNoVisiblePageTitle(QWidget *page, const char *message)
{
    require(page != nullptr, "page exists");
    const QList<QLabel*> pageTitleLabels =
        page->findChildren<QLabel *>(QStringLiteral("pageTitleLabel"));
    for (const QLabel *label : pageTitleLabels)
    {
        require(!label->isVisible(), message);
    }
}

SkyTelemetryRowWidgets findSkyTelemetryRowWidgets(QWidget *scope)
{
    SkyTelemetryRowWidgets widgets;
    if (!scope)
    {
        return widgets;
    }

    const QList<QComboBox*> combos = scope->findChildren<QComboBox *>();
    for (QComboBox *combo : combos)
    {
        if (combo->findData(QStringLiteral("tcp")) >= 0 &&
            combo->findData(QStringLiteral("serial")) >= 0)
        {
            widgets.transportCombo = combo;
            break;
        }
    }
    if (!widgets.transportCombo)
    {
        return widgets;
    }

    widgets.row = widgets.transportCombo->parentWidget();
    if (!widgets.row)
    {
        return widgets;
    }

    const QList<QLineEdit*> edits =
        widgets.row->findChildren<QLineEdit *>(QString(), Qt::FindDirectChildrenOnly);
    if (!edits.isEmpty())
    {
        widgets.tcpHostEdit = edits.first();
    }
    const QList<QSpinBox*> spinBoxes =
        widgets.row->findChildren<QSpinBox *>(QString(), Qt::FindDirectChildrenOnly);
    if (!spinBoxes.isEmpty())
    {
        widgets.tcpPortSpin = spinBoxes.first();
    }
    const QList<QComboBox*> rowCombos =
        widgets.row->findChildren<QComboBox *>(QString(), Qt::FindDirectChildrenOnly);
    for (QComboBox *combo : rowCombos)
    {
        if (combo == widgets.transportCombo)
        {
            continue;
        }
        if (combo->isEditable())
        {
            widgets.serialPortCombo = combo;
        }
        else
        {
            widgets.serialBaudCombo = combo;
        }
    }

    return widgets;
}

void setSkyTelemetryTransport(QComboBox *transportCombo, const QString& transport)
{
    require(transportCombo != nullptr, "sky telemetry transport combo exists");
    const int index = transportCombo->findData(transport);
    require(index >= 0, "sky telemetry transport option exists");
    transportCombo->setCurrentIndex(index);
}

void requireSkyTelemetryTransportLabels(const SkyTelemetryRowWidgets& widgets, bool english)
{
    require(widgets.transportCombo != nullptr, "sky telemetry transport combo exists");
    const int tcpIndex = widgets.transportCombo->findData(QStringLiteral("tcp"));
    const int serialIndex = widgets.transportCombo->findData(QStringLiteral("serial"));
    require(tcpIndex >= 0 && serialIndex >= 0, "sky telemetry transport options exist");
    require(widgets.transportCombo->itemText(tcpIndex) == QStringLiteral("TCP"),
            "sky telemetry TCP option is labelled TCP");
    require(widgets.transportCombo->itemText(serialIndex) ==
                (english ? QStringLiteral("Serial") : QStringLiteral("串口")),
            "sky telemetry serial option follows the UI language");
}

bool telemetryFieldVisible(const QWidget *widget, bool effectiveVisibility)
{
    return widget && (effectiveVisibility ? widget->isVisible() : !widget->isHidden());
}

void requireSkyTelemetryTcpMode(const SkyTelemetryRowWidgets& widgets, bool effectiveVisibility = true)
{
    require(widgets.tcpHostEdit != nullptr &&
                widgets.tcpPortSpin != nullptr &&
                widgets.serialPortCombo != nullptr &&
                widgets.serialBaudCombo != nullptr,
            "sky telemetry TCP and serial controls exist");
    require(telemetryFieldVisible(widgets.tcpHostEdit, effectiveVisibility) &&
                telemetryFieldVisible(widgets.tcpPortSpin, effectiveVisibility),
            "sky telemetry TCP IP and port fields are visible in TCP mode");
    require(!telemetryFieldVisible(widgets.serialPortCombo, effectiveVisibility) &&
                !telemetryFieldVisible(widgets.serialBaudCombo, effectiveVisibility),
            "sky telemetry serial and baud fields are hidden in TCP mode");
}

void requireSkyTelemetrySerialMode(const SkyTelemetryRowWidgets& widgets, bool effectiveVisibility = true)
{
    require(widgets.tcpHostEdit != nullptr &&
                widgets.tcpPortSpin != nullptr &&
                widgets.serialPortCombo != nullptr &&
                widgets.serialBaudCombo != nullptr,
            "sky telemetry TCP and serial controls exist");
    require(!telemetryFieldVisible(widgets.tcpHostEdit, effectiveVisibility) &&
                !telemetryFieldVisible(widgets.tcpPortSpin, effectiveVisibility),
            "sky telemetry TCP IP and port fields are hidden in serial mode");
    require(telemetryFieldVisible(widgets.serialPortCombo, effectiveVisibility) &&
                telemetryFieldVisible(widgets.serialBaudCombo, effectiveVisibility),
            "sky telemetry serial and baud fields are visible in serial mode");
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

void requireSameRect(const QRect& actual, const QRect& expected, int tolerance, const char *message)
{
    require(std::abs(actual.x() - expected.x()) <= tolerance &&
                std::abs(actual.y() - expected.y()) <= tolerance &&
                std::abs(actual.width() - expected.width()) <= tolerance &&
                std::abs(actual.height() - expected.height()) <= tolerance,
            message);
}

void requireChildInsideParent(QWidget *child, QWidget *parent, int tolerance, const char *message)
{
    require(child != nullptr && parent != nullptr, message);
    const QRect childRect(child->mapTo(parent, QPoint(0, 0)), child->size());
    const bool contained =
        childRect.left() >= -tolerance &&
        childRect.top() >= -tolerance &&
        childRect.right() <= parent->rect().right() + tolerance &&
        childRect.bottom() <= parent->rect().bottom() + tolerance;
    if (!contained)
    {
        std::cerr << "Child rect: "
                  << childRect.x() << ',' << childRect.y() << ' '
                  << childRect.width() << 'x' << childRect.height()
                  << " parent rect: "
                  << parent->rect().x() << ',' << parent->rect().y() << ' '
                  << parent->rect().width() << 'x' << parent->rect().height()
                  << " child=" << child->objectName().toStdString()
                  << " parent=" << parent->objectName().toStdString() << '\n';
    }
    require(contained, message);
}

void requireLastStyleRuleContains(const QString& styleSheet,
                                  const QString& selector,
                                  const QString& expected,
                                  const char *message)
{
    const int index = styleSheet.lastIndexOf(selector);
    require(index >= 0, message);
    const int ruleStart = styleSheet.indexOf(QLatin1Char('{'), index);
    const int ruleEnd = ruleStart >= 0 ? styleSheet.indexOf(QLatin1Char('}'), ruleStart) : -1;
    require(ruleStart >= 0 && ruleEnd > ruleStart, message);
    const QString rule = styleSheet.mid(index, ruleEnd - index + 1);
    if (!rule.contains(expected))
    {
        std::cerr << "Expected style fragment: " << expected.toStdString() << '\n'
                  << "Actual style rule: " << rule.toStdString() << '\n';
    }
    require(rule.contains(expected), message);
}

void requireMenuPopupStyleUnified(const QString& styleSheet, bool dark, const char *message)
{
    const QString hoverColor = VaporView::appThemeColorName(VaporView::AppThemeColor::MenuHover, dark);
    const QString textColor = VaporView::appThemeColorName(VaporView::AppThemeColor::MenuText, dark);
    const QString disabledColor = VaporView::appThemeColorName(VaporView::AppThemeColor::MenuDisabledText, dark);

    requireLastStyleRuleContains(styleSheet,
                                 QStringLiteral("QMenu {"),
                                 QStringLiteral("border-radius: 10px"),
                                 message);
    requireLastStyleRuleContains(styleSheet,
                                 QStringLiteral("QMenu {"),
                                 QStringLiteral("padding: 12px 0px"),
                                 message);
    requireLastStyleRuleContains(styleSheet,
                                 QStringLiteral("QMenu {"),
                                 QStringLiteral("color: %1").arg(textColor),
                                 message);
    requireLastStyleRuleContains(styleSheet,
                                 QStringLiteral("QMenu::item {"),
                                 QStringLiteral("border-radius: 0px"),
                                 message);
    requireLastStyleRuleContains(styleSheet,
                                 QStringLiteral("QMenu::item {"),
                                 QStringLiteral("border: none"),
                                 message);
    requireLastStyleRuleContains(styleSheet,
                                 QStringLiteral("QMenu::item:selected {"),
                                 QStringLiteral("background-color: %1").arg(hoverColor),
                                 message);
    requireLastStyleRuleContains(styleSheet,
                                 QStringLiteral("QMenu::item:selected {"),
                                 QStringLiteral("color: %1").arg(textColor),
                                 message);
    requireLastStyleRuleContains(styleSheet,
                                 QStringLiteral("QMenu::item:disabled {"),
                                 QStringLiteral("color: %1").arg(disabledColor),
                                 message);
}

QList<QFrame*> sortedTelemetrySections(QWidget *summaryContainer)
{
    if (!summaryContainer)
    {
        return {};
    }

    QList<QFrame*> sections =
        summaryContainer->findChildren<QFrame *>(QStringLiteral("homeTelemetrySectionCard"));
    std::sort(sections.begin(), sections.end(), [](QFrame *a, QFrame *b) {
        return a->mapTo(a->parentWidget(), QPoint(0, 0)).y() <
               b->mapTo(b->parentWidget(), QPoint(0, 0)).y();
    });
    return sections;
}

QFrame *firstTelemetrySection(QWidget *summaryContainer)
{
    const QList<QFrame*> sections = sortedTelemetrySections(summaryContainer);
    return sections.isEmpty() ? nullptr : sections.first();
}

QFrame *findTelemetryPillByName(QFrame *section, const QString& text)
{
    if (!section)
    {
        return nullptr;
    }

    const QList<QFrame*> pills =
        section->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
    for (QFrame *pill : pills)
    {
        QLabel *nameLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel"));
        if (nameLabel && nameLabel->text().contains(text))
        {
            return pill;
        }
    }
    return nullptr;
}

bool isRemoteSourceModeText(const QString& text)
{
    return text.contains(QStringLiteral("天地远程")) ||
           text.contains(QStringLiteral("远程")) ||
           text.contains(QStringLiteral("Remote")) ||
           text.contains(QStringLiteral("天空")) ||
           text.contains(QStringLiteral("Sky"));
}

QComboBox *findSourceModeCombo(QWidget *root)
{
    if (!root)
    {
        return nullptr;
    }

    const QList<QComboBox*> combos = root->findChildren<QComboBox *>();
    for (QComboBox *combo : combos)
    {
        if (combo->count() < 2)
        {
            continue;
        }
        const QString localText = combo->itemText(0);
        const QString remoteText = combo->itemText(1);
        if ((localText.contains(QStringLiteral("本地")) || localText.contains(QStringLiteral("Local"))) &&
            isRemoteSourceModeText(remoteText))
        {
            return combo;
        }
    }
    return nullptr;
}

QComboBox *findComboWithData(QWidget *root, const QString& data)
{
    if (!root)
    {
        return nullptr;
    }
    for (QComboBox *combo : root->findChildren<QComboBox *>())
    {
        if (combo->findData(data) >= 0)
        {
            return combo;
        }
    }
    return nullptr;
}

int telemetrySectionRightPadding(QFrame *section)
{
    require(section != nullptr, "home telemetry rate section exists");
    int rightmostPill = 0;
    const QList<QFrame*> pills =
        section->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
    require(!pills.isEmpty(), "home telemetry rate section has value pills");
    for (QFrame *pill : pills)
    {
        const QRect pillRect(pill->mapTo(section, QPoint(0, 0)), pill->size());
        rightmostPill = std::max(rightmostPill, pillRect.right());
    }
    return section->rect().right() - rightmostPill;
}

void requireTelemetryRightPadding(QWidget *deviceOverviewCard,
                                  QFrame *rateSection,
                                  const char *message)
{
    const int rightPadding = telemetrySectionRightPadding(rateSection);
    if (rightPadding < 12)
    {
        std::cerr << "Home rate right padding: " << rightPadding
                  << " section width: " << rateSection->width()
                  << " device card width: " << (deviceOverviewCard ? deviceOverviewCard->width() : 0)
                  << " device card min width: " << (deviceOverviewCard ? deviceOverviewCard->minimumWidth() : 0)
                  << '\n';
    }
    require(rightPadding >= 12, message);
}

QAction *findActionByText(QWidget *root, const QStringList& expectedTexts)
{
    const QList<QAction*> actions = root->findChildren<QAction *>();
    for (QAction *action : actions)
    {
        if (action && expectedTexts.contains(action->text()))
        {
            return action;
        }
    }
    return nullptr;
}

#ifdef VAPORVIEW_HAS_OSGEARTH
void requireMainWindowMap3DEntries(MainWindow& window)
{
    QAction *mapAction = findActionByText(&window,
                                          {QStringLiteral("三维地图..."),
                                           QStringLiteral("3D Map...")});
    QAction *diagnosticsAction = findActionByText(&window,
                                                  {QStringLiteral("地图数据诊断..."),
                                                   QStringLiteral("Map Data Diagnostics...")});
    require(mapAction != nullptr, "3D map action exists in the main window");
    require(diagnosticsAction != nullptr, "map data diagnostics action exists in the main window");

    QMenu *viewMenu = nullptr;
    const QList<QMenu*> menus = window.findChildren<QMenu *>();
    for (QMenu *menu : menus)
    {
        if (menu && menu->actions().contains(mapAction) &&
            menu->actions().contains(diagnosticsAction))
        {
            viewMenu = menu;
            break;
        }
    }
    require(viewMenu != nullptr, "View menu exists for 3D map entries");
    require(viewMenu->actions().contains(mapAction), "View menu contains the 3D map action");
    require(viewMenu->actions().contains(diagnosticsAction), "View menu contains the map data diagnostics action");

    bool foundMapTitleButton = false;
    bool foundDiagnosticsTitleButton = false;
    const QList<QToolButton*> titleButtons =
        window.findChildren<QToolButton *>(QStringLiteral("titleBarButton"));
    for (QToolButton *button : titleButtons)
    {
        if (!button)
        {
            continue;
        }
        const QString toolTip = button->toolTip();
        foundMapTitleButton = foundMapTitleButton ||
            toolTip == QStringLiteral("打开三维地图") ||
            toolTip == QStringLiteral("Open 3D map");
        foundDiagnosticsTitleButton = foundDiagnosticsTitleButton ||
            toolTip == QStringLiteral("打开三维地图数据诊断") ||
            toolTip == QStringLiteral("Open 3D map data diagnostics");
    }
    require(foundMapTitleButton, "title bar exposes the 3D map action");
    require(foundDiagnosticsTitleButton, "title bar exposes the map data diagnostics action");
}

#else
void requireMainWindowOmitsMap3DEntries(MainWindow& window)
{
    QAction *mapAction = findActionByText(&window,
                                          {QStringLiteral("三维地图..."),
                                           QStringLiteral("3D Map...")});
    QAction *diagnosticsAction = findActionByText(&window,
                                                  {QStringLiteral("地图数据诊断..."),
                                                   QStringLiteral("Map Data Diagnostics...")});
    require(mapAction == nullptr, "default OFF build omits the 3D map action");
    require(diagnosticsAction == nullptr, "default OFF build omits the map data diagnostics action");

    const QList<QToolButton*> titleButtons =
        window.findChildren<QToolButton *>(QStringLiteral("titleBarButton"));
    for (QToolButton *button : titleButtons)
    {
        if (!button)
        {
            continue;
        }
        const QString toolTip = button->toolTip();
        require(toolTip != QStringLiteral("打开三维地图") &&
                    toolTip != QStringLiteral("Open 3D map"),
                "default OFF build omits the 3D map title-bar button");
        require(toolTip != QStringLiteral("打开三维地图数据诊断") &&
                    toolTip != QStringLiteral("Open 3D map data diagnostics"),
                "default OFF build omits the map data diagnostics title-bar button");
    }
}
#endif

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
        settings.setValue(QStringLiteral("font_scale_percent"), 100);
#ifdef Q_OS_WIN
        settings.setValue(QStringLiteral("serial/temperature_port"), QStringLiteral("COM9"));
#else
        settings.setValue(QStringLiteral("serial/temperature_port"), QStringLiteral("/dev/ttyRD105"));
#endif
        settings.setValue(QStringLiteral("dark_theme_enabled"), false);
        settings.setValue(QStringLiteral("serial/temperature_baud"), QStringLiteral("38400"));
        settings.setValue(QStringLiteral("rate/temperature"), QStringLiteral("5"));
        settings.setValue(QStringLiteral("source/mode"), QStringLiteral("remote"));
        settings.setValue(QStringLiteral("sensor/pressure_source"), QStringLiteral("bmp390"));
        settings.setValue(QStringLiteral("serial/bmp390_baud"), QStringLiteral("57600"));
        settings.setValue(QStringLiteral("sensor/humidity_source"), QStringLiteral("sht45"));
        settings.setValue(QStringLiteral("serial/sht45_baud"), QStringLiteral("38400"));
        settings.sync();
    }

    {
        MainWindow rememberedModeWindow;
        rememberedModeWindow.resize(1000, 700);
        rememberedModeWindow.show();
        processEventsFor(300);
        QComboBox *rememberedSourceModeCombo = findSourceModeCombo(&rememberedModeWindow);
        require(rememberedSourceModeCombo != nullptr,
                "remembered source mode combo exists on startup");
        require(rememberedSourceModeCombo->itemText(1) == QStringLiteral("天地远程模式") ||
                    rememberedSourceModeCombo->itemText(1) == QStringLiteral("Sky-Ground Remote Mode"),
                "source mode combo uses the new remote-mode label");
        require(rememberedSourceModeCombo->property("usesSingleLevelPopupMenu").toBool(),
                "source mode combo uses the shared single-level popup");
        require(rememberedSourceModeCombo->currentIndex() == 1,
                "source mode restores the last remote selection on startup");
        QComboBox *rememberedPressureSource =
            findComboWithData(&rememberedModeWindow, QStringLiteral("bmp390"));
        QComboBox *rememberedHumiditySource =
            findComboWithData(&rememberedModeWindow, QStringLiteral("sht45"));
        auto *rememberedPressureBaud =
            rememberedModeWindow.findChild<QComboBox *>(QStringLiteral("devicePressureBaudCombo"));
        auto *rememberedHumidityBaud =
            rememberedModeWindow.findChild<QComboBox *>(QStringLiteral("deviceHumidityBaudCombo"));
        require(rememberedPressureSource != nullptr &&
                    rememberedPressureSource->currentData().toString() == QStringLiteral("bmp390") &&
                    rememberedPressureBaud != nullptr &&
                    rememberedPressureBaud->currentText() == QStringLiteral("57600"),
                "pressure source restores the remembered BMP390 baud rate on startup");
        require(rememberedHumiditySource != nullptr &&
                    rememberedHumiditySource->currentData().toString() == QStringLiteral("sht45") &&
                    rememberedHumidityBaud != nullptr &&
                    rememberedHumidityBaud->currentText() == QStringLiteral("38400"),
                "humidity source restores the remembered SHT45 baud rate on startup");
        rememberedModeWindow.close();
        processEventsFor(100);
    }

    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        settings.setValue(QStringLiteral("source/mode"), QStringLiteral("local"));
        settings.setValue(QStringLiteral("sensor/pressure_source"), QStringLiteral("ptb210"));
        settings.setValue(QStringLiteral("serial/ptb_baud"), QStringLiteral("9600"));
        settings.setValue(QStringLiteral("sensor/humidity_source"), QStringLiteral("hmp3"));
        settings.setValue(QStringLiteral("serial/hmp_baud"), QStringLiteral("19200"));
        settings.remove(QStringLiteral("serial/ptb210_baud"));
        settings.remove(QStringLiteral("serial/bmp390_baud"));
        settings.remove(QStringLiteral("serial/hmp3_baud"));
        settings.remove(QStringLiteral("serial/sht45_baud"));
        settings.sync();
    }

    MainWindow window;
    window.setWindowTitle(QStringLiteral("VaporView"));
    window.resize(1280, 800);
    window.show();
    processEventsFor(500);
#ifdef VAPORVIEW_HAS_OSGEARTH
    requireMainWindowMap3DEntries(window);
#else
    requireMainWindowOmitsMap3DEntries(window);
#endif
    require(qApp->styleSheet().contains(QStringLiteral("square.svg")) &&
                qApp->styleSheet().contains(QStringLiteral("square-check-big.svg")) &&
                !qApp->styleSheet().contains(QStringLiteral("lucide/check.svg")),
            "checkbox indicators use lucide square and square-check-big icons");
    requireMenuPopupStyleUnified(qApp->styleSheet(),
                                 false,
                                 "light popup menus use the shared menu hover and rounded panel style");
    requireComboPopupsStyledIn(&window,
                               "all main-window combo boxes use the shared rounded popup menu style");
    const QSize originalWindowSize = window.size();

    auto *appLayoutSplitter = window.findChild<QSplitter *>(QStringLiteral("appLayoutSplitter"));
    require(appLayoutSplitter != nullptr, "app layout splitter exists");
    require(window.centralWidget() != nullptr, "central widget exists");
    require(appLayoutSplitter->geometry().bottom() >= window.centralWidget()->contentsRect().bottom() - 1,
            "main content reaches the status bar without a bottom gap");
    auto *mainPageStackForScroll = window.findChild<QStackedWidget *>(QStringLiteral("mainPageStack"));
    require(mainPageStackForScroll != nullptr, "main page stack exists for home scroll check");
    auto *homeScrollArea = qobject_cast<QScrollArea *>(mainPageStackForScroll->currentWidget());
    require(homeScrollArea != nullptr, "home scroll area exists");
    processEventsFor(250);
    if (homeScrollArea->horizontalScrollBar()->maximum() != 0)
    {
        auto *homeContent = homeScrollArea->widget();
        auto *overviewSplitter = window.findChild<QSplitter *>(QStringLiteral("homeOverviewSplitter"));
        auto *sensorRow = window.findChild<QWidget *>(QStringLiteral("sensorRowContainer"));
        std::cerr << "Home horizontal overflow: max="
                  << homeScrollArea->horizontalScrollBar()->maximum()
                  << " viewport=" << homeScrollArea->viewport()->width()
                  << " contentWidth=" << (homeContent ? homeContent->width() : 0)
                  << " contentMin=" << (homeContent ? homeContent->minimumWidth() : 0)
                  << " overviewWidth=" << (overviewSplitter ? overviewSplitter->width() : 0)
                  << " overviewMin=" << (overviewSplitter ? overviewSplitter->minimumWidth() : 0)
                  << " overviewHint=" << (overviewSplitter ? overviewSplitter->sizeHint().width() : 0)
                  << " sensorWidth=" << (sensorRow ? sensorRow->width() : 0)
                  << " sensorMinHint=" << (sensorRow ? sensorRow->minimumSizeHint().width() : 0)
                  << '\n';
    }
    require(homeScrollArea->horizontalScrollBar()->maximum() == 0,
            "home page does not show a horizontal scrollbar in the default window");

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
    require(checkedSidebarButton->focusPolicy() == Qt::NoFocus,
            "compact sidebar selected icon does not draw a keyboard focus frame");
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
    auto *customTitleLabel = window.findChild<QLabel *>(QStringLiteral("customTitleLabel"));
    requireLabelTextOneOf(customTitleLabel,
                          {QStringLiteral("首页"), QStringLiteral("Home")},
                          "custom title bar starts with the selected home page title");
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
    QToolButton *hoverTitleButton = nullptr;
    const QList<QToolButton*> mainTitleButtons =
        window.findChildren<QToolButton *>(QStringLiteral("titleBarButton"));
    for (QToolButton *button : mainTitleButtons)
    {
        if (button && button->isVisible() && button->isEnabled() &&
            button->property("_vv_title_bar_hover_button").toBool())
        {
            hoverTitleButton = button;
            break;
        }
    }
    require(hoverTitleButton != nullptr, "main title bar hover participant exists");
    hoverWidget(hoverTitleButton, true);
    require(hoverTitleButton->property("titleBarHover").toBool(),
            "main title bar button hover property is enabled by enter");
    hoverWidget(hoverTitleButton, false);
    require(!hoverTitleButton->property("titleBarHover").toBool(),
            "main title bar button hover property is cleared by leave");

    auto *titleMenuButton = window.findChild<QToolButton *>(QStringLiteral("titleBarMenuButton"));
    require(titleMenuButton != nullptr, "title bar application menu button exists");
    clickWidget(titleMenuButton, 120);
    auto *titleApplicationPanel = window.findChild<QFrame *>(QStringLiteral("titleApplicationPanel"));
    requireTitleMenuFloatingPanel(titleApplicationPanel,
                                  "title bar application menu uses the shared floating rounded shadow chrome");
    auto *titleApplicationMainMenu =
        titleApplicationPanel->findChild<QFrame *>(QStringLiteral("titleApplicationMainMenu"));
    require(titleApplicationMainMenu != nullptr,
            "title bar application menu content is inside the floating panel");
    require(titleApplicationMainMenu->geometry().left() >= titleApplicationPanel->property("shadowMargin").toInt() &&
                titleApplicationMainMenu->geometry().top() >= titleApplicationPanel->property("shadowMargin").toInt(),
            "title bar application menu content is inset inside the shadow margin");
    const QList<QFrame*> titleApplicationRows =
        titleApplicationMainMenu->findChildren<QFrame *>(QStringLiteral("titleApplicationMenuItem"));
    require(!titleApplicationRows.isEmpty(),
            "title bar application menu exposes hoverable root rows");
    requireMenuRowsRespectRoundedVerticalPadding(
        titleApplicationPanel,
        titleApplicationMainMenu,
        titleApplicationRows,
        "title bar application main menu rows stay inside rounded vertical padding");
    auto *rootArrowLabel =
        titleApplicationRows.first()->findChild<QLabel *>(QStringLiteral("titleApplicationMenuArrow"));
    auto *rootTextLabel =
        titleApplicationRows.first()->findChild<QLabel *>(QStringLiteral("titleApplicationMenuText"));
    require(rootArrowLabel != nullptr && rootTextLabel != nullptr &&
                rootArrowLabel->property("usesLucideChevron").toBool() &&
                rootArrowLabel->property("iconSize").toInt() > rootTextLabel->font().pixelSize() &&
                !rootArrowLabel->pixmap().isNull(),
            "title bar application submenu chevron uses a larger lucide icon");
    const int rootArrowCenterDelta =
        std::abs(rootArrowLabel->geometry().center().y() - titleApplicationRows.first()->rect().center().y());
    require(rootArrowCenterDelta <= 1,
            "title bar application submenu chevron is vertically centered in the menu row");
    hoverWidget(titleApplicationRows.first(), true, 220);
    auto *titleApplicationSubPanel = window.findChild<QFrame *>(QStringLiteral("titleApplicationSubPanel"));
    requireTitleMenuFloatingPanel(titleApplicationSubPanel,
                                  "title bar application submenu uses the shared floating rounded shadow chrome");
    require(titleApplicationSubPanel->isVisible(),
            "title bar application submenu opens from a root row hover");
    auto *titleApplicationSubMenu =
        titleApplicationSubPanel->findChild<QFrame *>(QStringLiteral("titleApplicationSubMenu"));
    require(titleApplicationSubMenu != nullptr,
            "title bar application submenu content is inside the floating panel");
    require(titleApplicationSubMenu->geometry().left() >= titleApplicationSubPanel->property("shadowMargin").toInt() &&
                titleApplicationSubMenu->geometry().top() >= titleApplicationSubPanel->property("shadowMargin").toInt(),
            "title bar application submenu content is inset inside the shadow margin");

    QFrame *nestedRootRow = nullptr;
    for (QFrame *row : titleApplicationRows)
    {
        const QList<QLabel*> labels = row->findChildren<QLabel *>(QStringLiteral("titleApplicationMenuText"));
        for (const QLabel *label : labels)
        {
            if (label && (label->text() == QStringLiteral("开发者") ||
                          label->text() == QStringLiteral("Developer")))
            {
                nestedRootRow = row;
                break;
            }
        }
        if (nestedRootRow)
        {
            break;
        }
    }
    require(nestedRootRow != nullptr,
            "title bar application menu exposes a root row with nested commands");
    hoverWidget(nestedRootRow, true, 220);
    const QSize subPanelSizeBeforeNested = titleApplicationSubPanel->size();
    QRect subContentGlobalBefore(titleApplicationSubMenu->mapToGlobal(QPoint(0, 0)),
                                 titleApplicationSubMenu->size());

    QFrame *nestedCommandRow = nullptr;
    const QList<QFrame*> subRows =
        titleApplicationSubMenu->findChildren<QFrame *>(QStringLiteral("titleApplicationMenuItem"));
    requireMenuRowsRespectRoundedVerticalPadding(
        titleApplicationSubPanel,
        titleApplicationSubMenu,
        subRows,
        "title bar application submenu rows stay inside rounded vertical padding");
    for (QFrame *row : subRows)
    {
        const QList<QLabel*> labels = row->findChildren<QLabel *>(QStringLiteral("titleApplicationMenuText"));
        for (const QLabel *label : labels)
        {
            if (label && (label->text() == QStringLiteral("设备CSV记录频率") ||
                          label->text() == QStringLiteral("Device CSV recording rate")))
            {
                nestedCommandRow = row;
                break;
            }
        }
        if (nestedCommandRow)
        {
            break;
        }
    }
    require(nestedCommandRow != nullptr,
            "title bar application submenu exposes a row with tertiary commands");
    hoverWidget(nestedCommandRow, true, 220);

    auto *titleApplicationNestedPanel = window.findChild<QFrame *>(QStringLiteral("titleApplicationNestedPanel"));
    requireTitleMenuFloatingPanel(titleApplicationNestedPanel,
                                  "title bar application nested submenu uses separate floating chrome");
    require(titleApplicationNestedPanel->isVisible(),
            "title bar application nested submenu opens from a submenu row hover");
    auto *titleApplicationNestedMenu =
        titleApplicationNestedPanel->findChild<QFrame *>(QStringLiteral("titleApplicationNestedMenu"));
    require(titleApplicationNestedMenu != nullptr,
            "title bar application nested submenu content is inside its own floating panel");
    const QList<QFrame*> nestedRows =
        titleApplicationNestedMenu->findChildren<QFrame *>(QStringLiteral("titleApplicationMenuItem"));
    requireMenuRowsRespectRoundedVerticalPadding(
        titleApplicationNestedPanel,
        titleApplicationNestedMenu,
        nestedRows,
        "title bar application nested submenu rows stay inside rounded vertical padding");
    require(titleApplicationNestedMenu->parentWidget() == titleApplicationNestedPanel,
            "title bar application nested submenu is not parented into the secondary panel");
    require(titleApplicationSubPanel->size() == subPanelSizeBeforeNested,
            "title bar application secondary panel size is independent from the tertiary panel");
    require(titleApplicationNestedPanel != titleApplicationSubPanel,
            "title bar application tertiary panel is a separate region");
    const QRect nestedContentGlobal(titleApplicationNestedMenu->mapToGlobal(QPoint(0, 0)),
                                    titleApplicationNestedMenu->size());
    require(nestedContentGlobal.left() < subContentGlobalBefore.right() &&
                nestedContentGlobal.left() > subContentGlobalBefore.left(),
            "title bar application tertiary panel overlaps the secondary panel edge for visual grouping");
    require(titleApplicationNestedMenu->height() > titleApplicationSubMenu->height(),
            "title bar application tertiary panel can be taller without stretching the secondary panel");
    const QSize nestedPanelSizeBeforeRepeatedHover = titleApplicationNestedPanel->size();
    const QRect nestedPanelGeometryBeforeRepeatedHover = titleApplicationNestedPanel->geometry();
    QFrame *firstNestedRowBeforeRepeatedHover = nestedRows.isEmpty() ? nullptr : nestedRows.first();
    hoverWidget(nestedCommandRow, true, 220);
    require(titleApplicationNestedPanel->isVisible(),
            "title bar application nested submenu remains visible while hovering its source row");
    require(titleApplicationNestedPanel->size() == nestedPanelSizeBeforeRepeatedHover &&
                titleApplicationNestedPanel->geometry() == nestedPanelGeometryBeforeRepeatedHover,
            "title bar application nested submenu is not rebuilt or moved on repeated source hover");
    const QList<QFrame*> nestedRowsAfterRepeatedHover =
        titleApplicationNestedMenu->findChildren<QFrame *>(QStringLiteral("titleApplicationMenuItem"));
    require(!nestedRowsAfterRepeatedHover.isEmpty() &&
                nestedRowsAfterRepeatedHover.first() == firstNestedRowBeforeRepeatedHover,
            "title bar application nested submenu keeps its row widgets on repeated source hover");
    titleApplicationPanel->hide();
    titleApplicationSubPanel->hide();
    titleApplicationNestedPanel->hide();
    processEventsFor(50);

    QToolButton *logFilterButton = nullptr;
    const QList<QToolButton*> titleBarButtons =
        window.findChildren<QToolButton *>(QStringLiteral("titleBarButton"));
    for (QToolButton *button : titleBarButtons)
    {
        if (button && (button->toolTip() == QStringLiteral("日志过滤") ||
                       button->toolTip() == QStringLiteral("Log filters")))
        {
            logFilterButton = button;
            break;
        }
    }
    require(logFilterButton != nullptr, "log filter title-bar button exists");
    clickWidget(logFilterButton, 180);
    VaporView::SingleLevelPopupMenu *logFilterMenu = nullptr;
    for (QWidget *topLevel : QApplication::topLevelWidgets())
    {
        auto *menu = qobject_cast<VaporView::SingleLevelPopupMenu *>(topLevel);
        if (menu && menu->isVisible() &&
            (menu->title() == QStringLiteral("日志过滤") ||
             menu->title() == QStringLiteral("Log Filters")))
        {
            logFilterMenu = menu;
            break;
        }
    }
    require(logFilterMenu != nullptr, "log filter menu uses the shared single-level popup");
    require(logFilterMenu->rows().size() == 4,
            "log filter menu exposes four filter rows");
    require(logFilterMenu->cornerRadius() == 10,
            "log filter menu uses the shared 10px popup corner radius");
    require(logFilterMenu->panelPadding() == 12,
            "log filter menu uses the shared 12px popup vertical padding");
    require(logFilterMenu->property("floatingPanelChrome").toBool(),
            "log filter menu uses floating popup chrome");
    require(logFilterMenu->property("shadowMargin").toInt() == 22,
            "log filter menu preserves floating popup shadow margin");
    require(logFilterMenu->styleSheet().contains(QStringLiteral("background-color: transparent; border: none; border-radius: 10px; padding: 12px 0px")),
            "log filter menu stylesheet reflects the shared 10px radius and 12px padding");
    for (VaporView::SingleLevelPopupMenuRow *row : logFilterMenu->rows())
    {
        require(row->property("textAlignment").toString() == QStringLiteral("left") &&
                    row->property("checkIconAlignment").toString() == QStringLiteral("right") &&
                    row->textLabel() != nullptr &&
                    row->checkLabel() != nullptr &&
                    row->checkLabel()->geometry().right() > row->textLabel()->geometry().right(),
                "log filter menu rows share text-left and check-right layout");
        const int shadowMargin = logFilterMenu->property("shadowMargin").toInt();
        require(row->geometry().left() <= shadowMargin + 1 &&
                    row->geometry().right() >= logFilterMenu->width() - shadowMargin - 3,
                "log filter menu hover background spans the full floating panel row width");
        const QFontMetrics rowTextMetrics(row->textLabel()->font());
        require(row->textLabel()->width() >= rowTextMetrics.horizontalAdvance(row->text()),
                "log filter menu row text is not clipped by the popup content width");
    }
    VaporView::SingleLevelPopupMenuRow *logFilterFirstRow = logFilterMenu->rows().first();
    hoverWidget(logFilterFirstRow, true, 40);
    require(logFilterFirstRow->property("hovered").toBool(),
            "log filter menu row records hover before selection");
    clickWidget(logFilterFirstRow, 120);
    require(!logFilterMenu->isVisible(),
            "log filter menu closes after selecting a filter row");
    processEventsFor(420);
    clickWidget(logFilterButton, 220);
    logFilterMenu = nullptr;
    for (QWidget *topLevel : QApplication::topLevelWidgets())
    {
        auto *menu = qobject_cast<VaporView::SingleLevelPopupMenu *>(topLevel);
        if (menu && menu->isVisible() &&
            (menu->title() == QStringLiteral("日志过滤") ||
             menu->title() == QStringLiteral("Log Filters")))
        {
            logFilterMenu = menu;
            break;
        }
    }
    require(logFilterMenu != nullptr,
            "log filter menu reopens after selecting a checked row");
    logFilterFirstRow = logFilterMenu->rows().first();
    require(logFilterFirstRow->isChecked() &&
                logFilterFirstRow->property("hasCheckIcon").toBool(),
            "selected log filter row reopens with only its check indicator");
    require(!logFilterFirstRow->property("hovered").toBool(),
            "selected log filter row does not keep stale hover highlight after reopening");
    logFilterMenu->hide();
    processEventsFor(50);
    hoverWidget(checkedSidebarButton, true);
    require(checkedSidebarButton->property("_vv_hover").toBool(),
            "sidebar button hover property is enabled by enter");
    hoverWidget(checkedSidebarButton, false);
    require(!checkedSidebarButton->property("_vv_hover").toBool(),
            "sidebar button hover property is cleared by leave");
    requireRtkSidebarPage(window, customTitleLabel);

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
    require(deviceOverviewCard->minimumWidth() >= 500,
            "device overview card keeps a practical minimum width");
    require(deviceOverviewCard->minimumWidth() < 580,
            "device overview card minimum width follows its telemetry content instead of a fixed wide floor");

    auto *homeConfigCard = deviceOverviewCard;
    require(homeConfigCard != nullptr, "home configuration card exists");
    const QRect homeConfigLocalRect = homeConfigCard->geometry();
    QComboBox *homeSourceModeCombo = findSourceModeCombo(homeConfigCard);
    require(homeSourceModeCombo != nullptr, "home source mode combo exists");
    require(homeSourceModeCombo->property("usesSingleLevelPopupMenu").toBool(),
            "home source mode combo uses the shared single-level popup");
    const SkyTelemetryRowWidgets homeSkyTelemetry = findSkyTelemetryRowWidgets(homeConfigCard);
    require(homeSkyTelemetry.transportCombo != nullptr,
            "home sky telemetry transport combo exists");
    requireSkyTelemetryTransportLabels(homeSkyTelemetry, false);
    homeSourceModeCombo->setCurrentIndex(1);
    processEventsFor(150);
    activateLayouts(&window);
    setSkyTelemetryTransport(homeSkyTelemetry.transportCombo, QStringLiteral("tcp"));
    processEventsFor(100);
    activateLayouts(&window);
    requireSkyTelemetryTcpMode(homeSkyTelemetry, false);
    setSkyTelemetryTransport(homeSkyTelemetry.transportCombo, QStringLiteral("serial"));
    processEventsFor(100);
    activateLayouts(&window);
    requireSkyTelemetrySerialMode(homeSkyTelemetry, false);
    setSkyTelemetryTransport(homeSkyTelemetry.transportCombo, QStringLiteral("tcp"));
    processEventsFor(100);
    activateLayouts(&window);
    requireSameRect(homeConfigCard->geometry(), homeConfigLocalRect, 2,
                    "home configuration card geometry is stable in sky-ground remote mode");
    homeSourceModeCombo->setCurrentIndex(0);
    processEventsFor(150);
    activateLayouts(&window);
    requireSameRect(homeConfigCard->geometry(), homeConfigLocalRect, 2,
                    "home configuration card geometry is stable after switching back to local mode");

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
    requireComboPopupStyled(temperaturePortCombo,
                            "temperature port combo uses the shared popup styling helper");
    requireComboPopupStyled(temperatureBaudCombo,
                            "temperature baud combo uses the shared popup styling helper");
    requireComboPopupStyled(temperatureRateCombo,
                            "temperature rate combo uses the shared popup styling helper");
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
    require(temperatureChannelButton->toolButtonStyle() == Qt::ToolButtonTextBesideIcon &&
                temperatureChannelButton->layoutDirection() == Qt::RightToLeft &&
                !temperatureChannelButton->icon().isNull(),
            "temperature overview channel selector uses a right-side lucide chevron icon");
    require(temperatureChannelButton->property("textAlignment").toString() == QStringLiteral("center") &&
                temperatureChannelButton->property("iconAlignment").toString() == QStringLiteral("right"),
            "temperature overview channel selector keeps its text centered with the chevron right-aligned");
    require(temperatureChannelButton->property("available").isValid() &&
                !temperatureChannelButton->property("available").toBool(),
            "temperature overview channel selector starts unavailable without controller data");
    require(!temperatureChannelButton->isEnabled(),
            "temperature overview channel selector is disabled without controller data");
    require(qApp->styleSheet().contains(QStringLiteral("QToolButton#temperatureOverviewChannelButton[available=\"false\"]")),
            "temperature overview channel selector has a gray unavailable state");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QToolButton#temperatureOverviewChannelButton::menu-indicator {"),
                                 QStringLiteral("image: none"),
                                 "temperature overview channel selector hides the default dropdown indicator");
    require(temperatureChannelButton->menu() != nullptr,
            "temperature overview channel selector menu exists");
    require(temperatureChannelButton->menu()->testAttribute(Qt::WA_TranslucentBackground) &&
                !temperatureChannelButton->menu()->testAttribute(Qt::WA_StyledBackground),
            "temperature overview channel menu uses the floating translucent popup chrome");
    require(temperatureChannelButton->menu()->actions().size() == 2,
            "temperature overview channel menu has two channel options");
    const QList<VaporView::SingleLevelPopupMenuRow*> temperatureChannelMenuRows =
        temperatureChannelButton->menu()->findChildren<VaporView::SingleLevelPopupMenuRow *>();
    require(temperatureChannelMenuRows.size() == 2,
            "temperature overview channel menu uses the shared single-level popup rows");
    int selectedTemperatureChannelMenuItems = 0;
    for (VaporView::SingleLevelPopupMenuRow *row : temperatureChannelMenuRows)
    {
        require(row->property("textAlignment").toString() == QStringLiteral("center") &&
                    row->property("checkIconAlignment").toString() == QStringLiteral("right"),
                "temperature overview channel menu item text is centered while check icon is right-aligned");
        require(row->textLabel() != nullptr && row->checkLabel() != nullptr,
                "temperature overview channel menu row exposes text and check slots");
        require(row->textLabel()->alignment() == Qt::AlignCenter,
                "temperature overview channel menu text label is centered");
        require(row->checkLabel()->geometry().right() > row->textLabel()->geometry().right(),
                "temperature overview channel menu check slot is right-aligned");
        if (row->property("hasCheckIcon").toBool())
        {
            ++selectedTemperatureChannelMenuItems;
            require(row->isChecked(),
                    "temperature overview selected channel menu item shows a check icon");
        }
    }
    require(selectedTemperatureChannelMenuItems == 1,
            "temperature overview channel menu marks only the selected channel with a check icon");
    auto *temperatureChannelPopup = qobject_cast<VaporView::SingleLevelPopupMenu *>(temperatureChannelButton->menu());
    require(temperatureChannelPopup != nullptr &&
                temperatureChannelPopup->cornerRadius() == 10 &&
                temperatureChannelPopup->panelPadding() == 12,
            "temperature overview channel menu uses the shared single-level popup chrome");
    const int temperatureChannelShadowMargin = temperatureChannelPopup->property("shadowMargin").toInt();
    require(temperatureChannelShadowMargin == 22 &&
                temperatureChannelPopup->property("floatingPanelChrome").toBool(),
            "temperature overview channel menu uses the unified floating popup margins");
    require(temperatureChannelButton->menu()->minimumWidth() == temperatureChannelButton->width() + temperatureChannelShadowMargin * 2 &&
                temperatureChannelButton->menu()->maximumWidth() == temperatureChannelButton->width() + temperatureChannelShadowMargin * 2,
            "temperature overview channel menu reserves shadow space outside the capsule width");
    require(temperatureChannelPopup->styleSheet().contains(QStringLiteral("background-color: transparent; border: none; border-radius: 10px; padding: 12px 0px")),
            "temperature overview channel menu applies the shared floating popup style");
    temperatureChannelButton->menu()->popup(temperatureChannelButton->mapToGlobal(QPoint(0, temperatureChannelButton->height())));
    processEventsFor(50);
    require(!temperatureChannelButton->menu()->property("roundedMaskApplied").toBool() &&
                temperatureChannelButton->menu()->mask().isEmpty(),
            "temperature overview channel menu leaves rounded clipping to the floating popup painter");
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
        require(!pill->wordWrap(),
                "temperature overview value pill uses explicit two-line text without wrapping");
        require(pill->textFormat() == Qt::RichText &&
                    pill->text().contains(QStringLiteral("<br/>")) &&
                    pill->text().contains(QStringLiteral("px; font-weight: 700;\">---")),
                "temperature overview value pill uses rich text with an enlarged numeric row");
        require(pill->property("reservedValueText").toString() == QStringLiteral("999.99999") &&
                    pill->property("reservedValueFits").toBool(),
                "temperature overview value pill reserves width for 999.99999");
    }
    auto *temperatureOutputSwitch =
        window.findChild<QPushButton *>(QStringLiteral("temperatureOverviewOutputSwitch"));
    require(temperatureOutputSwitch != nullptr,
            "temperature overview output enable capsule exists");
    require(!temperatureOutputSwitch->isEnabled(),
            "temperature overview output enable capsule is disabled without controller data");
    auto *temperatureOverviewSummary =
        window.findChild<QWidget *>(QStringLiteral("temperatureOverviewSummary"));
    require(temperatureOverviewSummary != nullptr,
            "temperature overview summary column exists");
    require(temperatureOverviewSummary->layout() != nullptr,
            "temperature overview summary column has a layout");
    processEventsFor(50);
    activateLayouts(&window);
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QLabel#temperatureOverviewValuePill {"),
                                 QStringLiteral("font-size: 13px"),
                                 "temperature overview value pill font matches the other capsules");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QPushButton#temperatureOverviewOutputSwitch {"),
                                 QStringLiteral("font-size: 14px"),
                                 "temperature overview output switch font is enlarged for readability");
    const int temperatureSummarySpacing = temperatureOverviewSummary->layout()->spacing();
    int temperatureSummaryControlHeight =
        temperatureChannelButton->height() + temperatureOutputSwitch->height() +
        temperatureSummarySpacing * 3;
    for (QLabel *pill : temperatureValuePills)
    {
        temperatureSummaryControlHeight += pill->height();
        require(pill->height() >= 44,
                "temperature overview value capsules are taller than the old compact pills");
    }
    require(temperatureChannelButton->height() <= 38,
            "temperature overview channel selector is shorter than the value and output capsules");
    require(temperatureOutputSwitch->height() == 56,
            "temperature overview output enable capsule is restored to the previous compact height");
    require(std::abs(temperatureSummaryControlHeight - temperatureOverviewSummary->height()) <= 2,
            "temperature overview summary capsules fill the available card body height");

    const QList<QFrame*> homeTelemetryPills =
        deviceOverviewCard->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
    require(!homeTelemetryPills.isEmpty(),
            "home device overview telemetry pills exist before dark theme switch");
    const QString lightOverviewStyleSheet = qApp->styleSheet();
    requireLastStyleRuleContains(lightOverviewStyleSheet,
                                 QStringLiteral("QFrame#homeTelemetrySummaryPill {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::FieldBackground, false),
                                 "light theme keeps home telemetry summary pills on the normal white field background");
    require(!lightOverviewStyleSheet.contains(QStringLiteral("QFrame#homeTelemetrySummaryPill[hasData")),
            "home telemetry summary pills do not expose data-state background rules");
    for (QFrame *pill : homeTelemetryPills)
    {
        require(!pill->property("hasData").isValid(),
                "home telemetry summary pill does not carry data-state background property");
    }
    QWidget *homeTelemetrySummaryContainer =
        deviceOverviewCard->findChild<QWidget *>(QStringLiteral("homeTelemetrySummaryContainer"));
    require(homeTelemetrySummaryContainer != nullptr,
            "home device overview telemetry summary container exists before dark theme switch");
    QFrame *homeRateSection = firstTelemetrySection(homeTelemetrySummaryContainer);
    require(homeRateSection != nullptr,
            "home device overview telemetry sections exist before dark theme switch");
    const QList<QFrame*> homeTelemetrySections =
        sortedTelemetrySections(homeTelemetrySummaryContainer);
    require(homeTelemetrySections.size() >= 3,
            "home device overview telemetry summary has rate, link, and data sections");
    const QList<QFrame*> ratePills =
        homeRateSection->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
    require(!ratePills.isEmpty(),
            "home data-stream telemetry section has value pills");
    bool waveCaptureRateShowsZero = false;
    bool waveformTotalRateVisible = false;
    for (QFrame *pill : ratePills)
    {
        QLabel *nameLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel"));
        QLabel *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
        if (nameLabel && nameLabel->text().contains(QStringLiteral("波形总包")))
        {
            waveformTotalRateVisible = true;
        }
        if (nameLabel && valueLabel && nameLabel->text().contains(QStringLiteral("波形采集")))
        {
            require(valueLabel->text() == QStringLiteral("0.0 Hz"),
                    "home wave capture rate uses the same zero-frequency text as other rates");
            waveCaptureRateShowsZero = true;
        }
    }
    require(!waveformTotalRateVisible,
            "home data-stream telemetry section hides the waveform total packet rate");
    require(waveCaptureRateShowsZero,
            "home data-stream telemetry section exposes the wave capture rate");
    QFrame *featureRatePill = findTelemetryPillByName(homeRateSection, QStringLiteral("特征值"));
    QFrame *statusRatePill = findTelemetryPillByName(homeRateSection, QStringLiteral("状态"));
    QFrame *rawWaveRatePill = findTelemetryPillByName(homeRateSection, QStringLiteral("原始波形"));
    require(featureRatePill != nullptr && statusRatePill != nullptr && rawWaveRatePill != nullptr,
            "home data-stream telemetry section exposes feature, status, and raw-wave pills");
    const int featureRateY = featureRatePill->mapTo(homeRateSection, QPoint(0, 0)).y();
    const int statusRateY = statusRatePill->mapTo(homeRateSection, QPoint(0, 0)).y();
    const int rawWaveRateY = rawWaveRatePill->mapTo(homeRateSection, QPoint(0, 0)).y();
    const QRect featureRateRect(featureRatePill->mapTo(homeRateSection, QPoint(0, 0)), featureRatePill->size());
    const QRect statusRateRect(statusRatePill->mapTo(homeRateSection, QPoint(0, 0)), statusRatePill->size());
    require(std::abs(statusRateY - featureRateY) <= 2,
            "home status rate pill sits on the same line as the feature rate pill");
    require(statusRateRect.left() > featureRateRect.right(),
            "home status rate pill sits immediately after the feature rate pill");
    require(statusRateRect.left() - featureRateRect.right() <= 16,
            "home status rate pill is not pushed to the far right of the first telemetry line");
    require(rawWaveRateY > featureRateY,
            "home waveform rates move to the second data-stream line");
    requireTelemetryRightPadding(deviceOverviewCard,
                                 homeRateSection,
                                 "home data-stream telemetry row keeps right-side breathing room");
    QFrame *homeLinkSection = homeTelemetrySections.at(1);
    const QList<QFrame*> linkRatePills =
        homeLinkSection->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
    require(linkRatePills.size() == 3,
            "home link-rate telemetry section keeps three link pills");
    for (QFrame *pill : linkRatePills)
    {
        QLabel *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
        require(valueLabel != nullptr,
                "home link-rate pill has a value label");
        require(valueLabel->fontMetrics().horizontalAdvance(valueLabel->text()) <= valueLabel->width() + 1,
                "home link-rate value text fits its compact label");
        const int mbpsWidth = valueLabel->fontMetrics().horizontalAdvance(QStringLiteral("999.9 Mbps"));
        const int compactKbpsWidth = valueLabel->fontMetrics().horizontalAdvance(QStringLiteral("999.9kbps"));
        require(valueLabel->width() >= std::max(mbpsWidth, compactKbpsWidth),
                "home link-rate value label reserves room for 999.9 Mbps and 999.9kbps");
    }
    QFrame *homeDataSection = homeTelemetrySections.at(2);
    QLabel *homeDataTitle =
        homeDataSection->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryTitleLabel"));
    require(homeDataTitle != nullptr &&
                (homeDataTitle->text() == QStringLiteral("数据：") ||
                 homeDataTitle->text() == QStringLiteral("Data:")),
            "home data availability row starts with a data title");
    bool homeDataHasEpsilon = false;
    const QList<QFrame*> dataPills =
        homeDataSection->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
    require(dataPills.size() == 5,
            "home data availability row keeps five device pills");
    for (QFrame *pill : dataPills)
    {
        QLabel *nameLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel"));
        QLabel *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
        require(nameLabel != nullptr && valueLabel != nullptr,
                "home data availability pill has a name and compact value");
        if (nameLabel->text().contains(QStringLiteral("EPSILON")))
        {
            homeDataHasEpsilon = true;
        }
        const QString valueText = valueLabel->text();
        require(valueText == QStringLiteral("有") ||
                    valueText == QStringLiteral("无") ||
                    valueText == QStringLiteral("Yes") ||
                    valueText == QStringLiteral("No"),
                "home data availability values use compact yes/no text");
        require(valueText != QStringLiteral("有数据") &&
                    valueText != QStringLiteral("无数据"),
                "home data availability values omit the longer data suffix");
    }
    require(homeDataHasEpsilon,
            "home data availability row includes the EPSILON field");
    const int lightHomeTelemetrySummaryHeight = homeTelemetrySummaryContainer->height();
    int minHomeTelemetryPillHeight = std::numeric_limits<int>::max();
    for (QFrame *pill : homeTelemetryPills)
    {
        minHomeTelemetryPillHeight = std::min(minHomeTelemetryPillHeight, pill->height());
    }
    require(minHomeTelemetryPillHeight > 0,
            "home device overview telemetry pills have a measurable height");
    const bool startedDark =
        qApp->property(VaporView::kAppDarkThemeProperty).toBool();
    if (!startedDark)
    {
        require(QMetaObject::invokeMethod(&window, "onToggleTheme", Qt::DirectConnection),
                "main window can switch to dark theme for overview style checks");
        processEventsFor(150);
        activateLayouts(&window);
    }
    require(qApp->property(VaporView::kAppDarkThemeProperty).toBool(),
            "main window is in dark theme for overview style checks");
    const QString darkOverviewStyleSheet = qApp->styleSheet();
    requireMenuPopupStyleUnified(darkOverviewStyleSheet,
                                 true,
                                 "dark popup menus use the shared menu hover and rounded panel style");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QFrame#homeTelemetrySummaryPill {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::FieldBackground, true),
                                 "dark theme keeps home telemetry summary pills on the normal surface background");
    require(!darkOverviewStyleSheet.contains(QStringLiteral("QFrame#homeTelemetrySummaryPill[hasData")),
            "dark theme has no data-state background rules for telemetry pills");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QFrame#deviceTelemetrySectionTitlePane {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceAlt, true),
                                 "dark theme overrides device telemetry section title pane background");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QLabel#temperatureOverviewValuePill {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceAlt, true),
                                 "dark theme overrides temperature overview value pill background");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QToolButton#temperatureOverviewChannelButton[available=\"false\"] {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceAlt, true),
                                 "dark theme overrides unavailable temperature channel selector background");
    for (QFrame *pill : deviceOverviewCard->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill")))
    {
        require(pill->height() >= minHomeTelemetryPillHeight,
                "home device overview telemetry pills do not shrink after switching to dark theme");
    }
    homeTelemetrySummaryContainer =
        deviceOverviewCard->findChild<QWidget *>(QStringLiteral("homeTelemetrySummaryContainer"));
    require(homeTelemetrySummaryContainer != nullptr,
            "home device overview telemetry summary container exists after dark theme switch");
    require(homeTelemetrySummaryContainer->height() >= lightHomeTelemetrySummaryHeight,
            "home device overview telemetry summary container does not shrink in dark theme");
    const QList<QFrame*> darkHomeTelemetrySections =
        homeTelemetrySummaryContainer->findChildren<QFrame *>(QStringLiteral("homeTelemetrySectionCard"));
    require(!darkHomeTelemetrySections.isEmpty(),
            "home device overview telemetry sections exist after dark theme switch");
    requireTelemetryRightPadding(deviceOverviewCard,
                                 firstTelemetrySection(homeTelemetrySummaryContainer),
                                 "dark home data-stream telemetry row keeps right-side breathing room");
    for (QFrame *section : darkHomeTelemetrySections)
    {
        requireChildInsideParent(section, homeTelemetrySummaryContainer, 0,
                                 "dark home telemetry section is not clipped by the summary container");
        const QList<QFrame*> sectionPills =
            section->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
        for (QFrame *pill : sectionPills)
        {
            requireChildInsideParent(pill, section, 0,
                                     "dark home telemetry pill is not clipped by its section");
            const QList<QLabel*> pillLabels = pill->findChildren<QLabel *>();
            for (QLabel *pillLabel : pillLabels)
            {
                if (pillLabel->objectName() != QStringLiteral("homeTelemetrySummaryNameLabel") &&
                    pillLabel->objectName() != QStringLiteral("homeTelemetrySummaryValueLabel"))
                {
                    continue;
                }
                requireChildInsideParent(pillLabel, pill, 1,
                                         "dark home telemetry label is not clipped by its pill");
            }
        }
    }
    if (!startedDark)
    {
        require(QMetaObject::invokeMethod(&window, "onToggleTheme", Qt::DirectConnection),
                "main window can switch back to light theme after overview style checks");
        processEventsFor(150);
        activateLayouts(&window);
        homeTelemetrySummaryContainer =
            deviceOverviewCard->findChild<QWidget *>(QStringLiteral("homeTelemetrySummaryContainer"));
        requireTelemetryRightPadding(deviceOverviewCard,
                                     firstTelemetrySection(homeTelemetrySummaryContainer),
                                     "home data-stream telemetry row keeps right-side breathing room after returning to light theme");
    }

    qRegisterMetaType<VaporView::TemperatureControllerData>("VaporView::TemperatureControllerData");
    VaporView::TemperatureControllerData validTemperatureData;
    validTemperatureData.valid = true;
    validTemperatureData.channels[0].target_temperature_c = 25.0;
    validTemperatureData.channels[0].measured_temperature_c = 24.75;
    validTemperatureData.channels[0].output_enabled = true;
    validTemperatureData.channels[0].output_mode = 0;
    validTemperatureData.channels[0].max_output_percent = 70;
    validTemperatureData.channels[0].kp = 10;
    validTemperatureData.channels[0].ki = 20;
    validTemperatureData.channels[0].kd = 30;
    validTemperatureData.channels[0].auto_pid_mode = 0;
    validTemperatureData.internal_temperature_c = 25.0;
    validTemperatureData.controller_mode = 0;
    validTemperatureData.device_address = 2;
    validTemperatureData.rs485_baud_index = 7;
    validTemperatureData.overtemp_output_mode = 0;
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
    bool sawTemperatureOverviewTargetValue = false;
    bool sawTemperatureOverviewCurrentValue = false;
    for (QLabel *pill : temperatureValuePills)
    {
        if (pill->text().contains(QStringLiteral("<br/>")) &&
            pill->text().contains(QStringLiteral("25.00000")) &&
            !pill->text().contains(QStringLiteral("25.00000℃")) &&
            pill->text().contains(QStringLiteral("目标温度℃")))
        {
            sawTemperatureOverviewTargetValue = true;
        }
        if (pill->text().contains(QStringLiteral("<br/>")) &&
            pill->text().contains(QStringLiteral("24.75000")) &&
            !pill->text().contains(QStringLiteral("24.75000℃")) &&
            pill->text().contains(QStringLiteral("当前温度℃")))
        {
            sawTemperatureOverviewCurrentValue = true;
        }
    }
    require(sawTemperatureOverviewTargetValue && sawTemperatureOverviewCurrentValue,
            "temperature overview value pills use title-over-value layout with five decimal places");

    auto *temperaturePanel = window.findChild<TemperatureControllerPanel *>();
    require(temperaturePanel != nullptr,
            "temperature controller panel exists for pending command refresh checks");
    auto *controllerModeCombo =
        temperaturePanel->findChild<QComboBox *>(QStringLiteral("temperatureControllerModeCombo"));
    QLabel *controllerModeLabel = nullptr;
    for (QLabel *label : temperaturePanel->findChildren<QLabel *>(QStringLiteral("fieldLabel")))
    {
        if (label->property("temperatureControllerModeLabel").toBool())
        {
            controllerModeLabel = label;
            break;
        }
    }
    auto *temperatureStatusRateLabel =
        temperaturePanel->findChild<QLabel *>(QStringLiteral("rateLabel"));
    auto *targetSpin =
        temperaturePanel->findChild<QDoubleSpinBox *>(QStringLiteral("temperatureTargetSpinChannel1"));
    auto *enableSwitch =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureOutputEnableSwitchChannel1"));
    auto *enableSwitch2 =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureOutputEnableSwitchChannel2"));
    auto *modeCombo =
        temperaturePanel->findChild<QComboBox *>(QStringLiteral("temperatureOutputModeComboChannel1"));
    auto *maxOutputSpin =
        temperaturePanel->findChild<QSpinBox *>(QStringLiteral("temperatureMaxOutputSpinChannel1"));
    auto *kpSpin =
        temperaturePanel->findChild<QSpinBox *>(QStringLiteral("temperaturePidKpSpinChannel1"));
    auto *kiSpin =
        temperaturePanel->findChild<QSpinBox *>(QStringLiteral("temperaturePidKiSpinChannel1"));
    auto *kdSpin =
        temperaturePanel->findChild<QSpinBox *>(QStringLiteral("temperaturePidKdSpinChannel1"));
    auto *autoPidCombo =
        temperaturePanel->findChild<QComboBox *>(QStringLiteral("temperatureAutoPidComboChannel1"));
    auto *addressSpin =
        temperaturePanel->findChild<QSpinBox *>(QStringLiteral("temperatureDeviceAddressSpin"));
    auto *rs485BaudCombo =
        temperaturePanel->findChild<QComboBox *>(QStringLiteral("temperatureRs485BaudCombo"));
    auto *overtempOutputCombo =
        temperaturePanel->findChild<QComboBox *>(QStringLiteral("temperatureOvertempOutputModeCombo"));
    auto *commonInternalTemperatureEdit =
        temperaturePanel->findChild<QLineEdit *>(QStringLiteral("temperatureCommonInternalTemperatureEdit"));
    auto *factoryResetButton =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureFactoryResetButton"));
    auto *sensorModelSelector1 =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureSensorModelSelectorChannel1"));
    auto *sensorModelBValueRadio =
        temperaturePanel->findChild<QRadioButton *>(QStringLiteral("temperatureSensorModelBValueRadioChannel1"));
    auto *sensorModelPtRadio =
        temperaturePanel->findChild<QRadioButton *>(QStringLiteral("temperatureSensorModelPtRadioChannel1"));
    auto *sensorModelShRadio =
        temperaturePanel->findChild<QRadioButton *>(QStringLiteral("temperatureSensorModelShRadioChannel1"));
    auto *sensorModelMf501Radio =
        temperaturePanel->findChild<QRadioButton *>(QStringLiteral("temperatureSensorModelMf501RadioChannel1"));
    require(controllerModeCombo != nullptr && controllerModeLabel != nullptr && temperatureStatusRateLabel != nullptr &&
                targetSpin != nullptr && enableSwitch != nullptr && enableSwitch2 != nullptr && modeCombo != nullptr &&
                maxOutputSpin != nullptr && kpSpin != nullptr && kiSpin != nullptr &&
                kdSpin != nullptr && autoPidCombo != nullptr &&
                addressSpin != nullptr && rs485BaudCombo != nullptr && overtempOutputCombo != nullptr &&
                commonInternalTemperatureEdit != nullptr && factoryResetButton != nullptr &&
                sensorModelSelector1 != nullptr && sensorModelBValueRadio != nullptr &&
                sensorModelPtRadio != nullptr && sensorModelShRadio != nullptr && sensorModelMf501Radio != nullptr,
            "temperature controller editable controls are discoverable for stale telemetry checks");
    require(sensorModelBValueRadio->property("temperatureSensorModelOption").toBool() &&
                sensorModelPtRadio->property("temperatureSensorModelOption").toBool() &&
                sensorModelShRadio->property("temperatureSensorModelOption").toBool() &&
                sensorModelMf501Radio->property("temperatureSensorModelOption").toBool() &&
                qApp->styleSheet().contains(
                    QStringLiteral("QRadioButton[temperatureSensorModelOption=\"true\"]::indicator")) &&
                qApp->styleSheet().contains(
                    QStringLiteral("QRadioButton[temperatureSensorModelOption=\"true\"]::indicator:checked")),
            "temperature sensor model options use square and square-check-big indicator styling");
    require(controllerModeCombo->property("usesSingleLevelPopupMenu").toBool() &&
                modeCombo->property("usesSingleLevelPopupMenu").toBool() &&
                autoPidCombo->property("usesSingleLevelPopupMenu").toBool() &&
                overtempOutputCombo->property("usesSingleLevelPopupMenu").toBool(),
            "temperature fixed-option combos use SingleLevelPopupMenu instead of native combo popups");
    require(!rs485BaudCombo->property("usesSingleLevelPopupMenu").toBool(),
            "temperature RS485 baud combo keeps the native combo popup path");

    clickWidget(temperatureNavButton, 150);
    activateLayouts(&window);
    auto *temperaturePageForLayout = window.findChild<QWidget *>(QStringLiteral("temperaturePage"));
    require(temperaturePageForLayout != nullptr && temperaturePageForLayout->isVisible(),
            "temperature page is visible for controller layout checks");
    auto *temperatureStatusLayout = qobject_cast<QGridLayout *>(
        temperaturePanel->layout() ? temperaturePanel->layout()->itemAt(0)->layout() : nullptr);
    auto statusLabelAt = [temperatureStatusLayout](int column) {
        return temperatureStatusLayout
            ? qobject_cast<QLabel *>(temperatureStatusLayout->itemAtPosition(0, column)->widget())
            : nullptr;
    };
    QLabel *internalTemperatureStatusLabel = statusLabelAt(0);
    QLabel *internalTemperatureStatusValue = statusLabelAt(1);
    QLabel *errorCodeStatusLabel = statusLabelAt(2);
    QLabel *errorCodeStatusValue = statusLabelAt(3);
    QLabel *pollingRateStatusLabel = statusLabelAt(4);
    QLabel *pollingRateStatusValue = statusLabelAt(5);
    require(internalTemperatureStatusLabel != nullptr && internalTemperatureStatusValue != nullptr &&
                errorCodeStatusLabel != nullptr && errorCodeStatusValue != nullptr &&
                pollingRateStatusLabel != nullptr && pollingRateStatusValue != nullptr,
            "temperature, error, and polling-rate fields exist in the top status row");
    require((internalTemperatureStatusValue->alignment() & Qt::AlignHorizontal_Mask) == Qt::AlignLeft &&
                (errorCodeStatusValue->alignment() & Qt::AlignHorizontal_Mask) == Qt::AlignLeft &&
                (pollingRateStatusValue->alignment() & Qt::AlignHorizontal_Mask) == Qt::AlignLeft,
            "temperature status values align toward their labels");
    require(internalTemperatureStatusLabel->width() < controllerModeLabel->width() &&
                errorCodeStatusLabel->width() < controllerModeLabel->width(),
            "temperature and error labels do not reserve controller-mode label width");
    require(internalTemperatureStatusValue->x() -
                    (internalTemperatureStatusLabel->x() + internalTemperatureStatusLabel->width()) <= 12 &&
                errorCodeStatusValue->x() -
                    (errorCodeStatusLabel->x() + errorCodeStatusLabel->width()) <= 12,
            "temperature and error values stay close to their labels");
    require(pollingRateStatusLabel->text() == QStringLiteral("轮询频率:") &&
                pollingRateStatusLabel->property("temperatureControllerRateTitle").toBool() &&
                pollingRateStatusValue == temperatureStatusRateLabel &&
                pollingRateStatusValue->property("temperatureControllerRateValue").toBool(),
            "temperature status row describes the Hz value as polling rate");
    require(pollingRateStatusLabel->x() -
                    (errorCodeStatusValue->x() + errorCodeStatusValue->width()) <= 12 &&
                pollingRateStatusValue->x() -
                    (pollingRateStatusLabel->x() + pollingRateStatusLabel->width()) <= 12,
            "polling-rate label and value follow the error code without stretch spacing");
    require(pollingRateStatusValue->fontMetrics().height() >
                errorCodeStatusValue->fontMetrics().height(),
            "temperature polling-rate value and Hz unit use a larger font");
    QLabel *temperatureControllerTitleLabel = nullptr;
    const QList<QLabel*> temperaturePageTitleLabels =
        temperaturePageForLayout->findChildren<QLabel *>(QStringLiteral("sectionTitleLabel"));
    for (QLabel *label : temperaturePageTitleLabels)
    {
        if (label->text().contains(QStringLiteral("RD105激光驱动板温控器")))
        {
            temperatureControllerTitleLabel = label;
            break;
        }
    }
#ifdef Q_OS_WIN
    const QString expectedTemperaturePortText = QStringLiteral("COM9");
#else
    const QString expectedTemperaturePortText = QStringLiteral("/dev/ttyRD105");
#endif
    require(temperatureControllerTitleLabel != nullptr &&
                temperatureControllerTitleLabel->text().contains(expectedTemperaturePortText) &&
                !temperatureControllerTitleLabel->text().contains(QStringLiteral("RD105温控器")),
            "temperature controller title names the laser driver board and shows the selected serial port");
    auto *temperatureTitleConnectButton =
        temperaturePageForLayout->findChild<QPushButton *>(QStringLiteral("temperatureTitleConnectButton"));
    auto *temperatureTitleDisconnectButton =
        temperaturePageForLayout->findChild<QPushButton *>(QStringLiteral("temperatureTitleDisconnectButton"));
    auto *temperatureTitleReconnectButton =
        temperaturePageForLayout->findChild<QPushButton *>(QStringLiteral("temperatureTitleReconnectButton"));
    require(temperatureTitleConnectButton != nullptr &&
                temperatureTitleDisconnectButton != nullptr &&
                temperatureTitleReconnectButton != nullptr,
            "temperature controller title-bar connection buttons are discoverable");
    require(temperatureTitleConnectButton->isEnabled() &&
                !temperatureTitleDisconnectButton->isEnabled() &&
                temperatureTitleReconnectButton->isEnabled(),
            "temperature controller title-bar connect/reconnect buttons are usable in local serial mode");
#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
    std::vector<VaporView::CommandId> temperatureTitleCommands;
    window.testSetLocalTemperatureCommandObserver([&temperatureTitleCommands](VaporView::CommandId command) {
        temperatureTitleCommands.push_back(command);
    });
    const bool temperatureConnectWasEnabled = temperatureTitleConnectButton->isEnabled();
    const bool temperatureDisconnectWasEnabled = temperatureTitleDisconnectButton->isEnabled();
    const bool temperatureReconnectWasEnabled = temperatureTitleReconnectButton->isEnabled();
    temperatureTitleConnectButton->setEnabled(true);
    temperatureTitleDisconnectButton->setEnabled(true);
    temperatureTitleReconnectButton->setEnabled(true);
    temperatureTitleConnectButton->click();
    temperatureTitleDisconnectButton->click();
    temperatureTitleReconnectButton->click();
    require(temperatureTitleCommands ==
                std::vector<VaporView::CommandId>{VaporView::CommandId::ConnectDevice,
                                                  VaporView::CommandId::DisconnectDevice,
                                                  VaporView::CommandId::ReconnectDevice},
            "temperature title buttons dispatch only local RD105 device commands");
    window.testSetLocalTemperatureCommandObserver({});
    temperatureTitleConnectButton->setEnabled(temperatureConnectWasEnabled);
    temperatureTitleDisconnectButton->setEnabled(temperatureDisconnectWasEnabled);
    temperatureTitleReconnectButton->setEnabled(temperatureReconnectWasEnabled);
#endif
    auto *temperatureConfigCard =
        temperaturePanel->findChild<QFrame *>(QStringLiteral("temperatureConfigCard"));
    auto *temperatureChannelTopRow =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureChannelTopRow"));
    auto *temperatureChannelSelectorRow =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureChannelSelectorRow"));
    auto *temperatureChannelTopBar =
        temperaturePanel->findChild<QFrame *>(QStringLiteral("temperatureChannelTopBar"));
    auto *temperatureChannelTopControlsStack =
        temperaturePanel->findChild<QStackedWidget *>(QStringLiteral("temperatureChannelTopControlsStack"));
    auto *temperatureChannelCommonTopControls1 =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureChannelCommonTopControlsChannel1"));
    auto *temperatureChannelStack =
        temperaturePanel->findChild<QStackedWidget *>(QStringLiteral("temperatureChannelStack"));
    auto *temperatureConfigChannelButton1 =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureChannelSelectorButton1"));
    auto *temperatureConfigChannelButton2 =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureChannelSelectorButton2"));
    auto *temperatureCommonSettingsButton =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureCommonSettingsButton"));
    QWidget *temperatureConfigPlot = nullptr;
    const QList<QWidget*> controllerTrendPlots =
        temperaturePanel->findChildren<QWidget *>(QStringLiteral("temperatureTrendPlot"));
    for (QWidget *plot : controllerTrendPlots)
    {
        if (plot->property("temperatureConfigPlot").toBool())
        {
            temperatureConfigPlot = plot;
            break;
        }
    }
    require(temperatureConfigCard != nullptr &&
                temperatureChannelTopRow != nullptr &&
                temperatureChannelSelectorRow != nullptr &&
                temperatureChannelTopBar != nullptr &&
                temperatureChannelTopControlsStack != nullptr &&
                temperatureChannelCommonTopControls1 != nullptr &&
                temperatureChannelStack != nullptr &&
                temperatureConfigChannelButton1 != nullptr &&
                temperatureConfigChannelButton2 != nullptr &&
                temperatureCommonSettingsButton != nullptr &&
                temperatureConfigPlot != nullptr,
            "temperature controller page exposes a top channel selector and a full-width trend plot");
    require(temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureConfigTabs")) == nullptr,
            "temperature controller no longer uses the native tab widget that drew the gray base bar");
    require(temperatureChannelTopRow->parentWidget() == temperatureConfigCard &&
                temperatureChannelSelectorRow->parentWidget() == temperatureChannelTopRow &&
                temperatureChannelTopBar->parentWidget() == temperatureChannelSelectorRow &&
                temperatureChannelTopControlsStack->parentWidget() == temperatureChannelSelectorRow &&
                temperatureChannelStack->parentWidget() == temperatureConfigCard,
            "temperature top selector row and page stack live in the internal config card");
    require(temperatureConfigChannelButton1->parentWidget() == temperatureChannelTopBar &&
                temperatureConfigChannelButton2->parentWidget() == temperatureChannelTopBar &&
                temperatureCommonSettingsButton->parentWidget() == temperatureChannelTopBar,
            "temperature channel and common settings buttons live in the top bar");
    require(temperatureConfigChannelButton1->property("temperatureChannelSelector").toBool() &&
                temperatureConfigChannelButton2->property("temperatureChannelSelector").toBool() &&
                temperatureCommonSettingsButton->property("temperatureChannelSelector").toBool(),
            "temperature channel buttons use the scoped selector style");
    require(temperatureConfigChannelButton1->isCheckable() &&
                temperatureConfigChannelButton2->isCheckable() &&
                temperatureCommonSettingsButton->isCheckable() &&
                temperatureConfigChannelButton1->isChecked() &&
                !temperatureConfigChannelButton2->isChecked() &&
                !temperatureCommonSettingsButton->isChecked() &&
                temperatureChannelTopControlsStack->currentIndex() == 0 &&
                temperatureChannelStack->currentIndex() == 0,
            "temperature channel top bar defaults to channel 1");
    require(temperaturePanel->minimumWidth() == 0 &&
                temperaturePanel->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored &&
                temperatureConfigCard->minimumWidth() == 0 &&
                temperatureConfigCard->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored,
            "temperature controller card width follows the available page width instead of the active page size hint");
    const QRect temperatureHeaderRateRect(temperatureStatusRateLabel->mapTo(temperaturePanel, QPoint(0, 0)),
                                          temperatureStatusRateLabel->size());
    const QRect controllerModeLabelRect(controllerModeLabel->mapTo(temperaturePanel, QPoint(0, 0)),
                                        controllerModeLabel->size());
    const QRect controllerModeComboRect(controllerModeCombo->mapTo(temperaturePanel, QPoint(0, 0)),
                                        controllerModeCombo->size());
    require(temperatureHeaderRateRect.right() < controllerModeLabelRect.left() &&
                controllerModeLabelRect.right() < controllerModeComboRect.left() &&
                std::abs(temperatureHeaderRateRect.center().y() - controllerModeLabelRect.center().y()) <= 2 &&
                std::abs(controllerModeLabelRect.center().y() - controllerModeComboRect.center().y()) <= 2,
            "temperature controller mode label and combo sit on the status row to the right of Hz");
    const QRect topRowRectInCard(temperatureChannelTopRow->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                 temperatureChannelTopRow->size());
    const QRect selectorRowRectInTopRow(temperatureChannelSelectorRow->mapTo(temperatureChannelTopRow, QPoint(0, 0)),
                                        temperatureChannelSelectorRow->size());
    const QRect topBarRectInRow(temperatureChannelTopBar->mapTo(temperatureChannelTopRow, QPoint(0, 0)),
                                temperatureChannelTopBar->size());
    const QRect stackRectInCard(temperatureChannelStack->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                 temperatureChannelStack->size());
    require(topRowRectInCard.bottom() < stackRectInCard.top() &&
                topRowRectInCard.left() <= stackRectInCard.left() + 1 &&
                topBarRectInRow.width() < stackRectInCard.width(),
            "temperature channel selector is a compact top bar above the config stack");
    require(temperatureChannelTopControlsStack->isVisible() &&
                temperatureChannelCommonTopControls1->isVisible() &&
                temperatureChannelTopControlsStack->isAncestorOf(sensorModelSelector1) &&
                !sensorModelSelector1->parentWidget()->isVisible() &&
                sensorModelBValueRadio->isChecked() &&
                !sensorModelPtRadio->isChecked() &&
                !sensorModelShRadio->isChecked() &&
                !sensorModelMf501Radio->isChecked(),
            "temperature common parameters show output controls and hide the four-option sensor model selector");
    require(temperatureConfigChannelButton1->x() < temperatureConfigChannelButton2->x() &&
                temperatureConfigChannelButton2->x() < temperatureCommonSettingsButton->x() &&
                std::abs(temperatureConfigChannelButton1->y() - temperatureConfigChannelButton2->y()) <= 1 &&
                std::abs(temperatureConfigChannelButton2->y() - temperatureCommonSettingsButton->y()) <= 1 &&
                temperatureConfigChannelButton1->height() == 34 &&
                temperatureConfigChannelButton2->height() == 34 &&
                temperatureCommonSettingsButton->height() == 34,
            "temperature channel top bar arranges compact channel buttons horizontally");
    require(temperaturePanel->findChild<QLabel *>(QStringLiteral("temperatureOutputEnableTopLabel")) == nullptr,
            "temperature output enable no longer has a separate top-row label");
    require(!temperatureChannelStack->isAncestorOf(enableSwitch) &&
                !temperatureChannelStack->isAncestorOf(enableSwitch2) &&
                temperatureChannelSelectorRow->isAncestorOf(enableSwitch) &&
                temperatureChannelSelectorRow->isAncestorOf(enableSwitch2) &&
                temperatureChannelTopControlsStack->isAncestorOf(enableSwitch) &&
                temperatureChannelTopControlsStack->isAncestorOf(enableSwitch2) &&
                !temperatureChannelTopBar->isAncestorOf(enableSwitch) &&
                !temperatureChannelTopBar->isAncestorOf(enableSwitch2) &&
                !temperatureChannelStack->isAncestorOf(modeCombo) &&
                !temperatureChannelStack->isAncestorOf(targetSpin) &&
                temperatureChannelTopControlsStack->isAncestorOf(modeCombo) &&
                temperatureChannelTopControlsStack->isAncestorOf(targetSpin) &&
                !temperatureChannelTopBar->isAncestorOf(modeCombo) &&
                !temperatureChannelTopBar->isAncestorOf(targetSpin),
            "temperature output enable, mode, and target sit beside the top channel selector");
    const QRect topCommonRect(temperatureCommonSettingsButton->mapTo(temperatureChannelTopRow, QPoint(0, 0)),
                              temperatureCommonSettingsButton->size());
    require(topCommonRect.right() < stackRectInCard.right(),
            "temperature common settings selector stays inside the compact top bar");
    require(temperatureChannelSelectorRow->isAncestorOf(factoryResetButton) &&
                !temperatureChannelStack->isAncestorOf(factoryResetButton) &&
                !temperatureChannelTopBar->isAncestorOf(factoryResetButton) &&
                !factoryResetButton->isVisible() &&
                !factoryResetButton->icon().isNull(),
            "temperature factory reset button sits beside common settings and starts hidden");
    require(enableSwitch->height() == 34 &&
                enableSwitch->width() == 106 &&
                enableSwitch2->height() == 34 &&
                enableSwitch2->width() == 106 &&
                enableSwitch->focusPolicy() == Qt::NoFocus,
            "temperature output enable uses compact left-right switch geometry without a focus frame");
    const int channel1StackHeight = temperatureChannelStack->height();
    const int channel1TopRowHeight = temperatureChannelTopRow->height();
    const int channel1ConfigCardHeight = temperatureConfigCard->height();
    clickWidget(temperatureConfigChannelButton2, 150);
    activateLayouts(&window);
    require(temperatureChannelTopControlsStack->currentIndex() == 1 &&
                temperatureChannelTopControlsStack->isVisible() &&
                temperatureChannelStack->currentIndex() == 1 &&
                std::abs(temperatureChannelStack->height() - channel1StackHeight) <= 1 &&
                std::abs(temperatureChannelTopRow->height() - channel1TopRowHeight) <= 1 &&
                std::abs(temperatureConfigCard->height() - channel1ConfigCardHeight) <= 1 &&
                !temperatureConfigChannelButton1->isChecked() &&
                temperatureConfigChannelButton2->isChecked() &&
                !temperatureCommonSettingsButton->isChecked(),
            "temperature channel top bar switches the channel page with common output controls visible");
    clickWidget(temperatureCommonSettingsButton, 150);
    activateLayouts(&window);
    const int commonStackHeight = temperatureChannelStack->height();
    require(temperatureChannelTopControlsStack->isVisible() &&
                temperatureChannelTopControlsStack->currentIndex() == 2 &&
                temperatureChannelStack->currentIndex() == 2 &&
                std::abs(commonStackHeight - channel1StackHeight) <= 1 &&
                std::abs(temperatureChannelTopRow->height() - channel1TopRowHeight) <= 1 &&
                std::abs(temperatureConfigCard->height() - channel1ConfigCardHeight) <= 1 &&
                factoryResetButton->isVisible() &&
                !temperatureConfigChannelButton1->isChecked() &&
                !temperatureConfigChannelButton2->isChecked() &&
                temperatureCommonSettingsButton->isChecked(),
            "temperature top bar switches to a common settings page with common top controls visible");
    const QRect commonButtonRectInSelector(temperatureCommonSettingsButton->mapTo(temperatureChannelSelectorRow, QPoint(0, 0)),
                                           temperatureCommonSettingsButton->size());
    const QRect factoryResetRectInSelector(factoryResetButton->mapTo(temperatureChannelSelectorRow, QPoint(0, 0)),
                                           factoryResetButton->size());
    const QRect commonAddressRowRect(temperatureChannelSelectorRow->mapFromGlobal(addressSpin->parentWidget()->mapToGlobal(QPoint(0, 0))),
                                     addressSpin->parentWidget()->size());
    const QRect commonBaudRowRect(temperatureChannelSelectorRow->mapFromGlobal(rs485BaudCombo->parentWidget()->mapToGlobal(QPoint(0, 0))),
                                  rs485BaudCombo->parentWidget()->size());
    const QRect commonBaudComboRectInCard(rs485BaudCombo->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                          rs485BaudCombo->size());
    require(factoryResetRectInSelector.left() > commonButtonRectInSelector.right() &&
                factoryResetRectInSelector.right() < commonAddressRowRect.left() &&
                commonAddressRowRect.right() < commonBaudRowRect.left() &&
                std::abs(factoryResetRectInSelector.center().y() - commonAddressRowRect.center().y()) <= 2 &&
                std::abs(commonAddressRowRect.center().y() - commonBaudRowRect.center().y()) <= 2,
            "temperature common address and baud controls sit to the right of factory reset");
    require(rs485BaudCombo->width() <= 100 &&
                commonBaudComboRectInCard.right() <= temperatureConfigCard->rect().right() - 12,
            "temperature common RS485 baud combo stays compact and leaves room for its right border");
    const QRect commonOvertempRowRect(overtempOutputCombo->parentWidget()->mapTo(temperatureChannelStack, QPoint(0, 0)),
                                      overtempOutputCombo->parentWidget()->size());
    const QRect commonInternalRowRect(commonInternalTemperatureEdit->parentWidget()->mapTo(temperatureChannelStack, QPoint(0, 0)),
                                      commonInternalTemperatureEdit->parentWidget()->size());
    require(std::abs(commonOvertempRowRect.top() - commonInternalRowRect.top()) <= 2 &&
                commonInternalRowRect.left() - commonOvertempRowRect.right() <= 14 &&
                commonInternalRowRect.bottom() <= temperatureChannelStack->rect().bottom(),
            "temperature common settings fields stay close and fit inside the stack without overlap or clipping");
    const QRect commonCardRectInPanel(temperatureConfigCard->mapTo(temperaturePanel, QPoint(0, 0)),
                                      temperatureConfigCard->size());
    const QRect commonPlotRectInPanel(temperatureConfigPlot->mapTo(temperaturePanel, QPoint(0, 0)),
                                      temperatureConfigPlot->size());
    require(commonCardRectInPanel.right() <= temperaturePanel->rect().right() - 12 &&
                std::abs(commonCardRectInPanel.left() - commonPlotRectInPanel.left()) <= 2 &&
                std::abs(commonCardRectInPanel.right() - commonPlotRectInPanel.right()) <= 2,
            "temperature common settings keep the controller card right edge visible and aligned with the plot");
    clickWidget(temperatureConfigChannelButton1, 150);
    activateLayouts(&window);
    require(temperatureChannelTopControlsStack->currentIndex() == 0 &&
                temperatureChannelTopControlsStack->isVisible() &&
                temperatureChannelStack->currentIndex() == 0 &&
                std::abs(temperatureChannelStack->height() - channel1StackHeight) <= 1 &&
                std::abs(temperatureChannelTopRow->height() - channel1TopRowHeight) <= 1 &&
                std::abs(temperatureConfigCard->height() - channel1ConfigCardHeight) <= 1 &&
                enableSwitch->isVisible() &&
                !enableSwitch2->isVisible() &&
                !factoryResetButton->isVisible() &&
                temperatureConfigChannelButton1->isChecked() &&
                !temperatureConfigChannelButton2->isChecked() &&
                !temperatureCommonSettingsButton->isChecked(),
            "temperature channel top bar can switch back to channel 1");
    const QRect cardRectInPanel(temperatureConfigCard->mapTo(temperaturePanel, QPoint(0, 0)),
                                temperatureConfigCard->size());
    const QRect plotRectInPanel(temperatureConfigPlot->mapTo(temperaturePanel, QPoint(0, 0)),
                                temperatureConfigPlot->size());
    require(cardRectInPanel.bottom() < plotRectInPanel.top(),
            "temperature trend plot is laid out below the channel configuration card");
    require(std::abs(cardRectInPanel.left() - plotRectInPanel.left()) <= 2 &&
                std::abs(cardRectInPanel.width() - plotRectInPanel.width()) <= 2,
            "temperature trend plot follows the full channel configuration card width");
    require(plotRectInPanel.width() >= temperaturePanel->width() - 32,
            "temperature trend plot expands to the controller panel width");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("TemperatureControllerPanel QFrame#temperatureConfigCard {"),
                                 QStringLiteral("border-radius: 8px"),
                                 "temperature channel controls are wrapped in an internal rounded card");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("TemperatureControllerPanel QFrame#temperatureChannelTopBar {"),
                                 QStringLiteral("border-radius: 8px"),
                                 "temperature channel selector uses a rounded top bar");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"] {"),
                                 QStringLiteral("background-color: transparent"),
                                 "temperature channel top bar buttons override the global primary button fill");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"]:checked {"),
                                 QStringLiteral("font-weight: 600"),
                                 "temperature channel top bar marks the selected channel without native tab chrome");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("TemperatureControllerPanel QFrame#temperatureChannelSubTopBar {"),
                                 QStringLiteral("border-radius: 8px"),
                                 "temperature lower parameter selector uses a rounded segmented bar");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("TemperatureControllerPanel QPushButton[temperatureChannelSubSelector=\"true\"] {"),
                                 QStringLiteral("background-color: transparent"),
                                 "temperature lower parameter selector buttons override the global primary button fill");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("TemperatureControllerPanel QPushButton[temperatureChannelSubSelector=\"true\"]:checked {"),
                                 QStringLiteral("font-weight: 600"),
                                 "temperature lower parameter selector marks the selected page like the top bar");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("TemperatureControllerPanel QPushButton[temperatureOutputEnableSwitch=\"true\"] {"),
                                 QStringLiteral("min-height: 34px"),
                                 "temperature output enable switch uses compact top-row painting");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QPushButton#appSidebarButton {"),
                                 QStringLiteral("outline: none"),
                                 "sidebar buttons suppress native dotted focus outlines");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("TemperatureControllerPanel QPushButton#temperatureFactoryResetButton {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::Danger, false),
                                 "temperature factory reset button is styled as a standalone danger action");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("TemperatureControllerPanel QLabel#fieldLabel[temperatureMaxOutputWarning=\"true\"] {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::Danger, false),
                                 "temperature max output label is marked red");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("TemperatureControllerPanel QSpinBox[temperatureMaxOutputWarning=\"true\"] {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::Danger, false),
                                 "temperature max output value is marked red");

    auto *temperatureScrollArea =
        temperaturePageForLayout->findChild<QScrollArea *>(QStringLiteral("mainCardsScrollArea"));
    require(temperatureScrollArea != nullptr &&
                temperatureScrollArea->horizontalScrollBar() != nullptr &&
                temperatureScrollArea->horizontalScrollBar()->maximum() == 0,
            "temperature configuration page fits horizontally without clipping");

    auto *temperatureChannelAdvancedParamsButton =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureChannelAdvancedParamsButton1"));
    auto *temperatureChannelSensorConfigButton =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureChannelSensorConfigButton1"));
    auto *temperatureChannelCommonParamsButton =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureChannelCommonParamsButton1"));
    auto *temperatureChannelConfigSubStack =
        temperaturePanel->findChild<QStackedWidget *>(QStringLiteral("temperatureChannelConfigSubStackChannel1"));
    require(temperatureChannelCommonParamsButton != nullptr &&
                temperatureChannelAdvancedParamsButton != nullptr &&
                temperatureChannelSensorConfigButton != nullptr &&
                temperatureChannelConfigSubStack != nullptr,
            "temperature channel exposes lower common, advanced, and sensor config tabs");
    require(temperatureChannelConfigSubStack->currentWidget() != nullptr &&
                temperatureChannelConfigSubStack->currentWidget()->objectName() ==
                    QStringLiteral("temperatureChannelCommonParamsPageChannel1") &&
                temperatureChannelCommonParamsButton->isChecked(),
            "temperature channel defaults to the lower common-params page");
    clickWidget(temperatureChannelAdvancedParamsButton, 150);
    activateLayouts(&window);
    require(temperatureChannelConfigSubStack->currentWidget() != nullptr &&
                temperatureChannelConfigSubStack->currentWidget()->objectName() ==
                    QStringLiteral("temperatureChannelAdvancedParamsPageChannel1") &&
                temperatureChannelAdvancedParamsButton->isChecked() &&
                !temperatureChannelTopControlsStack->isVisible(),
            "temperature lower advanced tab switches to the reserved empty page");
    clickWidget(temperatureChannelCommonParamsButton, 150);
    activateLayouts(&window);
    require(temperatureChannelConfigSubStack->currentWidget() != nullptr &&
                temperatureChannelConfigSubStack->currentWidget()->objectName() ==
                    QStringLiteral("temperatureChannelCommonParamsPageChannel1") &&
                temperatureChannelCommonParamsButton->isChecked() &&
                temperatureChannelTopControlsStack->isVisible() &&
                temperatureChannelCommonTopControls1->isVisible(),
            "temperature lower common tab switches back to channel controls");

    auto requireCompactChannelFieldLayout = [temperatureChannelConfigSubStack](QWidget *editor, const char *message) {
        require(editor != nullptr && editor->parentWidget() != nullptr, message);
        QWidget *row = editor->parentWidget();
        require(row->objectName() == QStringLiteral("temperatureConfigFieldRow"), message);
        require(temperatureChannelConfigSubStack->isAncestorOf(row), message);
        const QList<QLabel*> labels =
            row->findChildren<QLabel *>(QStringLiteral("fieldLabel"), Qt::FindDirectChildrenOnly);
        require(!labels.isEmpty(), message);
        const QRect labelRect(labels.first()->mapTo(row, QPoint(0, 0)), labels.first()->size());
        const QRect editorRect(editor->mapTo(row, QPoint(0, 0)), editor->size());
        require(labelRect.left() <= 1 &&
                    labelRect.right() < editorRect.left() &&
                    editorRect.left() - labelRect.right() <= 10,
                message);
    };
    requireCompactChannelFieldLayout(maxOutputSpin,
                                     "temperature max output field lives in the lower common-params page");
    requireCompactChannelFieldLayout(autoPidCombo,
                                     "temperature auto PID field lives in the lower common-params page");
    auto *pidEditor =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperaturePidEditorChannel1"));
    require(pidEditor != nullptr &&
                kpSpin->parentWidget() == pidEditor &&
                kiSpin->parentWidget() == pidEditor &&
                kdSpin->parentWidget() == pidEditor,
            "temperature PID controls are grouped as one right-side editor");
    auto requirePidTextFits = [](QSpinBox *spin, const char *message) {
        QLineEdit *lineEdit = spin ? spin->findChild<QLineEdit *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
        require(lineEdit != nullptr, message);
        QStyleOptionFrame lineEditOption;
        lineEditOption.initFrom(lineEdit);
        lineEditOption.rect = lineEdit->rect();
        const QRect textRect = lineEdit->style()->subElementRect(QStyle::SE_LineEditContents,
                                                                   &lineEditOption,
                                                                   lineEdit);
        const int textWidth = lineEdit->fontMetrics().horizontalAdvance(QStringLiteral("100"));
        require(textRect.width() >= textWidth, message);
    };
    requirePidTextFits(kpSpin,
                       "temperature PID spin boxes leave enough unobscured edit area for a three-digit value");
    requireCompactChannelFieldLayout(pidEditor,
                                     "temperature PID field lives in the lower common-params page");
    const QRect maxOutputRowRect(maxOutputSpin->parentWidget()->mapTo(temperatureChannelStack, QPoint(0, 0)),
                                 maxOutputSpin->parentWidget()->size());
    const QRect pidRowRect(pidEditor->parentWidget()->mapTo(temperatureChannelStack, QPoint(0, 0)),
                           pidEditor->parentWidget()->size());
    const QRect autoPidRowRect(autoPidCombo->parentWidget()->mapTo(temperatureChannelStack, QPoint(0, 0)),
                               autoPidCombo->parentWidget()->size());
    require(std::abs(maxOutputRowRect.top() - pidRowRect.top()) <= 2 &&
                std::abs(pidRowRect.top() - autoPidRowRect.top()) <= 2 &&
                maxOutputRowRect.right() < pidRowRect.left() &&
                pidRowRect.right() < autoPidRowRect.left(),
            "temperature lower common tab lays max output, PID, and auto PID on one row");
    require(maxOutputRowRect.bottom() <= temperatureChannelStack->rect().bottom() &&
                pidRowRect.bottom() <= temperatureChannelStack->rect().bottom() &&
                autoPidRowRect.bottom() <= temperatureChannelStack->rect().bottom(),
            "temperature channel fields fit inside the stack without clipping");
    require(maxOutputSpin->property("temperatureMaxOutputWarning").toBool(),
            "temperature max output value carries warning styling");
    const QList<QLabel*> maxOutputLabels =
        maxOutputSpin->parentWidget()->findChildren<QLabel *>(QStringLiteral("fieldLabel"), Qt::FindDirectChildrenOnly);
    const QColor warningTextColor = VaporView::appThemeColor(VaporView::AppThemeColor::Danger, false);
    require(!maxOutputLabels.isEmpty() &&
                maxOutputLabels.first()->text() == QStringLiteral("最大输出电压百分比(%)") &&
                maxOutputLabels.first()->property("temperatureMaxOutputWarning").toBool(),
            "temperature max output label is renamed and marked red");
    require(maxOutputSpin->palette().color(QPalette::Text) == warningTextColor,
            "temperature max output value palette is actually painted red");
    require(targetSpin->width() >= 170,
            "temperature target spin reserves enough width for five decimals");
    require(kpSpin->width() >= 80 &&
                kiSpin->width() >= 80 &&
                kdSpin->width() >= 80,
            "temperature PID spin boxes reserve visible value width");
    auto requireTopBarFieldLayout = [temperatureChannelTopControlsStack](QWidget *editor, const char *message) {
        require(editor != nullptr && editor->parentWidget() != nullptr, message);
        QWidget *row = editor->parentWidget();
        require(row->objectName() == QStringLiteral("temperatureTopBarField"), message);
        require(temperatureChannelTopControlsStack->isAncestorOf(row), message);
    };
    require(temperatureChannelTopControlsStack->isAncestorOf(modeCombo) &&
                temperatureChannelTopControlsStack->isAncestorOf(targetSpin),
            "temperature output mode and target temperature are in the top controls");
    requireTopBarFieldLayout(enableSwitch,
                             "temperature output enable switch lives beside the channel selectors");
    requireTopBarFieldLayout(modeCombo,
                             "temperature output mode lives beside the channel selectors");
    requireTopBarFieldLayout(targetSpin,
                             "temperature target temperature lives beside the channel selectors");
    requireTopBarFieldLayout(sensorModelSelector1,
                             "temperature sensor model radio selector lives in the channel top row");
    requireTopBarFieldLayout(addressSpin,
                             "temperature common RS485 address field remains in the common top row");
    requireTopBarFieldLayout(rs485BaudCombo,
                             "temperature common RS485 baud field remains in the common top row");
    auto requireCommonFieldRowLayout = [temperatureChannelStack](QWidget *editor, const char *message) {
        require(editor != nullptr && editor->parentWidget() != nullptr, message);
        QWidget *row = editor->parentWidget();
        require(row->objectName() == QStringLiteral("temperatureCommonFieldRow"), message);
        require(temperatureChannelStack->isAncestorOf(row), message);
        const QList<QLabel*> labels =
            row->findChildren<QLabel *>(QStringLiteral("fieldLabel"), Qt::FindDirectChildrenOnly);
        require(!labels.isEmpty(), message);
        const QRect labelRect(labels.first()->mapTo(row, QPoint(0, 0)), labels.first()->size());
        const QRect editorRect(editor->mapTo(row, QPoint(0, 0)), editor->size());
        require(labelRect.left() <= 1 &&
                    labelRect.width() <= labels.first()->sizeHint().width() + 2 &&
                    labelRect.right() < editorRect.left() &&
                    editorRect.left() - labelRect.right() <= 10 &&
                    editorRect.right() >= row->rect().right() - 1,
                message);
    };
    requireCommonFieldRowLayout(overtempOutputCombo,
                                "temperature common over-temperature output field uses left label and right value layout");
    requireCommonFieldRowLayout(commonInternalTemperatureEdit,
                                "temperature common internal temperature field uses left label and right value layout");

    clickWidget(temperatureChannelSensorConfigButton, 150);
    activateLayouts(&window);
    require(temperatureChannelConfigSubStack->currentWidget() != nullptr &&
                temperatureChannelConfigSubStack->currentWidget()->objectName() ==
                    QStringLiteral("temperatureChannelSensorConfigPageChannel1") &&
                temperatureChannelSensorConfigButton->isChecked() &&
                temperatureChannelTopControlsStack->isVisible() &&
                !temperatureChannelCommonTopControls1->isVisible() &&
                sensorModelSelector1->parentWidget()->isVisible(),
            "temperature sensor config tab switches to the sensor config page");

    auto *temperatureChannelSubTopBar =
        temperaturePanel->findChild<QFrame *>(QStringLiteral("temperatureChannelSubTopBar"));
    auto *ntcR0Edit =
        temperaturePanel->findChild<QLineEdit *>(QStringLiteral("temperatureNtcR0EditChannel1"));
    auto *ntcBEdit =
        temperaturePanel->findChild<QLineEdit *>(QStringLiteral("temperatureNtcBEditChannel1"));
    auto *ptR0Edit =
        temperaturePanel->findChild<QLineEdit *>(QStringLiteral("temperaturePtR0EditChannel1"));
    auto *ptAEdit =
        temperaturePanel->findChild<QLineEdit *>(QStringLiteral("temperaturePtAEditChannel1"));
    auto *ptBEdit =
        temperaturePanel->findChild<QLineEdit *>(QStringLiteral("temperaturePtBEditChannel1"));
    auto *ptCEdit =
        temperaturePanel->findChild<QLineEdit *>(QStringLiteral("temperaturePtCEditChannel1"));
    auto *polynomialA0Edit =
        temperaturePanel->findChild<QLineEdit *>(QStringLiteral("temperaturePolynomialA0EditChannel1"));
    require(temperatureChannelSubTopBar != nullptr &&
                ntcR0Edit != nullptr &&
                ntcBEdit != nullptr &&
                ptR0Edit != nullptr &&
                ptAEdit != nullptr &&
                ptBEdit != nullptr &&
                ptCEdit != nullptr &&
                polynomialA0Edit != nullptr,
            "temperature channel exposes per-channel sensor config controls");
    require(temperatureChannelSubTopBar->property("temperatureChannelSelector").isValid() == false,
            "temperature sensor sub-tabs do not reuse the top channel selector identity");
    require(temperatureChannelSelectorRow->isAncestorOf(sensorModelSelector1) &&
                !temperatureChannelConfigSubStack->currentWidget()->isAncestorOf(sensorModelSelector1),
            "temperature sensor model radio selector appears beside the channel selectors only for sensor config");
    require(temperatureChannelConfigSubStack->currentWidget()->findChildren<QComboBox *>().isEmpty() &&
                temperatureChannelConfigSubStack->currentWidget()->findChildren<QSpinBox *>().isEmpty() &&
                temperatureChannelConfigSubStack->currentWidget()->findChildren<QDoubleSpinBox *>().isEmpty(),
            "temperature sensor config page uses text inputs instead of dropdowns or spin boxes");
    auto requireCompactSensorFieldLayout = [](QWidget *editor, const char *message) {
        require(editor != nullptr && editor->parentWidget() != nullptr, message);
        QWidget *row = editor->parentWidget();
        require(row->objectName() == QStringLiteral("temperatureConfigFieldRow"), message);
        const QList<QLabel*> labels =
            row->findChildren<QLabel *>(QStringLiteral("fieldLabel"), Qt::FindDirectChildrenOnly);
        require(!labels.isEmpty(), message);
        const QRect labelRect(labels.first()->mapTo(row, QPoint(0, 0)), labels.first()->size());
        const QRect editorRect(editor->mapTo(row, QPoint(0, 0)), editor->size());
        auto *fieldLayout = qobject_cast<QHBoxLayout *>(row->layout());
        const int requiredRowWidth = labels.first()->width() + editor->width() + 6;
        const bool layoutIsValid = labelRect.left() <= 1 &&
            labelRect.right() < editorRect.left() &&
            fieldLayout != nullptr &&
            fieldLayout->spacing() == 6 &&
            row->minimumWidth() == requiredRowWidth &&
            row->maximumWidth() == requiredRowWidth;
        require(layoutIsValid, message);
    };
    requireCompactSensorFieldLayout(ntcR0Edit,
                                    "temperature NTC R0 field keeps label and input tightly grouped");
    requireCompactSensorFieldLayout(ntcBEdit,
                                    "temperature NTC B field keeps label and input tightly grouped");
    requireCompactSensorFieldLayout(ptR0Edit,
                                    "temperature PT R0 field keeps label and input tightly grouped");
    requireCompactSensorFieldLayout(polynomialA0Edit,
                                    "temperature polynomial field keeps label and input tightly grouped");
    auto sensorFieldLabel = [](QWidget *editor) -> QLabel * {
        return editor && editor->parentWidget()
            ? editor->parentWidget()->findChild<QLabel *>(QStringLiteral("fieldLabel"), Qt::FindDirectChildrenOnly)
            : nullptr;
    };
    require(sensorFieldLabel(ntcR0Edit) && sensorFieldLabel(ntcR0Edit)->text() == QStringLiteral("NTC R0(Ohm)") &&
                sensorFieldLabel(ptR0Edit) && sensorFieldLabel(ptR0Edit)->text() == QStringLiteral("PT R0(Ohm)") &&
                sensorFieldLabel(ptAEdit) && sensorFieldLabel(ptAEdit)->text() == QStringLiteral("PT A(E-3)") &&
                sensorFieldLabel(ptBEdit) && sensorFieldLabel(ptBEdit)->text() == QStringLiteral("PT B(E-7)") &&
                sensorFieldLabel(ptCEdit) && sensorFieldLabel(ptCEdit)->text() == QStringLiteral("PT C(E-12)"),
            "temperature compact sensor fields retain their unit and exponent annotations");
    auto sensorLabelHasTrailingPadding = [](QLabel *label, int padding) {
        return label != nullptr &&
            label->width() >= label->fontMetrics().boundingRect(label->text()).width() + padding;
    };
    require(sensorLabelHasTrailingPadding(sensorFieldLabel(ntcR0Edit), 16) &&
                sensorLabelHasTrailingPadding(sensorFieldLabel(ptR0Edit), 16) &&
                sensorLabelHasTrailingPadding(sensorFieldLabel(ptAEdit), 16) &&
                sensorLabelHasTrailingPadding(sensorFieldLabel(ptBEdit), 16) &&
                sensorLabelHasTrailingPadding(sensorFieldLabel(ptCEdit), 16),
            "temperature sensor labels reserve enough trailing width to render closing parentheses");
    require(ntcR0Edit->width() <= 82 &&
                ntcBEdit->width() <= 82 &&
                ptR0Edit->width() <= 82 &&
                ptAEdit->width() == 104 &&
                ptBEdit->width() == 104 &&
                ptCEdit->width() == 104,
            "temperature PT coefficient inputs show full precision while the other RD105 fields stay compact");
    auto *sensorConfigGrid = qobject_cast<QGridLayout *>(temperatureChannelConfigSubStack->currentWidget()->layout());
    auto requireSensorGridPosition = [sensorConfigGrid](QWidget *editor, int row, int column, const char *message) {
        require(sensorConfigGrid != nullptr && editor != nullptr && editor->parentWidget() != nullptr, message);
        QLayoutItem *item = sensorConfigGrid->itemAtPosition(row, column * 2);
        require(item != nullptr && item->widget() == editor->parentWidget(), message);
    };
    requireSensorGridPosition(ntcR0Edit, 0, 0, "temperature sensor grid places NTC R0 at row 1 column 1");
    requireSensorGridPosition(ptR0Edit, 0, 1, "temperature sensor grid places PT R0 at row 1 column 2");
    requireSensorGridPosition(ptAEdit, 0, 2, "temperature sensor grid places PT A at row 1 column 3");
    requireSensorGridPosition(ptBEdit, 0, 3, "temperature sensor grid places PT B at row 1 column 4");
    requireSensorGridPosition(ptCEdit, 0, 4, "temperature sensor grid places PT C at row 1 column 5");
    requireSensorGridPosition(ntcBEdit, 1, 0, "temperature sensor grid places NTC B at row 2 column 1");
    std::array<QLineEdit *, 8> polynomialEdits{};
    for (int coefficient = 0; coefficient < 8; ++coefficient)
    {
        auto *edit = temperaturePanel->findChild<QLineEdit *>(
            QStringLiteral("temperaturePolynomialA%1EditChannel1").arg(coefficient));
        polynomialEdits[static_cast<size_t>(coefficient)] = edit;
        require(edit != nullptr,
                "temperature polynomial inputs all exist");
        requireSensorGridPosition(edit,
                                  1 + coefficient / 4,
                                  1 + coefficient % 4,
                                  "temperature polynomial input follows the requested 2x4 grid");
    }
    auto fieldLeftInSensorPage = [temperatureChannelConfigSubStack](QWidget *editor) {
        return editor->parentWidget()->mapTo(temperatureChannelConfigSubStack->currentWidget(), QPoint(0, 0)).x();
    };
    auto inputLeftInSensorPage = [temperatureChannelConfigSubStack](QWidget *editor) {
        return editor->mapTo(temperatureChannelConfigSubStack->currentWidget(), QPoint(0, 0)).x();
    };
    auto requireSensorColumnAlignment = [&fieldLeftInSensorPage, &inputLeftInSensorPage](
                                            const QList<QWidget *>& fields,
                                            const char *message) {
        require(!fields.isEmpty() && fields.first() != nullptr, message);
        const int fieldLeft = fieldLeftInSensorPage(fields.first());
        const int inputLeft = inputLeftInSensorPage(fields.first());
        for (QWidget *field : fields)
        {
            require(field != nullptr &&
                        fieldLeftInSensorPage(field) == fieldLeft &&
                        inputLeftInSensorPage(field) == inputLeft &&
                        field->width() == fields.first()->width(),
                    message);
        }
    };
    requireSensorColumnAlignment({ntcR0Edit, ntcBEdit},
                                 "temperature sensor column 1 aligns labels and inputs");
    requireSensorColumnAlignment({ptR0Edit, polynomialEdits[0], polynomialEdits[4]},
                                 "temperature sensor column 2 aligns labels and inputs");
    requireSensorColumnAlignment({ptAEdit, polynomialEdits[1], polynomialEdits[5]},
                                 "temperature sensor column 3 aligns labels and inputs");
    requireSensorColumnAlignment({ptBEdit, polynomialEdits[2], polynomialEdits[6]},
                                 "temperature sensor column 4 aligns labels and inputs");
    requireSensorColumnAlignment({ptCEdit, polynomialEdits[3], polynomialEdits[7]},
                                 "temperature sensor column 5 aligns labels and inputs");
    const std::array<QWidget *, 5> firstRowFields{
        ntcR0Edit, ptR0Edit, ptAEdit, ptBEdit, ptCEdit};
    int adaptiveColumnGap = -1;
    for (size_t column = 1; column < firstRowFields.size(); ++column)
    {
        QWidget *previousCell = firstRowFields[column - 1]->parentWidget();
        QWidget *currentCell = firstRowFields[column]->parentWidget();
        const QRect previousRect(previousCell->mapTo(temperatureChannelConfigSubStack->currentWidget(), QPoint(0, 0)),
                                 previousCell->size());
        const QRect currentRect(currentCell->mapTo(temperatureChannelConfigSubStack->currentWidget(), QPoint(0, 0)),
                                currentCell->size());
        const int gap = currentRect.left() - previousRect.right() - 1;
        if (adaptiveColumnGap < 0)
        {
            adaptiveColumnGap = gap;
        }
        require(gap >= 0 && std::abs(gap - adaptiveColumnGap) <= 1,
                "temperature sensor grid distributes available card width evenly between columns");
    }
    require(sensorConfigGrid != nullptr &&
                sensorConfigGrid->horizontalSpacing() == 0 &&
                sensorConfigGrid->columnStretch(1) == 1 &&
                sensorConfigGrid->columnStretch(3) == 1 &&
                sensorConfigGrid->columnStretch(5) == 1 &&
                sensorConfigGrid->columnStretch(7) == 1,
            "temperature sensor grid uses four adaptive spacer columns");
    const QMargins temperatureChannelPageMargins =
        temperatureChannelStack->currentWidget()->layout()->contentsMargins();
    require(temperatureChannelPageMargins.left() == 0 &&
                temperatureChannelPageMargins.right() == 0,
            "temperature channel page releases horizontal margins for the five sensor columns");
    require(sensorConfigGrid != nullptr && sensorConfigGrid->itemAtPosition(2, 0) == nullptr,
            "temperature sensor grid leaves row 3 column 1 empty");
    auto *polynomialA7Edit = temperaturePanel->findChild<QLineEdit *>(
        QStringLiteral("temperaturePolynomialA7EditChannel1"));
    QWidget *sensorLastRow = polynomialA7Edit ? polynomialA7Edit->parentWidget() : nullptr;
    const QRect sensorFirstRowRect(ntcR0Edit->parentWidget()->mapTo(
                                       temperatureChannelConfigSubStack->currentWidget(), QPoint(0, 0)),
                                   ntcR0Edit->parentWidget()->size());
    const QRect sensorLastRowRect = sensorLastRow
        ? QRect(sensorLastRow->mapTo(temperatureChannelConfigSubStack->currentWidget(), QPoint(0, 0)),
                sensorLastRow->size())
        : QRect();
    const int sensorPageBottomUnusedHeight =
        temperatureChannelConfigSubStack->currentWidget()->height() - 1 - sensorLastRowRect.bottom();
    require(polynomialA7Edit != nullptr &&
                temperatureChannelStack->height() == 190 &&
                temperatureConfigCard->height() <= 280 &&
                sensorConfigGrid->alignment() == Qt::AlignTop &&
                sensorFirstRowRect.top() >= 0 &&
                sensorFirstRowRect.top() <= 4 &&
                sensorLastRowRect.bottom() < temperatureChannelConfigSubStack->currentWidget()->height() &&
                sensorPageBottomUnusedHeight >= 0,
            "temperature sensor grid starts close to the model row without clipping");
    require(temperatureChannelSelectorRow->isAncestorOf(factoryResetButton) &&
                !temperatureChannelStack->isAncestorOf(factoryResetButton),
            "temperature factory reset button lives beside the common settings selector");
    if (checkedSidebarButton && !checkedSidebarButton->isChecked())
    {
        clickWidget(checkedSidebarButton, 150);
        activateLayouts(&window);
    }

    {
        const QSignalBlocker controllerModeBlocker(controllerModeCombo);
        const QSignalBlocker targetBlocker(targetSpin);
        const QSignalBlocker modeBlocker(modeCombo);
        const QSignalBlocker maxOutputBlocker(maxOutputSpin);
        const QSignalBlocker kpBlocker(kpSpin);
        const QSignalBlocker kiBlocker(kiSpin);
        const QSignalBlocker kdBlocker(kdSpin);
        const QSignalBlocker autoPidBlocker(autoPidCombo);
        const QSignalBlocker addressBlocker(addressSpin);
        const QSignalBlocker rs485BaudBlocker(rs485BaudCombo);
        const QSignalBlocker overtempOutputBlocker(overtempOutputCombo);
        controllerModeCombo->setCurrentIndex(controllerModeCombo->findData(3));
        targetSpin->setValue(26.5);
        modeCombo->setCurrentIndex(modeCombo->findData(2));
        maxOutputSpin->setValue(80);
        kpSpin->setValue(11);
        kiSpin->setValue(22);
        kdSpin->setValue(33);
        autoPidCombo->setCurrentIndex(autoPidCombo->findData(1));
        addressSpin->setValue(9);
        rs485BaudCombo->setCurrentIndex(rs485BaudCombo->findData(5));
        overtempOutputCombo->setCurrentIndex(overtempOutputCombo->findData(1));
    }

    VaporView::TemperatureControllerCommand pendingCommand;
    pendingCommand.channel = 1;
    pendingCommand.controller_mode = 3;
    temperaturePanel->markCommandPending(VaporView::CommandId::SetTemperatureControllerMode, pendingCommand);
    pendingCommand.target_temperature_c = 26.5;
    temperaturePanel->markCommandPending(VaporView::CommandId::SetTemperatureTarget, pendingCommand);
    pendingCommand.output_mode = 2;
    temperaturePanel->markCommandPending(VaporView::CommandId::SetTemperatureOutputMode, pendingCommand);
    pendingCommand.max_output_percent = 80;
    temperaturePanel->markCommandPending(VaporView::CommandId::SetTemperatureMaxOutputPercent, pendingCommand);
    pendingCommand.kp = 11;
    pendingCommand.ki = 22;
    pendingCommand.kd = 33;
    temperaturePanel->markCommandPending(VaporView::CommandId::SetTemperaturePid, pendingCommand);
    pendingCommand.auto_pid_mode = 1;
    temperaturePanel->markCommandPending(VaporView::CommandId::SetTemperatureAutoPid, pendingCommand);
    pendingCommand.device_address = 9;
    temperaturePanel->markCommandPending(VaporView::CommandId::SetTemperatureDeviceAddress, pendingCommand);
    pendingCommand.rs485_baud_index = 5;
    temperaturePanel->markCommandPending(VaporView::CommandId::SetTemperatureRs485Baud, pendingCommand);
    pendingCommand.overtemp_output_mode = 1;
    temperaturePanel->markCommandPending(VaporView::CommandId::SetTemperatureOvertempOutputMode, pendingCommand);
    temperaturePanel->updateData(validTemperatureData);
    require(controllerModeCombo->currentData().toInt() == 3 &&
                std::abs(targetSpin->value() - 26.5) < 0.0001 &&
                modeCombo->currentData().toInt() == 2 &&
                maxOutputSpin->value() == 80 &&
                kpSpin->value() == 11 &&
                kiSpin->value() == 22 &&
                kdSpin->value() == 33 &&
                autoPidCombo->currentData().toInt() == 1 &&
                addressSpin->value() == 9 &&
                rs485BaudCombo->currentData().toInt() == 5 &&
                overtempOutputCombo->currentData().toInt() == 1,
            "pending temperature controller edits are not overwritten by stale telemetry values");

    validTemperatureData.controller_mode = 3;
    validTemperatureData.channels[0].target_temperature_c = 26.5;
    validTemperatureData.channels[0].output_mode = 2;
    validTemperatureData.channels[0].max_output_percent = 80;
    validTemperatureData.channels[0].kp = 11;
    validTemperatureData.channels[0].ki = 22;
    validTemperatureData.channels[0].kd = 33;
    validTemperatureData.channels[0].auto_pid_mode = 1;
    validTemperatureData.device_address = 9;
    validTemperatureData.rs485_baud_index = 5;
    validTemperatureData.overtemp_output_mode = 1;
    temperaturePanel->updateData(validTemperatureData);
    validTemperatureData.controller_mode = 0;
    validTemperatureData.channels[0].target_temperature_c = 25.0;
    validTemperatureData.channels[0].output_mode = 0;
    validTemperatureData.channels[0].max_output_percent = 70;
    validTemperatureData.channels[0].kp = 10;
    validTemperatureData.channels[0].ki = 20;
    validTemperatureData.channels[0].kd = 30;
    validTemperatureData.channels[0].auto_pid_mode = 0;
    validTemperatureData.device_address = 2;
    validTemperatureData.rs485_baud_index = 7;
    validTemperatureData.overtemp_output_mode = 0;
    temperaturePanel->updateData(validTemperatureData);
    processEventsFor(50);
    require(commonInternalTemperatureEdit->text() == QStringLiteral("25"),
            "temperature common settings page shows the controller internal temperature");

    const QList<QWidget*> temperatureTrendPlots =
        window.findChildren<QWidget *>(QStringLiteral("temperatureTrendPlot"));
    require(!temperatureTrendPlots.isEmpty(),
            "temperature trend plots exist after controller data arrives");
    for (QWidget *plot : temperatureTrendPlots)
    {
        require(plot->property("sampleCount").toInt() > 0,
                "temperature trend plot has samples after controller data arrives");
        require(plot->property("yAxisMinC").toDouble() == 23.0 &&
                    plot->property("yAxisMaxC").toDouble() == 27.0,
                "temperature trend plot centers the default axis range around the target temperature");
        require(plot->property("axisLabelsVisible").toBool(),
                "temperature trend plot exposes visible axis labels");
        require(plot->property("yAxisTickCount").toInt() == 7 &&
                    plot->property("xAxisTickCount").toInt() == 5,
                "temperature trend plot shows numeric ticks on both axes");
    }

    validTemperatureData.channels[0].measured_temperature_c = 12.0;
    require(QMetaObject::invokeMethod(&window,
                                      "onRemoteTemperatureControllerStatusUpdated",
                                      Qt::DirectConnection,
                                      Q_ARG(VaporView::TemperatureControllerData, validTemperatureData)),
            "temperature overview can receive a low controller data frame");
    processEventsFor(50);
    for (QWidget *plot : temperatureTrendPlots)
    {
        require(plot->property("yAxisMinC").toDouble() == 11.0 &&
                    plot->property("yAxisMaxC").toDouble() == 27.0,
                "temperature trend plot extends the lower axis only when data drops below the target-centered range");
    }
    VaporView::TelemetryStatus disconnectedTemperatureStatus;
    disconnectedTemperatureStatus.devices.push_back(
        VaporView::DeviceStatusItem{VaporView::SkyDeviceId::TemperatureController,
                                    VaporView::DeviceState::Disconnected,
                                    0,
                                    0,
                                    0,
                                    0});
    require(QMetaObject::invokeMethod(&window,
                                      "onRemoteTelemetryStatusUpdated",
                                      Qt::DirectConnection,
                                      Q_ARG(VaporView::TelemetryStatus, disconnectedTemperatureStatus)),
            "remote temperature controller disconnect status can be applied");
    processEventsFor(50);
    require(!temperatureChannelButton->isEnabled(),
            "temperature overview channel selector is disabled after controller disconnect");
    require(temperatureChannelButton->property("available").isValid() &&
                !temperatureChannelButton->property("available").toBool(),
            "temperature overview channel selector marks disconnected controller data unavailable");
    require(!temperatureOutputSwitch->isEnabled(),
            "temperature overview output enable capsule is disabled after controller disconnect");
    for (QWidget *plot : temperatureTrendPlots)
    {
        require(plot->property("sampleCount").toInt() > 0,
                "temperature trend plot keeps existing samples when controller disconnects");
    }

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

    auto *tcpWaveDisplayButton = window.findChild<QToolButton *>(QStringLiteral("tcpWaveDisplayButton"));
    require(tcpWaveDisplayButton != nullptr,
            "TCP wave display settings button exists in the title bar");
    require(tcpWaveDisplayButton->isVisible(),
            "TCP wave display settings button is visible at the default window size");
    auto *tcpWaveTitleBar = tcpWaveDisplayButton->parentWidget();
    require(tcpWaveTitleBar != nullptr &&
                tcpWaveTitleBar->objectName() == QStringLiteral("sectionTitleBar"),
            "TCP wave display settings button lives in the TCP card title bar");
    requireChildInsideParent(tcpWaveDisplayButton, tcpWaveTitleBar, 0,
                             "TCP wave display settings button is not clipped by the title bar");
    require(tcpWaveDisplayButton->iconSize().width() >= 28 &&
                tcpWaveDisplayButton->iconSize().height() >= 28,
            "TCP wave display settings icon matches the standard title-bar icon size");
    QLabel *tcpWaveTitleLabel = nullptr;
    QLabel *tcpFrameRateLabel = nullptr;
    const QList<QLabel*> tcpTitleBarLabels = tcpWaveTitleBar->findChildren<QLabel *>();
    for (QLabel *label : tcpTitleBarLabels)
    {
        if (label->text() == QStringLiteral("TCP波形监视") ||
            label->text() == QStringLiteral("TCP Wave Monitor"))
        {
            tcpWaveTitleLabel = label;
        }
        if (label->text().contains(QStringLiteral("实时频率")) ||
            label->text().contains(QStringLiteral("Realtime")))
        {
            tcpFrameRateLabel = label;
        }
    }
    require(tcpWaveTitleLabel != nullptr,
            "TCP wave title label exists in the title bar");
    require(tcpWaveTitleLabel->width() >= tcpWaveTitleLabel->fontMetrics().horizontalAdvance(tcpWaveTitleLabel->text()) + 6,
            "TCP wave title label reserves enough width for the full title text");
    require(tcpFrameRateLabel != nullptr,
            "TCP wave frame-rate label exists in the title bar");
    const int displayButtonRight = tcpWaveDisplayButton->mapTo(tcpWaveTitleBar, QPoint(tcpWaveDisplayButton->width(), 0)).x();
    const int frameRateLeft = tcpFrameRateLabel->mapTo(tcpWaveTitleBar, QPoint(0, 0)).x();
    require(displayButtonRight + 4 <= frameRateLeft,
            "TCP wave display settings button stays between the title and realtime label");
    auto *tcpWavePanelWidget = tcpWaveTitleBar->parentWidget();
    auto *tcpWaveCard = qobject_cast<QGroupBox *>(tcpWavePanelWidget ? tcpWavePanelWidget->parentWidget() : nullptr);
    require(tcpWaveCard != nullptr,
            "TCP wave card can be identified from the title bar");
    auto groupForSectionTitle = [](QLabel *label) -> QGroupBox * {
        QWidget *widget = label;
        while (widget && !qobject_cast<QGroupBox *>(widget))
        {
            widget = widget->parentWidget();
        }
        return qobject_cast<QGroupBox *>(widget);
    };
    auto *rawWaveGroup = groupForSectionTitle(findLabelByText(tcpWaveCard, {QStringLiteral("原始信号"), QStringLiteral("Raw Signal")}));
    auto *harmonicWaveGroup = groupForSectionTitle(findLabelByText(tcpWaveCard,
                                                                  {QStringLiteral("归一化二次谐波"),
                                                                   QStringLiteral("Normalized Second Harmonic")}));
    auto *peakTrendGroup = groupForSectionTitle(peakTrendTitle);
    require(rawWaveGroup != nullptr && harmonicWaveGroup != nullptr && peakTrendGroup != nullptr,
            "TCP wave subcards can be identified before display-mode changes");
    auto visibleSingleLevelMenu = [](const QStringList& titles) -> VaporView::SingleLevelPopupMenu * {
        for (QWidget *topLevel : QApplication::topLevelWidgets())
        {
            auto *menu = qobject_cast<VaporView::SingleLevelPopupMenu *>(topLevel);
            if (menu && menu->isVisible() &&
                titles.contains(menu->title()))
            {
                return menu;
            }
        }
        return nullptr;
    };
    auto visibleWaveDisplayMenu = [&]() -> VaporView::SingleLevelPopupMenu * {
        return visibleSingleLevelMenu({QStringLiteral("波形显示"), QStringLiteral("Wave Display")});
    };
    auto clickWaveDisplayMenuRow = [&](const QStringList& labels, const char *message) {
        VaporView::SingleLevelPopupMenu *menu = visibleWaveDisplayMenu();
        if (!menu)
        {
            tcpWaveDisplayButton->click();
            processEventsFor(120);
            menu = visibleWaveDisplayMenu();
        }
        require(menu != nullptr, "TCP wave display menu opens from the title-bar settings button");
        require(menu->rows().size() == 4,
                "TCP wave display menu exposes the four display modes");
        require(menu->cornerRadius() == 10,
                "TCP wave display menu uses the unified 10px corner radius");
        require(menu->panelPadding() == 12,
                "TCP wave display menu uses the unified 12px vertical padding");
        require(menu->property("floatingPanelChrome").toBool(),
                "TCP wave display menu uses floating single-level popup chrome");
        require(menu->property("shadowMargin").toInt() == 22,
                "TCP wave display menu reserves the shared floating popup shadow margin");
        require(menu->styleSheet().contains(QStringLiteral("background-color: transparent; border: none; border-radius: 10px; padding: 12px 0px")),
                "TCP wave display menu applies the unified floating popup stylesheet");
        QLabel *rowLabel = findLabelByText(menu, labels);
        require(rowLabel != nullptr, message);
        auto *rowWidget = qobject_cast<VaporView::SingleLevelPopupMenuRow *>(rowLabel->parentWidget());
        require(rowWidget != nullptr, message);
        require(rowWidget->property("textAlignment").toString() == QStringLiteral("left") &&
                    rowWidget->property("checkIconAlignment").toString() == QStringLiteral("right"),
                "TCP wave display menu row keeps text left and check icon right");
        const int shadowMargin = menu->property("shadowMargin").toInt();
        require(rowWidget->geometry().left() <= shadowMargin + 1 &&
                    rowWidget->geometry().right() >= menu->width() - shadowMargin - 3,
                "TCP wave display menu hover background spans the full floating panel row width");
        hoverWidget(rowWidget, true, 40);
        require(rowWidget->property("hovered").toBool(),
                "TCP wave display menu row records hover before selection");
        clickWidget(rowWidget, 160);
    };
    auto requireCheckedWaveDisplayRowsHaveNoStaleHover = [&]() {
        tcpWaveDisplayButton->click();
        processEventsFor(120);
        VaporView::SingleLevelPopupMenu *menu = visibleWaveDisplayMenu();
        require(menu != nullptr,
                "TCP wave display menu reopens after selecting a checked display row");
        bool foundCheckedRow = false;
        for (VaporView::SingleLevelPopupMenuRow *row : menu->rows())
        {
            if (!row->isChecked())
            {
                continue;
            }
            foundCheckedRow = true;
            require(row->property("hasCheckIcon").toBool(),
                    "selected TCP wave display row reopens with its check indicator");
            require(!row->property("hovered").toBool(),
                    "selected TCP wave display row does not keep stale hover highlight after reopening");
        }
        require(foundCheckedRow,
                "TCP wave display menu has at least one checked row after re-enabling raw signal");
        menu->hide();
        processEventsFor(80);
    };
    clickWaveDisplayMenuRow({QStringLiteral("全部显示"), QStringLiteral("Show All")},
                            "TCP wave display menu can toggle the selected show-all row back off");
    processEventsFor(200);
    activateLayouts(&window);
    require(!rawWaveGroup->isVisible() && !harmonicWaveGroup->isVisible() && !peakTrendGroup->isVisible(),
            "TCP wave card hides all plot subcards when every display mode is disabled");
    require(tcpWaveCard->height() <= tcpWaveTitleBar->height() + 12,
            "TCP wave card collapses to the title bar when every display mode is disabled");
    auto tcpWaveCardRectInHome = [&]() {
        return QRect(tcpWaveCard->mapTo(homeScrollArea->widget(), QPoint(0, 0)), tcpWaveCard->size());
    };
    auto previousHomeCardBottom = [&](const QRect& tcpRect) {
        int previousBottom = std::numeric_limits<int>::min();
        const QList<QGroupBox*> homeCards = homeScrollArea->widget()->findChildren<QGroupBox *>(QStringLiteral("sensorGroupBox"));
        for (QGroupBox *card : homeCards)
        {
            if (card == tcpWaveCard || !card->isVisible())
            {
                continue;
            }
            const QRect cardRect(card->mapTo(homeScrollArea->widget(), QPoint(0, 0)), card->size());
            if (cardRect.bottom() <= tcpRect.top())
            {
                previousBottom = std::max(previousBottom, cardRect.bottom());
            }
        }
        return previousBottom;
    };
    QRect tcpWaveCardRect = tcpWaveCardRectInHome();
    int previousCardBottom = previousHomeCardBottom(tcpWaveCardRect);
    require(previousCardBottom != std::numeric_limits<int>::min(),
            "TCP wave card has a visible card above it on the home page");
    require(tcpWaveCardRect.top() - previousCardBottom <= 8,
            "collapsed TCP wave card stays tight against the card above it");
    clickWaveDisplayMenuRow({QStringLiteral("显示原始信号"), QStringLiteral("Show Raw Signal")},
                            "TCP wave display menu can re-enable only the raw-signal row");
    if (QMenu *menu = visibleWaveDisplayMenu())
    {
        menu->hide();
    }
    requireCheckedWaveDisplayRowsHaveNoStaleHover();
    processEventsFor(200);
    activateLayouts(&window);
    require(rawWaveGroup->isVisible() && !harmonicWaveGroup->isVisible() && !peakTrendGroup->isVisible(),
            "TCP wave card can show only the raw-signal plot after all plots were disabled");
    tcpWaveCardRect = tcpWaveCardRectInHome();
    previousCardBottom = previousHomeCardBottom(tcpWaveCardRect);
    require(previousCardBottom != std::numeric_limits<int>::min(),
            "raw-only TCP wave card still has a visible card above it on the home page");
    require(tcpWaveCardRect.top() - previousCardBottom <= 8,
            "raw-only TCP wave card stays tight against the card above it");
    require(tcpWaveCard->height() <= tcpWaveTitleBar->height() + rawWaveGroup->minimumSizeHint().height() + 24,
            "raw-only TCP wave card does not reserve hidden plot height");
    clickWaveDisplayMenuRow({QStringLiteral("全部显示"), QStringLiteral("Show All")},
                            "TCP wave display menu exposes the show-all row");
    if (QMenu *menu = visibleWaveDisplayMenu())
    {
        menu->hide();
    }
    processEventsFor(200);
    activateLayouts(&window);

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
    clickWidget(temperatureNavButton, 150);
    activateLayouts(&window);
    auto *temperaturePage = window.findChild<QWidget *>(QStringLiteral("temperaturePage"));
    require(temperaturePage != nullptr && temperaturePage->isVisible(),
            "temperature page can be opened");
    requireLabelTextOneOf(customTitleLabel,
                          {QStringLiteral("温控"), QStringLiteral("Thermal")},
                          "custom title bar follows the selected temperature page");
    requireNoVisiblePageTitle(temperaturePage,
                              "temperature page does not show an internal page title");
    clickWidget(deviceConfigNavButton, 150);
    activateLayouts(&window);
    auto *deviceConfigPage = window.findChild<QWidget *>(QStringLiteral("deviceConfigPage"));
    require(deviceConfigPage != nullptr && deviceConfigPage->isVisible(),
            "device configuration page can be opened");
    requireLabelTextOneOf(customTitleLabel,
                          {QStringLiteral("设备配置"), QStringLiteral("Device")},
                          "custom title bar follows the selected device configuration page");
    requireNoVisiblePageTitle(deviceConfigPage,
                              "device configuration page does not show an internal page title");
    auto *deviceConfigScrollArea =
        deviceConfigPage->findChild<QScrollArea *>(QStringLiteral("mainCardsScrollArea"));
    require(deviceConfigScrollArea != nullptr, "device configuration scroll area exists");
    require(deviceConfigScrollArea->horizontalScrollBar() != nullptr &&
                deviceConfigScrollArea->horizontalScrollBar()->maximum() == 0,
            "device configuration page fits horizontally at default window size");

    const QStringList removedDevicePageActions = {
        QStringLiteral("刷新"),
        QStringLiteral("取消"),
        QStringLiteral("Refresh"),
        QStringLiteral("Cancel"),
    };
    int localDeviceActionCount = 0;
    int connectRemoteActionCount = 0;
    int disconnectRemoteActionCount = 0;
    QToolButton *temperatureDeviceActionButton = nullptr;
    QPushButton *deviceAutoDetectButton = nullptr;
    QPushButton *deviceSkyConfigButton = nullptr;
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
        if (button->text().contains(QStringLiteral("自动识别")) ||
            button->text().contains(QStringLiteral("Auto Detect")))
        {
            deviceAutoDetectButton = button;
        }
        if (button->text().contains(QStringLiteral("天空端设备配置")) ||
            button->text().contains(QStringLiteral("Sky Device Config")))
        {
            deviceSkyConfigButton = button;
        }
    }
    for (QToolButton *button : deviceConfigPage->findChildren<QToolButton *>())
    {
        const QString remoteAction = button->property("deviceConfigRemoteAction").toString();
        if (!remoteAction.isEmpty())
        {
            require(button->objectName() == QStringLiteral("homeDeviceActionButton") &&
                        button->property("deviceConfigAction").toBool(),
                    "device configuration actions reuse the home device button style");
            require(button->autoRaise(),
                    "device configuration actions use flat tool buttons without a persistent background");
            require(button->text().isEmpty(),
                    "device configuration remote actions use icon-only visible labels");
            require(!button->icon().isNull(),
                    "device configuration remote actions use lucide icons");
            require(button->iconSize() == QSize(18, 18),
                    "device configuration actions reuse the home device icon size");
            require(std::abs(button->width() - button->height()) <= 1 &&
                        button->width() == 32,
                    "device configuration actions reuse the home device button size");
            require(!button->toolTip().trimmed().isEmpty() &&
                        button->accessibleName() == button->toolTip() &&
                        button->statusTip() == button->toolTip(),
                    "device configuration icon-only remote actions keep tooltip and accessibility text");
            require(button->toolTip().contains(QStringLiteral("本地串口设备")) ||
                        button->toolTip().contains(QStringLiteral("local serial device")),
                    "device configuration actions identify the local serial mode");
            if (remoteAction == QStringLiteral("connect"))
            {
                ++connectRemoteActionCount;
            }
            else if (remoteAction == QStringLiteral("disconnect"))
            {
                ++disconnectRemoteActionCount;
            }
            else
            {
                require(false, "device configuration remote actions only expose connect and disconnect commands");
            }
            if (button->property("deviceConfigRemoteDevice").toInt() ==
                static_cast<int>(VaporView::SkyDeviceId::TemperatureController))
            {
                temperatureDeviceActionButton = button;
            }
            ++localDeviceActionCount;
        }
    }
    require(localDeviceActionCount == 5,
            "device configuration keeps one local action per serial device");
    require(connectRemoteActionCount == 5 && disconnectRemoteActionCount == 0,
            "device configuration shows one connect action for every disconnected device");
    require(temperatureDeviceActionButton != nullptr,
            "device configuration exposes the RD105 connection action button");
    require(deviceAutoDetectButton != nullptr && deviceAutoDetectButton->width() <= 145,
            "device configuration auto-detect button uses compact title-bar width");
    require(deviceSkyConfigButton != nullptr && deviceSkyConfigButton->width() <= 145,
            "device configuration sky-device button uses compact title-bar width");

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
    require(devicePortCombo->isEnabled(),
            "device configuration serial combo is enabled in local mode");
    const QString originalDevicePort = devicePortCombo->currentText();
    devicePortCombo->setEditText(QStringLiteral("-- 选择 --"));
    processEventsFor(20);
    require(!temperatureDeviceActionButton->isEnabled(),
            "device configuration disables the local action when its serial port is cleared");
    devicePortCombo->setEditText(QStringLiteral("COM99"));
    processEventsFor(20);
    require(temperatureDeviceActionButton->isEnabled(),
            "device configuration immediately enables the local action after selecting a serial port");
    devicePortCombo->setEditText(originalDevicePort);
    processEventsFor(20);
    auto *deviceTemperaturePortCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceTemperaturePortCombo"));
    auto *deviceTemperatureBaudCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceTemperatureBaudCombo"));
    require(deviceTemperaturePortCombo != nullptr,
            "device configuration temperature serial-port combo exists");
    require(deviceTemperatureBaudCombo != nullptr,
            "device configuration temperature baud-rate combo exists");
    deviceConfigScrollArea->ensureWidgetVisible(deviceTemperaturePortCombo, 20, 20);
    processEventsFor(80);
    requireComboPopupFloatingContainer(deviceTemperaturePortCombo,
                                       "device serial-port combo opens with the shared rounded shadow popup");
    deviceConfigScrollArea->ensureWidgetVisible(deviceTemperatureBaudCombo, 20, 20);
    processEventsFor(80);
    requireComboPopupFloatingContainer(deviceTemperatureBaudCombo,
                                       "device baud-rate combo opens with the shared rounded shadow popup");
    QComboBox *homePortCombo = nullptr;
    for (QComboBox *combo : window.findChildren<QComboBox *>())
    {
        if (!combo->isEditable() ||
            combo == devicePortCombo ||
            deviceConfigPage->isAncestorOf(combo))
        {
            continue;
        }
        if (combo->currentText() == QStringLiteral("COM9"))
        {
            homePortCombo = combo;
            break;
        }
    }
    require(homePortCombo != nullptr,
            "home serial combo matching the device configuration combo exists");
    const QString syntheticPort = QStringLiteral("COM123");
    if (homePortCombo->findText(syntheticPort) < 0)
    {
        homePortCombo->addItem(syntheticPort);
    }
    homePortCombo->setCurrentIndex(homePortCombo->findText(syntheticPort));
    processEventsFor(50);
    activateLayouts(&window);
    const int deviceSyntheticPortIndex = devicePortCombo->findText(syntheticPort);
    require(deviceSyntheticPortIndex >= 0,
            "device configuration serial combo mirrors refreshed home serial items");
    devicePortCombo->setCurrentIndex(deviceSyntheticPortIndex);
    processEventsFor(50);
    activateLayouts(&window);
    require(devicePortCombo->currentIndex() == deviceSyntheticPortIndex &&
                devicePortCombo->currentText() == syntheticPort,
            "device configuration serial combo can select an existing serial item");
    require(homePortCombo->currentText() == syntheticPort,
            "device configuration serial selection mirrors back to the home combo");

    QGroupBox *serialConfigCard = nullptr;
    for (QWidget *ancestor = devicePortCombo ? devicePortCombo->parentWidget() : nullptr;
         ancestor != nullptr;
         ancestor = ancestor->parentWidget())
    {
        if (auto *group = qobject_cast<QGroupBox *>(ancestor);
            group && group->objectName() == QStringLiteral("sensorGroupBox"))
        {
            serialConfigCard = group;
            break;
        }
    }
    require(serialConfigCard != nullptr,
            "device configuration serial card can be identified from the serial controls");
    require(serialConfigCard->sizePolicy().horizontalPolicy() == QSizePolicy::Maximum,
            "device configuration serial card width follows its contents");
    const QRect serialConfigPageRect(serialConfigCard->mapTo(deviceConfigPage, QPoint(0, 0)),
                                     serialConfigCard->size());
    require(serialConfigCard->width() <= serialConfigCard->sizeHint().width() + 4,
            "device configuration serial card does not expand to fill the whole row");
    requireCardTitleBar(serialConfigCard,
                        QStringList{QStringLiteral("串口配置"), QStringLiteral("Serial Port Configuration")},
                        QStringLiteral("usb"),
                        "device serial configuration card uses the standard icon title bar");
    const QString appStyleSheet = qApp->styleSheet();
    const int serialCardStyleIndex = appStyleSheet.indexOf(QStringLiteral("QGroupBox#sensorGroupBox"));
    require(serialCardStyleIndex >= 0 &&
                appStyleSheet.mid(serialCardStyleIndex, 240).contains(QStringLiteral("border-radius: 8px")),
            "serial configuration card uses the standard 8px card radius");

    const QRect deviceRateRect(deviceRateCombo->mapTo(deviceConfigPage, QPoint(0, 0)),
                               deviceRateCombo->size());
    bool foundRemoteButtonsToRightOfRate = false;
    int rightmostRemoteButtonRight = -1;
    for (QToolButton *button : deviceConfigPage->findChildren<QToolButton *>())
    {
        if (!button->isVisible())
        {
            continue;
        }
        const QString remoteAction = button->property("deviceConfigRemoteAction").toString();
        if (remoteAction.isEmpty())
        {
            continue;
        }
        const QRect buttonRect(button->mapTo(deviceConfigPage, QPoint(0, 0)), button->size());
        require(remoteAction != QStringLiteral("reconnect"),
                "device configuration omits reconnect remote actions to keep the serial card compact");
        rightmostRemoteButtonRight = std::max(rightmostRemoteButtonRight, buttonRect.right());
        if (buttonRect.left() > deviceRateRect.right() &&
            std::abs(buttonRect.center().y() - deviceRateRect.center().y()) <= 2)
        {
            foundRemoteButtonsToRightOfRate = true;
        }
    }
    require(foundRemoteButtonsToRightOfRate,
            "device configuration remote actions sit to the right of the rate selector");
    require(rightmostRemoteButtonRight > 0 &&
                serialConfigPageRect.right() - rightmostRemoteButtonRight <= 32,
            "device configuration serial card right edge stays close to the compact remote buttons");

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
    require(epsilonConfigCard->parentWidget() == serialConfigCard->parentWidget(),
            "device EPSILON configuration card is a sibling of the serial configuration card");
    require(!serialConfigCard->isAncestorOf(epsilonConfigCard),
            "device EPSILON configuration card is not nested inside the serial configuration card");
    requireCardTitleBar(epsilonConfigCard,
                        QStringList{QStringLiteral("EPSILON 配置"), QStringLiteral("EPSILON Configuration")},
                        QStringLiteral("sliders-vertical"),
                        "device EPSILON configuration card uses the standard icon title bar");
    auto *epsilonPacketCustomCheck =
        epsilonConfigCard->findChild<QCheckBox *>(QStringLiteral("epsilonPacketCustomCheck"));
    require(epsilonPacketCustomCheck != nullptr &&
                epsilonPacketCustomCheck->focusPolicy() == Qt::TabFocus,
            "device EPSILON custom packet-rate checkbox keeps keyboard focus without retaining mouse-click focus");
    require(epsilonPacketCustomCheck->property("indicatorCanvasSize").toInt() == 34 &&
                epsilonPacketCustomCheck->property("indicatorIconSize").toInt() == 24 &&
                epsilonPacketCustomCheck->property("indicatorCanvasSize").toInt() >
                    epsilonPacketCustomCheck->property("indicatorIconSize").toInt() &&
                epsilonPacketCustomCheck->property("indicatorFeedbackColorRole").toString() ==
                    QStringLiteral("TitleBarHover"),
            "device EPSILON custom packet-rate checkbox uses the title-bar 34px canvas with a 24px icon");
    const int epsilonCustomCheckStyleIndex = appStyleSheet.indexOf(
        QStringLiteral("QCheckBox#epsilonPacketCustomCheck::indicator {"));
    require(epsilonCustomCheckStyleIndex >= 0 &&
                appStyleSheet.mid(epsilonCustomCheckStyleIndex, 700).contains(
                    QStringLiteral("width: 34px")) &&
                appStyleSheet.mid(epsilonCustomCheckStyleIndex, 700).contains(
                    QStringLiteral("height: 34px")) &&
                appStyleSheet.mid(epsilonCustomCheckStyleIndex, 700).contains(
                    QStringLiteral("image: none")) &&
                appStyleSheet.mid(epsilonCustomCheckStyleIndex, 700).contains(
                    QStringLiteral("background-color: transparent")),
            "device EPSILON custom packet-rate checkbox separates the title-bar hover canvas from its icon");
    QStyleOptionButton epsilonCheckOption;
    epsilonCheckOption.initFrom(epsilonPacketCustomCheck);
    epsilonCheckOption.text = epsilonPacketCustomCheck->text();
    const QRect epsilonCheckIndicatorRect = epsilonPacketCustomCheck->style()->subElementRect(
        QStyle::SE_CheckBoxIndicator,
        &epsilonCheckOption,
        epsilonPacketCustomCheck);
    require(epsilonCheckIndicatorRect.isValid(),
            "device EPSILON custom packet-rate checkbox exposes a valid icon hit area");
    const QPoint epsilonCheckTextPoint(
        std::min(epsilonPacketCustomCheck->width() - 1, epsilonCheckIndicatorRect.right() + 24),
        epsilonCheckIndicatorRect.center().y());
    const bool epsilonCheckInitiallyChecked = epsilonPacketCustomCheck->isChecked();
    clickWidgetAt(epsilonPacketCustomCheck, epsilonCheckTextPoint);
    require(epsilonPacketCustomCheck->isChecked() == epsilonCheckInitiallyChecked,
            "device EPSILON custom packet-rate checkbox ignores clicks on its descriptive text");
    clickWidgetAt(epsilonPacketCustomCheck, epsilonCheckIndicatorRect.center());
    require(epsilonPacketCustomCheck->isChecked() != epsilonCheckInitiallyChecked,
            "device EPSILON custom packet-rate checkbox toggles from its icon area");
    moveMouseOverWidgetAt(epsilonPacketCustomCheck, epsilonCheckIndicatorRect.center());
    require(epsilonPacketCustomCheck->property("indicatorHovered").toBool(),
            "device EPSILON custom packet-rate checkbox highlights only while its icon is hovered");
    moveMouseOverWidgetAt(epsilonPacketCustomCheck, epsilonCheckTextPoint);
    require(!epsilonPacketCustomCheck->property("indicatorHovered").toBool(),
            "device EPSILON custom packet-rate checkbox clears hover feedback outside the icon area");
    clickWidgetAt(epsilonPacketCustomCheck, epsilonCheckIndicatorRect.center());
    require(epsilonPacketCustomCheck->isChecked() == epsilonCheckInitiallyChecked,
            "device EPSILON custom packet-rate checkbox test restores the original checked state");
    const int epsilonCardStyleIndex = appStyleSheet.indexOf(QStringLiteral("QFrame#epsilonSectionCard"));
    require(epsilonCardStyleIndex >= 0 &&
                appStyleSheet.mid(epsilonCardStyleIndex, 200).contains(QStringLiteral("border-radius: 8px")),
            "device EPSILON and telemetry cards use the standard 8px card radius");
    int serialControlBottom = 0;
    for (QComboBox *combo : deviceConfigPage->findChildren<QComboBox *>())
    {
        if (!combo->isVisible() ||
            epsilonConfigCard->isAncestorOf(combo) ||
            combo->property("epsilonPacketId").isValid())
        {
            continue;
        }
        const QRect comboRect(combo->mapTo(deviceConfigPage, QPoint(0, 0)), combo->size());
        serialControlBottom = std::max(serialControlBottom, comboRect.bottom());
    }
    const QRect epsilonConfigPageRect(epsilonConfigCard->mapTo(deviceConfigPage, QPoint(0, 0)),
                                      epsilonConfigCard->size());
    require(epsilonConfigPageRect.top() > serialControlBottom,
            "device EPSILON configuration card sits below the serial and rate selectors");
    const QRect epsilonConfigBounds = epsilonConfigCard->rect().adjusted(-1, -1, 1, 1);
    int leftPacketComboColumn = -1;
    int rightPacketComboColumn = -1;
    int rightPacketComboRight = -1;
    int packetComboTop = epsilonConfigCard->height();
    int packetComboBottom = -1;
    for (QComboBox *combo : epsilonConfigCard->findChildren<QComboBox *>())
    {
        if (!combo->isVisible() || !combo->property("epsilonPacketId").isValid())
        {
            continue;
        }
        const QRect comboRect(combo->mapTo(epsilonConfigCard, QPoint(0, 0)), combo->size());
        packetComboTop = std::min(packetComboTop, comboRect.top());
        packetComboBottom = std::max(packetComboBottom, comboRect.bottom());
        require(epsilonConfigBounds.contains(comboRect),
                "device EPSILON packet-rate combos stay inside the embedded card");
        require(combo->width() >= combo->fontMetrics().horizontalAdvance(combo->currentText()) + 44,
                "device EPSILON packet-rate combo text is not clipped");
        QLabel *packetLabel = nullptr;
        for (QLabel *label : epsilonConfigCard->findChildren<QLabel *>())
        {
            if (label->property("epsilonPacketId").isValid() &&
                label->property("epsilonPacketId").toUInt() == combo->property("epsilonPacketId").toUInt())
            {
                packetLabel = label;
                break;
            }
        }
        require(packetLabel != nullptr && packetLabel->isVisible(),
                "device EPSILON packet-rate row has a matching label");
        require(!packetLabel->text().contains(QStringLiteral("最大")) &&
                    !packetLabel->text().contains(QStringLiteral("Max")),
                "device EPSILON packet-rate labels omit max-rate text");
        const QRect labelRect(packetLabel->mapTo(epsilonConfigCard, QPoint(0, 0)),
                              packetLabel->size());
        require(comboRect.left() > labelRect.right(),
                "device EPSILON packet-rate combo sits to the right of its label");
        require(std::abs(comboRect.center().y() - labelRect.center().y()) <= 3,
                "device EPSILON packet-rate label and combo are vertically aligned");
        require(combo->property("epsilonPacketGridColumn").isValid(),
                "device EPSILON packet-rate combo reports its visual column");
        const int visualColumn = combo->property("epsilonPacketGridColumn").toInt();
        require(visualColumn == 0 || visualColumn == 1,
                "device EPSILON packet-rate combo column is valid");
        int& expectedColumnLeft = visualColumn == 0 ? leftPacketComboColumn : rightPacketComboColumn;
        if (visualColumn == 1)
        {
            rightPacketComboRight = std::max(rightPacketComboRight, comboRect.right());
        }
        if (expectedColumnLeft < 0)
        {
            expectedColumnLeft = comboRect.left();
        }
        else
        {
            require(std::abs(comboRect.left() - expectedColumnLeft) <= 2,
                    "device EPSILON packet-rate combos align vertically within each column");
        }
    }
    require(leftPacketComboColumn >= 0 && rightPacketComboColumn > leftPacketComboColumn,
            "device EPSILON packet-rate layout exposes two aligned combo columns");
    require(rightPacketComboRight > 0,
            "device EPSILON packet-rate grid has measurable right-side blank space");
    int actionButtonColumnA = -1;
    int actionButtonColumnB = -1;
    int actionButtonTop = epsilonConfigCard->height();
    int actionButtonBottom = -1;
    for (const QString& buttonText : {QStringLiteral("恢复推荐"),
                                      QStringLiteral("分组模式"),
                                      QStringLiteral("保存并应用"),
                                      QStringLiteral("配置RTCM串口"),
                                      QStringLiteral("重新配置输出"),
                                      QStringLiteral("RTK配置")})
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
                require(buttonRect.left() > rightPacketComboRight,
                        "device EPSILON command buttons sit to the right of the second packet-rate combo column");
                actionButtonTop = std::min(actionButtonTop, buttonRect.top());
                actionButtonBottom = std::max(actionButtonBottom, buttonRect.bottom());
                if (actionButtonColumnA < 0 || std::abs(buttonRect.left() - actionButtonColumnA) <= 2)
                {
                    actionButtonColumnA = actionButtonColumnA < 0 ? buttonRect.left() : actionButtonColumnA;
                }
                else if (actionButtonColumnB < 0 || std::abs(buttonRect.left() - actionButtonColumnB) <= 2)
                {
                    actionButtonColumnB = actionButtonColumnB < 0 ? buttonRect.left() : actionButtonColumnB;
                }
                else
                {
                    require(false, "device EPSILON command buttons use exactly two visual columns");
                }
                foundButton = true;
                break;
            }
        }
        require(foundButton, "device EPSILON configuration card exposes expected command buttons");
    }
    require(actionButtonColumnA >= 0 && actionButtonColumnB > actionButtonColumnA,
            "device EPSILON command buttons are split into two columns");
    require(actionButtonTop >= packetComboTop - 2 && actionButtonBottom <= packetComboBottom + 4,
            "device EPSILON command buttons do not increase the packet-rate grid height");

    const QList<QFrame*> deviceSummaryCards =
        deviceConfigPage->findChildren<QFrame *>(QStringLiteral("epsilonSectionCard"));
    require(!deviceSummaryCards.isEmpty(), "device configuration telemetry summary card exists");
    QFrame *deviceTelemetrySummaryCard = nullptr;
    for (QFrame *summaryCard : deviceSummaryCards)
    {
        if (summaryCard->findChild<QFrame *>(QStringLiteral("deviceTelemetrySectionTitlePane")))
        {
            deviceTelemetrySummaryCard = summaryCard;
            break;
        }
        const QList<QLabel*> labels = summaryCard->findChildren<QLabel *>();
        for (QLabel *label : labels)
        {
            if (label->text().contains(QStringLiteral("天地通信链路状态")) ||
                label->text().contains(QStringLiteral("Sky-ground Communication Link Status")))
            {
                deviceTelemetrySummaryCard = summaryCard;
                break;
            }
        }
        if (deviceTelemetrySummaryCard)
        {
            break;
        }
    }
    require(deviceTelemetrySummaryCard != nullptr,
            "device configuration telemetry summary card can be identified by title text");
    require(deviceTelemetrySummaryCard->isVisible(),
            "device configuration telemetry summary card is visible before source mode changes");
    require(deviceTelemetrySummaryCard->parentWidget() == serialConfigCard->parentWidget(),
            "device telemetry summary card is a sibling of the serial configuration card");
    require(!serialConfigCard->isAncestorOf(deviceTelemetrySummaryCard),
            "device telemetry summary card is not nested inside the serial configuration card");
    requireCardTitleBar(deviceTelemetrySummaryCard,
                        QStringList{QStringLiteral("天地通信链路状态"), QStringLiteral("Sky-ground Communication Link Status")},
                        QStringLiteral("satellite"),
                        "device telemetry summary card uses the standard icon title bar");
    const int titlePaneStyleIndex = appStyleSheet.indexOf(QStringLiteral("QFrame#deviceTelemetrySectionTitlePane"));
    require(titlePaneStyleIndex >= 0 &&
                appStyleSheet.mid(titlePaneStyleIndex, 220).contains(QStringLiteral("background-color")) &&
                appStyleSheet.mid(titlePaneStyleIndex, 220).contains(QStringLiteral("border-right")),
            "device telemetry subcard title pane has a separated light background style");
    const int deviceLinkNameStyleIndex =
        appStyleSheet.indexOf(QStringLiteral("QLabel#homeTelemetrySummaryNameLabel[deviceConfigLink=\"true\"]"));
    require(deviceLinkNameStyleIndex >= 0 &&
                appStyleSheet.mid(deviceLinkNameStyleIndex, 180).contains(QStringLiteral("font-size: 14px")) &&
                appStyleSheet.mid(deviceLinkNameStyleIndex, 180).contains(QStringLiteral("font-weight: 700")),
            "device telemetry value field names use compact bold text");
    const int deviceLinkValueStyleIndex =
        appStyleSheet.indexOf(QStringLiteral("QLabel#homeTelemetrySummaryValueLabel[deviceConfigLink=\"true\"]"));
    require(deviceLinkValueStyleIndex >= 0 &&
                appStyleSheet.mid(deviceLinkValueStyleIndex, 180).contains(QStringLiteral("font-size: 14px")),
            "device telemetry values use compact text");
    QList<QFrame*> telemetrySubCards =
        deviceTelemetrySummaryCard->findChildren<QFrame *>(QStringLiteral("homeTelemetrySectionCard"));
    require(telemetrySubCards.size() == 3,
            "device telemetry summary card splits content into three home-style subcards");
    std::sort(telemetrySubCards.begin(), telemetrySubCards.end(), [](QFrame *a, QFrame *b) {
        return a->mapTo(a->parentWidget(), QPoint(0, 0)).y() <
               b->mapTo(b->parentWidget(), QPoint(0, 0)).y();
    });
    const QVector<QStringList> expectedTelemetrySubCardTitles = {
        {QStringLiteral("数据流频率"), QStringLiteral("Data stream rates")},
        {QStringLiteral("链路速率"), QStringLiteral("Link rate")},
        {QStringLiteral("数据"), QStringLiteral("Data")},
    };
    int previousSubCardBottom = -1;
    int previousSubCardLeft = -1;
    int previousTitleLeft = -1;
    for (int i = 0; i < telemetrySubCards.size(); ++i)
    {
        QFrame *subCard = telemetrySubCards.at(i);
        const QRect subCardRect(subCard->mapTo(deviceTelemetrySummaryCard, QPoint(0, 0)), subCard->size());
        require(previousSubCardBottom < 0 || subCardRect.top() > previousSubCardBottom,
                "device telemetry summary subcards are stacked vertically");
        require(previousSubCardLeft < 0 || std::abs(subCardRect.left() - previousSubCardLeft) <= 2,
                "device telemetry summary subcards align on the left edge");
        previousSubCardBottom = subCardRect.bottom();
        previousSubCardLeft = subCardRect.left();

        QFrame *titlePane = subCard->findChild<QFrame *>(QStringLiteral("deviceTelemetrySectionTitlePane"));
        require(titlePane != nullptr,
                "device telemetry summary subcard has a dedicated left title pane");
        QLabel *expectedTitleLabel = titlePane->findChild<QLabel *>(QStringLiteral("deviceTelemetrySectionTitleLabel"));
        require(expectedTitleLabel != nullptr,
                "device telemetry summary subcard title pane owns its title label");
        bool titleMatches = false;
        const QString plainTitle = expectedTitleLabel->property("plainTitle").toString();
        for (const QString& expectedTitle : expectedTelemetrySubCardTitles.at(i))
        {
            if (plainTitle == expectedTitle)
            {
                titleMatches = true;
                break;
            }
        }
        require(titleMatches,
                "device telemetry summary subcard keeps the expected section title");
        require(expectedTitleLabel->text().contains(QLatin1Char('\n')),
                "device telemetry summary subcard title text is arranged vertically");

        const QRect titlePaneRect(titlePane->mapTo(subCard, QPoint(0, 0)),
                                  titlePane->size());
        require(titlePaneRect.width() <= 30,
                "device telemetry summary subcard title pane matches the compact EPSILON title width");
        const QRect titleRect(expectedTitleLabel->mapTo(subCard, QPoint(0, 0)),
                              expectedTitleLabel->size());
        require(previousTitleLeft < 0 || std::abs(titleRect.left() - previousTitleLeft) <= 2,
                "device telemetry summary subcard titles align in a left-side column");
        previousTitleLeft = titleRect.left();
        require(titlePaneRect.left() <= 2 &&
                    titlePaneRect.height() >= subCardRect.height() - 4,
                "device telemetry summary subcard title pane spans the left side");

        const QList<QFrame*> valuePills =
            subCard->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
        require(!valuePills.isEmpty(),
                "device telemetry summary subcard has value pills beside the title column");
        int leftmostPillLeft = subCard->width();
        for (QFrame *pill : valuePills)
        {
            require(pill->property("deviceConfigLink").toBool(),
                    "device telemetry summary subcard value pill uses device-config styling");
            const QRect pillRect(pill->mapTo(subCard, QPoint(0, 0)), pill->size());
            leftmostPillLeft = std::min(leftmostPillLeft, pillRect.left());
            require(pillRect.right() <= subCard->width() - 2,
                    "device telemetry summary pill stays inside its subcard");
            const QList<QLabel*> pillLabels = pill->findChildren<QLabel *>();
            for (QLabel *pillLabel : pillLabels)
            {
                if (pillLabel->objectName() != QStringLiteral("homeTelemetrySummaryNameLabel") &&
                    pillLabel->objectName() != QStringLiteral("homeTelemetrySummaryValueLabel"))
                {
                    continue;
                }
                require(pillLabel->fontMetrics().horizontalAdvance(pillLabel->text()) <= pillLabel->width() + 1,
                        "device telemetry summary label text fits inside its label");
            }
        }
        QLabel *firstNameLabel = subCard->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel"));
        require(firstNameLabel != nullptr &&
                    firstNameLabel->property("deviceConfigLink").toBool(),
                "device telemetry summary field name uses device-config text styling");
        if (i == 2)
        {
            const QList<QLabel*> availabilityValues =
                subCard->findChildren<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
            require(!availabilityValues.isEmpty(),
                    "device telemetry availability subcard has value labels");
            for (QLabel *valueLabel : availabilityValues)
            {
                const QString text = valueLabel->text();
                require(text == QStringLiteral("有") ||
                            text == QStringLiteral("无") ||
                            text == QStringLiteral("Yes") ||
                            text == QStringLiteral("No"),
                        "device telemetry availability values use compact yes/no text");
            }
        }
        require(leftmostPillLeft > titlePaneRect.right(),
                "device telemetry summary subcard title sits in a separate left area");
    }
    const QRect telemetrySummaryPageRect(deviceTelemetrySummaryCard->mapTo(deviceConfigPage, QPoint(0, 0)),
                                         deviceTelemetrySummaryCard->size());
    require(std::abs(telemetrySummaryPageRect.top() - serialConfigPageRect.top()) <= 2,
            "device telemetry summary card is aligned with the serial configuration card row");
    require(telemetrySummaryPageRect.left() > serialConfigPageRect.right(),
            "device telemetry summary card sits to the right of the serial configuration card");
    const QRect localEpsilonConfigRect = epsilonConfigCard->geometry();
    const QRect localTelemetrySummaryRect = deviceTelemetrySummaryCard->geometry();
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

    QComboBox *deviceSourceModeCombo = findSourceModeCombo(deviceConfigPage);
    require(deviceSourceModeCombo != nullptr,
            "device configuration source mode combo exists");
    require(deviceSourceModeCombo->property("usesSingleLevelPopupMenu").toBool(),
            "device configuration source mode combo uses the shared single-level popup");
    require(deviceSourceModeCombo->width() <= 160,
            "device configuration source mode combo uses compact width");
    QComboBox *pressureSourceCombo =
        findComboWithData(deviceConfigPage, QStringLiteral("bmp390"));
    QComboBox *humiditySourceCombo =
        findComboWithData(deviceConfigPage, QStringLiteral("sht45"));
    require(pressureSourceCombo != nullptr && pressureSourceCombo->width() == 108,
            "device pressure source combo fully shows the BMP390 option");
    require(humiditySourceCombo != nullptr && humiditySourceCombo->width() == 108,
            "device source combos keep a consistent widened column");
    auto *pressureBaudCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("devicePressureBaudCombo"));
    auto *humidityBaudCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceHumidityBaudCombo"));
    require(pressureBaudCombo != nullptr &&
                pressureSourceCombo->currentData().toString() == QStringLiteral("ptb210") &&
                pressureBaudCombo->currentText() == QStringLiteral("9600"),
            "PTB210 uses its 9600 default baud when no device-specific value is remembered");
    require(humidityBaudCombo != nullptr &&
                humiditySourceCombo->currentData().toString() == QStringLiteral("hmp3") &&
                humidityBaudCombo->currentText() == QStringLiteral("19200"),
            "HMP3 uses its 19200 default baud when no device-specific value is remembered");

    pressureSourceCombo->setCurrentIndex(
        pressureSourceCombo->findData(QStringLiteral("bmp390")));
    processEventsFor(50);
    require(pressureBaudCombo->currentText() == QStringLiteral("115200"),
            "BMP390 uses its 115200 default baud when no value is remembered");
    pressureBaudCombo->setCurrentText(QStringLiteral("57600"));
    processEventsFor(50);
    pressureSourceCombo->setCurrentIndex(
        pressureSourceCombo->findData(QStringLiteral("ptb210")));
    processEventsFor(50);
    require(pressureBaudCombo->currentText() == QStringLiteral("9600"),
            "switching back to PTB210 restores its remembered baud");
    pressureBaudCombo->setCurrentText(QStringLiteral("19200"));
    processEventsFor(50);
    pressureSourceCombo->setCurrentIndex(
        pressureSourceCombo->findData(QStringLiteral("bmp390")));
    processEventsFor(50);
    require(pressureBaudCombo->currentText() == QStringLiteral("57600"),
            "switching back to BMP390 restores its separate remembered baud");
    pressureSourceCombo->setCurrentIndex(
        pressureSourceCombo->findData(QStringLiteral("ptb210")));
    pressureBaudCombo->setCurrentText(QStringLiteral("9600"));
    processEventsFor(50);

    humiditySourceCombo->setCurrentIndex(
        humiditySourceCombo->findData(QStringLiteral("sht45")));
    processEventsFor(50);
    require(humidityBaudCombo->currentText() == QStringLiteral("115200"),
            "SHT45 uses its 115200 default baud when no value is remembered");
    humidityBaudCombo->setCurrentText(QStringLiteral("57600"));
    processEventsFor(50);
    humiditySourceCombo->setCurrentIndex(
        humiditySourceCombo->findData(QStringLiteral("hmp3")));
    processEventsFor(50);
    require(humidityBaudCombo->currentText() == QStringLiteral("19200"),
            "switching back to HMP3 restores its remembered baud");
    humidityBaudCombo->setCurrentText(QStringLiteral("38400"));
    processEventsFor(50);
    humiditySourceCombo->setCurrentIndex(
        humiditySourceCombo->findData(QStringLiteral("sht45")));
    processEventsFor(50);
    require(humidityBaudCombo->currentText() == QStringLiteral("57600"),
            "switching back to SHT45 restores its separate remembered baud");
    humiditySourceCombo->setCurrentIndex(
        humiditySourceCombo->findData(QStringLiteral("hmp3")));
    humidityBaudCombo->setCurrentText(QStringLiteral("19200"));
    processEventsFor(50);
    const SkyTelemetryRowWidgets deviceSkyTelemetry = findSkyTelemetryRowWidgets(deviceConfigPage);
    require(deviceSkyTelemetry.transportCombo != nullptr,
            "device configuration sky telemetry transport combo exists");
    requireSkyTelemetryTransportLabels(deviceSkyTelemetry, false);
    deviceSourceModeCombo->setCurrentIndex(1);
    processEventsFor(150);
    activateLayouts(&window);
    setSkyTelemetryTransport(deviceSkyTelemetry.transportCombo, QStringLiteral("tcp"));
    processEventsFor(100);
    activateLayouts(&window);
    requireSkyTelemetryTcpMode(deviceSkyTelemetry);
    setSkyTelemetryTransport(deviceSkyTelemetry.transportCombo, QStringLiteral("serial"));
    processEventsFor(100);
    activateLayouts(&window);
    requireSkyTelemetrySerialMode(deviceSkyTelemetry);
    setSkyTelemetryTransport(deviceSkyTelemetry.transportCombo, QStringLiteral("tcp"));
    processEventsFor(100);
    activateLayouts(&window);
    require(epsilonConfigCard->isVisible(),
            "device EPSILON configuration card stays visible in sky-ground remote mode");
    require(deviceTelemetrySummaryCard->isVisible(),
            "device telemetry summary remains visible after switching to sky-ground remote mode");
    requireSameRect(epsilonConfigCard->geometry(), localEpsilonConfigRect, 2,
                    "device EPSILON configuration card geometry is stable in sky-ground remote mode");
    requireSameRect(deviceTelemetrySummaryCard->geometry(), localTelemetrySummaryRect, 2,
                    "device telemetry summary geometry is stable in sky-ground remote mode");
    deviceSourceModeCombo->setCurrentIndex(0);
    processEventsFor(150);
    activateLayouts(&window);
    require(epsilonConfigCard->isVisible(),
            "device EPSILON configuration card stays visible after switching back to local mode");
    require(deviceTelemetrySummaryCard->isVisible(),
            "device telemetry summary remains visible after switching back to local mode");
    requireSameRect(epsilonConfigCard->geometry(), localEpsilonConfigRect, 2,
                    "device EPSILON configuration card geometry is stable after switching back to local mode");
    requireSameRect(deviceTelemetrySummaryCard->geometry(), localTelemetrySummaryRect, 2,
                    "device telemetry summary geometry is stable after switching back to local mode");

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
    const bool sensorCardsStacked =
        std::abs(epsilonGroup->x() - environmentGroup->x()) <= 4 &&
        environmentGroup->y() > epsilonGroup->y();
    if (sensorCardsStacked)
    {
        require(std::abs(epsilonGroup->width() - environmentGroup->width()) <= 4,
                "compact sensor cards keep matching widths");
        require(environmentGroup->y() >= epsilonGroup->geometry().bottom(),
                "compact environment card is stacked below the EPSILON card");
    }
    else
    {
        const int sensorRowWidth = epsilonGroup->width() + environmentGroup->width();
        require(sensorRowWidth > 0, "sensor row has measurable width");
        const double environmentRatio =
            static_cast<double>(environmentGroup->width()) / static_cast<double>(sensorRowWidth);
        require(environmentRatio >= 0.17 && environmentRatio <= 0.23,
                "environment and lidar card stays close to one fifth of the sensor row at wide widths");
        require(epsilonGroup->width() >= environmentGroup->width() * 3.6,
                "EPSILON card keeps an approximately 4:1 width relationship against environment card");
    }
    QList<QFrame*> wideCards = dataGroup->findChildren<QFrame *>(QStringLiteral("epsilonSectionCard"));
    require(wideCards.size() == 3, "three EPSILON section cards at wide window size");
    std::sort(wideCards.begin(), wideCards.end(), [](const QFrame *lhs, const QFrame *rhs) {
        if (std::abs(lhs->y() - rhs->y()) > 4)
        {
            return lhs->y() < rhs->y();
        }
        return lhs->x() < rhs->x();
    });
    int wideCardsRight = 0;
    int wideCardsTotalWidth = 0;
    int wideCardsTotalMinimumWidth = 0;
    for (const QFrame *card : wideCards)
    {
        wideCardsRight = std::max(wideCardsRight,
                                  card->mapTo(epsilonGroup, QPoint(card->width(), 0)).x());
        wideCardsTotalWidth += card->width();
        wideCardsTotalMinimumWidth += card->minimumWidth();
        require(card->width() + 2 >= card->minimumWidth(),
                "EPSILON section cards keep their content-driven minimum width at wide window size");
    }
    require(wideCardsRight >= epsilonGroup->contentsRect().right() - 8,
            "EPSILON section cards expand to fill the available group width at wide window size");
    require(wideCardsTotalWidth > wideCardsTotalMinimumWidth + 24,
            "EPSILON section cards grow beyond their minimum widths at wide window size");
    for (const QFrame *card : wideCards)
    {
        const double actualRatio =
            static_cast<double>(card->width()) / static_cast<double>(wideCardsTotalWidth);
        const double minimumRatio =
            static_cast<double>(card->minimumWidth()) / static_cast<double>(wideCardsTotalMinimumWidth);
        require(std::abs(actualRatio - minimumRatio) <= 0.06,
                "EPSILON section cards expand proportionally to their content widths");
    }
    require(std::abs(wideCards.at(0)->height() - wideCards.at(1)->height()) <= 2 &&
                std::abs(wideCards.at(0)->height() - wideCards.at(2)->height()) <= 2,
            "EPSILON section cards have matching heights at wide window size");

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
    require(std::abs(cards.at(0)->height() - cards.at(1)->height()) <= 2 &&
                std::abs(cards.at(0)->height() - cards.at(2)->height()) <= 2,
            "EPSILON section cards have matching heights at default window size");

    for (QTimer *timer : window.findChildren<QTimer *>())
    {
        timer->stop();
    }

    QStringList sampleValues = {
        QStringLiteral("9999-12-31T23:59:59.999Z"),
        QStringLiteral("18446744073709551615 us"),
        QStringLiteral("原始 4294967295 / 丢帧 4294967295"),
        QStringLiteral("0xFFFF 已初始化 / 定位融合中"),
        QStringLiteral("9999.999m/9999.999m"),
        QStringLiteral("-9999.999/9999.999/-9999.999"),
        QStringLiteral("-9999.999/9999.999/-9999.999"),
        QStringLiteral("-9999.9999/9999.9999/-9999.9999"),
        QStringLiteral("-180.00/90.00/359.99")
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

    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        settings.setValue(QStringLiteral("font_scale_percent"), 130);
        settings.sync();

        MainWindow scaledWindow;
        scaledWindow.resize(1664, 1040);
        scaledWindow.show();
        processEventsFor(500);
        QPushButton *scaledTemperatureNavButton = nullptr;
        for (QPushButton *button : scaledWindow.findChildren<QPushButton *>())
        {
            if (button->accessibleName() == QStringLiteral("温控") ||
                button->accessibleName() == QStringLiteral("Thermal"))
            {
                scaledTemperatureNavButton = button;
                break;
            }
        }
        require(scaledTemperatureNavButton != nullptr,
                "scaled temperature sidebar button exists");
        clickWidget(scaledTemperatureNavButton, 150);
        activateLayouts(&scaledWindow);
        auto *scaledTemperaturePanel = scaledWindow.findChild<TemperatureControllerPanel *>();
        auto *scaledCommonParamsStack =
            scaledTemperaturePanel
                ? scaledTemperaturePanel->findChild<QStackedWidget *>(QStringLiteral("temperatureChannelConfigSubStackChannel1"))
                : nullptr;
        auto *scaledTargetSpin =
            scaledTemperaturePanel
                ? scaledTemperaturePanel->findChild<QDoubleSpinBox *>(QStringLiteral("temperatureTargetSpinChannel1"))
                : nullptr;
        require(scaledCommonParamsStack != nullptr &&
                    scaledCommonParamsStack->currentWidget() != nullptr &&
                    scaledCommonParamsStack->currentWidget()->objectName() ==
                        QStringLiteral("temperatureChannelCommonParamsPageChannel1") &&
                    scaledTargetSpin != nullptr,
                "scaled temperature lower common controls are discoverable");
        const QRect scaledTargetRect(scaledTargetSpin->mapTo(scaledCommonParamsStack, QPoint(0, 0)),
                                    scaledTargetSpin->size());
        require(scaledTargetRect.left() >= scaledCommonParamsStack->contentsRect().left() &&
                    scaledTargetRect.right() <= scaledCommonParamsStack->contentsRect().right(),
                "temperature target input is fully visible in the lower common controls on the first scaled opening");
        scaledWindow.close();
        processEventsFor(100);
        settings.setValue(QStringLiteral("font_scale_percent"), 100);
        settings.sync();
    }

    window.close();
    processEventsFor(100);
    std::cout << "main_window_layout_test passed\n";
    return 0;
}
