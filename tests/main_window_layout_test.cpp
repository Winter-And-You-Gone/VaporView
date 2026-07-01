#include "AppTheme.h"
#include "MainWindow.h"
#include "RtkConfigDialog.h"

#include <QApplication>
#include <QAction>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFrame>
#include <QGroupBox>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QPixmap>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
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
    require(actionCard->width() >= dialog->width() - 40,
            "RTK bottom actions are collected in a full-width service card");
    auto *serverEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkServerEdit"));
    auto *usernameEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkUsernameEdit"));
    auto *portEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkPortEdit"));
    auto *passwordEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkPasswordEdit"));
    auto *mountpointCombo = dialog->findChild<QComboBox *>(QStringLiteral("rtkMountpointCombo"));
    auto *fetchMountpointsButton = dialog->findChild<QPushButton *>(QStringLiteral("rtkFetchMountpointsButton"));
    require(serverEdit != nullptr && usernameEdit != nullptr && portEdit != nullptr &&
                passwordEdit != nullptr && mountpointCombo != nullptr && fetchMountpointsButton != nullptr,
            "RTK NTRIP compact fields exist for alignment checks");
    const int rtkInputHeight = serverEdit->height();
    const QList<QPushButton*> rtkPushButtons = dialog->findChildren<QPushButton *>();
    for (QPushButton *button : rtkPushButtons)
    {
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

QFrame *firstTelemetrySection(QWidget *summaryContainer)
{
    if (!summaryContainer)
    {
        return nullptr;
    }

    QList<QFrame*> sections =
        summaryContainer->findChildren<QFrame *>(QStringLiteral("homeTelemetrySectionCard"));
    std::sort(sections.begin(), sections.end(), [](QFrame *a, QFrame *b) {
        return a->mapTo(a->parentWidget(), QPoint(0, 0)).y() <
               b->mapTo(b->parentWidget(), QPoint(0, 0)).y();
    });
    return sections.isEmpty() ? nullptr : sections.first();
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
    if (rightPadding < 12 || rightPadding > 24)
    {
        std::cerr << "Home rate right padding: " << rightPadding
                  << " section width: " << rateSection->width()
                  << " device card width: " << (deviceOverviewCard ? deviceOverviewCard->width() : 0)
                  << " device card min width: " << (deviceOverviewCard ? deviceOverviewCard->minimumWidth() : 0)
                  << '\n';
    }
    require(rightPadding >= 12, message);
    require(rightPadding <= 24, "home data-stream telemetry row avoids excessive right-side blank space");
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
        settings.setValue(QStringLiteral("dark_theme_enabled"), false);
        settings.setValue(QStringLiteral("serial/temperature_baud"), QStringLiteral("38400"));
        settings.setValue(QStringLiteral("rate/temperature"), QStringLiteral("5"));
        settings.sync();
    }

    MainWindow window;
    window.setWindowTitle(QStringLiteral("VaporView"));
    window.resize(1280, 800);
    window.show();
    processEventsFor(500);
    require(qApp->styleSheet().contains(QStringLiteral("square.svg")) &&
                qApp->styleSheet().contains(QStringLiteral("square-check-big.svg")) &&
                !qApp->styleSheet().contains(QStringLiteral("lucide/check.svg")),
            "checkbox indicators use lucide square and square-check-big icons");
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
    require(deviceOverviewCard->minimumWidth() >= 540,
            "device overview card keeps a practical minimum width");
    require(deviceOverviewCard->minimumWidth() < 620,
            "device overview card minimum width follows its telemetry content instead of a fixed wide floor");

    auto *homeConfigCard = deviceOverviewCard;
    require(homeConfigCard != nullptr, "home configuration card exists");
    const QRect homeConfigLocalRect = homeConfigCard->geometry();
    QComboBox *homeSourceModeCombo = nullptr;
    const QList<QComboBox*> homeConfigCombos = homeConfigCard->findChildren<QComboBox *>();
    for (QComboBox *combo : homeConfigCombos)
    {
        if (combo->count() < 2)
        {
            continue;
        }
        const QString localText = combo->itemText(0);
        const QString remoteText = combo->itemText(1);
        if ((localText.contains(QStringLiteral("本地")) || localText.contains(QStringLiteral("Local"))) &&
            (remoteText.contains(QStringLiteral("天空")) || remoteText.contains(QStringLiteral("Sky"))))
        {
            homeSourceModeCombo = combo;
            break;
        }
    }
    require(homeSourceModeCombo != nullptr, "home source mode combo exists");
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
                    "home configuration card geometry is stable in sky-ground receive mode");
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

    const QList<QFrame*> homeTelemetryPills =
        deviceOverviewCard->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
    require(!homeTelemetryPills.isEmpty(),
            "home device overview telemetry pills exist before dark theme switch");
    QWidget *homeTelemetrySummaryContainer =
        deviceOverviewCard->findChild<QWidget *>(QStringLiteral("homeTelemetrySummaryContainer"));
    require(homeTelemetrySummaryContainer != nullptr,
            "home device overview telemetry summary container exists before dark theme switch");
    QFrame *homeRateSection = firstTelemetrySection(homeTelemetrySummaryContainer);
    require(homeRateSection != nullptr,
            "home device overview telemetry sections exist before dark theme switch");
    const QList<QFrame*> ratePills =
        homeRateSection->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
    require(!ratePills.isEmpty(),
            "home data-stream telemetry section has value pills");
    bool tcpActualRateShowsZero = false;
    for (QFrame *pill : ratePills)
    {
        QLabel *nameLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel"));
        QLabel *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
        if (nameLabel && valueLabel && nameLabel->text().contains(QStringLiteral("TCP")))
        {
            require(valueLabel->text() == QStringLiteral("0.0 Hz"),
                    "home wave TCP actual rate uses the same zero-frequency text as other rates");
            tcpActualRateShowsZero = true;
        }
    }
    require(tcpActualRateShowsZero,
            "home data-stream telemetry section exposes the wave TCP actual rate");
    requireTelemetryRightPadding(deviceOverviewCard,
                                 homeRateSection,
                                 "home data-stream telemetry row keeps right-side breathing room");
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
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QFrame#homeTelemetrySummaryPill {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceAlt, true),
                                 "dark theme overrides home telemetry summary pill background");
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
    int disabledLocalRemoteActionCount = 0;
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
        if (button->text() == QStringLiteral("连接") ||
            button->text() == QStringLiteral("断开") ||
            button->text() == QStringLiteral("重连") ||
            button->text() == QStringLiteral("Connect") ||
            button->text() == QStringLiteral("Disconnect") ||
            button->text() == QStringLiteral("Reconnect"))
        {
            require(!button->isEnabled(),
                    "device configuration remote actions are visible but disabled in local mode");
            ++disabledLocalRemoteActionCount;
        }
    }
    require(disabledLocalRemoteActionCount >= 15,
            "device configuration keeps all remote device actions present in local mode");
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
    int rightmostReconnectButtonRight = -1;
    for (QPushButton *button : deviceConfigPage->findChildren<QPushButton *>())
    {
        if (!button->isVisible())
        {
            continue;
        }
        if (button->text() != QStringLiteral("连接") &&
            button->text() != QStringLiteral("断开") &&
            button->text() != QStringLiteral("重连") &&
            button->text() != QStringLiteral("Connect") &&
            button->text() != QStringLiteral("Disconnect") &&
            button->text() != QStringLiteral("Reconnect"))
        {
            continue;
        }
        const QRect buttonRect(button->mapTo(deviceConfigPage, QPoint(0, 0)), button->size());
        if (button->text() == QStringLiteral("重连") ||
            button->text() == QStringLiteral("Reconnect"))
        {
            rightmostReconnectButtonRight = std::max(rightmostReconnectButtonRight, buttonRect.right());
        }
        if (buttonRect.left() > deviceRateRect.right() &&
            std::abs(buttonRect.center().y() - deviceRateRect.center().y()) <= 2)
        {
            foundRemoteButtonsToRightOfRate = true;
        }
    }
    require(foundRemoteButtonsToRightOfRate,
            "device configuration remote actions sit to the right of the rate selector");
    require(rightmostReconnectButtonRight > 0 &&
                serialConfigPageRect.right() - rightmostReconnectButtonRight <= 32,
            "device configuration serial card right edge stays close to the reconnect buttons");

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

    QComboBox *deviceSourceModeCombo = nullptr;
    for (QComboBox *combo : deviceConfigPage->findChildren<QComboBox *>())
    {
        if (!combo->isVisible() || combo->count() < 2)
        {
            continue;
        }
        const QString localText = combo->itemText(0);
        const QString remoteText = combo->itemText(1);
        if ((localText.contains(QStringLiteral("本地")) || localText.contains(QStringLiteral("Local"))) &&
            (remoteText.contains(QStringLiteral("天空")) || remoteText.contains(QStringLiteral("Sky"))))
        {
            deviceSourceModeCombo = combo;
            break;
        }
    }
    require(deviceSourceModeCombo != nullptr,
            "device configuration source mode combo exists");
    require(deviceSourceModeCombo->width() <= 160,
            "device configuration source mode combo uses compact width");
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
            "device EPSILON configuration card stays visible in sky-ground receive mode");
    require(deviceTelemetrySummaryCard->isVisible(),
            "device telemetry summary remains visible after switching to sky-ground receive mode");
    requireSameRect(epsilonConfigCard->geometry(), localEpsilonConfigRect, 2,
                    "device EPSILON configuration card geometry is stable in sky-ground receive mode");
    requireSameRect(deviceTelemetrySummaryCard->geometry(), localTelemetrySummaryRect, 2,
                    "device telemetry summary geometry is stable in sky-ground receive mode");
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
