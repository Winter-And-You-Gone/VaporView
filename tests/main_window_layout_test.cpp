#include "shared/theme/AppTheme.h"
#include "shared/theme/TopLevelCardStyle.h"
#include "ground/main/MainWindow.h"
#include "ground/main/GroundMainWindowSupport.h"
#include "ground/navigation/CombinationNavigationPage.h"
#include "ground/navigation/EpsilonConfigPanel.h"
#include "ground/rtk/RtkConfigDialog.h"
#include "ground/widgets/TelemetryPanels.h"
#include "ground/widgets/VisualTextLabel.h"
#include "shared/config/ApplicationConfig.h"
#include "shared/theme/SingleLevelPopupMenu.h"
#include "test_ui_helpers.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QAction>
#include <QColor>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QFocusEvent>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QHoverEvent>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QMetaObject>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStyleOptionFrame>
#include <QStyleOptionSpinBox>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextOption>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWidget>
#include <algorithm>
#include <array>
#include <cmath>
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

void requireTopLevelCardElevation(QWidget *card, qreal expectedScale, const char *message)
{
    require(card != nullptr, message);
    require(card->property(VaporView::kTopLevelCardProperty).toBool(), message);
    require(card->graphicsEffect() == nullptr, message);

    QWidget *shadowHost = card;
    for (QWidget *ancestor = card; ancestor && ancestor != card->window();
         ancestor = ancestor->parentWidget())
    {
        shadowHost = ancestor;
        auto *scrollArea =
            qobject_cast<QAbstractScrollArea *>(ancestor->parentWidget());
        if (scrollArea && scrollArea->viewport() == ancestor)
        {
            shadowHost = scrollArea;
            break;
        }
    }

    QWidget *shadowLayer = shadowHost->findChild<QWidget *>(
        QString::fromLatin1(VaporView::kTopLevelCardShadowLayerName),
        Qt::FindDirectChildrenOnly);
    require(shadowLayer != nullptr &&
                shadowLayer->testAttribute(Qt::WA_TransparentForMouseEvents) &&
                shadowLayer->focusPolicy() == Qt::NoFocus,
            message);
    require(std::abs(shadowLayer
                         ->property("vaporViewTopLevelCardShadowBlurRadius")
                         .toDouble() -
                     VaporView::kTopLevelCardShadowBlurRadius * expectedScale) <= 0.1 &&
                std::abs(shadowLayer
                             ->property("vaporViewTopLevelCardShadowOffsetY")
                             .toDouble() -
                         VaporView::kTopLevelCardShadowOffsetY * expectedScale) <= 0.1 &&
                shadowLayer
                        ->property("vaporViewTopLevelCardShadowColor")
                        .value<QColor>() ==
                    QColor(0, 0, 0, VaporView::kTopLevelCardShadowAlpha) &&
                shadowLayer
                        ->property("vaporViewTopLevelCardShadowCardCount")
                        .toInt() > 0,
            message);
}

class ResizeHeightRecorder final : public QObject
{
public:
    explicit ResizeHeightRecorder(QWidget *target)
        : target_(target)
    {
    }

    void reset()
    {
        observed_heights_.clear();
    }

    bool observedHeightDifferentFrom(int expectedHeight) const
    {
        return std::any_of(
            observed_heights_.cbegin(),
            observed_heights_.cend(),
            [expectedHeight](int height) {
                return height != expectedHeight;
            });
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == target_ && event->type() == QEvent::Resize)
        {
            observed_heights_.append(
                static_cast<QResizeEvent *>(event)->size().height());
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget *target_;
    QVector<int> observed_heights_;
};

class ResizeWidthRecorder final : public QObject
{
public:
    explicit ResizeWidthRecorder(QWidget *target)
        : target_(target)
    {
    }

    void reset()
    {
        observed_widths_.clear();
        observed_paintable_widths_.clear();
        observed_width_details_.clear();
    }

    bool observedWidthDifferentFrom(int expectedWidth) const
    {
        return std::any_of(
            observed_widths_.cbegin(),
            observed_widths_.cend(),
            [expectedWidth](int width) {
                return width != expectedWidth;
            });
    }

    bool observedPaintableWidthDifferentFrom(int expectedWidth) const
    {
        return std::any_of(
            observed_paintable_widths_.cbegin(),
            observed_paintable_widths_.cend(),
            [expectedWidth](int width) {
                return width != expectedWidth;
            });
    }

    QString observedWidthDetailsSummary() const
    {
        return observed_width_details_.join(QLatin1Char(','));
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == target_ && event->type() == QEvent::Resize)
        {
            const int width = static_cast<QResizeEvent *>(event)->size().width();
            observed_widths_.append(width);
            const bool paintable = isEffectivelyPaintable();
            if (paintable)
            {
                observed_paintable_widths_.append(width);
            }
            observed_width_details_.append(QStringLiteral("%1:p%2")
                                               .arg(width)
                                               .arg(paintable ? 1 : 0));
        }
        return QObject::eventFilter(watched, event);
    }

private:
    bool isEffectivelyPaintable() const
    {
        for (QWidget *widget = target_; widget; widget = widget->parentWidget())
        {
            if (!widget->isVisible() || !widget->updatesEnabled())
            {
                return false;
            }
        }
        return true;
    }

    QWidget *target_;
    QVector<int> observed_widths_;
    QVector<int> observed_paintable_widths_;
    QStringList observed_width_details_;
};

QAction *findAboutAction(MainWindow *window)
{
    if (!window)
    {
        return nullptr;
    }

    for (QAction *action : window->findChildren<QAction *>())
    {
        if (action && (action->text() == QStringLiteral("关于(&A)") ||
                       action->text() == QStringLiteral("&About")))
        {
            return action;
        }
    }
    return nullptr;
}

QAction *findCheckUpdatesAction(MainWindow *window)
{
    if (!window)
    {
        return nullptr;
    }
    return window->findChild<QAction *>(QStringLiteral("checkUpdatesAction"));
}

void requireAboutDialogLayout(MainWindow *window,
                              QAction *aboutAction,
                              bool english,
                              const QString& expectedVersion)
{
    require(window != nullptr && aboutAction != nullptr,
            "about dialog test has a window and action");

    bool inspected = false;
    QTimer::singleShot(0, window, [&]() {
        auto *dialog = window->findChild<QDialog *>(QStringLiteral("aboutDialog"));
        require(dialog != nullptr && dialog->isVisible(),
                "about action opens the custom about dialog");
        require(dialog->windowTitle() == (english ? QStringLiteral("About VaporView")
                                                  : QStringLiteral("关于 VaporView")),
                "about dialog title follows the interface language");
        require(dialog->isModal(), "about dialog is modal");
        require(dialog->size() == QSize(500, 365) &&
                    dialog->minimumSize() == QSize(480, 280),
                "about dialog matches the update dialog reference size");

        auto *body = dialog->findChild<QWidget *>(QStringLiteral("aboutDialogBody"));
        auto *footer = dialog->findChild<QWidget *>(QStringLiteral("aboutDialogFooter"));
        auto *logo = dialog->findChild<QLabel *>(QStringLiteral("aboutDialogLogo"));
        auto *productName = dialog->findChild<QLabel *>(QStringLiteral("aboutDialogProductNameLabel"));
        auto *description = dialog->findChild<QLabel *>(QStringLiteral("aboutDialogDescriptionLabel"));
        auto *framework = dialog->findChild<QLabel *>(QStringLiteral("aboutDialogFrameworkLabel"));
        auto *version = dialog->findChild<QLabel *>(QStringLiteral("aboutDialogVersionLabel"));
        auto *copyright = dialog->findChild<QLabel *>(QStringLiteral("aboutDialogCopyrightLabel"));
        auto *okButton = dialog->findChild<QPushButton *>(QStringLiteral("aboutDialogOkButton"));
        require(body != nullptr && footer != nullptr && logo != nullptr && productName != nullptr &&
                    description != nullptr && framework != nullptr && version != nullptr &&
                    copyright != nullptr && okButton != nullptr,
                "about dialog exposes the update-sized reference layout");
        const QList<QLabel *> aboutLabels = dialog->findChildren<QLabel *>();
        require(dialog->findChild<QLabel *>(QStringLiteral("aboutDialogSupportedDevicesLabel")) == nullptr &&
                    aboutLabels.cend() ==
                        std::find_if(aboutLabels.cbegin(),
                                     aboutLabels.cend(),
                                     [](QLabel *label) {
                                         return label && (label->text().contains(QStringLiteral("EPSILON")) ||
                                                          label->text().contains(QStringLiteral("RD105")));
                                     }),
                "about dialog omits the supported hardware summary");
        require(!logo->pixmap().isNull() && logo->width() == logo->height(),
                "about dialog renders the square VaporView logo");
        require(productName->text() == QStringLiteral("VaporView") &&
                    productName->font().pointSizeF() > description->font().pointSizeF(),
                "about dialog gives the product name the strongest text hierarchy");
        require(description->text() ==
                    (english
                         ? QStringLiteral("Integrated Navigation and Environmental Monitoring System")
                         : QStringLiteral("组合导航与环境监控系统")),
                "about dialog describes the product in the active language");
        require(framework->text() == (english ? QStringLiteral("Built with Qt 6")
                                                   : QStringLiteral("基于 Qt 6 构建")),
                "about dialog identifies the Qt 6 foundation");
        require(version->text() == (english
                                         ? QStringLiteral("Version %1").arg(expectedVersion)
                                         : QStringLiteral("版本 %1").arg(expectedVersion)),
                "about dialog uses the runtime application version with a stable fallback");
        require(copyright->text() == QStringLiteral("© 2026 VaporView"),
                "about dialog shows the product copyright");
        require(okButton->text() == (english ? QStringLiteral("OK") : QStringLiteral("确定")),
                "about dialog confirmation button follows the active language");
        require(okButton->isDefault(),
                "about dialog confirmation button remains the default action");
        require(okButton->size() == QSize(74, 30),
                "about dialog confirmation button keeps the compact fixed size");
        require(okButton->mapTo(footer, QPoint(0, 0)).x() > footer->width() / 2,
                "about dialog confirmation button stays on the right side of the footer");
        require(footer->styleSheet().isEmpty() &&
                    dialog->styleSheet().contains(QStringLiteral("QWidget#aboutDialogFooter")) &&
                    dialog->styleSheet().contains(QStringLiteral("border-top: 1px solid")),
                "about dialog footer is a separately styled action band");

        QToolButton *languageButton = nullptr;
        QToolButton *themeButton = nullptr;
        for (QToolButton *button : dialog->findChildren<QToolButton *>())
        {
            if (button->accessibleName() == QStringLiteral("titleLanguageButton"))
            {
                languageButton = button;
            }
            else if (button->accessibleName() == QStringLiteral("titleThemeButton"))
            {
                themeButton = button;
            }
        }
        auto *separator = dialog->findChild<QFrame *>(QStringLiteral("titleBarSeparator"));
        auto *closeButton = dialog->findChild<QToolButton *>(QStringLiteral("windowCloseButton"));
        auto *titleLogo = dialog->findChild<QLabel *>(QStringLiteral("customTitleLogo"));
        require(languageButton != nullptr && themeButton != nullptr &&
                    !languageButton->isVisible() && !themeButton->isVisible() &&
                    separator != nullptr && !separator->isVisible(),
                "about title bar hides unrelated language and theme controls");
        require(titleLogo != nullptr && titleLogo->size() == QSize(34, 34) &&
                    titleLogo->property("customTitleLogoSize").toInt() == 34 &&
                    titleLogo->property("customTitleLogoRenderSize").toInt() == 28 &&
                    !titleLogo->pixmap().isNull() &&
                    titleLogo->pixmap().deviceIndependentSize().toSize() == QSize(28, 28),
                "about title bar matches the update dialog logo size");
        QEvent styleChange(QEvent::StyleChange);
        QCoreApplication::sendEvent(dialog, &styleChange);
        require(titleLogo->size() == QSize(34, 34) &&
                    titleLogo->property("customTitleLogoSize").toInt() == 34 &&
                    titleLogo->property("customTitleLogoRenderSize").toInt() == 28 &&
                    !titleLogo->pixmap().isNull() &&
                    titleLogo->pixmap().deviceIndependentSize().toSize() == QSize(28, 28),
                "about title bar keeps the update dialog logo size after a title bar refresh");
        require(closeButton != nullptr && closeButton->isVisible() &&
                    closeButton->focusPolicy() == Qt::TabFocus,
                "about title bar retains an accessible close control");

        inspected = true;
        dialog->accept();
    });

    aboutAction->trigger();
    require(inspected, "about dialog layout was inspected before closing");
}

using VaporViewTest::processEventsFor;
using VaporViewTest::processEventsUntil;
using VaporViewTest::waitForWindowExposed;

void requireComboPopupStyled(QComboBox *combo, const char *message)
{
    VaporViewTest::requireComboPopupStyled(combo, message, require);
}

void requireComboPopupFloatingContainer(QComboBox *combo, const char *message)
{
    requireComboPopupStyled(combo, message);

    QAbstractItemView *view = combo->view();
    require(view != nullptr, "combo popup view exists before opening");
    combo->showPopup();
    require(processEventsUntil(1000, [view]() {
                QWidget *container = view->window();
                return view->isVisible() && container && container->isVisible() &&
                       container->width() > 0 && container->height() > 0;
            }),
            "combo popup becomes visible with a usable native container");
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
    require(processEventsUntil(1000, [view]() { return !view->isVisible(); }),
            "combo popup closes after the live container audit");
}

QString localSerialPortValue(QComboBox *combo)
{
    if (!combo)
    {
        return QString();
    }
    const QString data = combo->currentData().toString().trimmed();
    if (!data.isEmpty() && data != QStringLiteral("__vv_manual_serial_port__"))
    {
        return data;
    }
    QString text = combo->currentText().trimmed();
    if (text.startsWith(QStringLiteral("历史：")))
    {
        text = text.mid(QStringLiteral("历史：").size()).trimmed();
    }
    else if (text.startsWith(QStringLiteral("历史:")))
    {
        text = text.mid(QStringLiteral("历史:").size()).trimmed();
    }
    else if (text.startsWith(QStringLiteral("History:"), Qt::CaseInsensitive))
    {
        text = text.mid(QStringLiteral("History:").size()).trimmed();
    }
    if (text == QStringLiteral("未选择") ||
        text.startsWith(QStringLiteral("--")) ||
        text == QStringLiteral("手动添加") ||
        text == QStringLiteral("Add Port"))
    {
        return QString();
    }
    return text;
}

void requireLocalSerialPortComboReady(QComboBox *combo, const char *message)
{
    require(combo != nullptr, message);
    require(!combo->isEditable(), message);
    require(combo->findText(QStringLiteral("手动添加")) >= 0 ||
                combo->findText(QStringLiteral("Add Port")) >= 0,
            message);
    require(combo->findData(QStringLiteral("__vv_manual_serial_port__")) >= 0,
            message);
    require(combo->view() != nullptr &&
                combo->view()->itemDelegate() != nullptr &&
                combo->view()->itemDelegate()->property("vaporViewLocalSerialHistoryDelegate").toBool(),
            message);
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

void requireSelectableCardTitle(QLabel *titleLabel, const char *message)
{
    require(titleLabel != nullptr, message);
    const Qt::TextInteractionFlags flags = titleLabel->textInteractionFlags();
    QWidget *cluster = titleLabel->parentWidget();
    if (dynamic_cast<VaporView::VisualTextLabel *>(titleLabel))
    {
        require(!flags.testFlag(Qt::TextSelectableByMouse), message);
    }
    else
    {
        require(flags.testFlag(Qt::TextSelectableByMouse), message);
    }
    require(flags.testFlag(Qt::TextSelectableByKeyboard), message);
    require(!titleLabel->testAttribute(Qt::WA_TransparentForMouseEvents), message);
    if (cluster && cluster->objectName() == QStringLiteral("sectionTitleCluster"))
    {
        require(!cluster->testAttribute(Qt::WA_TransparentForMouseEvents), message);
    }
}

void requireCardTitleMouseSelectionAndCopy(QLabel *titleLabel)
{
    require(titleLabel != nullptr, "card title selection target exists");
    require(titleLabel->isVisibleTo(titleLabel->window()), "card title selection target is visible");
    require(titleLabel->text().size() >= 2, "card title selection target contains selectable text");

    const QRect contentRect = titleLabel->contentsRect();
    const int textWidth = titleLabel->fontMetrics().horizontalAdvance(titleLabel->text());
    const QPoint start(contentRect.left() + 2, contentRect.center().y());
    const QPoint end(std::min(contentRect.right() - 2,
                              contentRect.left() + std::max(8, textWidth - 2)),
                     contentRect.center().y());
    require(end.x() > start.x(), "card title selection target has a usable drag width");

    const QPoint globalStart = titleLabel->mapToGlobal(start);
    const QPoint globalEnd = titleLabel->mapToGlobal(end);
    require(titleLabel->rect().contains(start) && titleLabel->rect().contains(end),
            "card title drag points remain inside the selectable label");

    titleLabel->setSelection(0, 0);
    qApp->clipboard()->setText(QStringLiteral("card-title-copy-sentinel"));

    QMouseEvent press(QEvent::MouseButtonPress,
                      start,
                      globalStart,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(titleLabel, &press);
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     globalEnd,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    QCoreApplication::sendEvent(titleLabel, &move);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        end,
                        globalEnd,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(titleLabel, &release);
    processEventsFor(50);

    require(titleLabel->hasSelectedText(), "card title mouse drag creates a text selection");

    titleLabel->repaint();
    processEventsFor(20);
    const QImage selectedImage = titleLabel->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QColor activeHighlight = titleLabel->palette().color(QPalette::Active, QPalette::Highlight);
    const QColor inactiveHighlight = titleLabel->palette().color(QPalette::Inactive, QPalette::Highlight);
    const auto matchesHighlight = [&](const QColor& pixel, const QColor& highlight) {
        return std::abs(pixel.red() - highlight.red()) <= 8 &&
            std::abs(pixel.green() - highlight.green()) <= 8 &&
            std::abs(pixel.blue() - highlight.blue()) <= 8;
    };
    int highlightPixelCount = 0;
    for (int y = 0; y < selectedImage.height(); ++y)
    {
        for (int x = 0; x < selectedImage.width(); ++x)
        {
            const QColor pixel = selectedImage.pixelColor(x, y);
            if (matchesHighlight(pixel, activeHighlight) ||
                matchesHighlight(pixel, inactiveHighlight))
            {
                ++highlightPixelCount;
            }
        }
    }
    require(highlightPixelCount > 0, "card title paints the selection highlight");

    QApplication::setActiveWindow(titleLabel->window());
    titleLabel->setFocus(Qt::MouseFocusReason);
    require(titleLabel->hasFocus(),
            "selected card title accepts keyboard focus before copying");
    QKeyEvent copyPress(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QCoreApplication::sendEvent(titleLabel, &copyPress);
    QKeyEvent copyRelease(QEvent::KeyRelease, Qt::Key_C, Qt::ControlModifier);
    QCoreApplication::sendEvent(titleLabel, &copyRelease);
    processEventsFor(20);
    require(qApp->clipboard()->text() == titleLabel->selectedText(),
            "card title copies the selected text with Ctrl+C");

    bool contextMenuShown = false;
    QTimer::singleShot(0, qApp, [&contextMenuShown]() {
        for (QWidget *topLevel : QApplication::topLevelWidgets())
        {
            auto *menu = qobject_cast<QMenu *>(topLevel);
            if (menu && menu->isVisible())
            {
                contextMenuShown = true;
                menu->hide();
            }
        }
    });
    const QPoint contextPosition = titleLabel->rect().center();
    QContextMenuEvent contextEvent(QContextMenuEvent::Mouse,
                                   contextPosition,
                                   titleLabel->mapToGlobal(contextPosition));
    contextEvent.ignore();
    QCoreApplication::sendEvent(titleLabel, &contextEvent);
    processEventsFor(20);
    require(contextEvent.isAccepted() && !contextMenuShown,
            "card title suppresses the standard context menu");
}

void requireCardTitleBar(QWidget *card,
                         const QStringList& expectedTitles,
                         const QString& expectedIconName,
                         const char *message)
{
    require(card != nullptr, message);
    QWidget *matchedTitleBar = nullptr;
    QLabel *matchedTitleLabel = nullptr;
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
                matchedTitleLabel = titleLabel;
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
    requireSelectableCardTitle(matchedTitleLabel,
                               "card title text is selectable and copyable");
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

class MinimumHeightRecorder final : public QObject
{
public:
    explicit MinimumHeightRecorder(QWidget *widget)
        : QObject(widget)
        , widget_(widget)
        , minimum_height_(widget ? widget->height() : 0)
    {
        if (widget_)
        {
            widget_->installEventFilter(this);
        }
    }

    int minimumHeight() const
    {
        return minimum_height_;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == widget_ && event->type() == QEvent::Resize && widget_)
        {
            minimum_height_ = std::min(minimum_height_, widget_->height());
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget *widget_ = nullptr;
    int minimum_height_ = 0;
};

class ResizeEventCounter final : public QObject
{
public:
    explicit ResizeEventCounter(QWidget *widget)
        : QObject(widget)
        , widget_(widget)
    {
        if (widget_)
        {
            widget_->installEventFilter(this);
        }
    }

    int count() const
    {
        return count_;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == widget_ && event->type() == QEvent::Resize)
        {
            ++count_;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget *widget_ = nullptr;
    int count_ = 0;
};

struct VerticalDragContext
{
    QWidget *widget = nullptr;
    QPoint localStart;
    QPoint globalStart;
};

VerticalDragContext beginVerticalDrag(QWidget *widget)
{
    require(widget != nullptr, "vertical drag widget exists");
    VerticalDragContext context;
    context.widget = widget;
    context.localStart = widget->rect().center();
    context.globalStart = widget->mapToGlobal(context.localStart);
    context.widget->setProperty(
        VaporView::Ground::MainSupport::kMainCardResizeTestCursorYProperty,
        context.globalStart.y());
    QMouseEvent press(QEvent::MouseButtonPress,
                      context.localStart,
                      context.globalStart,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &press);
    return context;
}

void moveVerticalDrag(const VerticalDragContext& context, int offset)
{
    context.widget->setProperty(
        VaporView::Ground::MainSupport::kMainCardResizeTestCursorYProperty,
        context.globalStart.y() + offset);
    QMouseEvent move(QEvent::MouseMove,
                     context.localStart + QPoint(0, offset),
                     context.globalStart + QPoint(0, offset),
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    QCoreApplication::sendEvent(context.widget, &move);
}

void endVerticalDrag(const VerticalDragContext& context, int offset)
{
    context.widget->setProperty(
        VaporView::Ground::MainSupport::kMainCardResizeTestCursorYProperty,
        context.globalStart.y() + offset);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        context.localStart + QPoint(0, offset),
                        context.globalStart + QPoint(0, offset),
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(context.widget, &release);
    context.widget->setProperty(
        VaporView::Ground::MainSupport::kMainCardResizeTestCursorYProperty,
        QVariant());
}

void moveVerticalDragWithStaleEventPosition(const VerticalDragContext& context,
                                            int cursorOffset,
                                            int eventOffset)
{
    context.widget->setProperty(
        VaporView::Ground::MainSupport::kMainCardResizeTestCursorYProperty,
        context.globalStart.y() + cursorOffset);
    QMouseEvent move(QEvent::MouseMove,
                     context.localStart + QPoint(0, eventOffset),
                     context.globalStart + QPoint(0, eventOffset),
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    QCoreApplication::sendEvent(context.widget, &move);
}

void doubleClickWidget(QWidget *widget, int waitMs = 50)
{
    require(widget != nullptr, "double-click widget exists");
    const QPoint localPoint = widget->rect().center();
    const QPoint globalPoint = widget->mapToGlobal(localPoint);
    clickWidgetAt(widget, localPoint, 0);
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick,
                            localPoint,
                            globalPoint,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &doubleClick);
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

int countPixelsNearColor(const QImage& image,
                         const QRect& area,
                         const QColor& expected,
                         int tolerance = 16)
{
    const QRect boundedArea = area.intersected(image.rect());
    int count = 0;
    for (int y = boundedArea.top(); y <= boundedArea.bottom(); ++y)
    {
        for (int x = boundedArea.left(); x <= boundedArea.right(); ++x)
        {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() >= 16 &&
                std::abs(pixel.red() - expected.red()) <= tolerance &&
                std::abs(pixel.green() - expected.green()) <= tolerance &&
                std::abs(pixel.blue() - expected.blue()) <= tolerance)
            {
                ++count;
            }
        }
    }
    return count;
}

void requireHomeDeviceColumnsAligned(QWidget *scope)
{
    auto *deviceGrid = scope
        ? scope->findChild<QWidget *>(QStringLiteral("homeOverviewDeviceGrid"))
        : nullptr;
    require(deviceGrid != nullptr,
            "home device overview grid exists for column alignment checks");
    const QList<QWidget *> columns =
        deviceGrid->findChildren<QWidget *>(QStringLiteral("homeDeviceColumn"),
                                            Qt::FindDirectChildrenOnly);
    require(columns.size() == 3,
            "home device overview exposes three aligned device columns");

    for (QWidget *column : columns)
    {
        const QList<QLabel *> capsules =
            column->findChildren<QLabel *>(QStringLiteral("homeDeviceStatusCapsule"),
                                           Qt::FindDirectChildrenOnly);
        const QList<QToolButton *> actionButtons =
            column->findChildren<QToolButton *>(QStringLiteral("homeDeviceActionButton"),
                                                Qt::FindDirectChildrenOnly);
        require(capsules.size() >= 2 && actionButtons.size() == capsules.size(),
                "each home device column contains aligned capsules and actions");
        for (int i = 1; i < capsules.size(); ++i)
        {
            require(capsules.at(0)->width() == capsules.at(i)->width(),
                    "home device capsules use the widest capsule width within their column");
        }
        for (int i = 1; i < actionButtons.size(); ++i)
        {
            require(actionButtons.at(0)->geometry().x() == actionButtons.at(i)->geometry().x(),
                    "home device connection actions align vertically within their column");
        }
        for (QLabel *capsule : capsules)
        {
            require(capsule->alignment().testFlag(Qt::AlignHCenter),
                    "home device capsule text is horizontally centered");
            require(!capsule->text().startsWith(QChar(0x2022)),
                    "home device capsule names omit the leading bullet");
        }
    }

    for (int i = 1; i < columns.size(); ++i)
    {
        const int gap = columns.at(i)->geometry().left() - columns.at(i - 1)->geometry().right() - 1;
        require(gap == 6,
                "home device columns keep a 6px gap between the previous action and next capsule");
    }
}

void requireHomeDeviceMinimumWidthMatchesControls(QWidget *scope)
{
    auto *deviceGrid = scope
        ? scope->findChild<QWidget *>(QStringLiteral("homeOverviewDeviceGrid"))
        : nullptr;
    auto *deviceBody = scope
        ? scope->findChild<QWidget *>(QStringLiteral("homeOverviewDeviceBody"))
        : nullptr;
    require(deviceGrid != nullptr && deviceBody != nullptr,
            "home device controls exist for minimum-width checks");

    QGroupBox *deviceCard = nullptr;
    for (QWidget *ancestor = deviceBody; ancestor && !deviceCard; ancestor = ancestor->parentWidget())
    {
        deviceCard = qobject_cast<QGroupBox *>(ancestor);
    }
    require(deviceCard != nullptr && deviceCard->layout() != nullptr && deviceBody->layout() != nullptr,
            "home device card layouts exist for minimum-width checks");

    const int controlsWidth = std::max(
        deviceGrid->minimumWidth(),
        std::max(deviceGrid->minimumSizeHint().width(), deviceGrid->sizeHint().width()));
    auto *telemetrySummary = deviceBody->findChild<QWidget *>(
        QStringLiteral("homeTelemetrySummaryContainer"));
    int telemetrySummaryWidth = 0;
    if (telemetrySummary)
    {
        telemetrySummaryWidth = telemetrySummary->minimumWidth();
        if (QLayout *summaryLayout = telemetrySummary->layout())
        {
            summaryLayout->invalidate();
            telemetrySummaryWidth = std::max(telemetrySummaryWidth,
                                             summaryLayout->minimumSize().width());
        }
    }
    const QMargins cardMargins = deviceCard->layout()->contentsMargins();
    const QMargins bodyMargins = deviceBody->layout()->contentsMargins();
    const int expectedMinimumWidth = std::max(controlsWidth, telemetrySummaryWidth) +
                                     cardMargins.left() +
                                     cardMargins.right() +
                                     bodyMargins.left() +
                                     bodyMargins.right();
    require(deviceCard->minimumWidth() == expectedMinimumWidth,
            "home device card minimum width covers its controls and telemetry summary capsules");
}

void requireHomeDeviceGeometryStableAcrossCardResize(QWidget *scope,
                                                     QSplitter *splitter,
                                                     QGroupBox *deviceCard,
                                                     QGroupBox *companionCard)
{
    auto *deviceGrid = scope
        ? scope->findChild<QWidget *>(QStringLiteral("homeOverviewDeviceGrid"))
        : nullptr;
    require(deviceGrid != nullptr && splitter != nullptr && deviceCard != nullptr && companionCard != nullptr,
            "home device controls and overview splitter exist for resize checks");

    QList<QWidget *> controls;
    for (QLabel *capsule : deviceGrid->findChildren<QLabel *>(QStringLiteral("homeDeviceStatusCapsule")))
    {
        controls.append(capsule);
    }
    for (QToolButton *button : deviceGrid->findChildren<QToolButton *>(QStringLiteral("homeDeviceActionButton")))
    {
        controls.append(button);
    }
    require(controls.size() == 14,
            "home device resize check covers seven capsules and seven action icons");

    std::vector<std::pair<QWidget *, QRect>> originalGeometries;
    originalGeometries.reserve(static_cast<std::size_t>(controls.size()));
    for (QWidget *control : controls)
    {
        originalGeometries.emplace_back(
            control,
            QRect(control->mapTo(deviceCard, QPoint(0, 0)), control->size()));
    }

    const QList<int> originalSizes = splitter->sizes();
    require(originalSizes.size() == 2,
            "home overview splitter exposes two resize sections");
    const int availableGrowth = originalSizes.at(1) - companionCard->minimumWidth();
    const int growth = std::min(80, availableGrowth);
    require(growth >= 40,
            "home overview splitter has enough room to test a wider device card");

    splitter->setSizes({originalSizes.at(0) + growth, originalSizes.at(1) - growth});
    processEventsFor(40);
    activateLayouts(scope);
    require(deviceCard->width() >= originalSizes.at(0) + growth - 1,
            "home device card expands during the resize stability check");

    for (const auto& [control, originalGeometry] : originalGeometries)
    {
        const QRect resizedGeometry(control->mapTo(deviceCard, QPoint(0, 0)), control->size());
        require(resizedGeometry == originalGeometry,
                "home device capsules and action icons keep their geometry when the card widens");
    }

    splitter->setSizes(originalSizes);
    processEventsFor(40);
    activateLayouts(scope);
}

void requireWidgetInteriorUsesBackground(QWidget *widget,
                                         const QColor& expected,
                                         const char *message)
{
    require(widget != nullptr, message);
    widget->ensurePolished();
    const QImage image = widget->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QRect interior = image.rect().adjusted(2, 2, -2, -2);
    require(!interior.isEmpty(), message);
    require(countPixelsNearColor(image, interior, expected, 0) >=
                interior.width() * interior.height() / 4,
            message);
}

void requireSpinArrowHoverUsesPrimary(bool dark, const char *message)
{
    QSpinBox spin;
    spin.setRange(0, 10);
    spin.setValue(5);
    spin.resize(90, 34);
    spin.show();
    processEventsFor(80);

    if (dark)
    {
        const QImage baseImage = spin.grab().toImage().convertToFormat(QImage::Format_ARGB32);
        const QRect arrowArea(baseImage.width() / 2,
                              0,
                              baseImage.width() - baseImage.width() / 2,
                              baseImage.height());
        require(countPixelsNearColor(
                    baseImage,
                    arrowArea,
                    VaporView::appThemeColor(VaporView::AppThemeColor::Text, true)) >= 1,
                "dark theme spin arrows render white before hover");
    }

    QStyleOptionSpinBox option;
    option.initFrom(&spin);
    option.subControls = QStyle::SC_All;
    option.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
    const QRect upButtonRect = spin.style()->subControlRect(QStyle::CC_SpinBox,
                                                            &option,
                                                            QStyle::SC_SpinBoxUp,
                                                            &spin);
    require(!upButtonRect.isEmpty(), message);
    moveMouseOverWidgetAt(&spin, upButtonRect.center(), 80);
    QHoverEvent hoverMove(QEvent::HoverMove,
                          QPointF(upButtonRect.center()),
                          QPointF(-1, -1));
    QCoreApplication::sendEvent(&spin, &hoverMove);
    processEventsFor(80);
    require(spin.property("spinArrowHover").toString() == QStringLiteral("up"),
            "spin hover filter tracks the upper step button");

    const QImage image = spin.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QColor expected = VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark);
    const int primaryPixelCount = countPixelsNearColor(
        image,
        QRect(image.width() / 2,
              0,
              image.width() - image.width() / 2,
              image.height() / 2),
        expected);
    require(primaryPixelCount >= 1, message);
}

void requireComboArrowUsesDarkIdleAndPrimaryHighlight(const char *message)
{
    QComboBox combo;
    combo.addItem(QStringLiteral("COM9"));
    combo.resize(120, 36);
    combo.setFocusPolicy(Qt::NoFocus);
    combo.show();
    processEventsFor(80);
    combo.clearFocus();
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&combo, &leave);
    combo.setAttribute(Qt::WA_UnderMouse, false);

    QStyleOptionComboBox option;
    option.initFrom(&combo);
    option.currentText = combo.currentText();
    const QRect arrowRect = combo.style()->subControlRect(QStyle::CC_ComboBox,
                                                          &option,
                                                          QStyle::SC_ComboBoxArrow,
                                                          &combo);
    require(!arrowRect.isEmpty(), message);
    const QColor idleColor = VaporView::appThemeColor(VaporView::AppThemeColor::Text, true);
    const QColor highlightColor = VaporView::appThemeColor(VaporView::AppThemeColor::Primary, true);

    const QImage idleImage = combo.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const qreal devicePixelRatio = idleImage.devicePixelRatio();
    const QRect arrowPixelRect(qRound(arrowRect.x() * devicePixelRatio),
                               qRound(arrowRect.y() * devicePixelRatio),
                               qRound(arrowRect.width() * devicePixelRatio),
                               qRound(arrowRect.height() * devicePixelRatio));
    const QRect contentPixelArea(0, 0, arrowPixelRect.left(), idleImage.height());
    const int idleArrowPixels = countPixelsNearColor(idleImage, arrowPixelRect, idleColor);
    const int idleContentHighlightPixels = countPixelsNearColor(
        idleImage, contentPixelArea, highlightColor);
    require(idleArrowPixels >= 1,
            "dark theme combo arrow renders white while idle");
    require(idleContentHighlightPixels == 0,
            "dark theme combo does not draw a second highlighted arrow in its content area");

    QEvent enter(QEvent::Enter);
    combo.setAttribute(Qt::WA_UnderMouse, true);
    QCoreApplication::sendEvent(&combo, &enter);
    moveMouseOverWidgetAt(&combo, arrowRect.center(), 80);

    const QImage highlightedImage = combo.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const int highlightedArrowPixels = countPixelsNearColor(
        highlightedImage, arrowPixelRect, highlightColor);
    const int highlightedContentPixels = countPixelsNearColor(
        highlightedImage, contentPixelArea, highlightColor);
    require(highlightedArrowPixels >= 1,
            "dark theme combo arrow renders orange while highlighted");
    require(highlightedContentPixels == 0,
            "dark theme combo highlight stays in the right arrow slot");
}

void requireComboDarkFocusBorderUsesPrimary(const char *message)
{
    QComboBox combo;
    combo.addItem(QStringLiteral("19200"));
    combo.resize(120, 36);
    combo.show();
    combo.clearFocus();
    processEventsFor(80);

    combo.setFocus(Qt::TabFocusReason);
    processEventsFor(80);
    require(combo.hasFocus(), message);

    const QImage focusedImage = combo.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const int borderBandHeight = qMax(1, qRound(2.0 * focusedImage.devicePixelRatio()));
    const QRect topBorderBand(0, 0, focusedImage.width(), borderBandHeight);
    require(countPixelsNearColor(
                focusedImage,
                topBorderBand,
                VaporView::appThemeColor(VaporView::AppThemeColor::Primary, true)) >= 1,
            message);
}

void hoverWidget(QWidget *widget, bool hovered, int waitMs = 50)
{
    require(widget != nullptr, "hover widget exists");
    const auto sendHoverEvent = [widget, hovered]()
    {
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
    };

    sendHoverEvent();
    if (waitMs > 0)
    {
        processEventsFor(waitMs);
        // Keep synthetic hover checks independent of the physical cursor position.
        sendHoverEvent();
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
                                                  const QList<QWidget *>& rows,
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
    for (QWidget *row : rows)
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

void requireMainWindowOuterBorder(MainWindow& window, bool dark, const char *message);

void requireRtkSidebarPage(
    MainWindow& window,
    QLabel *customTitleLabel,
    bool requireStableFirstFrame = true)
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
        else if (accessibleName == QStringLiteral("组合导航") ||
                 accessibleName == QStringLiteral("Combination Navigation"))
        {
            rtkButton = button;
        }
        else if (accessibleName == QStringLiteral("设备配置") || accessibleName == QStringLiteral("Device"))
        {
            deviceButton = button;
        }
    }
    require(temperatureButton != nullptr, "temperature sidebar button exists for combination-navigation order check");
    require(rtkButton != nullptr, "combination-navigation sidebar button exists");
    require(deviceButton != nullptr, "device configuration sidebar button exists for combination-navigation order check");
    require(homeButton != nullptr, "home sidebar button exists after combination-navigation check");
    require(rtkButton->property("_vv_sidebar_icon_name").toString() == QStringLiteral("satellite"),
            "combination-navigation sidebar button uses the existing satellite icon");
    require(homeButton->y() < deviceButton->y() &&
                deviceButton->y() < temperatureButton->y() &&
                temperatureButton->y() < rtkButton->y(),
            "sidebar order is home, device configuration, thermal, combination navigation");
    require(homeButton->focusPolicy() == Qt::TabFocus &&
                deviceButton->focusPolicy() == Qt::TabFocus &&
                temperatureButton->focusPolicy() == Qt::TabFocus &&
                rtkButton->focusPolicy() == Qt::TabFocus,
            "sidebar navigation buttons are reachable by keyboard tab focus");
    homeButton->setFocus(Qt::OtherFocusReason);
    QKeyEvent sidebarDown(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(homeButton, &sidebarDown);
    processEventsFor(60);
    require(deviceButton->isChecked(),
            "sidebar Down arrow moves page selection to device configuration");
    QKeyEvent sidebarUp(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(deviceButton, &sidebarUp);
    processEventsFor(60);
    require(homeButton->isChecked(),
            "sidebar Up arrow moves page selection back to home");
    require(rtkButton->toolTip().contains(QStringLiteral("未启动")) ||
                rtkButton->toolTip().contains(QStringLiteral("stopped")),
            "combination-navigation sidebar button starts with stopped RTK status text");

    const QList<QToolButton*> titleButtons = window.findChildren<QToolButton *>(QStringLiteral("titleBarButton"));
    for (QToolButton *button : titleButtons)
    {
        require(!button->toolTip().contains(QStringLiteral("RTK")),
                "RTK config is not duplicated in the title bar");
    }

    const QSize iconSize(32, 32);
    const qint64 stoppedIconKey = rtkButton->icon().pixmap(iconSize).cacheKey();

    auto *pageStack = window.findChild<QStackedWidget *>(QStringLiteral("mainPageStack"));
    require(pageStack != nullptr && pageStack->count() == 4,
            "main page stack keeps the existing four-page order");
    auto *preDialog = window.findChild<RtkConfigDialog *>();
    require(preDialog != nullptr, "embedded RTK dialog exists before sidebar click");
    auto *preGgaCard = findCardByTitle(preDialog,
                                       {QStringLiteral("GGA 监视"),
                                        QStringLiteral("GGA Monitor")});
    auto *preLogCard = findCardByTitle(preDialog,
                                       {QStringLiteral("RTK 服务日志"),
                                        QStringLiteral("RTK Service Log")});
    auto *preRtcmCard = findCardByTitle(preDialog,
                                        {QStringLiteral("RTCM 输出配置"),
                                         QStringLiteral("RTCM Output Configuration")});
    auto *preGgaSourceCombo =
        preDialog->findChild<QComboBox *>(QStringLiteral("rtkGgaPortCombo"));
    require(preGgaCard != nullptr && preLogCard != nullptr && preRtcmCard != nullptr &&
                preGgaSourceCombo != nullptr,
            "RTK cards exist before sidebar click for resize sampling");
    require(preGgaSourceCombo->width() <= 140,
            "RTK GGA source combo starts compact before the differential page is first shown");
    ResizeWidthRecorder ggaResizeRecorder(preGgaCard);
    ResizeWidthRecorder rtcmResizeRecorder(preRtcmCard);
    ResizeWidthRecorder logResizeRecorder(preLogCard);
    ResizeWidthRecorder ggaSourceComboResizeRecorder(preGgaSourceCombo);
    preGgaCard->installEventFilter(&ggaResizeRecorder);
    preRtcmCard->installEventFilter(&rtcmResizeRecorder);
    preLogCard->installEventFilter(&logResizeRecorder);
    preGgaSourceCombo->installEventFilter(&ggaSourceComboResizeRecorder);
    clickWidget(rtkButton, 0);
    processEventsFor(150);
    auto *combinationPage = qobject_cast<VaporView::Ground::Navigation::CombinationNavigationPage *>(
        pageStack->currentWidget());
    require(combinationPage != nullptr && pageStack->indexOf(combinationPage) == 3,
            "combination navigation reuses the former RTK page index");
    auto *combinationStack = combinationPage->findChild<QStackedWidget *>(
        QStringLiteral("combinationNavigationStack"));
    auto *combinationNavigationRow = combinationPage->findChild<QWidget *>(
        QStringLiteral("combinationNavigationNavigationRow"));
    auto *combinationNavigationBar = combinationPage->findChild<QFrame *>(
        QStringLiteral("combinationNavigationNavigationBar"));
    auto *combinationNavigationTrack = combinationPage->findChild<QFrame *>(
        QStringLiteral("combinationNavigationNavigationTrack"));
    auto *combinationNavigationSelectionThumb = combinationPage->findChild<QFrame *>(
        QStringLiteral("combinationNavigationNavigationSelectionThumb"));
    auto *combinationNavigationSelectionAnimation = combinationPage->findChild<QPropertyAnimation *>(
        QStringLiteral("combinationNavigationNavigationSelectionAnimation"));
    auto *statusButton = combinationPage->findChild<QPushButton *>(
        QStringLiteral("combinationNavigationStatusButton"));
    auto *epsilonButton = combinationPage->findChild<QPushButton *>(
        QStringLiteral("combinationNavigationEpsilonButton"));
    auto *differentialButton = combinationPage->findChild<QPushButton *>(
        QStringLiteral("combinationNavigationDifferentialButton"));
    auto *statusPage = combinationPage->findChild<QWidget *>(
        QStringLiteral("combinationNavigationStatusPage"));
    auto *epsilonPanel = combinationPage->epsilonConfigPanel();
    auto *epsilonPage = combinationPage->findChild<QWidget *>(
        QStringLiteral("combinationNavigationEpsilonPage"));
    auto *statusScrollArea = combinationPage->findChild<QScrollArea *>(
        QStringLiteral("navigationStatusScrollArea"));
    auto *statusContent = combinationPage->findChild<QWidget *>(
        QStringLiteral("navigationStatusContent"));
    auto *epsilonScrollArea = combinationPage->findChild<QScrollArea *>(
        QStringLiteral("epsilonConfigScrollArea"));
    auto *epsilonContent = combinationPage->findChild<QWidget *>(
        QStringLiteral("epsilonConfigContent"));
    require(combinationNavigationRow != nullptr &&
                combinationNavigationBar != nullptr && combinationNavigationBar->layout() != nullptr &&
                combinationNavigationTrack != nullptr && combinationNavigationTrack->layout() != nullptr &&
                combinationNavigationSelectionThumb != nullptr &&
                combinationNavigationSelectionAnimation != nullptr &&
                combinationStack != nullptr && combinationStack->count() == 3 &&
                statusButton != nullptr && epsilonButton != nullptr && differentialButton != nullptr &&
                statusPage != nullptr && epsilonPage != nullptr &&
                statusScrollArea != nullptr && statusContent != nullptr && statusContent->layout() != nullptr &&
                epsilonScrollArea != nullptr && epsilonContent != nullptr && epsilonContent->layout() != nullptr,
            "combination navigation exposes three real buttons and three stacked pages");
    require(combinationNavigationBar->height() == 36 &&
                combinationNavigationTrack->parentWidget() == combinationNavigationBar &&
                statusButton->parentWidget() == combinationNavigationTrack &&
                epsilonButton->parentWidget() == combinationNavigationTrack &&
                differentialButton->parentWidget() == combinationNavigationTrack &&
                statusButton->height() == epsilonButton->height() &&
                epsilonButton->height() == differentialButton->height() &&
                statusButton->width() == epsilonButton->width() &&
                epsilonButton->width() == differentialButton->width(),
            "combination navigation uses an equal three-segment capsule layout");
    require(std::abs(combinationNavigationBar->geometry().center().x() -
                     combinationNavigationRow->rect().center().x()) <= 1,
            "combination navigation bar is horizontally centered in its page row");
    require(statusScrollArea->mapTo(combinationPage, QPoint(0, 0)).y() == 0 &&
                epsilonScrollArea->mapTo(combinationPage, QPoint(0, 0)).y() == 0,
            "combination-navigation scroll areas start at the page top behind the floating navigation");
    require(combinationNavigationSelectionThumb->parentWidget() == combinationNavigationTrack &&
                combinationNavigationSelectionThumb->geometry() == statusButton->geometry() &&
                combinationNavigationSelectionAnimation->targetObject() == combinationNavigationSelectionThumb &&
                combinationNavigationSelectionAnimation->duration() == 240,
            "combination navigation centers the selected thumb and configures its slide animation");
    const QPoint hoverPoint = epsilonButton->rect().center();
    epsilonButton->setAttribute(Qt::WA_UnderMouse, true);
    QEvent enterEvent(QEvent::Enter);
    QCoreApplication::sendEvent(epsilonButton, &enterEvent);
    moveMouseOverWidgetAt(epsilonButton, hoverPoint, 80);
    const QImage hoverImage =
        combinationNavigationBar->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const qreal devicePixelRatio = hoverImage.devicePixelRatio();
    const QPoint epsilonTopLeft =
        epsilonButton->mapTo(combinationNavigationBar, QPoint(0, 0));
    const QRect epsilonPixelRect(
        qRound(epsilonTopLeft.x() * devicePixelRatio),
        qRound(epsilonTopLeft.y() * devicePixelRatio),
        qRound(epsilonButton->width() * devicePixelRatio),
        qRound(epsilonButton->height() * devicePixelRatio));
    const QColor hoverFill = VaporView::appThemeColor(
        VaporView::AppThemeColor::PrimarySubtlePressed,
        VaporView::isDarkThemeEnabled());
    const int hoverInset = qMax(1, qRound(6.0 * devicePixelRatio));
    const QRect hoverInterior = epsilonPixelRect.adjusted(
        hoverInset, hoverInset, -hoverInset, -hoverInset);
    require(countPixelsNearColor(hoverImage, hoverInterior, hoverFill, 12) <
                qMax(1, hoverInterior.width() * hoverInterior.height() / 20),
            "combination navigation hover does not add a background fill");
    QEvent leaveEvent(QEvent::Leave);
    QCoreApplication::sendEvent(epsilonButton, &leaveEvent);
    epsilonButton->setAttribute(Qt::WA_UnderMouse, false);
    require(combinationStack->currentWidget() == statusPage && statusButton->isChecked() &&
                !epsilonButton->isChecked() && !differentialButton->isChecked(),
            "combination navigation opens on the status page by default");
    const std::array<std::pair<QPushButton *, QString>, 3> combinationButtons{{
        {statusButton, QStringLiteral("状态")},
        {epsilonButton, QStringLiteral("EPSILON")},
        {differentialButton, QStringLiteral("差分定位")},
    }};
    for (const auto& [button, expectedText] : combinationButtons)
    {
        require(button->isCheckable() && button->text() == expectedText &&
                    button->accessibleName() == expectedText &&
                    button->focusPolicy() == Qt::TabFocus &&
                    !button->property("combinationNavigationKeyboardFocus").toBool(),
                "combination-navigation buttons are real, named and keyboard-tab focusable without mouse-open focus rings");
    }
    require(combinationPage->styleSheet().contains(
                QStringLiteral("QFrame#combinationNavigationNavigationTrack")) &&
                combinationPage->styleSheet().contains(QStringLiteral(":checked")) &&
                !combinationPage->styleSheet().contains(QStringLiteral(":focus")),
            "combination-navigation capsule keeps checked styling without stylesheet/native focus rings");
    require(combinationPage->findChildren<RtkConfigDialog *>().size() == 1 &&
                combinationPage->differentialPage() == preDialog &&
                combinationStack->indexOf(preDialog) == 2,
            "combination navigation owns exactly the original embedded RTK page");
    require(window.findChildren<VaporView::Ground::Navigation::CombinationNavigationPage *>().size() == 1 &&
                window.findChildren<RtkConfigDialog *>().size() == 1,
            "main window owns exactly one combination-navigation page and one RTK dialog");
    auto *deviceConfigPageForBoundary =
        window.findChild<QWidget *>(QStringLiteral("deviceConfigPage"));
    int epsilonPacketRateControlCount = 0;
    for (QComboBox *combo : window.findChildren<QComboBox *>())
    {
        if (combo->property("epsilonPacketId").isValid())
        {
            ++epsilonPacketRateControlCount;
        }
    }
    int deviceConfigPacketRateControlCount = 0;
    if (deviceConfigPageForBoundary)
    {
        for (QComboBox *combo : deviceConfigPageForBoundary->findChildren<QComboBox *>())
        {
            if (combo->property("epsilonPacketId").isValid())
            {
                ++deviceConfigPacketRateControlCount;
            }
        }
    }
    require(deviceConfigPageForBoundary != nullptr && epsilonPanel != nullptr &&
                epsilonPacketRateControlCount == 11 && deviceConfigPacketRateControlCount == 0 &&
                epsilonPage->isAncestorOf(epsilonPanel),
            "the single detailed EPSILON configuration now belongs to Combination Navigation");
    auto *rtkServiceStatus = combinationPage->findChild<QLabel *>(
        QStringLiteral("navigationStatusRtkServiceValue"));
    auto *gnssStatus = combinationPage->findChild<QLabel *>(
        QStringLiteral("navigationStatusGnssValue"));
    auto *positioningMode = combinationPage->findChild<QLabel *>(
        QStringLiteral("navigationStatusFixValue"));
    auto *ntripStatus = combinationPage->findChild<QLabel *>(
        QStringLiteral("navigationStatusNtripValue"));
    auto *rtcmStatus = combinationPage->findChild<QLabel *>(
        QStringLiteral("navigationStatusRtcmValue"));
    auto *longitudeValue = combinationPage->findChild<QLabel *>(
        QStringLiteral("navigationStatusLongitudeValue"));
    auto *differentialStatusCard = combinationPage->findChild<QFrame *>(
        QStringLiteral("navigationStatusDifferentialCard"));
    auto *statusSummaryCard = combinationPage->findChild<QFrame *>(
        QStringLiteral("navigationStatusSummaryCard"));
    auto *epsilonOutputCard = epsilonPanel
        ? epsilonPanel->findChild<QFrame *>(QStringLiteral("epsilonOutputCard"))
        : nullptr;
    require(statusSummaryCard != nullptr && epsilonOutputCard != nullptr,
            "combination-navigation exposes stable status and EPSILON card anchors");
    ResizeWidthRecorder statusSummaryResizeRecorder(statusSummaryCard);
    ResizeWidthRecorder epsilonOutputResizeRecorder(epsilonOutputCard);
    ResizeWidthRecorder differentialGgaResizeRecorder(preGgaCard);
    statusSummaryCard->installEventFilter(&statusSummaryResizeRecorder);
    epsilonOutputCard->installEventFilter(&epsilonOutputResizeRecorder);
    preGgaCard->installEventFilter(&differentialGgaResizeRecorder);
    require(rtkServiceStatus != nullptr && gnssStatus != nullptr && positioningMode != nullptr &&
                ntripStatus != nullptr && rtcmStatus != nullptr &&
                longitudeValue != nullptr && differentialStatusCard != nullptr &&
                differentialStatusCard->isVisible() &&
                ntripStatus->text() == QStringLiteral("--") && rtcmStatus->text() == QStringLiteral("--"),
            "status page keeps its differential region without fabricating NTRIP or RTCM health state");
    VaporView::Ground::Navigation::CombinationNavigationPage::StatusSnapshot sampleStatus;
    sampleStatus.epsilonOnline = true;
    sampleStatus.epsilonDataFresh = true;
    sampleStatus.navigationDataAvailable = true;
    sampleStatus.gnssFixText = QStringLiteral("RTK_FIXED");
    sampleStatus.gnssFixCode = 6;
    sampleStatus.filterStatusAvailable = true;
    sampleStatus.filterStatusBits = 0x0062;
    sampleStatus.updateStatusBits = 0x000c;
    sampleStatus.gnssQualityAvailable = true;
    sampleStatus.satelliteCount = 24;
    sampleStatus.horizontalAccuracyM = 0.015;
    sampleStatus.rtkServiceRunning = true;
    sampleStatus.positionAvailable = true;
    sampleStatus.longitudeDeg = 120.14530;
    sampleStatus.latitudeDeg = 30.24620;
    sampleStatus.heightM = 42.5;
    combinationPage->setStatusSnapshot(sampleStatus);
    require(rtkServiceStatus->text().contains(QStringLiteral("运行中")) &&
                positioningMode->text() == QStringLiteral("RTK固定解") &&
                longitudeValue->text() != QStringLiteral("--") &&
                ntripStatus->text() == QStringLiteral("--") &&
                rtcmStatus->text() == QStringLiteral("--"),
            "status page displays reliable navigation and RTK-service data without inferring connection health");
    combinationPage->refreshStatus();
    require(gnssStatus->text() == QStringLiteral("--") &&
                positioningMode->text() == QStringLiteral("--") &&
                longitudeValue->text() == QStringLiteral("--"),
            "local provider keeps GNSS fix and position unavailable without field-level freshness");
    ggaResizeRecorder.reset();
    rtcmResizeRecorder.reset();
    logResizeRecorder.reset();
    ggaSourceComboResizeRecorder.reset();
    clickWidget(differentialButton, 0);
    processEventsFor(150);
    require(combinationStack->currentWidget() == preDialog && differentialButton->isChecked(),
            "combination navigation switches directly from status to differential positioning");
    const bool statusToDifferentialWidthsStable =
        !ggaResizeRecorder.observedPaintableWidthDifferentFrom(preGgaCard->width()) &&
        !rtcmResizeRecorder.observedPaintableWidthDifferentFrom(preRtcmCard->width()) &&
        !logResizeRecorder.observedPaintableWidthDifferentFrom(preLogCard->width()) &&
        !ggaSourceComboResizeRecorder.observedPaintableWidthDifferentFrom(preGgaSourceCombo->width());
    if (!statusToDifferentialWidthsStable)
    {
        std::cerr << "RTK status->differential widths: gga="
                  << ggaResizeRecorder.observedWidthDetailsSummary().toStdString()
                  << " final " << preGgaCard->width()
                  << " rtcm=" << rtcmResizeRecorder.observedWidthDetailsSummary().toStdString()
                  << " final " << preRtcmCard->width()
                  << " log=" << logResizeRecorder.observedWidthDetailsSummary().toStdString()
                  << " final " << preLogCard->width()
                  << " combo=" << ggaSourceComboResizeRecorder.observedWidthDetailsSummary().toStdString()
                  << " final " << preGgaSourceCombo->width() << '\n';
    }
    require(statusToDifferentialWidthsStable,
            "status to differential first show keeps RTK cards and GGA source combo widths stable");
    clickWidget(statusButton, 0);
    processEventsFor(50);
    require(combinationStack->currentWidget() == statusPage && statusButton->isChecked(),
            "combination navigation returns to status after first-show width sampling");
    auto *unsavedRtkServerEdit =
        preDialog->findChild<QLineEdit *>(QStringLiteral("rtkServerEdit"));
    require(unsavedRtkServerEdit != nullptr,
            "RTK server input exists for page-switch persistence testing");
    const QString originalRtkServer = unsavedRtkServerEdit->text();
    const QString unsavedRtkServer = QStringLiteral("unsaved-combination-navigation.test");
    unsavedRtkServerEdit->setText(unsavedRtkServer);
    statusSummaryResizeRecorder.reset();
    epsilonOutputResizeRecorder.reset();
    differentialGgaResizeRecorder.reset();
    epsilonButton->setFocus(Qt::TabFocusReason);
    QKeyEvent epsilonSpacePress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QKeyEvent epsilonSpaceRelease(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(epsilonButton, &epsilonSpacePress);
    QApplication::sendEvent(epsilonButton, &epsilonSpaceRelease);
    processEventsFor(50);
    require(combinationStack->currentWidget() == epsilonPage && epsilonButton->isChecked(),
            "combination navigation switches from status to EPSILON by keyboard");
    require(!epsilonOutputResizeRecorder.observedWidthDifferentFrom(epsilonOutputCard->width()),
            "status to EPSILON switching keeps the packet-rate card width stable");
    require(combinationNavigationSelectionAnimation->state() == QAbstractAnimation::Running,
            "combination navigation starts the selected-thumb animation when switching sections");
    processEventsFor(250);
    require(combinationNavigationSelectionThumb->geometry() == epsilonButton->geometry(),
            "combination navigation settles the selected thumb on the new section");
    ggaResizeRecorder.reset();
    logResizeRecorder.reset();
    ggaSourceComboResizeRecorder.reset();
    statusSummaryResizeRecorder.reset();
    epsilonOutputResizeRecorder.reset();
    differentialGgaResizeRecorder.reset();
    clickWidget(differentialButton, 0);
    processEventsFor(150);
    require(!epsilonButton->property("combinationNavigationKeyboardFocus").toBool() &&
                !differentialButton->property("combinationNavigationKeyboardFocus").toBool(),
            "combination navigation pointer switching clears keyboard-only focus borders");
    auto *dialog = qobject_cast<RtkConfigDialog *>(combinationStack->currentWidget());
    require(dialog == preDialog && differentialButton->isChecked(),
            "combination navigation switches from EPSILON to the original RTK page");
    require(!differentialGgaResizeRecorder.observedWidthDifferentFrom(preGgaCard->width()),
            "EPSILON to differential switching keeps the RTK card width stable");
    require(!ggaSourceComboResizeRecorder.observedWidthDifferentFrom(preGgaSourceCombo->width()),
            "EPSILON to differential switching keeps the GGA source combo width stable");
    require(unsavedRtkServerEdit->text() == unsavedRtkServer,
            "switching internal pages preserves unsaved RTK input");
    clickWidget(epsilonButton, 0);
    processEventsFor(50);
    require(combinationStack->currentWidget() == epsilonPage &&
                combinationPage->epsilonConfigPanel() == epsilonPanel,
            "returning from differential positioning reuses the same EPSILON panel instance");
    clickWidget(differentialButton, 0);
    processEventsFor(50);
    require(combinationStack->currentWidget() == preDialog &&
                unsavedRtkServerEdit->text() == unsavedRtkServer,
            "status, EPSILON, differential and EPSILON switching preserves the original RTK page");
    unsavedRtkServerEdit->setText(originalRtkServer);
    require(dialog->isVisible(), "embedded RTK config page is visible after sidebar click");
    auto *initialGgaCard = findCardByTitle(dialog,
                                           {QStringLiteral("GGA 监视"),
                                            QStringLiteral("GGA Monitor")});
    auto *initialLogCard = findCardByTitle(dialog,
                                           {QStringLiteral("RTK 服务日志"),
                                            QStringLiteral("RTK Service Log")});
    require(initialGgaCard != nullptr && initialLogCard != nullptr,
            "RTK GGA and log cards are visible after sidebar click");
    if (requireStableFirstFrame)
    {
        const bool firstFrameWidthsStable =
            !ggaResizeRecorder.observedWidthDifferentFrom(initialGgaCard->width()) &&
            !logResizeRecorder.observedWidthDifferentFrom(initialLogCard->width());
        if (!firstFrameWidthsStable)
        {
            auto *firstFrameScroll = dialog->findChild<QScrollArea *>(QStringLiteral("rtkConfigScrollArea"));
            std::cerr << "RTK first-frame width change: viewport="
                      << (firstFrameScroll ? firstFrameScroll->viewport()->size().width() : -1)
                      << 'x' << (firstFrameScroll ? firstFrameScroll->viewport()->size().height() : -1)
                      << " content="
                      << (firstFrameScroll && firstFrameScroll->widget()
                              ? firstFrameScroll->widget()->size().width()
                              : -1)
                      << 'x' << (firstFrameScroll && firstFrameScroll->widget()
                              ? firstFrameScroll->widget()->size().height()
                              : -1)
                      << " verticalMaximum="
                      << (firstFrameScroll ? firstFrameScroll->verticalScrollBar()->maximum() : -1)
                      << '\n';
        }
        require(firstFrameWidthsStable,
                "RTK GGA and service log cards keep a stable first-frame width");
    }
    activateLayouts(dialog);
    processEventsFor(100);
    requireLabelTextOneOf(customTitleLabel,
                          {QStringLiteral("组合导航"), QStringLiteral("Combination Navigation")},
                          "custom title bar follows the selected combination-navigation page");
    statusSummaryResizeRecorder.reset();
    clickWidget(statusButton, 0);
    processEventsFor(50);
    require(combinationStack->currentWidget() == statusPage && statusButton->isChecked(),
            "combination navigation switches from differential positioning back to status");
    require(!statusSummaryResizeRecorder.observedWidthDifferentFrom(statusSummaryCard->width()),
            "differential to status switching keeps the status card width stable");
    clickWidget(differentialButton, 0);
    processEventsFor(100);
    require(combinationStack->currentWidget() == dialog && differentialButton->isChecked(),
            "combination navigation returns to the same RTK page instance");
    auto *rtkScrollArea = dialog->findChild<QScrollArea *>(QStringLiteral("rtkConfigScrollArea"));
    require(rtkScrollArea != nullptr, "RTK config page uses a scroll area");
    require(rtkScrollArea->mapTo(combinationPage, QPoint(0, 0)).y() == 0,
            "RTK differential scroll area starts at the page top behind the floating navigation");
    require(rtkScrollArea->horizontalScrollBar() != nullptr &&
                rtkScrollArea->horizontalScrollBar()->maximum() == 0,
            "RTK config page avoids a horizontal scrollbar at the default window size");
    require(rtkScrollArea->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff &&
                rtkScrollArea->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOn,
            "RTK config page forbids horizontal scrolling and reserves a stable vertical rail");
    const std::vector<std::pair<QString, int>> compactCombos = {
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
    const std::vector<std::pair<QString, int>> usableLineEdits = {
        {QStringLiteral("rtkServerEdit"), 110},
        {QStringLiteral("rtkUsernameEdit"), 100},
        {QStringLiteral("rtkPasswordEdit"), 110},
    };
    for (const auto& [objectName, minWidth] : usableLineEdits)
    {
        auto *lineEdit = dialog->findChild<QLineEdit *>(objectName);
        require(lineEdit != nullptr, "usable RTK line edit exists");
        require(lineEdit->width() >= minWidth,
                "RTK server/account line edit keeps usable horizontal space");
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
          "RTK service-log card uses the standard icon title bar"},
    };
    for (const auto& [titles, iconName, message] : rtkCards)
    {
        QGroupBox *card = findCardByTitle(dialog, titles);
        require(card != nullptr, message);
        require(card->objectName() == QStringLiteral("sensorGroupBox"),
                "RTK card reuses the home page sensor card style");
        requireTopLevelCardElevation(card, 1.0, message);
        requireCardTitleBar(card, titles, iconName, message);
    }
    auto *ntripCard = findCardByTitle(dialog,
                                      {QStringLiteral("NTRIP 服务器配置"),
                                       QStringLiteral("NTRIP Server Configuration")});
    auto *rtcmCard = findCardByTitle(dialog,
                                     {QStringLiteral("RTCM 输出配置"),
                                      QStringLiteral("RTCM Output Configuration")});
    require(ntripCard != nullptr && rtcmCard != nullptr,
            "RTK NTRIP and EPSILON data-path cards exist for layout checks");
    constexpr int kExpectedPageLeftInset =
        VaporView::Ground::MainSupport::kMainContentLeftCardInset;
    constexpr int kExpectedPageRightGap =
        VaporView::Ground::MainSupport::kMainContentRightCardInset;
    constexpr int kExpectedPageChromeInset =
        VaporView::Ground::MainSupport::kTopLevelCardOuterVerticalInset;
    constexpr int kExpectedTopLevelCardGap =
        VaporView::Ground::MainSupport::kTopLevelCardGap;
    const int expectedCombinationNavigationTopInset = combinationNavigationRow->height();
    require(kExpectedTopLevelCardGap == 12,
            "top-level cards keep the requested 12px spacing rhythm");
    const auto requireCombinationPageRightInset =
        [&](QScrollArea *scrollArea, QWidget *content, const char *message) {
            require(scrollArea->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOn &&
                        content->layout()->contentsMargins().right() == kExpectedPageRightGap,
                    message);
        };
    const auto requireStableVerticalScrollBarVisibility =
        [](QScrollArea *scrollArea, const char *message) {
            auto *effect = qobject_cast<QGraphicsOpacityEffect *>(
                scrollArea->verticalScrollBar()->graphicsEffect());
            const qreal expectedOpacity =
                scrollArea->verticalScrollBar()->maximum() >
                        scrollArea->verticalScrollBar()->minimum()
                    ? 1.0
                    : 0.0;
            require(effect != nullptr &&
                        std::abs(effect->opacity() - expectedOpacity) < 0.01,
                    message);
        };
    requireCombinationPageRightInset(
        statusScrollArea,
        statusContent,
        "combination-navigation status page reserves a stable scrollbar rail and right inset");
    requireCombinationPageRightInset(
        epsilonScrollArea,
        epsilonContent,
        "combination-navigation EPSILON page reserves a stable scrollbar rail and right inset");
    requireStableVerticalScrollBarVisibility(
        statusScrollArea,
        "combination-navigation status scrollbar is transparent when no scrolling is needed");
    requireStableVerticalScrollBarVisibility(
        epsilonScrollArea,
        "combination-navigation EPSILON scrollbar is transparent when no scrolling is needed");
    auto widgetRectInCentralForRtk = [&window](QWidget *widget) {
        return QRect(widget->mapTo(window.centralWidget(), QPoint(0, 0)), widget->size());
    };
    auto rightEdgeForRtk = [](const QRect& rect) {
        return rect.left() + rect.width();
    };
    auto bottomEdgeForRtk = [](const QRect& rect) {
        return rect.top() + rect.height();
    };
    require(std::abs(widgetRectInCentralForRtk(ntripCard).left() -
                     (widgetRectInCentralForRtk(dialog).left() +
                      kExpectedPageLeftInset)) <= 1,
            "RTK differential page keeps its shared 18px card inset without an extra wrapper margin");
    require(std::abs(widgetRectInCentralForRtk(ntripCard).top() -
                     (widgetRectInCentralForRtk(dialog).top() +
                      expectedCombinationNavigationTopInset +
                      kExpectedPageChromeInset)) <= 1,
            "RTK differential page keeps its own compact top inset below the floating sub-navigation");
    auto *rtkContent = dialog->findChild<QWidget *>(QStringLiteral("rtkConfigContent"));
    require(rtkContent != nullptr && rtkContent->layout() != nullptr,
            "RTK embedded content exposes its page layout");
    require(rtkScrollArea->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOn,
            "RTK embedded page reserves the same stable scrollbar rail as its sibling pages");
    requireStableVerticalScrollBarVisibility(
        rtkScrollArea,
        "RTK embedded scrollbar is transparent when no scrolling is needed");
    require(rtkContent->layout()->contentsMargins() ==
                QMargins(kExpectedPageLeftInset,
                         expectedCombinationNavigationTopInset + kExpectedPageChromeInset,
                         kExpectedPageRightGap,
                         kExpectedPageChromeInset),
            "RTK embedded page keeps the shared insets below the floating sub-navigation");
    require(rtkContent->layout()->spacing() == kExpectedTopLevelCardGap,
            "RTK embedded page uses the shared top-level card gap");
    auto *ggaCard = findCardByTitle(dialog,
                                    {QStringLiteral("GGA 监视"),
                                     QStringLiteral("GGA Monitor")});
    auto *logCard = findCardByTitle(dialog,
                                    {QStringLiteral("RTK 服务日志"),
                                     QStringLiteral("RTK Service Log")});
    auto *actionCard = findCardByTitle(dialog,
                                      {QStringLiteral("服务操作"),
                                       QStringLiteral("Service Operations")});
    require(ggaCard != nullptr && logCard != nullptr && actionCard != nullptr &&
                dialog->findChildren<QGroupBox *>(QStringLiteral("sensorGroupBox")).size() == 5,
            "RTK page exposes five business-level cards including service operations");
    const QRect ntripCardRect = widgetRectInCentralForRtk(ntripCard);
    const QRect ggaCardRect = widgetRectInCentralForRtk(ggaCard);
    const QRect rtcmCardRect = widgetRectInCentralForRtk(rtcmCard);
    const QRect logCardRect = widgetRectInCentralForRtk(logCard);
    const QRect actionCardRect = widgetRectInCentralForRtk(actionCard);
    require(std::abs(ggaCardRect.top() - ntripCardRect.top()) <= 1 &&
                std::abs((ggaCardRect.left() - rightEdgeForRtk(ntripCardRect)) -
                         kExpectedTopLevelCardGap) <= 1 &&
                ntripCardRect.width() > ggaCardRect.width() &&
                ntripCardRect.width() >= ggaCardRect.width() * 1.25,
            "RTK NTRIP and GGA cards share the wide-left first row");
    require(std::abs(logCardRect.top() - rtcmCardRect.top()) <= 1 &&
                std::abs((logCardRect.left() - rightEdgeForRtk(rtcmCardRect)) -
                         kExpectedTopLevelCardGap) <= 1 &&
                logCardRect.width() > rtcmCardRect.width() &&
                logCardRect.width() >= rtcmCardRect.width() * 1.25 &&
                std::abs((rtcmCardRect.top() - bottomEdgeForRtk(ntripCardRect)) -
                         kExpectedTopLevelCardGap) <= 1,
            "RTK RTCM and service-log cards share the wide-right second row");
    require(std::abs(ggaCardRect.height() - ntripCardRect.height()) <= 1 &&
                std::abs(logCardRect.height() - rtcmCardRect.height()) <= 1,
            "RTK cards in each visual row use equal heights");
    require(std::abs((actionCardRect.top() -
                      std::max(bottomEdgeForRtk(rtcmCardRect), bottomEdgeForRtk(logCardRect))) -
                     kExpectedTopLevelCardGap) <= 1 &&
                std::abs(ntripCardRect.left() - actionCardRect.left()) <= 1 &&
                std::abs((ntripCardRect.width() + kExpectedTopLevelCardGap + ggaCardRect.width()) -
                         actionCardRect.width()) <= 1,
            "RTK service operations form a full-width final row after RTCM and the log");
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
    auto *rtkActionStatusLabel = dialog->findChild<QLabel *>(QStringLiteral("rtkStatusLabel"));
    auto *rtkActionStatusIcon = dialog->findChild<QLabel *>(QStringLiteral("rtkStatusIcon"));
    QLabel *actionTitleLabel = findLabelByText(actionCard,
                                               {QStringLiteral("服务操作"),
                                                QStringLiteral("Service Operations")});
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
            "RTK service-operations title bar contains service status text and icon");
    require(actionTitleBar->isAncestorOf(rtkActionStatusLabel) &&
                actionTitleBar->isAncestorOf(rtkActionStatusIcon),
            "RTK service status is placed in the service-operations title bar");
    require(widgetX(rtkActionStatusIcon) > widgetX(actionTitleLabel) + actionTitleLabel->width() &&
                widgetX(rtkActionStatusLabel) > widgetX(rtkActionStatusIcon),
            "RTK service status sits to the right of the service-operations title");
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
    require(mountpointCombo->property("usesSingleLevelPopupMenu").toBool(),
            "RTK mountpoint combo uses the single-level popup to avoid stale native selection highlights");
    require(mountpointCombo->currentText() == QStringLiteral("请先检测") ||
                mountpointCombo->currentText() == QStringLiteral("Detect first"),
            "RTK mountpoint combo defaults to an explicit detect-first prompt");
    require(mountpointCombo->findText(QStringLiteral("AUTO")) < 0,
            "RTK mountpoint combo does not expose AUTO as a fake mountpoint");
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
    const QStringList rtkLogClearButtonNames = {
        QStringLiteral("rtkGgaClearLogButton"),
        QStringLiteral("rtkServiceLogClearButton"),
    };
    for (QToolButton *button : rtkToolButtons)
    {
        if (rtkLogClearButtonNames.contains(button->objectName()))
        {
            require(button->size() == QSize(34, 34),
                    "RTK log clear buttons match the main log-card action size");
            continue;
        }
        require(std::abs(button->height() - rtkInputHeight) <= 1,
                "RTK tool buttons match the input field height");
    }
    require(std::abs(widgetY(serverEdit) - widgetY(portEdit)) <= 2 &&
                std::abs(widgetY(serverEdit) - widgetY(mountpointCombo)) <= 2 &&
                widgetX(serverEdit) < widgetX(portEdit) &&
                widgetX(portEdit) < widgetX(mountpointCombo),
            "RTK NTRIP server, port, and mountpoint fields share the first row");
    require(widgetY(usernameEdit) > widgetY(serverEdit) &&
                std::abs(widgetY(usernameEdit) - widgetY(passwordEdit)) <= 2 &&
                std::abs(widgetY(usernameEdit) - widgetY(fetchMountpointsButton)) <= 2 &&
                widgetX(usernameEdit) < widgetX(passwordEdit) &&
                widgetX(fetchMountpointsButton) > widgetX(passwordEdit) + passwordEdit->width(),
            "RTK NTRIP credentials and mountpoint detection share the second row");
    require(passwordEdit->width() >= 110 && mountpointCombo->width() >= 150,
            "RTK NTRIP password and mountpoint fields keep useful horizontal space");
    auto *ntripPanel = dialog->findChild<QWidget *>(QStringLiteral("rtkNtripConfigPanel"));
    auto *testConnectionButton = dialog->findChild<QPushButton *>(QStringLiteral("rtkTestConnectionButton"));
    auto *startButton = dialog->findChild<QPushButton *>(QStringLiteral("rtkStartButton"));
    auto *stopButton = dialog->findChild<QPushButton *>(QStringLiteral("rtkStopButton"));
    auto *clearLogButton = dialog->findChild<QPushButton *>(QStringLiteral("rtkClearLogButton"));
    auto *serviceOperationsPanel =
        dialog->findChild<QWidget *>(QStringLiteral("rtkServiceOperationsPanel"));
    require(ntripPanel && serviceOperationsPanel && testConnectionButton && startButton &&
                stopButton && clearLogButton &&
                serviceOperationsPanel->isAncestorOf(startButton) &&
                serviceOperationsPanel->isAncestorOf(stopButton) &&
                serviceOperationsPanel->isAncestorOf(testConnectionButton) &&
                serviceOperationsPanel->isAncestorOf(clearLogButton),
            "RTK connection actions are collected inside the bottom service-operations panel");
    require(std::abs(widgetY(testConnectionButton) - widgetY(startButton)) <= 2 &&
                std::abs(widgetY(startButton) - widgetY(stopButton)) <= 2 &&
                std::abs(widgetY(stopButton) - widgetY(clearLogButton)) <= 2 &&
                widgetX(startButton) < widgetX(stopButton) &&
                widgetX(stopButton) < widgetX(testConnectionButton) &&
                widgetX(testConnectionButton) < widgetX(clearLogButton),
            "RTK start, stop, test, and clear actions form one ordered operation group");
    auto *ggaSourceCombo = dialog->findChild<QComboBox *>(QStringLiteral("rtkGgaPortCombo"));
    auto *ggaToggleButton = dialog->findChild<QPushButton *>(QStringLiteral("rtkGgaToggleButton"));
    auto *ggaOutputText = dialog->findChild<QTextEdit *>(QStringLiteral("rtkGgaTextEdit"));
    QLabel *ggaSourceLabel = findLabelByText(dialog,
                                             {QStringLiteral("GGA来源:"),
                                              QStringLiteral("GGA Source:")});
    QLabel *ggaFrequencyLabel = nullptr;
    for (QLabel *label : dialog->findChildren<QLabel *>())
    {
        if (label->text() == QStringLiteral("0.00 Hz"))
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
    for (int index = 0; index < ggaSourceCombo->count(); ++index)
    {
        require(!ggaSourceCombo->itemText(index).trimmed().isEmpty(),
                "RTK GGA source combo does not include blank separator rows");
    }
    const int compactGgaSourceWidth =
        ggaSourceCombo->fontMetrics().horizontalAdvance(ggaSourceCombo->currentText()) + 120;
    require(ggaSourceCombo->width() <= compactGgaSourceWidth &&
                ggaSourceCombo->width() >= ggaSourceCombo->fontMetrics().horizontalAdvance(
                    ggaSourceCombo->currentText()) + 32,
            "RTK GGA source combo keeps usable compact title-row width");
    require(ggaSourceCombo->lineEdit() != nullptr,
            "RTK GGA source combo exposes its editable text field");
    const int ggaSourceTextWidth =
        ggaSourceCombo->lineEdit()->fontMetrics().horizontalAdvance(ggaSourceCombo->currentText());
    require(ggaSourceCombo->lineEdit()->contentsRect().width() >= ggaSourceTextWidth + 4,
            "RTK GGA source combo text field fully shows the Epsilon generated label");
    require(ggaToggleButton->text() == QStringLiteral("读取") ||
                ggaToggleButton->text() == QStringLiteral("Read"),
            "RTK GGA idle action uses a compact read label");
    require(ggaToggleButton->focusPolicy() == Qt::TabFocus,
            "RTK GGA read button remains keyboard reachable");
    require(ggaFrequencyLabel->text() == QStringLiteral("0.00 Hz") &&
                !ggaFrequencyLabel->text().contains(QStringLiteral("频率")) &&
                !ggaFrequencyLabel->text().contains(QStringLiteral("Rate")),
            "RTK GGA frequency readout shows only the numeric hertz value");
    require(ggaFrequencyLabel->alignment().testFlag(Qt::AlignHCenter) &&
                ggaFrequencyLabel->alignment().testFlag(Qt::AlignVCenter),
            "RTK GGA frequency readout centers its text in both directions");
    const int legacyFrequencyWidth = ggaFrequencyLabel->fontMetrics().horizontalAdvance(
        QStringLiteral("频率: -999.99 Hz"));
    require(ggaToggleButton->width() < legacyFrequencyWidth &&
                ggaFrequencyLabel->width() < legacyFrequencyWidth,
            "RTK GGA read controls no longer reserve the legacy prefixed frequency width");
    auto *streamPanel = dialog->findChild<QWidget *>(QStringLiteral("rtkStreamStatusPanel"));
    auto *ggaClearButton = dialog->findChild<QToolButton *>(QStringLiteral("rtkGgaClearLogButton"));
    require(streamPanel && ggaClearButton && ggaCard->isAncestorOf(ggaSourceLabel) &&
                ggaCard->isAncestorOf(ggaSourceCombo) &&
                ggaCard->isAncestorOf(ggaClearButton) &&
                streamPanel->isAncestorOf(ggaToggleButton) &&
                streamPanel->isAncestorOf(ggaOutputText) &&
                widgetY(ggaSourceCombo) < widgetY(ggaToggleButton),
            "RTK GGA source selector sits in the title row above its monitor actions");
    const bool ggaActionRowAligned =
        std::abs((widgetY(ggaFrequencyLabel) + ggaFrequencyLabel->height() / 2) -
                 (widgetY(ggaToggleButton) + ggaToggleButton->height() / 2)) <= 2 &&
        std::abs((widgetY(ggaClearButton) + ggaClearButton->height() / 2) -
                 (widgetY(ggaSourceCombo) + ggaSourceCombo->height() / 2)) <= 2 &&
        widgetX(ggaToggleButton) < widgetX(ggaFrequencyLabel) &&
        widgetX(ggaClearButton) > widgetX(ggaSourceCombo);
    if (!ggaActionRowAligned)
    {
        std::cerr << "GGA action row: toggle=" << widgetRect(ggaToggleButton).x() << ','
                  << widgetRect(ggaToggleButton).y() << ' ' << widgetRect(ggaToggleButton).width()
                  << 'x' << widgetRect(ggaToggleButton).height()
                  << " frequency=" << widgetRect(ggaFrequencyLabel).x() << ','
                  << widgetRect(ggaFrequencyLabel).y() << ' ' << widgetRect(ggaFrequencyLabel).width()
                  << 'x' << widgetRect(ggaFrequencyLabel).height()
                  << " clear=" << widgetRect(ggaClearButton).x() << ','
                  << widgetRect(ggaClearButton).y() << ' ' << widgetRect(ggaClearButton).width()
                  << 'x' << widgetRect(ggaClearButton).height() << '\n';
    }
    require(ggaActionRowAligned,
            "RTK GGA title-row source/clear and body read/frequency controls stay ordered");
    require(widgetX(ggaOutputText) >= widgetX(ggaToggleButton) + ggaToggleButton->width(),
            "RTK GGA output text area sits to the right of the read controls");
    const int controlsTop = widgetY(ggaToggleButton);
    const int controlsBottom = widgetY(ggaToggleButton) + ggaToggleButton->height();
    const int outputTop = widgetY(ggaOutputText);
    const int outputBottom = outputTop + ggaOutputText->height();
    require(std::abs((controlsTop + controlsBottom) - (outputTop + outputBottom)) <= 4,
            "RTK GGA read controls are vertically centered beside the output text area");
    require(ggaOutputText->width() >= 120,
            "RTK GGA reminder output keeps a usable minimum width");
    require(dialog->findChild<QLabel *>(QStringLiteral("rtkGgaStatusLabel")) == nullptr,
            "RTK GGA monitor does not use a standalone status label");
    require(!ggaOutputText->toPlainText().contains(QStringLiteral("状态:")) &&
                !ggaOutputText->toPlainText().contains(QStringLiteral("Status:")),
            "RTK GGA log stays quiet before reading starts");
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
    auto *rtcmOutputPanel = dialog->findChild<QWidget *>(QStringLiteral("rtkEpsilonDataPathPanel"));
    QLabel *outputPortLabel = findLabelByText(dialog,
                                              {QStringLiteral("RTCM 输出串口:"),
                                               QStringLiteral("RTCM Output Port:")});
    require(rtcmOutputPanel != nullptr && outputPortCombo != nullptr && baudrateCombo != nullptr && timeoutCombo != nullptr &&
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
            "RTK RTCM baudrate shares the first compact output row");
    const bool timingRowAligned =
        rtcmOutputPanel->isAncestorOf(timeoutCombo) && rtcmOutputPanel->isAncestorOf(reconnectCombo) &&
        widgetY(timeoutCombo) > widgetY(baudrateCombo) &&
        std::abs(widgetY(reconnectCombo) - widgetY(timeoutCombo)) <= 2 &&
        widgetX(reconnectCombo) > widgetX(timeoutCombo);
    require(timingRowAligned,
            "RTK timeout and reconnect interval share the second RTCM output row");
    auto *leverXEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkLeverXEdit"));
    auto *leverYEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkLeverYEdit"));
    auto *leverZEdit = dialog->findChild<QLineEdit *>(QStringLiteral("rtkLeverZEdit"));
    require(leverXEdit != nullptr && leverYEdit != nullptr && leverZEdit != nullptr,
            "RTK RTCM lever-arm XYZ edits exist");
    require(widgetY(leverXEdit) > widgetY(timeoutCombo) &&
                std::abs(widgetY(leverYEdit) - widgetY(leverXEdit)) <= 2 &&
                std::abs(widgetY(leverZEdit) - widgetY(leverXEdit)) <= 2,
            "RTK RTCM lever-arm XYZ controls share the third output row");
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
    require(leverHelpButton->styleSheet().contains(QStringLiteral("border-radius: 6px")) &&
                leverHelpButton->styleSheet().contains(QStringLiteral("QToolButton:pressed")) &&
                leverHelpButton->styleSheet().contains(
                    VaporView::appThemeColorName(VaporView::AppThemeColor::TitleBarHover, false)),
            "RTK lever-arm help uses the title-bar rounded gray hover and pressed background");
    clickWidget(leverHelpButton, 100);
    auto *leverHelpPopup = dialog->findChild<VaporView::SingleLevelPopupMenu *>(QStringLiteral("rtkLeverHelpPopup"));
    require(leverHelpPopup != nullptr && leverHelpPopup->isVisible(),
            "RTK lever-arm help opens a menu-like popup");
    require(leverHelpPopup->property("floatingPanelChrome").toBool() &&
                leverHelpPopup->property("shadowMargin").toInt() == 40 &&
                leverHelpPopup->property("shadowBottomMargin").toInt() == 50 &&
                leverHelpPopup->testAttribute(Qt::WA_TranslucentBackground) &&
                leverHelpPopup->styleSheet().contains(QStringLiteral("background-color: transparent; border: none")),
            "RTK lever-arm help uses the shared rounded shadow popup chrome");
    auto *leverHelpText = leverHelpPopup->findChild<QLabel *>(QStringLiteral("rtkLeverHelpPopupText"));
    require(leverHelpText != nullptr &&
                (leverHelpText->text().contains(QStringLiteral("主天线杆臂")) ||
                 leverHelpText->text().contains(QStringLiteral("Main antenna lever arm"))),
            "RTK lever-arm help popup contains the lever-arm guidance");
    leverHelpPopup->hide();
    processEventsFor(50);

    require(processEventsUntil(1000, [dialog, ntripCard, ggaCard, rtcmCard, logCard]() {
                activateLayouts(dialog);
                return std::abs(ntripCard->height() - ggaCard->height()) <= 2 &&
                    std::abs(rtcmCard->height() - logCard->height()) <= 2;
            }),
            "RTK cards settle to equal heights within each visual row after page return");

    const QRect ntripRectBeforeTheme = widgetRect(ntripCard);
    const QRect ggaRectBeforeTheme = widgetRect(ggaCard);
    const QRect rtcmRectBeforeTheme = widgetRect(rtcmCard);
    const QRect logRectBeforeTheme = widgetRect(logCard);
    const auto rectStableWithinTolerance = [](const QRect& actual, const QRect& expected) {
        return std::abs(actual.x() - expected.x()) <= 2 &&
               std::abs(actual.y() - expected.y()) <= 2 &&
               std::abs(actual.width() - expected.width()) <= 2 &&
               std::abs(actual.height() - expected.height()) <= 2;
    };
    const auto themeCardGeometrySettled = [&]() {
        activateLayouts(dialog);
        return rectStableWithinTolerance(widgetRect(ntripCard), ntripRectBeforeTheme) &&
               rectStableWithinTolerance(widgetRect(ggaCard), ggaRectBeforeTheme) &&
               rectStableWithinTolerance(widgetRect(rtcmCard), rtcmRectBeforeTheme) &&
               rectStableWithinTolerance(widgetRect(logCard), logRectBeforeTheme);
    };
    require(outputPortLabel->width() >= outputPortLabel->fontMetrics().horizontalAdvance(outputPortLabel->text()) + 4,
            "RTK RTCM output port label has enough width before theme switch");
    require(QMetaObject::invokeMethod(&window, "onToggleTheme", Qt::DirectConnection),
            "main window can switch to dark theme from the RTK page");
    processEventsFor(250);
    requireMainWindowOuterBorder(window, true,
                                 "dark theme main window refreshes white left, right and bottom outer borders");
    activateLayouts(dialog);
    processEventsFor(100);
    require(qApp->property(VaporView::kAppDarkThemeProperty).toBool(),
            "main window is in dark theme for RTK layout stability checks");
    require(!combinationPage->styleSheet().contains(QStringLiteral(":focus")),
            "combination-navigation dark-theme style keeps focus rings out of stylesheet/native painting");
    require(combinationPage->styleSheet().contains(
                VaporView::appThemeColorName(VaporView::AppThemeColor::Window, true)) &&
                combinationPage->styleSheet().contains(
                    QStringLiteral("QScrollArea#navigationStatusScrollArea QScrollBar:vertical")) &&
                combinationPage->styleSheet().contains(QStringLiteral("margin: 0px")),
            "combination-navigation outer background and scrollbar rail use the dark window color");
    auto *statusPanel = combinationPage->findChild<QWidget *>(
        QStringLiteral("navigationStatusPanel"));
    require(statusPanel != nullptr &&
                statusPanel->styleSheet().contains(
                    VaporView::appThemeColorName(VaporView::AppThemeColor::Window, true)) &&
                epsilonPanel->styleSheet().contains(
                    VaporView::appThemeColorName(VaporView::AppThemeColor::Window, true)),
            "status and EPSILON card containers use the dark window color outside cards");
    const bool darkThemeGeometrySettled = processEventsUntil(1500, themeCardGeometrySettled);
    if (!darkThemeGeometrySettled)
    {
        const auto printRect = [](const char *name, const QRect& before, const QRect& after) {
            std::cerr << name << " before=" << before.x() << ',' << before.y() << ' '
                      << before.width() << 'x' << before.height()
                      << " after=" << after.x() << ',' << after.y() << ' '
                      << after.width() << 'x' << after.height() << '\n';
        };
        printRect("NTRIP", ntripRectBeforeTheme, widgetRect(ntripCard));
        printRect("Differential", ggaRectBeforeTheme, widgetRect(ggaCard));
        printRect("EPSILON", rtcmRectBeforeTheme, widgetRect(rtcmCard));
        printRect("Messages", logRectBeforeTheme, widgetRect(logCard));
    }
    require(darkThemeGeometrySettled,
            "RTK card geometry settles after switching to dark theme");
    requireSameRect(widgetRect(ntripCard), ntripRectBeforeTheme,
                    "RTK NTRIP card geometry stays stable after switching to dark theme");
    requireSameRect(widgetRect(ggaCard), ggaRectBeforeTheme,
                    "RTK differential-link card geometry stays stable after switching to dark theme");
    requireSameRect(widgetRect(rtcmCard), rtcmRectBeforeTheme,
                    "RTK EPSILON data-path card geometry stays stable after switching to dark theme");
    requireSameRect(widgetRect(logCard), logRectBeforeTheme,
                    "RTK status/message card geometry stays stable after switching to dark theme");
    require(std::abs(ntripCard->height() - ggaCard->height()) <= 2 &&
                std::abs(rtcmCard->height() - logCard->height()) <= 2,
            "RTK cards remain equal-height within each visual row in dark theme");
    requireColorNear(averageVisibleIconColor(leverHelpButton->icon()),
                     VaporView::appThemeColor(VaporView::AppThemeColor::Primary, true),
                     6,
                     "RTK lever-arm help icon uses the dark theme primary color");
    require(leverHelpButton->styleSheet().contains(
                VaporView::appThemeColorName(VaporView::AppThemeColor::TitleBarHover, true)),
            "RTK lever-arm help keeps the title-bar gray hover and pressed background in dark theme");
    require(outputPortLabel->width() >= outputPortLabel->fontMetrics().horizontalAdvance(outputPortLabel->text()) + 4,
            "RTK RTCM output port label has enough width after switching to dark theme");
    require(rtkScrollArea->horizontalScrollBar()->maximum() == 0 &&
                rtkScrollArea->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOn,
            "RTK config page remains free of horizontal scrolling after switching to dark theme");
    require(QMetaObject::invokeMethod(&window, "onToggleTheme", Qt::DirectConnection),
            "main window can switch back to light theme from the RTK page");
    processEventsFor(250);
    requireMainWindowOuterBorder(window, false,
                                 "light theme main window restores black outer borders after switching back");
    activateLayouts(dialog);
    processEventsFor(100);
    require(!qApp->property(VaporView::kAppDarkThemeProperty).toBool(),
            "main window returns to light theme after RTK layout stability checks");
    require(!combinationPage->styleSheet().contains(QStringLiteral(":focus")),
            "combination-navigation light-theme style keeps focus rings out of stylesheet/native painting");
    require(processEventsUntil(1500, themeCardGeometrySettled),
            "RTK card geometry settles after switching back to light theme");
    requireSameRect(widgetRect(ntripCard), ntripRectBeforeTheme,
                    "RTK NTRIP card geometry returns unchanged after switching back to light theme");
    requireSameRect(widgetRect(ggaCard), ggaRectBeforeTheme,
                    "RTK differential-link card geometry returns unchanged after switching back to light theme");
    requireSameRect(widgetRect(rtcmCard), rtcmRectBeforeTheme,
                    "RTK EPSILON data-path card geometry returns unchanged after switching back to light theme");
    requireSameRect(widgetRect(logCard), logRectBeforeTheme,
                    "RTK status/message card geometry returns unchanged after switching back to light theme");
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

    clickWidget(statusButton, 0);
    processEventsFor(50);
    clickWidget(epsilonButton, 0);
    processEventsFor(50);
    require(combinationStack->currentWidget() == epsilonPage &&
                combinationPage->differentialPage() == dialog &&
                combinationPage->epsilonConfigPanel() == epsilonPanel &&
                combinationPage->findChildren<RtkConfigDialog *>().size() == 1 &&
                (rtkButton->toolTip().contains(QStringLiteral("运行中")) ||
                 rtkButton->toolTip().contains(QStringLiteral("running"))) &&
                rtkButton->icon().pixmap(iconSize).cacheKey() == runningIconKey,
            "switching status and EPSILON pages preserves the same running RTK page and status");
    QComboBox *unsavedPacketRateCombo = nullptr;
    for (QComboBox *combo : epsilonPanel->findChildren<QComboBox *>())
    {
        if (combo->property("epsilonPacketId").isValid() && combo->count() > 1)
        {
            unsavedPacketRateCombo = combo;
            break;
        }
    }
    require(unsavedPacketRateCombo != nullptr,
            "EPSILON page exposes a packet-rate input for state-persistence testing");
    const int originalPacketRateIndex = unsavedPacketRateCombo->currentIndex();
    const int unsavedPacketRateIndex = (originalPacketRateIndex + 1) % unsavedPacketRateCombo->count();
    unsavedPacketRateCombo->setCurrentIndex(unsavedPacketRateIndex);
    clickWidget(differentialButton, 0);
    processEventsFor(50);
    require(combinationStack->currentWidget() == dialog,
            "the running RTK page remains available after internal navigation");
    clickWidget(statusButton, 0);
    processEventsFor(50);
    clickWidget(epsilonButton, 0);
    processEventsFor(50);
    require(combinationPage->epsilonConfigPanel() == epsilonPanel &&
                unsavedPacketRateCombo->currentIndex() == unsavedPacketRateIndex,
            "switching status, differential and EPSILON pages preserves the same panel and unsaved input");
    unsavedPacketRateCombo->setCurrentIndex(originalPacketRateIndex);
    clickWidget(differentialButton, 0);
    processEventsFor(50);

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
    require(QMetaObject::invokeMethod(&window, "onRtkConfigClicked", Qt::DirectConnection),
            "existing RTK action can open combination navigation");
    processEventsFor(100);
    require(pageStack->currentWidget() == combinationPage &&
                combinationStack->currentWidget() == dialog && differentialButton->isChecked(),
            "existing RTK action deep-links to the differential-positioning subpage");
    clickWidget(homeButton);
    processEventsFor(100);
    require(QMetaObject::invokeMethod(&window, "onSwitchLanguage", Qt::DirectConnection),
            "combination-navigation labels can switch to English");
    processEventsFor(100);
    require(statusButton->text() == QStringLiteral("Status") &&
                statusButton->accessibleName() == statusButton->text() &&
                epsilonButton->text() == QStringLiteral("EPSILON") &&
                epsilonButton->accessibleName() == epsilonButton->text() &&
                differentialButton->text() == QStringLiteral("Differential Positioning") &&
                differentialButton->accessibleName() == differentialButton->text() &&
                rtkButton->accessibleName() == QStringLiteral("Combination Navigation"),
            "combination-navigation visible and accessible labels update together in English");
    require(QMetaObject::invokeMethod(&window, "onSwitchLanguage", Qt::DirectConnection),
            "combination-navigation labels can return to Chinese");
    processEventsFor(100);
    require(statusButton->text() == QStringLiteral("状态") &&
                differentialButton->text() == QStringLiteral("差分定位") &&
                rtkButton->accessibleName() == QStringLiteral("组合导航"),
            "combination-navigation labels return to Chinese without rebuilding the page");
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
        if (combo->findData(QStringLiteral("__vv_manual_serial_port__")) >= 0)
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

void requireLabelTextSlack(const QLabel *label, int minimumSlack, const char *message)
{
    require(label != nullptr, message);
    const int textWidth = std::max(label->fontMetrics().horizontalAdvance(label->text()),
                                   label->fontMetrics().boundingRect(label->text()).width());
    require(label->width() >= textWidth + minimumSlack, message);
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

void requireSidebarCardStyle(const QString& styleSheet,
                             bool dark,
                             const char *message)
{
    const QString borderColor =
        VaporView::appThemeColorName(VaporView::AppThemeColor::Border, dark);
    const QString selector = QStringLiteral("QFrame#appSidebar {");
    requireLastStyleRuleContains(styleSheet, selector,
                                 QStringLiteral("border: 1px solid %1").arg(borderColor), message);
    requireLastStyleRuleContains(styleSheet, selector, QStringLiteral("border-radius: 8px"), message);
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
                                 QStringLiteral("background-color: transparent"),
                                 message);
    requireLastStyleRuleContains(styleSheet,
                                 QStringLiteral("QMenu::item:selected {"),
                                 QStringLiteral("color: %1").arg(textColor),
                                 message);
    requireLastStyleRuleContains(styleSheet,
                                 QStringLiteral("QMenu::item:hover {"),
                                 QStringLiteral("background-color: %1").arg(hoverColor),
                                 message);
    requireLastStyleRuleContains(styleSheet,
                                 QStringLiteral("QMenu::item:hover {"),
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

struct TelemetryPillWidthSnapshotItem
{
    QString key;
    int width = 0;
};

QVector<TelemetryPillWidthSnapshotItem> telemetryPillWidthSnapshot(QWidget *summaryContainer)
{
    QVector<TelemetryPillWidthSnapshotItem> snapshot;
    const QList<QFrame*> sections = sortedTelemetrySections(summaryContainer);
    for (int sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex)
    {
        QFrame *section = sections.at(sectionIndex);
        const QList<QFrame*> sectionPills =
            section->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
        QList<QWidget*> lines;
        for (QFrame *pill : sectionPills)
        {
            if (QWidget *line = pill->parentWidget(); line && !lines.contains(line))
            {
                lines.append(line);
            }
        }
        std::sort(lines.begin(), lines.end(), [section](QWidget *a, QWidget *b) {
            const QPoint aPos = a->mapTo(section, QPoint(0, 0));
            const QPoint bPos = b->mapTo(section, QPoint(0, 0));
            return std::make_tuple(aPos.y(), aPos.x()) < std::make_tuple(bPos.y(), bPos.x());
        });
        for (QWidget *line : lines)
        {
            QList<QFrame*> linePills =
                line->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"),
                                             Qt::FindDirectChildrenOnly);
            std::sort(linePills.begin(), linePills.end(), [section](QFrame *a, QFrame *b) {
                return a->mapTo(section, QPoint(0, 0)).x() < b->mapTo(section, QPoint(0, 0)).x();
            });
            for (QFrame *pill : linePills)
            {
                QLabel *nameLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel"));
                QLabel *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
                snapshot.append({
                    QStringLiteral("%1:%2=%3").arg(sectionIndex)
                                             .arg(nameLabel ? nameLabel->text() : QString())
                                             .arg(valueLabel ? valueLabel->text() : QString()),
                    pill->width()});
            }
        }
    }
    return snapshot;
}

void requireTelemetryPillWidthSnapshotNotWider(const QVector<TelemetryPillWidthSnapshotItem>& expected,
                                               const QVector<TelemetryPillWidthSnapshotItem>& actual,
                                               const char *message)
{
    require(expected.size() == actual.size(), message);
    for (int i = 0; i < expected.size(); ++i)
    {
        if (expected.at(i).key != actual.at(i).key ||
            actual.at(i).width > expected.at(i).width + 1)
        {
            std::cerr << "Telemetry pill width mismatch at " << i
                      << ": expected " << expected.at(i).key.toStdString()
                      << " max width=" << expected.at(i).width
                      << ", actual " << actual.at(i).key.toStdString()
                      << " width=" << actual.at(i).width << '\n';
        }
        require(expected.at(i).key == actual.at(i).key, message);
        require(actual.at(i).width <= expected.at(i).width + 1, message);
    }
}

void requireTelemetryLabelsHaveNoColon(QWidget *summaryContainer, const char *message)
{
    require(summaryContainer != nullptr, message);
    const QList<QLabel*> labels = summaryContainer->findChildren<QLabel *>();
    require(!labels.isEmpty(), message);
    for (QLabel *label : labels)
    {
        if (label && label->objectName() == QStringLiteral("homeTelemetrySummaryValueLabel"))
        {
            continue;
        }
        require(label != nullptr &&
                    !label->text().contains(QLatin1Char(':')) &&
                    !label->text().contains(QStringLiteral("：")),
                message);
    }
}

void requireTelemetryLabelsFit(QWidget *summaryContainer, const char *message)
{
    require(summaryContainer != nullptr, message);
    const QList<QLabel*> labels = summaryContainer->findChildren<QLabel *>();
    require(!labels.isEmpty(), message);
    for (QLabel *label : labels)
    {
        if (label->text().isEmpty())
        {
            continue;
        }
        const int textWidth = label->fontMetrics().horizontalAdvance(label->text());
        if (textWidth > label->width() + 1)
        {
            std::cerr << "Telemetry label clipped: object=" << label->objectName().toStdString()
                      << " text='" << label->text().toStdString()
                      << "' textWidth=" << textWidth
                      << " labelWidth=" << label->width() << '\n';
        }
        require(textWidth <= label->width() + 1, message);
    }
}

void requireCompactTelemetryPillTextGap(QFrame *pill, const char *message)
{
    require(pill != nullptr, message);
    QLabel *nameLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel"));
    QLabel *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
    require(nameLabel != nullptr && valueLabel != nullptr, message);
    require(valueLabel->alignment().testFlag(Qt::AlignLeft),
            "home telemetry values start near their field names");

    const int nameTextRight = nameLabel->mapTo(pill, QPoint(0, 0)).x() +
        nameLabel->fontMetrics().horizontalAdvance(nameLabel->text());
    const int valueTextLeft = valueLabel->mapTo(pill, QPoint(0, 0)).x();
    const int gap = valueTextLeft - nameTextRight;
    if (gap < 3 || gap > 10)
    {
        std::cerr << "Home telemetry pill text gap: name='"
                  << nameLabel->text().toStdString()
                  << "' value='" << valueLabel->text().toStdString()
                  << "' gap=" << gap << '\n';
    }
    require(gap >= 3 && gap <= 10,
            "home telemetry field/value text gap keeps a small reserved space");
}

void requireTelemetrySummaryPillsOrdered(QWidget *summaryContainer, const char *message)
{
    const QList<QFrame*> sections = sortedTelemetrySections(summaryContainer);
    require(sections.size() >= 3, message);
    for (QFrame *section : sections)
    {
        const QList<QFrame*> pills =
            section->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
        require(!pills.isEmpty(), message);
        QList<QWidget*> lines;
        for (QFrame *pill : pills)
        {
            if (QWidget *line = pill->parentWidget(); line && !lines.contains(line))
            {
                lines.append(line);
            }
            const QRect pillRect(pill->mapTo(section, QPoint(0, 0)), pill->size());
            if (!section->rect().adjusted(0, 0, 1, 1).contains(pillRect))
            {
                QGroupBox *deviceCard = nullptr;
                for (QWidget *ancestor = section; ancestor && !deviceCard; ancestor = ancestor->parentWidget())
                {
                    deviceCard = qobject_cast<QGroupBox *>(ancestor);
                }
                std::cerr << "Telemetry pill outside section: section=" << section->rect().width()
                          << " section min=" << section->minimumWidth()
                          << " card width=" << (deviceCard ? deviceCard->width() : 0)
                          << " card min=" << (deviceCard ? deviceCard->minimumWidth() : 0)
                          << " pill x=" << pillRect.x()
                          << " width=" << pillRect.width()
                          << " right=" << pillRect.right() << '\n';
            }
            require(section->rect().adjusted(0, 0, 1, 1).contains(pillRect), message);
        }

        for (QWidget *line : lines)
        {
            QLabel *titleLabel = line->findChild<QLabel *>(
                QStringLiteral("homeTelemetrySummaryTitleLabel"),
                Qt::FindDirectChildrenOnly);
            QList<QFrame*> linePills =
                line->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"),
                                             Qt::FindDirectChildrenOnly);
            std::sort(linePills.begin(), linePills.end(), [section](QFrame *a, QFrame *b) {
                return a->mapTo(section, QPoint(0, 0)).x() < b->mapTo(section, QPoint(0, 0)).x();
            });
            if (titleLabel && !linePills.isEmpty())
            {
                const QRect titleRect(titleLabel->mapTo(section, QPoint(0, 0)), titleLabel->size());
                const QRect firstPillRect(linePills.first()->mapTo(section, QPoint(0, 0)),
                                          linePills.first()->size());
                if (firstPillRect.left() <= titleRect.right())
                {
                    std::cerr << "Telemetry title overlap: title='" << titleLabel->text().toStdString()
                              << "' title right=" << titleRect.right()
                              << " first pill left=" << firstPillRect.left()
                              << " first pill width=" << firstPillRect.width() << '\n';
                }
                require(firstPillRect.left() > titleRect.right(), message);
                const int titleTextRight =
                    titleRect.left() + titleLabel->fontMetrics().horizontalAdvance(titleLabel->text());
                require(firstPillRect.left() - titleTextRight >= 6,
                        "home telemetry title text keeps a visible gap before the first pill");
            }
            QRect previousRect;
            bool hasPrevious = false;
            for (QFrame *pill : linePills)
            {
                const QRect pillRect(pill->mapTo(section, QPoint(0, 0)), pill->size());
                if (hasPrevious && pillRect.left() <= previousRect.right())
                {
                    std::cerr << "Telemetry pill overlap: section=" << section->rect().width()
                              << " previous right=" << previousRect.right()
                              << " current left=" << pillRect.left()
                              << " current width=" << pillRect.width() << '\n';
                }
                require(!hasPrevious || pillRect.left() > previousRect.right(), message);
                previousRect = pillRect;
                hasPrevious = true;
            }
        }
    }
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
                                          {QStringLiteral("三维地图"),
                                           QStringLiteral("3D Map")});
    QAction *diagnosticsAction = findActionByText(&window,
                                                  {QStringLiteral("地图数据诊断"),
                                                   QStringLiteral("Map Data Diagnostics")});
    require(mapAction != nullptr, "3D map action exists in the main window");
    require(diagnosticsAction != nullptr, "map data diagnostics action exists in the main window");

    QMenu *viewMenu = nullptr;
    QMenu *developerMenu = nullptr;
    const QList<QMenu*> menus = window.findChildren<QMenu *>();
    for (QMenu *menu : menus)
    {
        if (menu && menu->actions().contains(mapAction))
        {
            viewMenu = menu;
        }
        if (menu && menu->actions().contains(diagnosticsAction))
        {
            developerMenu = menu;
        }
    }
    require(viewMenu != nullptr, "View menu exists for the 3D map entry");
    require(developerMenu != nullptr, "Developer menu exists for map diagnostics");
    require(viewMenu->actions().contains(mapAction), "View menu contains the 3D map action");
    require(!viewMenu->actions().contains(diagnosticsAction),
            "View menu omits the developer-only map data diagnostics action");
    require(developerMenu->actions().contains(diagnosticsAction),
            "Developer menu contains the map data diagnostics action");

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
    require(!foundDiagnosticsTitleButton,
            "title bar omits the troubleshooting-only map data diagnostics action");
}

#else
void requireMainWindowOmitsMap3DEntries(MainWindow& window)
{
    QAction *mapAction = findActionByText(&window,
                                          {QStringLiteral("三维地图"),
                                           QStringLiteral("3D Map")});
    QAction *diagnosticsAction = findActionByText(&window,
                                                  {QStringLiteral("地图数据诊断"),
                                                   QStringLiteral("Map Data Diagnostics")});
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

void requireMainWindowOuterBorder(MainWindow& window, bool dark, const char *message)
{
    const QString expectedColor = VaporView::appThemeColorName(
        dark ? VaporView::AppThemeColor::White : VaporView::AppThemeColor::Black,
        dark);
    const QList<QFrame *> borders = {
        window.findChild<QFrame *>(QStringLiteral("windowBorderLeft")),
        window.findChild<QFrame *>(QStringLiteral("windowBorderRight")),
        window.findChild<QFrame *>(QStringLiteral("windowBorderBottom"))};
    require(std::all_of(borders.cbegin(), borders.cend(), [&expectedColor](QFrame *border) {
                return border && border->isVisible() && border->styleSheet().contains(expectedColor);
            }),
            message);
}

void requireEpsilonSectionTitleWidths(QWidget *epsilonPanel, bool english)
{
    require(epsilonPanel != nullptr, "language overview EPSILON panel exists");
    const QList<QLabel*> sectionLabels =
        epsilonPanel->findChildren<QLabel *>(QStringLiteral("epsilonSectionLabel"));
    require(sectionLabels.size() == 3,
            "language overview EPSILON panel exposes three section title labels");

    bool sawWideEnglishTitle = false;
    for (const QLabel *label : sectionLabels)
    {
        require(label != nullptr, "language overview EPSILON section title label exists");
        const QFontMetrics metrics(label->font());
        int maxLineWidth = 0;
        const QStringList lines = label->text().split(QChar('\n'));
        for (const QString& line : lines)
        {
            maxLineWidth = std::max(maxLineWidth, metrics.horizontalAdvance(line));
        }
        require(label->width() >= maxLineWidth + 4,
                "language overview EPSILON section title fits its widest line");
        if (english)
        {
            sawWideEnglishTitle = sawWideEnglishTitle || label->width() > 24;
        }
        else
        {
            require(label->width() <= 30,
                    "language overview EPSILON Chinese section title stays compact");
        }
    }
    if (english)
    {
        require(sawWideEnglishTitle,
                "language overview EPSILON English section titles widen beyond the compact Chinese rail");
    }
}

bool epsilonSectionTitlesContain(QWidget *epsilonPanel, const QString& token)
{
    if (!epsilonPanel)
    {
        return false;
    }
    const QList<QLabel*> sectionLabels =
        epsilonPanel->findChildren<QLabel *>(QStringLiteral("epsilonSectionLabel"));
    return std::any_of(sectionLabels.cbegin(), sectionLabels.cend(), [&token](const QLabel *label) {
        return label && label->text().contains(token);
    });
}

QList<QFrame*> sortedEpsilonSectionCards(QWidget *epsilonPanel)
{
    if (!epsilonPanel)
    {
        return {};
    }
    QList<QFrame*> cards = epsilonPanel->findChildren<QFrame *>(QStringLiteral("epsilonSectionCard"));
    std::sort(cards.begin(), cards.end(), [](const QFrame *lhs, const QFrame *rhs) {
        if (std::abs(lhs->y() - rhs->y()) > 4)
        {
            return lhs->y() < rhs->y();
        }
        return lhs->x() < rhs->x();
    });
    return cards;
}

bool epsilonSectionCardsShareRow(QWidget *epsilonPanel)
{
    const QList<QFrame*> cards = sortedEpsilonSectionCards(epsilonPanel);
    if (cards.size() != 3)
    {
        return false;
    }
    const int firstTop = cards.first()->mapTo(epsilonPanel, QPoint(0, 0)).y();
    return std::all_of(cards.cbegin(), cards.cend(), [epsilonPanel, firstTop](const QFrame *card) {
        return card && std::abs(card->mapTo(epsilonPanel, QPoint(0, 0)).y() - firstTop) <= 2;
    });
}

bool epsilonSectionCardsUseWrappedMotionRow(QWidget *epsilonPanel)
{
    const QList<QFrame*> cards = sortedEpsilonSectionCards(epsilonPanel);
    if (cards.size() != 3)
    {
        return false;
    }
    const QRect first(cards.at(0)->mapTo(epsilonPanel, QPoint(0, 0)), cards.at(0)->size());
    const QRect second(cards.at(1)->mapTo(epsilonPanel, QPoint(0, 0)), cards.at(1)->size());
    const QRect third(cards.at(2)->mapTo(epsilonPanel, QPoint(0, 0)), cards.at(2)->size());
    return std::abs(first.top() - second.top()) <= 4 &&
           second.left() > first.right() &&
           second.left() - first.right() <= 1 &&
           third.top() > first.bottom() &&
           third.left() <= first.left() + 2 &&
           third.right() >= second.right() - 2;
}

void requireEpsilonSectionCardsUseWrappedMotionRow(QWidget *epsilonPanel, const char *message)
{
    if (!epsilonSectionCardsUseWrappedMotionRow(epsilonPanel))
    {
        const QList<QFrame*> cards = sortedEpsilonSectionCards(epsilonPanel);
        std::cerr << "EPSILON card geometry:";
        for (const QFrame *card : cards)
        {
            const QRect rect(card->mapTo(epsilonPanel, QPoint(0, 0)), card->size());
            std::cerr << " [" << rect.x() << ',' << rect.y() << ' '
                      << rect.width() << 'x' << rect.height()
                      << " min=" << card->minimumWidth() << ']';
        }
        std::cerr << '\n';
    }
    require(epsilonSectionCardsUseWrappedMotionRow(epsilonPanel), message);
}

void requireEpsilonSectionCardsStayInsidePanel(QWidget *epsilonPanel, const char *message)
{
    const QList<QFrame*> cards = sortedEpsilonSectionCards(epsilonPanel);
    require(epsilonPanel != nullptr && cards.size() == 3, "EPSILON section cards exist for bounds check");
    const QRect bounds = epsilonPanel->contentsRect();
    bool inside = true;
    for (const QFrame *card : cards)
    {
        const QRect rect(card->mapTo(epsilonPanel, QPoint(0, 0)), card->size());
        if (rect.left() < bounds.left() - 2 || rect.right() > bounds.right() + 2)
        {
            inside = false;
        }
    }
    if (!inside)
    {
        std::cerr << "EPSILON panel bounds: [" << bounds.x() << ',' << bounds.y()
                  << ' ' << bounds.width() << 'x' << bounds.height() << ']'
                  << " card geometry:";
        for (const QFrame *card : cards)
        {
            const QRect rect(card->mapTo(epsilonPanel, QPoint(0, 0)), card->size());
            std::cerr << " [" << rect.x() << ',' << rect.y() << ' '
                      << rect.width() << 'x' << rect.height()
                      << " min=" << card->minimumWidth() << ']';
        }
        std::cerr << '\n';
    }
    require(inside, message);
}

void requireEpsilonSectionCardsFillWrappedTopRow(QWidget *epsilonPanel, const char *message)
{
    const QList<QFrame*> cards = sortedEpsilonSectionCards(epsilonPanel);
    require(epsilonPanel != nullptr && cards.size() == 3,
            "EPSILON section cards exist for wrapped-row fill check");
    const QRect first(cards.at(0)->mapTo(epsilonPanel, QPoint(0, 0)), cards.at(0)->size());
    const QRect second(cards.at(1)->mapTo(epsilonPanel, QPoint(0, 0)), cards.at(1)->size());
    const QRect third(cards.at(2)->mapTo(epsilonPanel, QPoint(0, 0)), cards.at(2)->size());
    const QRect bounds = epsilonPanel->contentsRect();
    const int topRowJoinGap = second.left() - first.right() - 1;
    const bool fillsWrappedRow =
        std::abs(first.top() - second.top()) <= 4 &&
        topRowJoinGap >= 0 &&
        topRowJoinGap <= 1 &&
        first.left() <= bounds.left() + 6 &&
        third.left() <= first.left() + 2 &&
        std::abs(second.right() - third.right()) <= 2 &&
        second.right() >= bounds.right() - 6;
    if (!fillsWrappedRow)
    {
        std::cerr << "EPSILON wrapped fill geometry: bounds=" << bounds.width()
                  << " first=[" << first.x() << ',' << first.y() << ' '
                  << first.width() << 'x' << first.height() << ']'
                  << " second=[" << second.x() << ',' << second.y() << ' '
                  << second.width() << 'x' << second.height() << ']'
                  << " third=[" << third.x() << ',' << third.y() << ' '
                  << third.width() << 'x' << third.height() << "]\n";
    }
    require(fillsWrappedRow, message);
}

QLabel *epsilonValueLabelForField(QWidget *epsilonPanel, const QString& fieldText)
{
    if (!epsilonPanel)
    {
        return nullptr;
    }
    QLabel *fieldLabel = nullptr;
    const QList<QLabel*> fieldLabels = epsilonPanel->findChildren<QLabel *>(QStringLiteral("fieldLabel"));
    for (QLabel *label : fieldLabels)
    {
        if (label && label->text() == fieldText)
        {
            fieldLabel = label;
            break;
        }
    }
    if (!fieldLabel)
    {
        return nullptr;
    }

    const QRect fieldRect(fieldLabel->mapTo(epsilonPanel, QPoint(0, 0)), fieldLabel->size());
    QLabel *bestValue = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    const QList<QLabel*> valueLabels = epsilonPanel->findChildren<QLabel *>(QStringLiteral("valueLabel"));
    for (QLabel *valueLabel : valueLabels)
    {
        if (!valueLabel)
        {
            continue;
        }
        const QRect valueRect(valueLabel->mapTo(epsilonPanel, QPoint(0, 0)), valueLabel->size());
        if (std::abs(valueRect.center().y() - fieldRect.center().y()) > std::max(fieldRect.height(), valueRect.height()))
        {
            continue;
        }
        const int distance = valueRect.left() - fieldRect.right();
        if (distance >= 0 && distance < bestDistance)
        {
            bestDistance = distance;
            bestValue = valueLabel;
        }
    }
    return bestValue;
}

void requireHomeOverviewLanguageWidthRoundTrip()
{
    MainWindow languageOverviewWindow;
    languageOverviewWindow.setWindowTitle(QStringLiteral("VaporView"));
    languageOverviewWindow.resize(1280, 800);
    languageOverviewWindow.show();
    require(waitForWindowExposed(&languageOverviewWindow),
            "dedicated language overview window becomes exposed");

    auto *languageHomeOverviewSplitter =
        languageOverviewWindow.findChild<QSplitter *>(QStringLiteral("homeOverviewSplitter"));
    require(languageHomeOverviewSplitter != nullptr &&
                languageHomeOverviewSplitter->count() == 2,
            "language overview window exposes the home overview splitter");
    auto *languageDeviceOverviewCard =
        qobject_cast<QGroupBox *>(languageHomeOverviewSplitter->widget(0));
    require(languageDeviceOverviewCard != nullptr,
            "language overview window exposes the device overview card");
    auto *languageEpsilonPanel =
        languageOverviewWindow.findChild<QWidget *>(QStringLiteral("epsilonPanel"));
    auto *languageTelemetrySummaryContainer = languageDeviceOverviewCard->findChild<QWidget *>(
        QStringLiteral("homeTelemetrySummaryContainer"));
    requireTelemetrySummaryPillsOrdered(
        languageTelemetrySummaryContainer,
        "language overview telemetry capsules start without overlap");
    requireTelemetryLabelsHaveNoColon(
        languageTelemetrySummaryContainer,
        "language overview Chinese waveform telemetry labels start without ASCII colons");
    requireTelemetryLabelsFit(
        languageTelemetrySummaryContainer,
        "language overview telemetry labels fit at startup");
    const QVector<TelemetryPillWidthSnapshotItem> initialTelemetryPillWidths =
        telemetryPillWidthSnapshot(languageTelemetrySummaryContainer);
    requireEpsilonSectionTitleWidths(languageEpsilonPanel, false);
    requireEpsilonSectionCardsUseWrappedMotionRow(
        languageEpsilonPanel,
        "language overview EPSILON Chinese cards start with motion details on the second row");
    QLabel *attitudeConsistencyValue = epsilonValueLabelForField(
        languageEpsilonPanel,
        QStringLiteral("姿态一致性[最大差值]:"));
    require(attitudeConsistencyValue != nullptr,
            "language overview exposes the EPSILON attitude consistency value label");
    attitudeConsistencyValue->setText(
        QStringLiteral("最大 0.066°（41-63 0.037°，41-64 0.030°，63-64 0.066°）"));
    attitudeConsistencyValue->setToolTip(attitudeConsistencyValue->text());
    attitudeConsistencyValue->updateGeometry();
    activateLayouts(&languageOverviewWindow);
    processEventsFor(50);
    activateLayouts(languageDeviceOverviewCard);
    requireEpsilonSectionCardsUseWrappedMotionRow(
        languageEpsilonPanel,
        "language overview keeps attitude consistency on the expanded second row after data arrives");
    const QRect attitudeConsistencyBounds = wrappedTextBounds(attitudeConsistencyValue);
    require(attitudeConsistencyBounds.height() <= attitudeConsistencyValue->height() + 2,
            "language overview shows the full EPSILON attitude consistency value without clipping");
    requireHomeDeviceColumnsAligned(&languageOverviewWindow);
    requireHomeDeviceMinimumWidthMatchesControls(&languageOverviewWindow);

    const QList<int> initialHomeOverviewSizes = languageHomeOverviewSplitter->sizes();
    require(initialHomeOverviewSizes.size() == 2,
            "language overview splitter exposes initial device and temperature widths");
    const int initialDeviceOverviewWidth = initialHomeOverviewSizes.at(0);
    require(std::abs(initialDeviceOverviewWidth -
                     languageDeviceOverviewCard->minimumWidth()) <= 1,
            "language overview device card starts at its Chinese minimum width");
    require(QMetaObject::invokeMethod(&languageOverviewWindow,
                                      "onSwitchLanguage",
                                      Qt::DirectConnection),
            "language overview window switches to English");
    require(processEventsUntil(1000, [&languageOverviewWindow,
                                      languageHomeOverviewSplitter,
                                      languageDeviceOverviewCard,
                                      languageEpsilonPanel]() {
                activateLayouts(&languageOverviewWindow);
                const QList<int> sizes = languageHomeOverviewSplitter->sizes();
                return sizes.size() == 2 &&
                       epsilonSectionTitlesContain(languageEpsilonPanel, QStringLiteral("Overall")) &&
                       std::abs(sizes.at(0) - languageDeviceOverviewCard->minimumWidth()) <= 1;
            }),
            "language overview device card settles at its English minimum width");
    const QList<int> englishHomeOverviewSizes = languageHomeOverviewSplitter->sizes();
    require(englishHomeOverviewSizes.size() == 2,
            "language overview splitter exposes English device and temperature widths");
    const int englishDeviceOverviewWidth = englishHomeOverviewSizes.at(0);
    const int englishAvailableWidth = englishHomeOverviewSizes.at(0) + englishHomeOverviewSizes.at(1);
    const int inflatedEnglishDeviceWidth =
        std::min(englishDeviceOverviewWidth + 72,
                 englishAvailableWidth - languageHomeOverviewSplitter->widget(1)->minimumWidth());
    require(inflatedEnglishDeviceWidth > englishDeviceOverviewWidth,
            "language overview has room to simulate an auto-expanded English device card");
    languageHomeOverviewSplitter->setProperty(
        VaporView::Ground::MainSupport::kHomeOverviewDeviceProgrammaticResizeProperty,
        true);
    languageHomeOverviewSplitter->setSizes({
        inflatedEnglishDeviceWidth,
        std::max(languageHomeOverviewSplitter->widget(1)->minimumWidth(),
                 englishAvailableWidth - inflatedEnglishDeviceWidth)
    });
    languageHomeOverviewSplitter->setProperty(
        VaporView::Ground::MainSupport::kHomeOverviewDeviceProgrammaticResizeProperty,
        false);
    languageHomeOverviewSplitter->setProperty(
        VaporView::Ground::MainSupport::kHomeOverviewDeviceAutoManagedWidthProperty,
        true);
    activateLayouts(&languageOverviewWindow);
    const QList<int> inflatedEnglishHomeOverviewSizes = languageHomeOverviewSplitter->sizes();
    require(inflatedEnglishHomeOverviewSizes.size() == 2 &&
                inflatedEnglishHomeOverviewSizes.at(0) > englishDeviceOverviewWidth,
            "language overview reproduces the wider auto-managed English device card");
    requireTelemetrySummaryPillsOrdered(
        languageTelemetrySummaryContainer,
        "language overview telemetry capsules stay ordered in English");
    requireTelemetryLabelsHaveNoColon(
        languageTelemetrySummaryContainer,
        "language overview English telemetry labels stay without colons");
    requireTelemetryLabelsFit(
        languageTelemetrySummaryContainer,
        "language overview telemetry labels fit in English");
    requireEpsilonSectionTitleWidths(languageEpsilonPanel, true);
    require(QMetaObject::invokeMethod(&languageOverviewWindow,
                                      "onSwitchLanguage",
                                      Qt::DirectConnection),
            "language overview window switches back to Chinese");
    const bool returnedToChineseWidth = processEventsUntil(1000, [&languageOverviewWindow,
                                                                  languageHomeOverviewSplitter,
                                                                  languageDeviceOverviewCard,
                                                                  inflatedEnglishDeviceWidth]() {
                const QList<int> sizes = languageHomeOverviewSplitter->sizes();
                return sizes.size() == 2 &&
                       sizes.at(0) < inflatedEnglishDeviceWidth &&
                       std::abs(sizes.at(0) - languageDeviceOverviewCard->minimumWidth()) <= 1;
            });
    if (!returnedToChineseWidth)
    {
        const QList<int> sizes = languageHomeOverviewSplitter->sizes();
        std::cerr << "Language overview splitter width after Chinese: initial="
                  << initialDeviceOverviewWidth
                  << " current=" << (sizes.size() == 2 ? sizes.at(0) : -1)
                  << " english=" << englishDeviceOverviewWidth
                  << " card min=" << languageDeviceOverviewCard->minimumWidth()
                  << '\n';
    }
    require(returnedToChineseWidth,
            "language overview device card returns to the Chinese minimum width after language toggles");
    processEventsFor(50);
    activateLayouts(languageDeviceOverviewCard);
    requireTelemetrySummaryPillsOrdered(
        languageTelemetrySummaryContainer,
        "language overview telemetry capsules stay ordered after returning to Chinese");
    requireTelemetryLabelsHaveNoColon(
        languageTelemetrySummaryContainer,
        "language overview Chinese waveform telemetry labels stay without ASCII colons after language toggles");
    requireTelemetryLabelsFit(
        languageTelemetrySummaryContainer,
        "language overview telemetry labels fit after returning to Chinese");
    requireTelemetryPillWidthSnapshotNotWider(
        initialTelemetryPillWidths,
        telemetryPillWidthSnapshot(languageTelemetrySummaryContainer),
        "language overview telemetry capsule widths do not exceed their initial Chinese widths");
    requireEpsilonSectionTitleWidths(languageEpsilonPanel, false);
    requireEpsilonSectionCardsUseWrappedMotionRow(
        languageEpsilonPanel,
        "language overview EPSILON cards return to the wrapped motion row after switching back to Chinese");
    languageOverviewWindow.close();
    require(processEventsUntil(1000, [&languageOverviewWindow]() {
                return !languageOverviewWindow.isVisible();
            }),
            "dedicated language overview test window closes cleanly");

    MainWindow wideOverviewWindow;
    wideOverviewWindow.setWindowTitle(QStringLiteral("VaporView"));
    wideOverviewWindow.resize(1920, 1000);
    wideOverviewWindow.show();
    require(waitForWindowExposed(&wideOverviewWindow),
            "wide overview window becomes exposed");
    require(processEventsUntil(1000, [&wideOverviewWindow]() {
                activateLayouts(&wideOverviewWindow);
                return wideOverviewWindow.size() == QSize(1920, 1000);
            }),
            "wide overview window settles at the maximized-style size");
    auto *wideEpsilonPanel =
        wideOverviewWindow.findChild<QWidget *>(QStringLiteral("epsilonPanel"));
    require(wideEpsilonPanel != nullptr,
            "wide overview exposes the EPSILON panel");
    QGroupBox *wideEpsilonCard = nullptr;
    for (QWidget *ancestor = wideEpsilonPanel; ancestor && !wideEpsilonCard;
         ancestor = ancestor->parentWidget())
    {
        auto *group = qobject_cast<QGroupBox *>(ancestor);
        if (group && group->objectName() == QStringLiteral("sensorGroupBox"))
        {
            wideEpsilonCard = group;
        }
    }
    QGroupBox *wideEnvironmentCard = nullptr;
    if (auto *wideDataGroup =
            wideOverviewWindow.findChild<QGroupBox *>(QStringLiteral("sensorRowContainer")))
    {
        for (QGroupBox *card :
             wideDataGroup->findChildren<QGroupBox *>(QStringLiteral("sensorGroupBox")))
        {
            if (card != wideEpsilonCard &&
                card->findChildren<QLabel *>(QStringLiteral("envStatusIcon")).size() == 3)
            {
                wideEnvironmentCard = card;
                break;
            }
        }
    }
    auto wideWidgetRect = [&wideOverviewWindow](QWidget *widget) {
        return QRect(widget->mapTo(wideOverviewWindow.centralWidget(), QPoint(0, 0)),
                     widget->size());
    };
    auto wideRightEdge = [](const QRect& rect) {
        return rect.left() + rect.width();
    };
    require(wideEpsilonCard != nullptr && wideEnvironmentCard != nullptr,
            "wide overview exposes EPSILON and environment top-level cards");
    const QRect wideEpsilonCardRect = wideWidgetRect(wideEpsilonCard);
    const QRect wideEnvironmentCardRect = wideWidgetRect(wideEnvironmentCard);
    require(std::abs(wideEpsilonCardRect.top() - wideEnvironmentCardRect.top()) <= 1,
            "wide overview keeps the environment card on the EPSILON row");
    require(std::abs((wideEnvironmentCardRect.left() -
                      wideRightEdge(wideEpsilonCardRect)) -
                     VaporView::Ground::MainSupport::kTopLevelCardGap) <= 1,
            "wide overview places the environment card to the right of EPSILON with the shared gap");
    QLabel *wideAttitudeConsistencyValue = epsilonValueLabelForField(
        wideEpsilonPanel,
        QStringLiteral("姿态一致性[最大差值]:"));
    require(wideAttitudeConsistencyValue != nullptr,
            "wide overview exposes the EPSILON attitude consistency value label");
    wideAttitudeConsistencyValue->setText(
        QStringLiteral("最大 0.066°（41-63 0.037°，41-64 0.030°，63-64 0.066°）"));
    wideAttitudeConsistencyValue->setToolTip(wideAttitudeConsistencyValue->text());
    wideAttitudeConsistencyValue->updateGeometry();
    activateLayouts(&wideOverviewWindow);
    processEventsFor(50);
    activateLayouts(&wideOverviewWindow);
    requireEpsilonSectionCardsStayInsidePanel(
        wideEpsilonPanel,
        "wide overview EPSILON section cards stay inside the EPSILON panel");
    requireEpsilonSectionCardsFillWrappedTopRow(
        wideEpsilonPanel,
        "wide overview EPSILON first-row cards fill the wrapped row without a center gap");
    requireEpsilonSectionCardsUseWrappedMotionRow(
        wideEpsilonPanel,
        "wide overview wraps motion details instead of overflowing into the environment card");
    const QRect wideAttitudeConsistencyBounds = wrappedTextBounds(wideAttitudeConsistencyValue);
    require(wideAttitudeConsistencyBounds.height() <= wideAttitudeConsistencyValue->height() + 2,
            "wide overview shows the full EPSILON attitude consistency value without clipping");
    requireLabelTextSlack(wideAttitudeConsistencyValue, 8,
                          "wide overview EPSILON longest value keeps a small readable slack");
    wideOverviewWindow.close();
    require(processEventsUntil(1000, [&wideOverviewWindow]() {
                return !wideOverviewWindow.isVisible();
            }),
            "wide overview test window closes cleanly");
}

void requireHomeEnvironmentCardLayout(MainWindow& window, bool requireSideBySide)
{
    activateLayouts(&window);
    processEventsFor(500);
    activateLayouts(&window);

    auto *dataGroup = window.findChild<QGroupBox *>(QStringLiteral("sensorRowContainer"));
    require(dataGroup != nullptr, "sensor row container exists");

    QGroupBox *epsilonGroup = nullptr;
    QGroupBox *environmentGroup = nullptr;
    const QList<QGroupBox*> sensorGroups =
        dataGroup->findChildren<QGroupBox *>(QStringLiteral("sensorGroupBox"));
    for (QGroupBox *group : sensorGroups)
    {
        if (!group)
        {
            continue;
        }
        if (group->findChildren<QLabel *>(QStringLiteral("envStatusIcon")).size() == 3)
        {
            environmentGroup = group;
        }
        else if (group->findChild<QWidget *>(QStringLiteral("epsilonPanel")))
        {
            epsilonGroup = group;
        }
    }
    require(epsilonGroup != nullptr, "EPSILON card exists");
    require(environmentGroup != nullptr, "environment and lidar card exists");

    const QRect epsilonGeometry = epsilonGroup->geometry();
    const QRect environmentGeometry = environmentGroup->geometry();
    const int sideBySideRequiredWidth =
        epsilonGroup->minimumSizeHint().width() +
        VaporView::Ground::MainSupport::kTopLevelCardGap +
        environmentGroup->minimumSizeHint().width();
    const bool sideBySideFits = dataGroup->contentsRect().width() >= sideBySideRequiredWidth;
    const bool sideBySide =
        std::abs(environmentGeometry.top() - epsilonGeometry.top()) <= 1 &&
        environmentGeometry.left() > epsilonGeometry.right();
    const bool stacked =
        std::abs(environmentGeometry.left() - epsilonGeometry.left()) <= 4 &&
        environmentGeometry.top() > epsilonGeometry.bottom();
    require((sideBySideFits || requireSideBySide) ? sideBySide : (sideBySide || stacked),
            "home data cards either place environment to the right when width permits, or stack cleanly when narrow");
    require(environmentGeometry.right() <= dataGroup->contentsRect().right() + 1,
            "default environment and lidar card stays inside the home data card edge");

    auto labelContains = [](QWidget *panel, const QString& objectName, const QString& token) -> QLabel * {
        if (!panel)
        {
            return nullptr;
        }
        const QList<QLabel*> labels = panel->findChildren<QLabel *>(objectName);
        for (QLabel *label : labels)
        {
            if (label && label->text().contains(token))
            {
                return label;
            }
        }
        return nullptr;
    };
    auto labelCenterY = [](QWidget *container, const QLabel *label) {
        return label->mapTo(container, QPoint(0, label->height() / 2)).y();
    };
    auto labelRectInside = [](QWidget *container, const QLabel *label) {
        return QRect(label->mapTo(container, QPoint(0, 0)), label->size());
    };

    auto *hmpPanel = environmentGroup->findChild<HmpPanel *>();
    require(hmpPanel != nullptr, "environment card contains the temperature/humidity panel");
    QLabel *humidityValueLabel = labelContains(hmpPanel, QStringLiteral("highlightedValue"), QStringLiteral("%RH"));
    const QList<QLabel*> hmpRateLabels = hmpPanel->findChildren<QLabel *>(QStringLiteral("rateLabel"));
    require(humidityValueLabel != nullptr && hmpRateLabels.size() >= 2,
            "humidity row has a dedicated polling-rate label");
    const int humidityCenterY = labelCenterY(hmpPanel, humidityValueLabel);
    bool humidityRateVisibleOnRow = false;
    for (const QLabel *rateLabel : hmpRateLabels)
    {
        const QRect rateRect = labelRectInside(hmpPanel, rateLabel);
        require(rateLabel->text().contains(QStringLiteral("Hz")) &&
                    rateRect.right() <= hmpPanel->contentsRect().right() + 1,
                "HMP polling-rate labels are visible inside the panel");
        humidityRateVisibleOnRow =
            humidityRateVisibleOnRow ||
            std::abs(labelCenterY(hmpPanel, rateLabel) - humidityCenterY) <= 2;
    }
    require(humidityRateVisibleOnRow,
            "humidity row shows its polling-rate label on the same row");

    auto *lidarPanel = environmentGroup->findChild<LidarPanel *>();
    require(lidarPanel != nullptr, "environment card contains the lidar panel");
    QLabel *distanceValueLabel = labelContains(lidarPanel, QStringLiteral("highlightedValue"), QStringLiteral("m"));
    QLabel *strengthValueLabel = nullptr;
    const QList<QLabel*> lidarValueLabels =
        lidarPanel->findChildren<QLabel *>(QStringLiteral("highlightedValue"));
    for (QLabel *label : lidarValueLabels)
    {
        if (label && label != distanceValueLabel)
        {
            strengthValueLabel = label;
            break;
        }
    }
    const QList<QLabel*> lidarRateLabels = lidarPanel->findChildren<QLabel *>(QStringLiteral("rateLabel"));
    require(distanceValueLabel != nullptr && strengthValueLabel != nullptr && !lidarRateLabels.isEmpty(),
            "lidar panel exposes distance, strength, and rate labels");
    const QLabel *lidarRateLabel = lidarRateLabels.first();
    require(std::abs(labelCenterY(lidarPanel, distanceValueLabel) -
                     labelCenterY(lidarPanel, strengthValueLabel)) <= 2 &&
                std::abs(labelCenterY(lidarPanel, distanceValueLabel) -
                         labelCenterY(lidarPanel, lidarRateLabel)) <= 2,
            "default home environment card keeps distance, strength, and rate on one row");
    const std::array<const QLabel *, 3> lidarRowLabels = {
        distanceValueLabel,
        strengthValueLabel,
        lidarRateLabel
    };
    for (const QLabel *label : lidarRowLabels)
    {
        const QRect rect = labelRectInside(lidarPanel, label);
        require(rect.right() <= lidarPanel->contentsRect().right() + 1,
                "lidar row labels stay visible inside the panel");
    }
}

}  // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewLayoutTest"));
    app.setApplicationName(QStringLiteral("main_window_layout_test"));

    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        settings.setValue(QStringLiteral("app_sidebar_width"), 56);
        settings.setValue(QStringLiteral("font_scale_percent"), 100);
        settings.setValue(QStringLiteral("serial/temperature_port"), QStringLiteral(""));
        QSettings(QStringLiteral("VaporView"), QStringLiteral("SerialPortHistory"))
            .setValue(QStringLiteral("ports"), QStringList{QStringLiteral("COM123")});
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

    auto triggerFontScaleAction = [](MainWindow& target, int percent) {
        for (QAction *action : target.findChildren<QAction *>())
        {
            if (action && action->data().toInt() == percent)
            {
                action->trigger();
                processEventsFor(50);
                return;
            }
        }
        require(false, "main window exposes requested font-scale action");
    };

    if (app.arguments().contains(QStringLiteral("--home-overview-language-only")))
    {
        requireHomeOverviewLanguageWidthRoundTrip();
        std::cout << "home_overview_language_layout_test passed\n";
        return 0;
    }

    if (app.arguments().contains(QStringLiteral("--home-environment-layout-only")))
    {
        MainWindow environmentWindow;
        environmentWindow.setWindowTitle(QStringLiteral("VaporView"));
        environmentWindow.resize(2000, 1250);
        environmentWindow.show();
        require(waitForWindowExposed(&environmentWindow),
                "home-environment test window becomes exposed");
        requireHomeEnvironmentCardLayout(environmentWindow, true);
        std::cout << "home_environment_layout_test passed\n";
        return 0;
    }

    if (app.arguments().contains(QStringLiteral("--combination-navigation-only")))
    {
        MainWindow combinationWindow;
        combinationWindow.setWindowTitle(QStringLiteral("VaporView"));
        combinationWindow.resize(1280, 800);
        combinationWindow.show();
        require(waitForWindowExposed(&combinationWindow),
                "combination-navigation test window becomes exposed");
        activateLayouts(&combinationWindow);
        processEventsFor(500);
        activateLayouts(&combinationWindow);
        auto *customTitleLabel =
            combinationWindow.findChild<QLabel *>(QStringLiteral("customTitleLabel"));
        require(customTitleLabel != nullptr,
                "combination-navigation test window exposes its title label");
        requireRtkSidebarPage(combinationWindow, customTitleLabel, false);
        auto *embeddedRtk = combinationWindow.findChild<RtkConfigDialog *>();
        auto *rtkScrollArea = embeddedRtk
            ? embeddedRtk->findChild<QScrollArea *>(QStringLiteral("rtkConfigScrollArea"))
            : nullptr;
        auto *rtkNtripCard = embeddedRtk
            ? findCardByTitle(embeddedRtk,
                              {QStringLiteral("NTRIP 服务器配置"),
                               QStringLiteral("NTRIP Server Configuration")})
            : nullptr;
        auto *rtkGgaCard = embeddedRtk
            ? findCardByTitle(embeddedRtk,
                              {QStringLiteral("GGA 监视"), QStringLiteral("GGA Monitor")})
            : nullptr;
        auto *rtkRtcmCard = embeddedRtk
            ? findCardByTitle(embeddedRtk,
                              {QStringLiteral("RTCM 输出配置"),
                               QStringLiteral("RTCM Output Configuration")})
            : nullptr;
        auto *rtkLogCard = embeddedRtk
            ? findCardByTitle(embeddedRtk,
                              {QStringLiteral("RTK 服务日志"),
                               QStringLiteral("RTK Service Log")})
            : nullptr;
        require(embeddedRtk && rtkScrollArea && rtkNtripCard && rtkGgaCard && rtkRtcmCard && rtkLogCard,
                "combination-navigation test finds the responsive RTK card grid");
        combinationWindow.resize(1024, 800);
        processEventsFor(200);
        activateLayouts(embeddedRtk);
        processEventsFor(100);
        require(rtkScrollArea->horizontalScrollBar()->maximum() == 0,
                "RTK embedded page remains free of horizontal scrolling at 1024px");
        const QRect narrowViewport = rtkScrollArea->viewport()->rect();
        const QRect narrowGgaRect(rtkGgaCard->mapTo(rtkScrollArea->viewport(), QPoint(0, 0)),
                                  rtkGgaCard->size());
        const QRect narrowLogRect(rtkLogCard->mapTo(rtkScrollArea->viewport(), QPoint(0, 0)),
                                  rtkLogCard->size());
        require(narrowGgaRect.right() < narrowViewport.right() + 1 &&
                    narrowLogRect.right() < narrowViewport.right() + 1 &&
                    std::abs(rtkNtripCard->geometry().top() - rtkGgaCard->geometry().top()) <= 2 &&
                    std::abs(rtkRtcmCard->geometry().top() - rtkLogCard->geometry().top()) <= 2,
                "RTK cards stay in two compact rows without narrow-window clipping");
        combinationWindow.resize(1280, 800);
        processEventsFor(150);
        activateLayouts(embeddedRtk);
        combinationWindow.close();
        require(processEventsUntil(1000, [&combinationWindow]() {
                    return !combinationWindow.isVisible();
                }),
                "combination-navigation test window closes cleanly");
        std::cout << "combination_navigation_page_test passed\n";
        return 0;
    }

    {
        MainWindow rememberedModeWindow;
        rememberedModeWindow.resize(1000, 700);
        rememberedModeWindow.show();
        require(waitForWindowExposed(&rememberedModeWindow),
                "remembered source-mode window becomes exposed");
        QComboBox *rememberedSourceModeCombo = findSourceModeCombo(&rememberedModeWindow);
        require(rememberedSourceModeCombo != nullptr,
                "remembered source mode combo exists on startup");
        require(rememberedSourceModeCombo->itemText(1) == QStringLiteral("远程") ||
                    rememberedSourceModeCombo->itemText(1) == QStringLiteral("Remote"),
                "source mode combo uses the new remote-mode label");
        require(rememberedSourceModeCombo->property("usesSingleLevelPopupMenu").toBool(),
                "source mode combo uses the shared single-level popup");
        require(rememberedSourceModeCombo->currentIndex() == 1,
                "source mode restores the last remote selection on startup");
        auto *rememberedSourceModeSwitch =
            rememberedModeWindow.findChild<QPushButton *>(QStringLiteral("sourceModeOverviewSwitch"));
        require(rememberedSourceModeSwitch != nullptr &&
                    rememberedSourceModeSwitch->property("segmentedSwitchControl").toBool() &&
                    rememberedSourceModeSwitch->focusPolicy() == Qt::TabFocus &&
                    !rememberedSourceModeSwitch->property("keyboardFocusIndicatorVisible").toBool(),
                "source mode uses the shared segmented switch without an automatic focus ring");
        const QPoint repeatedSourceModeClick(rememberedSourceModeSwitch->width() / 4,
                                             rememberedSourceModeSwitch->height() / 2);
        for (int expectedIndex : {0, 1, 0, 1})
        {
            clickWidgetAt(rememberedSourceModeSwitch, repeatedSourceModeClick, 0);
            processEventsFor(20);
            require(rememberedSourceModeCombo->currentIndex() == expectedIndex,
                    "repeated source-mode clicks at one position toggle every time");
        }
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
        auto *rememberedTemperaturePort =
            rememberedModeWindow.findChild<QComboBox *>(QStringLiteral("temperaturePortCombo"));
        const QString staleHistoryPortText = QStringLiteral("COM123");
        require(rememberedTemperaturePort != nullptr &&
                    rememberedTemperaturePort->findText(staleHistoryPortText) < 0 &&
                    rememberedTemperaturePort->currentText() == QStringLiteral("未选择") &&
                    localSerialPortValue(rememberedTemperaturePort).isEmpty(),
                "an unselected serial port displays the Chinese placeholder and does not restore history");
        auto *rememberedTemperatureTitlePort =
            rememberedModeWindow.findChild<QComboBox *>(QStringLiteral("temperatureTitlePortCombo"));
        require(rememberedTemperatureTitlePort != nullptr &&
                    rememberedTemperatureTitlePort->currentData().toString().isEmpty(),
                "the RD105 title selector stays unselected when no serial port is saved");
        rememberedModeWindow.close();
        require(processEventsUntil(1000, [&rememberedModeWindow]() {
                    return !rememberedModeWindow.isVisible();
                }),
                "remembered source-mode window closes cleanly");
    }

    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        settings.setValue(QStringLiteral("source/mode"), QStringLiteral("local"));
        settings.setValue(QStringLiteral("sensor/pressure_source"), QStringLiteral("ptb210"));
        settings.setValue(QStringLiteral("serial/ptb_baud"), QStringLiteral("9600"));
        settings.setValue(QStringLiteral("sensor/humidity_source"), QStringLiteral("hmp3"));
        settings.setValue(QStringLiteral("serial/hmp_baud"), QStringLiteral("19200"));
#ifdef Q_OS_WIN
        settings.setValue(QStringLiteral("serial/temperature_port"), QStringLiteral("COM9"));
        QSettings(QStringLiteral("VaporView"), QStringLiteral("SerialPortHistory"))
            .setValue(QStringLiteral("ports"), QStringList{QStringLiteral("COM9")});
#else
        settings.setValue(QStringLiteral("serial/temperature_port"), QStringLiteral("/dev/ttyRD105"));
        QSettings(QStringLiteral("VaporView"), QStringLiteral("SerialPortHistory"))
            .setValue(QStringLiteral("ports"), QStringList{QStringLiteral("/dev/ttyRD105")});
#endif
        settings.remove(QStringLiteral("serial/ptb210_baud"));
        settings.remove(QStringLiteral("serial/bmp390_baud"));
        settings.remove(QStringLiteral("serial/hmp3_baud"));
        settings.remove(QStringLiteral("serial/sht45_baud"));
        settings.sync();

        QSettings applicationSettings = VaporView::applicationConfigSettings();
        applicationSettings.beginGroup(QStringLiteral("MainWindow"));
        applicationSettings.setValue(QStringLiteral("sensor/pressure_source"), QStringLiteral("ptb210"));
        applicationSettings.setValue(QStringLiteral("serial/ptb_baud"), QStringLiteral("9600"));
        applicationSettings.setValue(QStringLiteral("sensor/humidity_source"), QStringLiteral("hmp3"));
        applicationSettings.setValue(QStringLiteral("serial/hmp_baud"), QStringLiteral("19200"));
        applicationSettings.setValue(QStringLiteral("local_enabled/epsilon"), true);
        applicationSettings.setValue(QStringLiteral("local_enabled/ptb"), true);
        applicationSettings.setValue(QStringLiteral("local_enabled/hmp"), true);
        applicationSettings.setValue(QStringLiteral("local_enabled/lidar"), true);
        applicationSettings.setValue(QStringLiteral("local_enabled/temperature"), true);
        applicationSettings.setValue(QStringLiteral("local_enabled/ai8_temperature"), true);
        applicationSettings.remove(QStringLiteral("serial/ptb210_baud"));
        applicationSettings.remove(QStringLiteral("serial/bmp390_baud"));
        applicationSettings.remove(QStringLiteral("serial/hmp3_baud"));
        applicationSettings.remove(QStringLiteral("serial/sht45_baud"));
        applicationSettings.endGroup();
        applicationSettings.sync();
    }

    {
        MainWindow aboutWindow;
        aboutWindow.setWindowTitle(QStringLiteral("VaporView"));
        aboutWindow.resize(900, 650);
        aboutWindow.show();
        require(waitForWindowExposed(&aboutWindow),
                "dedicated window becomes exposed for about dialog testing");

        QAction *aboutAction = findAboutAction(&aboutWindow);
        require(aboutAction != nullptr, "help menu exposes the about action");
        QAction *checkUpdatesAction = findCheckUpdatesAction(&aboutWindow);
        require(checkUpdatesAction != nullptr, "help menu exposes the check updates action");
        require(checkUpdatesAction->text() == QStringLiteral("检查更新"),
                "check updates action defaults to Chinese");
        require(checkUpdatesAction->toolTip() == QStringLiteral("检查 VaporView 更新"),
                "check updates action has a Chinese tooltip");
        const QString originalApplicationVersion = app.applicationVersion();
        app.setApplicationVersion(QStringLiteral("9.8.7-test"));
        requireAboutDialogLayout(&aboutWindow, aboutAction, false, QStringLiteral("9.8.7-test"));
        require(QMetaObject::invokeMethod(&aboutWindow, "onSwitchLanguage", Qt::DirectConnection),
                "main window switches to English for about dialog coverage");
        require(processEventsUntil(1000, [aboutAction]() {
                    return aboutAction->text() == QStringLiteral("&About");
                }),
                "about action updates to English");
        require(checkUpdatesAction->text() == QStringLiteral("Check for Updates") &&
                    checkUpdatesAction->toolTip() == QStringLiteral("Check for VaporView updates"),
                "check updates action updates to English");
        app.setApplicationVersion(QString());
        requireAboutDialogLayout(&aboutWindow, aboutAction, true, QStringLiteral("1.0.23"));
        require(QMetaObject::invokeMethod(&aboutWindow, "onSwitchLanguage", Qt::DirectConnection),
                "main window switches back to Chinese after about dialog coverage");
        require(processEventsUntil(1000, [aboutAction]() {
                    return aboutAction->text() == QStringLiteral("关于(&A)");
                }),
                "about action returns to Chinese");
        require(checkUpdatesAction->text() == QStringLiteral("检查更新") &&
                    checkUpdatesAction->toolTip() == QStringLiteral("检查 VaporView 更新"),
                "check updates action returns to Chinese");
        app.setApplicationVersion(originalApplicationVersion);
        aboutWindow.close();
        require(processEventsUntil(1000, [&aboutWindow]() {
                    return !aboutWindow.isVisible();
                }),
                "dedicated about dialog test window closes cleanly");
    }

    MainWindow window;
    triggerFontScaleAction(window, 100);
    window.setWindowTitle(QStringLiteral("VaporView"));
    window.resize(1280, 800);
    window.show();
    require(waitForWindowExposed(&window), "main window becomes exposed for layout testing");
    requireMainWindowOuterBorder(window, false,
                                 "light theme main window keeps black left, right and bottom outer borders");
#ifdef VAPORVIEW_HAS_OSGEARTH
    requireMainWindowMap3DEntries(window);
#else
    requireMainWindowOmitsMap3DEntries(window);
#endif
    require(qApp->styleSheet().contains(QStringLiteral("square.svg")) &&
                qApp->styleSheet().contains(QStringLiteral("square-check-big.svg")) &&
                !qApp->styleSheet().contains(QStringLiteral("lucide/check.svg")),
            "checkbox indicators use lucide square and square-check-big icons");
    require(qApp->styleSheet().contains(QStringLiteral("chevron-up.svg")) &&
                qApp->styleSheet().contains(QStringLiteral("chevron-down.svg")) &&
                qApp->styleSheet().contains(QStringLiteral("chevron-up-primary.svg")) &&
                qApp->styleSheet().contains(QStringLiteral("chevron-down-primary.svg")) &&
                qApp->styleSheet().contains(QStringLiteral("QAbstractSpinBox::up-arrow")) &&
                qApp->styleSheet().contains(QStringLiteral("QAbstractSpinBox[spinArrowHover=\"up\"]::up-arrow")) &&
                qApp->styleSheet().contains(QStringLiteral("QComboBox::down-arrow")) &&
                qApp->styleSheet().contains(QStringLiteral("background-color: transparent")),
            "spin arrows use enlarged primary lucide hover icons without button backgrounds");
    const QString lightFieldBackground =
        VaporView::appThemeColorName(VaporView::AppThemeColor::FieldBackground, false);
    require(VaporView::appThemeColor(VaporView::AppThemeColor::FieldBackground, false) ==
                QColor(QStringLiteral("#FFFFFF")),
            "light theme field background is pure white");
    require(qApp->styleSheet().contains(
                QStringLiteral("QComboBox {\n    background-color: %1").arg(lightFieldBackground)) &&
                qApp->styleSheet().contains(
                    QStringLiteral("QLineEdit {\n    background-color: %1").arg(lightFieldBackground)) &&
                qApp->styleSheet().contains(
                    QStringLiteral("QAbstractSpinBox {\n    background-color: %1").arg(lightFieldBackground)) &&
                qApp->styleSheet().contains(
                    QStringLiteral("QPlainTextEdit,\nQTextEdit {\n    background-color: %1")
                        .arg(lightFieldBackground)),
            "light theme gives combo, line, spin/date and multiline fields a pure white background");
    requireSpinArrowHoverUsesPrimary(false,
                                     "light theme spin arrow hover renders the primary lucide icon");
    requireMenuPopupStyleUnified(qApp->styleSheet(),
                                 false,
                                 "light popup menus use the shared menu hover and rounded panel style");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QSplitter#mainContentSplitter::handle:horizontal {"),
                                 QStringLiteral("background-color: transparent"),
                                 "main content splitter handle is invisible until hovered");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QSplitter#mainContentSplitter::handle:horizontal {"),
                                 QStringLiteral("width: 0px"),
                                 "main content splitter does not add a visual card gutter");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QSplitter#mainContentSplitter::handle:horizontal:hover {"),
                                 VaporView::appThemeRgba(VaporView::AppThemeColor::Primary, false, 0.18),
                                 "main content splitter keeps a resize hover cue");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QSplitter#mainContentSplitter::handle:horizontal:pressed {"),
                                 VaporView::appThemeRgba(VaporView::AppThemeColor::Primary, false, 0.28),
                                 "main content splitter keeps a resize pressed cue");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QSplitter#homeOverviewSplitter::handle:horizontal {"),
                                 QStringLiteral("width: 12px"),
                                 "home overview splitter keeps the requested 12px card gap");
    const QString homeOverviewSplitterSurface =
        QStringLiteral("background-color: ") +
        VaporView::appThemeColorName(VaporView::AppThemeColor::Surface, false);
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QSplitter#homeOverviewSplitter::handle:horizontal:hover {"),
                                 homeOverviewSplitterSurface,
                                 "home overview splitter keeps its surface color while hovered");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QSplitter#homeOverviewSplitter::handle:horizontal:pressed {"),
                                 homeOverviewSplitterSurface,
                                 "home overview splitter keeps its surface color while pressed");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QScrollArea#mainCardsScrollArea QScrollBar:vertical {"),
        QStringLiteral("background-color: ") +
            VaporView::appThemeColorName(VaporView::AppThemeColor::Surface, false),
        "main card scrollbar track uses an opaque surface plane");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QScrollArea#mainCardsScrollArea QScrollBar:vertical {"),
        QStringLiteral("width: 8px"),
        "main card scrollbar track is an actual 8px rail, not a 12px rail with inset paint");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QScrollArea#mainCardsScrollArea QScrollBar:vertical {"),
        QStringLiteral("border-radius: 4px"),
        "main card scrollbar track keeps proportional rounded ends at 8px width");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QScrollArea#mainCardsScrollArea QScrollBar:vertical {"),
        QStringLiteral("margin: 14px 0px 14px 0px"),
        "main card scrollbar track keeps the shared arrow-button margins");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QScrollArea#mainCardsScrollArea QScrollBar::add-line:vertical, "),
        QStringLiteral("height: 14px"),
        "main card scrollbar line buttons remain visible for arrow indicators");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QScrollBar::up-arrow:vertical {"),
        QStringLiteral("image: url("),
        "main card scrollbar keeps the shared up-arrow indicator");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QScrollBar::up-arrow:vertical {"),
        QStringLiteral("width: 8px"),
        "main card scrollbar arrow artwork matches the 8px rail width");
    require(!qApp->styleSheet().contains(
                QStringLiteral("QScrollArea#mainCardsScrollArea QScrollBar::up-arrow:vertical")),
            "main card scrollbar does not hide the shared arrow indicators");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QScrollArea#mainCardsScrollArea QScrollBar::handle:vertical, "),
        QStringLiteral("border: none"),
        "main card scrollbar handle has no border paint");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QScrollArea#mainCardsScrollArea QScrollBar::handle:vertical, "),
        QStringLiteral("border-radius: 4px"),
        "main card scrollbar handle keeps rounded ends at the 8px track width");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QScrollArea#mainCardsScrollArea QScrollBar::handle:vertical, "),
        QStringLiteral("margin: 0px"),
        "main card scrollbar handle fills the 8px rail without using margins as fake gutters");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QDialog#rtkConfigDialog QGroupBox#sensorGroupBox[vaporViewTopLevelCard=\"true\"] {"),
        QStringLiteral("border-radius: 12px"),
        "RTK embedded cards use the same 12px top-level radius as their shadow clip");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QDialog#rtkConfigDialog QGroupBox#sensorGroupBox[vaporViewTopLevelCard=\"true\"] > QWidget#sectionTitleBar {"),
        QStringLiteral("border-top-left-radius: 11px"),
        "RTK embedded card title bars do not expose black corner arcs");
    requireComboPopupsStyledIn(&window,
                               "all main-window combo boxes use the shared rounded popup menu style");
    const QSize originalWindowSize = window.size();

    auto *appLayoutSplitter = window.findChild<QSplitter *>(QStringLiteral("appLayoutSplitter"));
    require(appLayoutSplitter != nullptr, "app layout splitter exists");
    auto *mainContentSplitter =
        window.findChild<QSplitter *>(QStringLiteral("mainContentSplitter"));
    require(mainContentSplitter != nullptr, "main content splitter exists");
    require(mainContentSplitter->count() == 2 &&
                mainContentSplitter->isCollapsible(0) &&
                mainContentSplitter->isCollapsible(1),
            "main content splitter initializes both collapsible child panes");
    require(appLayoutSplitter->handleWidth() ==
                VaporView::Ground::MainSupport::kSidePanelSplitterVisualWidth &&
                mainContentSplitter->handleWidth() ==
                    VaporView::Ground::MainSupport::kSidePanelSplitterVisualWidth,
            "side-panel splitters do not contribute extra visible card spacing");
    require(window.centralWidget() != nullptr, "central widget exists");
    require(window.findChild<QStatusBar *>() == nullptr,
            "main window does not create a bottom status bar");
    const QMargins mainContentMargins = window.centralWidget()->layout()->contentsMargins();
    const int mainContentTopGap =
        appLayoutSplitter->geometry().top() - window.centralWidget()->contentsRect().top();
    const int mainContentBottomGap =
        window.centralWidget()->contentsRect().bottom() - appLayoutSplitter->geometry().bottom();
    require(mainContentMargins.top() > 0 &&
                mainContentMargins.bottom() == mainContentMargins.top() &&
                std::abs(mainContentTopGap - mainContentMargins.top()) <= 1 &&
                std::abs(mainContentBottomGap - mainContentMargins.bottom()) <= 1,
            "main cards stay inside matching top and bottom safe margins");
    auto *mainPageStackForScroll = window.findChild<QStackedWidget *>(QStringLiteral("mainPageStack"));
    require(mainPageStackForScroll != nullptr, "main page stack exists for home scroll check");
    auto *homeScrollArea = qobject_cast<QScrollArea *>(mainPageStackForScroll->currentWidget());
    require(homeScrollArea != nullptr, "home scroll area exists");
    require(homeScrollArea->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOn,
            "home scroll area keeps a stable vertical gutter to prevent card edge flicker");
    QWidget *homeScrollShadowLayer = homeScrollArea->findChild<QWidget *>(
        QString::fromLatin1(VaporView::kTopLevelCardShadowLayerName),
        Qt::FindDirectChildrenOnly);
    require(homeScrollShadowLayer != nullptr &&
                homeScrollShadowLayer->parentWidget() == homeScrollArea,
            "home scroll card shadows are hosted by the scroll area, not clipped by the viewport");
    const QList<QScrollArea*> mainContentScrollAreas =
        mainPageStackForScroll->findChildren<QScrollArea *>(QStringLiteral("mainCardsScrollArea"));
    require(mainContentScrollAreas.size() >= 3,
            "main pages expose their scroll areas for bottom-fade coverage");
    for (QScrollArea *scrollArea : mainContentScrollAreas)
    {
        auto *bottomFade = scrollArea->viewport()->findChild<QWidget *>(
            QStringLiteral("mainContentBottomFade"), Qt::FindDirectChildrenOnly);
        require(bottomFade != nullptr,
                "each main scroll area owns the shared bottom fade");
        const Qt::ScrollBarPolicy expectedVerticalScrollBarPolicy =
            scrollArea == homeScrollArea ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAsNeeded;
        require(scrollArea->verticalScrollBarPolicy() == expectedVerticalScrollBarPolicy,
                "only the scrollable home page keeps a permanently visible scrollbar rail");
        require(bottomFade->testAttribute(Qt::WA_TransparentForMouseEvents) &&
                    bottomFade->focusPolicy() == Qt::NoFocus,
                "bottom fades do not intercept pointer or keyboard input");
    }
    auto *rtkScrollArea =
        mainPageStackForScroll->findChild<QScrollArea *>(QStringLiteral("rtkConfigScrollArea"));
    require(rtkScrollArea != nullptr &&
                rtkScrollArea->viewport()->findChild<QWidget *>(
                    QStringLiteral("mainContentBottomFade"), Qt::FindDirectChildrenOnly) != nullptr,
            "embedded RTK page also owns the bottom fade");

    auto *homeBottomFade = homeScrollArea->viewport()->findChild<QWidget *>(
        QStringLiteral("mainContentBottomFade"), Qt::FindDirectChildrenOnly);
    require(homeBottomFade != nullptr, "home bottom fade exists");
    constexpr int kExpectedPageLeftInset =
        VaporView::Ground::MainSupport::kMainContentLeftCardInset;
    constexpr int kExpectedScrollBarWidth =
        VaporView::Ground::MainSupport::kMainContentVerticalScrollBarWidth;
    constexpr int kExpectedPageTopInset =
        VaporView::Ground::MainSupport::kTopLevelCardOuterVerticalInset;
    constexpr int kExpectedTopLevelCardGap =
        VaporView::Ground::MainSupport::kTopLevelCardGap;
    constexpr int kExpectedSidebarToMainCardGap =
        VaporView::Ground::MainSupport::kMainContentSidebarCardGap;
    constexpr int kExpectedVisibleOuterGap =
        VaporView::Ground::MainSupport::kTopLevelCardGap;
    constexpr int kExpectedHomeShadowSafeRightInset =
        VaporView::Ground::MainSupport::kTopLevelCardShadowSafeInset;
    constexpr int kExpectedNoScrollPageRightInset =
        kExpectedHomeShadowSafeRightInset + kExpectedScrollBarWidth;
    QWidget *homeScrollContent = homeScrollArea->widget();
    require(homeScrollContent != nullptr, "home scroll content exists for bottom-fade behavior");
    const int originalContentMinimumHeight = homeScrollContent->minimumHeight();
    const int originalScrollValue = homeScrollArea->verticalScrollBar()->value();
    const bool forcedScrollableHeight = homeScrollArea->verticalScrollBar()->maximum() == 0;
    if (forcedScrollableHeight)
    {
        homeScrollContent->setMinimumHeight(
            std::max(originalContentMinimumHeight,
                     homeScrollArea->viewport()->height() + 200));
        require(processEventsUntil(1000, [homeScrollArea]() {
                    return homeScrollArea->verticalScrollBar()->maximum() > 0;
                }),
                "home page can expose scrollable content for bottom-fade behavior");
    }
    homeScrollArea->verticalScrollBar()->setValue(homeScrollArea->verticalScrollBar()->minimum());
    require(processEventsUntil(500, [homeBottomFade]() {
                return homeBottomFade->isVisible();
            }),
            "bottom fade appears while more content remains below");
    require(homeScrollArea->verticalScrollBar()->width() == kExpectedScrollBarWidth,
            "home card scrollbar reserves an actual 8px track");
    require(homeScrollShadowLayer->width() >=
                homeScrollArea->viewport()->width() +
                    homeScrollArea->verticalScrollBar()->width(),
            "home scroll card shadow layer covers the scroll area canvas");
    require(homeScrollShadowLayer
                ->property("vaporViewTopLevelCardShadowUsesOpaqueScrollBarTrack")
                .toBool(),
            "home scroll card shadow layer uses an opaque scrollbar track outside the shadow area");
    require(homeScrollShadowLayer
                ->property("vaporViewTopLevelCardShadowClipsScrollBarTrack")
                .toBool(),
            "home scroll card shadow layer clips the full scrollbar track");
    require(homeBottomFade->geometry().bottom() ==
                    homeScrollArea->viewport()->rect().bottom() &&
                homeBottomFade->width() == homeScrollArea->viewport()->width(),
            "bottom fade stays on the viewport edge instead of following the content shadow inset");
    homeScrollArea->verticalScrollBar()->setValue(homeScrollArea->verticalScrollBar()->maximum());
    require(processEventsUntil(500, [homeBottomFade]() {
                return homeBottomFade->isHidden();
            }),
            "bottom fade disappears at the end of the page");
    if (forcedScrollableHeight)
    {
        homeScrollContent->setMinimumHeight(originalContentMinimumHeight);
    }
    homeScrollArea->verticalScrollBar()->setValue(originalScrollValue);
    processEventsFor(50);
    processEventsFor(250);
    if (QLayout *homeContentLayout = homeScrollContent->layout())
    {
        const QMargins homeContentMargins = homeContentLayout->contentsMargins();
        require(homeContentMargins.right() == kExpectedHomeShadowSafeRightInset,
                "home card right inset keeps the card shadow physically separated from the scrollbar track");
        require(homeContentMargins.bottom() ==
                    VaporView::Ground::MainSupport::kMainContentBottomShadowSafeInset,
                "home card content leaves 8px of physical room below the bottom card shadow");
    }
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
    requireTopLevelCardElevation(appSidebar,
                                 1.0,
                                 "app sidebar uses the shared soft elevation");
    requireSidebarCardStyle(qApp->styleSheet(), false,
                            "light sidebar uses a complete rounded card border");
    auto *recordingStatusCard =
        window.findChild<QFrame *>(QStringLiteral("recordingStatusCard"));
    auto *recordingStatusView =
        window.findChild<QWidget *>(QStringLiteral("recordingStatusView"));
    auto *logPanelFrame =
        window.findChild<QFrame *>(QStringLiteral("logPanelFrame"));
    require(recordingStatusCard != nullptr && recordingStatusView != nullptr &&
                logPanelFrame != nullptr,
            "right-side recording and log cards exist for outer-margin checks");
    const QString recordingStatusDetail = recordingStatusView->toolTip();
    require(recordingStatusDetail.contains(QStringLiteral("RAW EPSILON")) &&
                recordingStatusDetail.contains(QStringLiteral("RAW PTB210")) &&
                recordingStatusDetail.contains(QStringLiteral("RAW HMP3")) &&
                recordingStatusDetail.contains(QStringLiteral("RAW TFA1500")) &&
                recordingStatusDetail.contains(QStringLiteral("RAW TCP")) &&
                recordingStatusDetail.contains(QStringLiteral("RAW RD105")) &&
                recordingStatusDetail.contains(QStringLiteral("RAW AI-8288")),
            "recording status uses hardware model names");
    require(!recordingStatusDetail.contains(QStringLiteral("RAW 激光温控")) &&
                !recordingStatusDetail.contains(QStringLiteral("RAW 系统温控")) &&
                !recordingStatusDetail.contains(QStringLiteral("双路精密温控器")) &&
                !recordingStatusDetail.contains(QStringLiteral("8路温控器")),
            "recording status does not show semantic temperature-controller names");
    auto *recordingStatusBody =
        recordingStatusCard->findChild<QWidget *>(QStringLiteral("recordingStatusBody"));
    require(recordingStatusBody != nullptr &&
                recordingStatusBody->layout() != nullptr,
            "recording status body exists for local-mode padding checks");
    require(recordingStatusBody->layout()->contentsMargins().bottom() == 4,
            "local recording status card uses tighter bottom padding");
    auto recordingStatusBottomGap = [&window, recordingStatusBody, recordingStatusView]() {
        int lastLineBottom = 0;
        const QList<QLabel*> labels = recordingStatusView->findChildren<QLabel *>();
        require(!labels.isEmpty(),
                "local recording status rows are visible for bottom padding checks");
        for (QLabel *label : labels)
        {
            if (!label->isVisibleTo(&window))
            {
                continue;
            }
            const QRect labelRect(label->mapTo(recordingStatusBody, QPoint(0, 0)),
                                  label->size());
            lastLineBottom =
                std::max(lastLineBottom,
                         labelRect.top() + labelRect.height());
        }
        const QRect contents = recordingStatusBody->contentsRect();
        return contents.top() + contents.height() - lastLineBottom;
    };
    const int localRecordingStatusBottomGap = recordingStatusBottomGap();
    require(localRecordingStatusBottomGap >= 4 && localRecordingStatusBottomGap <= 6,
            "local recording status last row stays close to the card bottom");
    requireTopLevelCardElevation(recordingStatusCard,
                                 1.0,
                                 "recording status card uses the shared soft elevation");
    requireTopLevelCardElevation(logPanelFrame,
                                 1.0,
                                 "log card uses the shared soft elevation");
    auto widgetRectInCentral = [&window](QWidget *widget) {
        return QRect(widget->mapTo(window.centralWidget(), QPoint(0, 0)), widget->size());
    };
    auto rightEdge = [](const QRect& rect) {
        return rect.left() + rect.width();
    };
    auto bottomEdge = [](const QRect& rect) {
        return rect.top() + rect.height();
    };
    auto sensorGroupAncestor = [](QWidget *widget) -> QWidget * {
        for (QWidget *ancestor = widget; ancestor != nullptr; ancestor = ancestor->parentWidget())
        {
            if (ancestor->objectName() == QStringLiteral("sensorGroupBox"))
            {
                return ancestor;
            }
        }
        return nullptr;
    };
    const QRect centralRect = window.centralWidget()->contentsRect();
    const QRect sidebarRect = widgetRectInCentral(appSidebar);
    const QRect recordingCardRect = widgetRectInCentral(recordingStatusCard);
    const QRect logCardRect = widgetRectInCentral(logPanelFrame);
    auto *homeOverviewSplitterForPageSpacing =
        window.findChild<QSplitter *>(QStringLiteral("homeOverviewSplitter"));
    require(homeOverviewSplitterForPageSpacing != nullptr &&
                homeOverviewSplitterForPageSpacing->count() >= 1,
            "home overview splitter exists for page spacing baseline");
    auto *homePrimaryCardForPageSpacing =
        qobject_cast<QGroupBox *>(homeOverviewSplitterForPageSpacing->widget(0));
    auto *homeTemperatureCardForPageSpacing =
        qobject_cast<QGroupBox *>(homeOverviewSplitterForPageSpacing->widget(1));
    require(homePrimaryCardForPageSpacing != nullptr,
            "home primary card exists for page spacing baseline");
    require(homeTemperatureCardForPageSpacing != nullptr,
            "home temperature overview card exists for page spacing baseline");
    requireTopLevelCardElevation(homePrimaryCardForPageSpacing,
                                 1.0,
                                 "home device overview uses the shared soft elevation");
    requireTopLevelCardElevation(homeTemperatureCardForPageSpacing,
                                 1.0,
                                 "home temperature overview uses the shared soft elevation");
    const QRect mainPageStackCentralRect = widgetRectInCentral(mainPageStackForScroll);
    const QRect homeOverviewSplitterRect = widgetRectInCentral(homeOverviewSplitterForPageSpacing);
    const QRect homePrimaryCardRect = widgetRectInCentral(homePrimaryCardForPageSpacing);
    const QRect homeTemperatureCardRect = widgetRectInCentral(homeTemperatureCardForPageSpacing);
    const int homePrimaryCardLeft =
        homePrimaryCardRect.left();
    require(std::abs(homePrimaryCardLeft -
                     (mainPageStackCentralRect.left() + kExpectedPageLeftInset)) <= 1,
            "home page keeps the shared 18px sidebar-to-card gap");
    const int homeTopVisualGap =
        homePrimaryCardRect.top() - mainPageStackCentralRect.top();
    if (std::abs(homeTopVisualGap - kExpectedPageTopInset) > 1)
    {
        std::cerr << "Home top gap: actual=" << homeTopVisualGap
                  << " expected=" << kExpectedPageTopInset
                  << " cardTop=" << homePrimaryCardRect.top()
                  << " stackTop=" << mainPageStackCentralRect.top() << '\n';
    }
    require(std::abs(homeTopVisualGap - kExpectedPageTopInset) <= 1,
            "home page keeps the compact inner top inset that balances the app chrome");
    require(std::abs((homePrimaryCardRect.top() - centralRect.top()) -
                     kExpectedVisibleOuterGap) <= 1,
            "home first-row cards sit 12px below the titlebar content edge");
    const int homeOverviewCardGap =
        homeTemperatureCardRect.left() - rightEdge(homePrimaryCardRect);
    require(std::abs(homeOverviewCardGap - kExpectedTopLevelCardGap) <= 1,
            "home overview cards keep the shared shadow-safe horizontal gap");
    const int sidebarLeftGap = sidebarRect.left() - centralRect.left();
    const int sidebarBottomGap = centralRect.bottom() - sidebarRect.bottom();
    const int recordingRightGap = centralRect.right() - recordingCardRect.right();
    const int logRightGap = centralRect.right() - logCardRect.right();
    const QRect homeViewportRect =
        QRect(homeScrollArea->viewport()->mapTo(window.centralWidget(), QPoint(0, 0)),
              homeScrollArea->viewport()->size());
    require(rightEdge(homeTemperatureCardRect) <= rightEdge(homeViewportRect) + 1,
            "home temperature overview card stays fully inside the scroll viewport");
    const int homeLeftVisualGap = homePrimaryCardLeft - rightEdge(sidebarRect);
    const int homeRightVisualGap = recordingCardRect.left() - rightEdge(homeViewportRect);
    const int homeTemperatureToRecordingGap =
        recordingCardRect.left() - rightEdge(homeTemperatureCardRect);
    const int kExpectedRightSideScrollBarToCardGap =
        kExpectedScrollBarWidth + kExpectedHomeShadowSafeRightInset;
    if (!(std::abs(homeLeftVisualGap - kExpectedSidebarToMainCardGap) <= 1 &&
          std::abs(homeRightVisualGap - kExpectedRightSideScrollBarToCardGap) <= 1))
    {
        std::cerr << "Home visual gaps: left=" << homeLeftVisualGap
                  << " right=" << homeRightVisualGap
                  << " sidebarToMainGap=" << kExpectedSidebarToMainCardGap
                  << " scrollBarWidth=" << kExpectedScrollBarWidth
                  << " shadowInset=" << kExpectedHomeShadowSafeRightInset << '\n';
    }
    require(std::abs(homeLeftVisualGap - kExpectedSidebarToMainCardGap) <= 1 &&
                std::abs(homeRightVisualGap - kExpectedRightSideScrollBarToCardGap) <= 1,
            "home cards keep the 18px left sidebar gap and the 8px scrollbar plus 5px right shadow inset");
    require(std::abs(homeTemperatureToRecordingGap -
                     (kExpectedHomeShadowSafeRightInset +
                      kExpectedScrollBarWidth +
                      kExpectedHomeShadowSafeRightInset)) <= 1,
            "home temperature overview and recording status cards keep symmetric shadow-safe spacing around the 8px scrollbar rail");
    auto *homeDataGroupForSpacing =
        window.findChild<QGroupBox *>(QStringLiteral("sensorRowContainer"));
    require(homeDataGroupForSpacing != nullptr, "home sensor row container exists for top-level spacing checks");
    auto *homeEpsilonCardForSpacing =
        homeDataGroupForSpacing->findChild<QGroupBox *>(QStringLiteral("sensorGroupBox"));
    QGroupBox *homeEnvironmentCardForSpacing = nullptr;
    const QList<QGroupBox*> homeSensorCardsForSpacing =
        homeDataGroupForSpacing->findChildren<QGroupBox *>(QStringLiteral("sensorGroupBox"));
    for (QGroupBox *card : homeSensorCardsForSpacing)
    {
        if (card != homeEpsilonCardForSpacing &&
            card->findChildren<QLabel *>(QStringLiteral("envStatusIcon")).size() == 3)
        {
            homeEnvironmentCardForSpacing = card;
            break;
        }
    }
    require(homeEpsilonCardForSpacing != nullptr &&
                homeEnvironmentCardForSpacing != nullptr,
            "home EPSILON and environment top-level cards exist for spacing checks");
    requireTopLevelCardElevation(homeEpsilonCardForSpacing,
                                 1.0,
                                 "home EPSILON card uses the shared soft elevation");
    requireTopLevelCardElevation(homeEnvironmentCardForSpacing,
                                 1.0,
                                 "home environment card uses the shared soft elevation");
    auto *tcpWaveDisplayButtonForSpacing =
        window.findChild<QToolButton *>(QStringLiteral("tcpWaveDisplayButton"));
    QWidget *tcpWaveTitleBarForSpacing =
        tcpWaveDisplayButtonForSpacing ? tcpWaveDisplayButtonForSpacing->parentWidget() : nullptr;
    QWidget *tcpWavePanelForSpacing =
        tcpWaveTitleBarForSpacing ? tcpWaveTitleBarForSpacing->parentWidget() : nullptr;
    QWidget *tcpWaveCardForSpacing =
        tcpWavePanelForSpacing ? tcpWavePanelForSpacing->parentWidget() : nullptr;
    require(tcpWaveCardForSpacing != nullptr,
            "home TCP wave top-level card exists for spacing checks");
    requireTopLevelCardElevation(tcpWaveCardForSpacing,
                                 1.0,
                                 "home TCP wave card uses the shared soft elevation");
    const QRect epsilonCardRect = widgetRectInCentral(homeEpsilonCardForSpacing);
    const QRect environmentCardRect = widgetRectInCentral(homeEnvironmentCardForSpacing);
    const QRect homeTcpWaveCardRect = widgetRectInCentral(tcpWaveCardForSpacing);
    auto *homeEpsilonTitleBar = homeEpsilonCardForSpacing->findChild<QWidget *>(
        QStringLiteral("sectionTitleBar"), Qt::FindDirectChildrenOnly);
    auto *homeEnvironmentTitleBar = homeEnvironmentCardForSpacing->findChild<QWidget *>(
        QStringLiteral("environmentSectionTitleBar"), Qt::FindDirectChildrenOnly);
    require(homeEpsilonTitleBar != nullptr && homeEnvironmentTitleBar != nullptr,
            "home EPSILON and environment cards expose standard title bars");
    require(homeEnvironmentTitleBar->height() == homeEpsilonTitleBar->height(),
            "home environment title bar matches the EPSILON title-bar height");
    require(homeEnvironmentTitleBar->y() <= 2,
            "home environment title bar sits flush with the top of the card");
    require(std::abs((std::min(epsilonCardRect.top(), environmentCardRect.top()) -
                      bottomEdge(homeOverviewSplitterRect)) -
                     kExpectedTopLevelCardGap) <= 1,
            "home sensor row keeps the shared shadow-safe vertical gap");
    if (std::abs(epsilonCardRect.top() - environmentCardRect.top()) <= 1)
    {
        const int homeSensorCardGap =
            environmentCardRect.left() - rightEdge(epsilonCardRect);
        require(std::abs(homeSensorCardGap - homeOverviewCardGap) <= 1,
                "home EPSILON and environment cards match the device and temperature overview gap");
    }
    else
    {
        require(std::abs((environmentCardRect.top() - bottomEdge(epsilonCardRect)) -
                         kExpectedTopLevelCardGap) <= 1,
                "home EPSILON and environment cards keep the shared vertical gap");
    }
    require(std::abs((homeTcpWaveCardRect.top() -
                      std::max(bottomEdge(epsilonCardRect), bottomEdge(environmentCardRect))) -
                     kExpectedTopLevelCardGap) <= 1,
            "home TCP wave card keeps the shared vertical gap");
    const int expectedCardOuterTopGap =
        mainContentMargins.top() + kExpectedPageTopInset;
    const int expectedCardOuterBottomGap =
        mainContentMargins.bottom() + kExpectedPageTopInset;
    const int expectedCardOuterRightGap =
        mainContentMargins.right();
    require(std::abs((recordingCardRect.top() - centralRect.top()) -
                     expectedCardOuterTopGap) <= 1,
            "recording status card keeps the app safe margin plus card shadow inset");
    require(std::abs((logCardRect.top() - bottomEdge(recordingCardRect)) -
                     kExpectedTopLevelCardGap) <= 1,
            "right-side log card keeps the shared vertical gap");
    require(std::abs((centralRect.bottom() - logCardRect.bottom()) -
                     expectedCardOuterBottomGap) <= 1,
            "right-side log card keeps the visible 12px bottom gap");
    require(sidebarLeftGap > 0 &&
                std::abs(sidebarLeftGap - sidebarBottomGap) <= 1,
            "sidebar left border uses the same outer margin as its bottom border");
    require(std::abs(recordingRightGap - expectedCardOuterRightGap) <= 1 &&
                std::abs(logRightGap - expectedCardOuterRightGap) <= 1,
            "recording status and log cards keep the 8px right safe margin");
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
    require(checkedSidebarButton->focusPolicy() == Qt::TabFocus,
            "compact sidebar remains keyboard reachable after collapsing");
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
    const int visualLeftPadding = checkedSidebarButton->x();
    const int visualRightPadding =
        appSidebar->width() - checkedSidebarButton->x() - checkedSidebarButton->width();
    if (std::abs(visualLeftPadding - visualRightPadding) > 1)
    {
        std::cerr << "Sidebar visual padding: left=" << visualLeftPadding
                  << " right=" << visualRightPadding
                  << " buttonX=" << checkedSidebarButton->x()
                  << " sidebarWidth=" << appSidebar->width()
                  << " buttonWidth=" << checkedSidebarButton->width() << '\n';
    }
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
    doubleClickWidget(customLogo, 0);
    require(processEventsUntil(1000, [appSidebar, customLogo]() {
                return appSidebar->width() >= 44 &&
                       customLogo->property("_vv_logo_state").toString() ==
                           QStringLiteral("close-sidebar");
            }),
            "custom title logo double-click applies both sidebar toggles and restores the collapse icon");
    clickWidget(customLogo, 0);
    require(processEventsUntil(1000, [appSidebar, customLogo]() {
                return appSidebar->width() <= 1 &&
                       customLogo->property("_vv_logo_state").toString() ==
                           QStringLiteral("open-sidebar");
            }),
            "custom title logo click collapses the sidebar and changes to the expand icon");
    clickWidget(customLogo, 0);
    require(processEventsUntil(1000, [appSidebar, customLogo]() {
                return appSidebar->width() >= 44 &&
                       customLogo->property("_vv_logo_state").toString() ==
                           QStringLiteral("close-sidebar");
            }),
            "custom title logo click restores the sidebar and returns to the collapse icon");
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
    auto findTitleApplicationRows = [](QWidget *menu, Qt::FindChildOptions options = Qt::FindChildrenRecursively) {
        QList<QWidget *> rows;
        if (!menu)
        {
            return rows;
        }
        for (QToolButton *row : menu->findChildren<QToolButton *>(QString(), options))
        {
            if (row && row->property("titleApplicationMenuItem").toBool())
            {
                rows.push_back(row);
            }
        }
        return rows;
    };
    QList<QWidget *> titleApplicationRows = findTitleApplicationRows(titleApplicationMainMenu);
    require(!titleApplicationRows.isEmpty(),
            "title bar application menu exposes hoverable root rows");
    requireMenuRowsRespectRoundedVerticalPadding(
        titleApplicationPanel,
        titleApplicationMainMenu,
        titleApplicationRows,
        "title bar application main menu rows stay inside rounded vertical padding");
    auto findTitleApplicationRow = [](const QList<QWidget *>& rows, const QStringList& texts) -> QWidget * {
        for (QWidget *row : rows)
        {
            const QList<QLabel*> labels = row->findChildren<QLabel *>(QStringLiteral("titleApplicationMenuText"));
            for (const QLabel *label : labels)
            {
                if (label && texts.contains(label->text()))
                {
                    return row;
                }
            }
        }
        return nullptr;
    };
    QWidget *helpRootRow = findTitleApplicationRow(
        titleApplicationRows,
        QStringList{QStringLiteral("帮助"), QStringLiteral("Help")});
    require(helpRootRow != nullptr,
            "title bar application menu exposes the Help root row");
    hoverWidget(helpRootRow, true, 220);
    require(helpRootRow->property("selected").toBool(),
            "title bar application menu keeps the hovered Help row selected while its submenu is open");
    auto *titleApplicationHelpSubPanel = window.findChild<QFrame *>(QStringLiteral("titleApplicationSubPanel"));
    titleApplicationPanel->hide();
    if (titleApplicationHelpSubPanel)
    {
        titleApplicationHelpSubPanel->hide();
    }
    if (auto *titleApplicationNestedPanel = window.findChild<QFrame *>(QStringLiteral("titleApplicationNestedPanel")))
    {
        titleApplicationNestedPanel->hide();
    }
    processEventsFor(50);
    clickWidget(titleMenuButton, 120);
    require(titleApplicationPanel->isVisible(),
            "title bar application menu reopens after closing the Help submenu");
    titleApplicationRows =
        findTitleApplicationRows(titleApplicationMainMenu);
    for (QWidget *row : titleApplicationRows)
    {
        require(!row->property("selected").toBool(),
                "title bar application root menu reopens without stale hover selection");
    }
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

    QWidget *nestedRootRow = nullptr;
    for (QWidget *row : titleApplicationRows)
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

    QWidget *nestedCommandRow = nullptr;
#ifdef VAPORVIEW_HAS_OSGEARTH
    bool foundMapDiagnosticsCommand = false;
#endif
    const QList<QWidget *> subRows = findTitleApplicationRows(titleApplicationSubMenu);
    requireMenuRowsRespectRoundedVerticalPadding(
        titleApplicationSubPanel,
        titleApplicationSubMenu,
        subRows,
        "title bar application submenu rows stay inside rounded vertical padding");
    for (QWidget *row : subRows)
    {
        const QList<QLabel*> labels = row->findChildren<QLabel *>(QStringLiteral("titleApplicationMenuText"));
        for (const QLabel *label : labels)
        {
#ifdef VAPORVIEW_HAS_OSGEARTH
            foundMapDiagnosticsCommand = foundMapDiagnosticsCommand ||
                (label && (label->text() == QStringLiteral("地图数据诊断") ||
                           label->text() == QStringLiteral("Map Data Diagnostics")));
#endif
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
#ifdef VAPORVIEW_HAS_OSGEARTH
    require(foundMapDiagnosticsCommand,
            "title application Developer menu contains map data diagnostics");
#endif
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
    const QList<QWidget *> nestedRows = findTitleApplicationRows(titleApplicationNestedMenu);
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
    const QSize nestedPanelSizeBeforeRepeatedHover = titleApplicationNestedPanel->size();
    const QRect nestedPanelGeometryBeforeRepeatedHover = titleApplicationNestedPanel->geometry();
    QWidget *firstNestedRowBeforeRepeatedHover = nestedRows.isEmpty() ? nullptr : nestedRows.first();
    hoverWidget(nestedCommandRow, true, 220);
    require(titleApplicationNestedPanel->isVisible(),
            "title bar application nested submenu remains visible while hovering its source row");
    require(titleApplicationNestedPanel->size() == nestedPanelSizeBeforeRepeatedHover &&
                titleApplicationNestedPanel->geometry() == nestedPanelGeometryBeforeRepeatedHover,
            "title bar application nested submenu is not rebuilt or moved on repeated source hover");
    const QList<QWidget *> nestedRowsAfterRepeatedHover = findTitleApplicationRows(titleApplicationNestedMenu);
    require(!nestedRowsAfterRepeatedHover.isEmpty() &&
                nestedRowsAfterRepeatedHover.first() == firstNestedRowBeforeRepeatedHover,
            "title bar application nested submenu keeps its row widgets on repeated source hover");
    clickWidget(titleMenuButton, 50);
    require(!titleApplicationPanel->isVisible() &&
                !titleApplicationSubPanel->isVisible() &&
                !titleApplicationNestedPanel->isVisible(),
            "title bar application menu closes through its menu button");
    processEventsFor(50);

    QToolButton *logFilterButton = nullptr;
    QToolButton *logSearchButton = nullptr;
    QToolButton *logClearButton = nullptr;
    const QList<QToolButton*> titleBarButtons =
        window.findChildren<QToolButton *>(QStringLiteral("titleBarButton"));
    for (QToolButton *button : titleBarButtons)
    {
        if (button && button->accessibleName() == QStringLiteral("logSearchButton"))
        {
            logSearchButton = button;
        }
        if (button && (button->toolTip() == QStringLiteral("仅清空当前显示，不删除日志文件") ||
                       button->toolTip() == QStringLiteral("Clear the visible log panel without deleting log files")))
        {
            logClearButton = button;
        }
        if (button && (button->toolTip() == QStringLiteral("日志视图") ||
                       button->toolTip() == QStringLiteral("Log view")))
        {
            logFilterButton = button;
        }
    }
    require(logSearchButton != nullptr, "log search title-bar button exists");
    auto *logTitleActions = window.findChild<QWidget *>(QStringLiteral("logTitleActions"));
    require(logTitleActions != nullptr && logTitleActions->layout() &&
                logTitleActions->layout()->spacing() == 0,
            "log title action icon cluster has zero spacing");
    require(logFilterButton != nullptr && logClearButton != nullptr &&
                logSearchButton->parentWidget() == logTitleActions &&
                logFilterButton->parentWidget() == logTitleActions &&
                logClearButton->parentWidget() == logTitleActions,
            "log title search, filter, and clear icons share the zero-spacing cluster");
    require(!logSearchButton->icon().isNull(),
            "log search title-bar button has a visible search icon");
    require(window.findChildren<QToolButton *>(QStringLiteral("logViewModeButton")).isEmpty(),
            "log mode buttons are not duplicated below the title bar");
    require(window.findChild<QToolButton *>(QStringLiteral("logAutoFollowButton")) == nullptr,
            "log follow button is not duplicated below the title bar");
    auto *logNewEntriesRow = window.findChild<QWidget *>(QStringLiteral("logNewEntriesRow"));
    auto *logNewEntriesButton = window.findChild<QPushButton *>(QStringLiteral("logNewEntriesButton"));
    require(logNewEntriesRow != nullptr && logNewEntriesButton != nullptr &&
                logNewEntriesButton->parentWidget() == logNewEntriesRow,
            "log new-entry reminder sits in its own row below the title bar");
    clickWidget(logSearchButton, 120);
    auto *logSearchMenu = window.findChild<QMenu *>(QStringLiteral("logSearchMenu"));
    auto *logSearchEdit = window.findChild<QLineEdit *>(QStringLiteral("logSearchEdit"));
    require(logSearchMenu != nullptr && logSearchMenu->isVisible() &&
                logSearchEdit != nullptr && logSearchEdit->isVisible(),
            "log search icon opens a popup search field");
    logSearchMenu->hide();
    processEventsFor(50);
    require(logFilterButton != nullptr, "log filter title-bar button exists");
    clickWidget(logFilterButton, 180);
    VaporView::SingleLevelPopupMenu *logFilterMenu = nullptr;
    for (QWidget *topLevel : QApplication::topLevelWidgets())
    {
        auto *menu = qobject_cast<VaporView::SingleLevelPopupMenu *>(topLevel);
        if (menu && menu->isVisible() &&
            (menu->title() == QStringLiteral("日志视图") ||
             menu->title() == QStringLiteral("Log View")))
        {
            logFilterMenu = menu;
            break;
        }
    }
    require(logFilterMenu != nullptr, "log filter menu uses the shared single-level popup");
    require(logFilterMenu->rows().size() == 5,
            "log view menu exposes attention, all, debug, follow, and source/category rows");
    QStringList logFilterTexts;
    for (VaporView::SingleLevelPopupMenuRow *row : logFilterMenu->rows())
    {
        if (row)
        {
            logFilterTexts.append(row->text());
        }
    }
    require(logFilterTexts.contains(QStringLiteral("关注")) &&
                logFilterTexts.contains(QStringLiteral("全部")) &&
                logFilterTexts.contains(QStringLiteral("调试")) &&
                logFilterTexts.contains(QStringLiteral("自动跟随")) &&
                logFilterTexts.contains(QStringLiteral("过滤[来源/分类]")),
            "log view menu exposes positive view choices");
    require(logFilterMenu->cornerRadius() == 10,
            "log filter menu uses the shared 10px popup corner radius");
    require(logFilterMenu->panelPadding() == 12,
            "log filter menu uses the shared 12px popup vertical padding");
    require(logFilterMenu->property("floatingPanelChrome").toBool(),
            "log filter menu uses floating popup chrome");
    require(logFilterMenu->property("shadowMargin").toInt() == 22 &&
                logFilterMenu->property("shadowBottomMargin").toInt() == 50,
            "log filter menu preserves its horizontal and extended bottom popup shadow margins");
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
            (menu->title() == QStringLiteral("日志视图") ||
             menu->title() == QStringLiteral("Log View")))
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
    const QList<QLabel*> cardTitleLabels =
        window.findChildren<QLabel *>(QStringLiteral("sectionTitleLabel"));
    require(!cardTitleLabels.isEmpty(), "main window exposes card title labels");
    for (QLabel *titleLabel : cardTitleLabels)
    {
        requireSelectableCardTitle(titleLabel,
                                   "all main-window card titles are selectable and copyable");
    }
    QLabel *visibleCardTitle = nullptr;
    for (QLabel *titleLabel : cardTitleLabels)
    {
        if (titleLabel->isVisibleTo(&window) && titleLabel->text().size() >= 2)
        {
            visibleCardTitle = titleLabel;
            break;
        }
    }
    requireCardTitleMouseSelectionAndCopy(visibleCardTitle);

    auto *homeOverviewSplitter = window.findChild<QSplitter *>(QStringLiteral("homeOverviewSplitter"));
    require(homeOverviewSplitter != nullptr, "home overview splitter exists");
    require(homeOverviewSplitter->count() == 2, "home overview splitter has device and temperature cards");
    require(!homeOverviewSplitter->isCollapsible(0) &&
                !homeOverviewSplitter->isCollapsible(1),
            "home overview splitter keeps both overview cards non-collapsible");

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
    requireHomeDeviceMinimumWidthMatchesControls(&window);

    auto *homeConfigCard = deviceOverviewCard;
    require(homeConfigCard != nullptr, "home configuration card exists");
    QComboBox *homeSourceModeCombo = findSourceModeCombo(homeConfigCard);
    require(homeSourceModeCombo != nullptr, "home source mode combo exists");
    require(homeSourceModeCombo->property("usesSingleLevelPopupMenu").toBool(),
            "home source mode combo uses the shared single-level popup");
    QWidget *homeOverviewResizeHandle = nullptr;
    QWidget *homeDataResizeHandle = nullptr;
    int closestOverviewHandleDistance = std::numeric_limits<int>::max();
    int closestDataHandleDistance = std::numeric_limits<int>::max();
    const QRect overviewResizeBaselineRect = widgetRectInCentral(homeOverviewSplitter);
    auto *homeDataCard = window.findChild<QGroupBox *>(QStringLiteral("sensorRowContainer"));
    require(homeDataCard != nullptr, "home data card exists for resize testing");
    const QRect dataResizeBaselineRect = widgetRectInCentral(homeDataCard);
    const QList<QWidget*> mainCardResizeHandles =
        window.findChildren<QWidget *>(QStringLiteral("mainCardResizeHandle"));
    for (QWidget *resizeHandle : mainCardResizeHandles)
    {
        if (!resizeHandle->isVisibleTo(&window))
        {
            continue;
        }
        const QRect handleRect = widgetRectInCentral(resizeHandle);
        const int distanceBelowOverview = handleRect.top() - bottomEdge(overviewResizeBaselineRect);
        if (distanceBelowOverview >= 0 && distanceBelowOverview < closestOverviewHandleDistance)
        {
            closestOverviewHandleDistance = distanceBelowOverview;
            homeOverviewResizeHandle = resizeHandle;
        }
        const int distanceBelowData = handleRect.top() - bottomEdge(dataResizeBaselineRect);
        if (distanceBelowData >= 0 && distanceBelowData < closestDataHandleDistance)
        {
            closestDataHandleDistance = distanceBelowData;
            homeDataResizeHandle = resizeHandle;
        }
    }
    require(homeOverviewResizeHandle != nullptr,
            "home overview vertical resize handle exists");
    require(homeDataResizeHandle != nullptr && homeDataResizeHandle != homeOverviewResizeHandle,
            "home data-card vertical resize handle exists");
    auto *homeOverviewResizeGap = window.findChild<QWidget *>(QStringLiteral("homeOverviewResizeGap"));
    require(homeOverviewResizeGap != nullptr,
            "home overview resize handle has a dedicated blank gap target");
    require(homeOverviewResizeGap->height() ==
                VaporView::Ground::MainSupport::kTopLevelCardSpacerAfterResizeHandle,
            "home overview blank gap keeps the shared 9px baseline spacing");
    homeSourceModeCombo->setCurrentIndex(1);
    processEventsFor(150);
    activateLayouts(&window);
    VaporView::TelemetryStatus dragTelemetryStatus;
    dragTelemetryStatus.status_rate_hz = 10.0;
    require(QMetaObject::invokeMethod(
                &window,
                "onRemoteTelemetryStatusUpdated",
                Qt::DirectConnection,
                Q_ARG(VaporView::TelemetryStatus, dragTelemetryStatus)),
            "remote telemetry layout is initialized before card resizing");
    processEventsFor(80);
    activateLayouts(&window);
    const int overviewHeightBeforeDrag = homeOverviewSplitter->height();
    const int deviceOverviewHeightBeforeDrag = deviceOverviewCard->height();
    const int temperatureOverviewHeightBeforeDrag = temperatureOverviewCard->height();
    const int overviewGapBeforeDrag = homeOverviewResizeGap->height();
    const VerticalDragContext overviewExpandDrag =
        beginVerticalDrag(homeOverviewResizeHandle);
    moveVerticalDrag(overviewExpandDrag, 64);
    endVerticalDrag(overviewExpandDrag, 64);
    processEventsFor(40);
    activateLayouts(&window);
    const int overviewHeightAfterExpand = homeOverviewSplitter->height();
    const int overviewGapAfterExpand = homeOverviewResizeGap->height();
    require(overviewHeightAfterExpand == overviewHeightBeforeDrag &&
                deviceOverviewCard->height() == deviceOverviewHeightBeforeDrag &&
                temperatureOverviewCard->height() == temperatureOverviewHeightBeforeDrag,
            "home overview resize handle leaves both overview card heights unchanged");
    require(overviewGapAfterExpand >= overviewGapBeforeDrag + 48,
            "home overview resize handle expands the blank gap below the overview cards");

    const VerticalDragContext overviewShrinkDrag =
        beginVerticalDrag(homeOverviewResizeHandle);
    int previousTemperatureBottom = bottomEdge(widgetRectInCentral(temperatureOverviewCard));
    int previousOverviewGap = homeOverviewResizeGap->height();
    for (int offset = -1; offset >= -48; --offset)
    {
        moveVerticalDrag(overviewShrinkDrag, offset);
        if (offset == -16)
        {
            const int bottomBeforeStaleEvent =
                bottomEdge(widgetRectInCentral(temperatureOverviewCard));
            moveVerticalDragWithStaleEventPosition(
                overviewShrinkDrag,
                offset,
                0);
            const int bottomAfterStaleEvent =
                bottomEdge(widgetRectInCentral(temperatureOverviewCard));
            require(bottomAfterStaleEvent <= bottomBeforeStaleEvent + 1,
                    "a stale event coordinate cannot bounce the temperature overview edge downward");
        }
        if (offset % 4 == 0)
        {
            dragTelemetryStatus.telemetry_basic_rate_hz += 1.0;
            require(QMetaObject::invokeMethod(
                        &window,
                        "onRemoteTelemetryStatusUpdated",
                        Qt::DirectConnection,
                        Q_ARG(VaporView::TelemetryStatus, dragTelemetryStatus)),
                    "remote telemetry refresh can be injected during card resizing");
        }
        processEventsFor(5);
        const int nextTemperatureBottom = bottomEdge(widgetRectInCentral(temperatureOverviewCard));
        const int nextOverviewGap = homeOverviewResizeGap->height();
        if (nextTemperatureBottom > previousTemperatureBottom + 1)
        {
            std::cerr << "Temperature overview drag bounce: previous="
                      << previousTemperatureBottom
                      << " next=" << nextTemperatureBottom
                      << " offset=" << offset << '\n';
        }
        require(nextTemperatureBottom <= previousTemperatureBottom + 1,
                "remote telemetry refresh cannot bounce the temperature overview edge downward while dragging upward");
        require(homeOverviewSplitter->height() == overviewHeightBeforeDrag &&
                    deviceOverviewCard->height() == deviceOverviewHeightBeforeDrag &&
                    temperatureOverviewCard->height() == temperatureOverviewHeightBeforeDrag &&
                    nextOverviewGap <= previousOverviewGap + 1,
                "overview gap resizing does not change the overview card heights");
        previousTemperatureBottom = nextTemperatureBottom;
        previousOverviewGap = nextOverviewGap;
    }
    endVerticalDrag(overviewShrinkDrag, -48);
    processEventsFor(40);
    activateLayouts(&window);
    require(homeOverviewResizeGap->height() >= overviewGapBeforeDrag &&
                homeOverviewResizeGap->height() <= overviewGapAfterExpand - 32 &&
                homeOverviewSplitter->height() == overviewHeightBeforeDrag &&
                deviceOverviewCard->height() == deviceOverviewHeightBeforeDrag &&
                temperatureOverviewCard->height() == temperatureOverviewHeightBeforeDrag,
            "home overview resize handle can shrink the blank gap without resizing either card");
    const VerticalDragContext overviewClampDrag =
        beginVerticalDrag(homeOverviewResizeHandle);
    moveVerticalDrag(overviewClampDrag, -256);
    const int dragLockedOverviewHeight = homeOverviewSplitter->height();
            require(homeOverviewResizeGap->height() == overviewGapBeforeDrag &&
                dragLockedOverviewHeight == overviewHeightBeforeDrag &&
                deviceOverviewCard->height() == deviceOverviewHeightBeforeDrag &&
                temperatureOverviewCard->height() == temperatureOverviewHeightBeforeDrag,
            "overview gap clamps at its minimum without changing either overview card");
    QEvent overviewLayoutRequest(QEvent::LayoutRequest);
    QCoreApplication::sendEvent(homeOverviewResizeGap, &overviewLayoutRequest);
    processEventsFor(20);
    require(homeOverviewResizeGap->height() == overviewGapBeforeDrag &&
                homeOverviewSplitter->height() == dragLockedOverviewHeight &&
                deviceOverviewCard->height() == deviceOverviewHeightBeforeDrag &&
                temperatureOverviewCard->height() == temperatureOverviewHeightBeforeDrag,
            "layout requests cannot release the overview gap or resize its cards");
    endVerticalDrag(overviewClampDrag, -256);
    processEventsFor(40);
    activateLayouts(&window);
    const int clampedOverviewHeight = homeOverviewSplitter->height();
    ResizeHeightRecorder overviewRefreshRecorder(homeOverviewSplitter);
    homeOverviewSplitter->installEventFilter(&overviewRefreshRecorder);
    overviewRefreshRecorder.reset();
    dragTelemetryStatus.telemetry_basic_rate_hz += 1.0;
    require(QMetaObject::invokeMethod(
                &window,
                "onRemoteTelemetryStatusUpdated",
                Qt::DirectConnection,
                Q_ARG(VaporView::TelemetryStatus, dragTelemetryStatus)),
            "remote telemetry refresh can run at the overview minimum height");
    processEventsFor(20);
    require(!overviewRefreshRecorder.observedHeightDifferentFrom(
                clampedOverviewHeight),
            "remote telemetry refresh cannot transiently release the overview fixed height");
    homeOverviewSplitter->removeEventFilter(&overviewRefreshRecorder);
    homeSourceModeCombo->setCurrentIndex(0);
    processEventsFor(120);
    activateLayouts(&window);

    const int dataHeightBeforeDrag = homeDataCard->height();
    const VerticalDragContext dataExpandDrag = beginVerticalDrag(homeDataResizeHandle);
    moveVerticalDrag(dataExpandDrag, 64);
    endVerticalDrag(dataExpandDrag, 64);
    processEventsFor(40);
    activateLayouts(&window);
    const int dataHeightAfterExpand = homeDataCard->height();
    require(dataHeightAfterExpand >= dataHeightBeforeDrag + 48,
            "home data-card resize handle can expand the sensor row");

    const VerticalDragContext dataShrinkDrag = beginVerticalDrag(homeDataResizeHandle);
    const int stableDataTop = widgetRectInCentral(homeDataCard).top();
    int previousDataBottom = bottomEdge(widgetRectInCentral(homeDataCard));
    ResizeHeightRecorder dataRefreshRecorder(homeDataCard);
    homeDataCard->installEventFilter(&dataRefreshRecorder);
    for (int offset = -1; offset >= -48; --offset)
    {
        moveVerticalDrag(dataShrinkDrag, offset);
        if (offset % 4 == 0)
        {
            const int dataHeightBeforeResponsiveRefresh =
                homeDataCard->height();
            dataRefreshRecorder.reset();
            QResizeEvent responsiveRefresh(window.size(), window.size());
            QCoreApplication::sendEvent(&window, &responsiveRefresh);
            require(!dataRefreshRecorder.observedHeightDifferentFrom(
                        dataHeightBeforeResponsiveRefresh),
                    "responsive refresh cannot transiently release the data-card fixed height");
        }
        processEventsFor(5);
        const QRect nextDataRect = widgetRectInCentral(homeDataCard);
        require(std::abs(nextDataRect.top() - stableDataTop) <= 1,
                "responsive refresh cannot move the data card while its lower edge is dragged");
        require(bottomEdge(nextDataRect) <= previousDataBottom + 1,
                "responsive refresh cannot bounce the data-card edge downward while dragging upward");
        previousDataBottom = bottomEdge(nextDataRect);
    }
    endVerticalDrag(dataShrinkDrag, -48);
    homeDataCard->removeEventFilter(&dataRefreshRecorder);
    processEventsFor(40);
    activateLayouts(&window);
    require(homeDataCard->height() <= dataHeightAfterExpand - 32,
            "home data-card resize handle can shrink the expanded sensor row");
    homeDataCard->setProperty(
        VaporView::Ground::MainSupport::kMainCardUserResizedHeightProperty,
        false);
    QResizeEvent restoreResponsiveLayout(window.size(), window.size());
    QCoreApplication::sendEvent(&window, &restoreResponsiveLayout);
    processEventsFor(40);
    activateLayouts(&window);

    const QRect homeConfigLocalRect = homeConfigCard->geometry();
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
    require(homeDeviceCapsules.size() == 7,
            "home device overview includes seven status capsules");
    const QList<QToolButton*> homeDeviceActionButtons =
        window.findChildren<QToolButton *>(QStringLiteral("homeDeviceActionButton"));
    require(homeDeviceActionButtons.size() >= 7,
            "home device overview includes seven connection action buttons");
    requireHomeDeviceColumnsAligned(&window);
    requireHomeDeviceMinimumWidthMatchesControls(&window);
    const bool homeDeviceActionDark =
        qApp->property(VaporView::kAppDarkThemeProperty).toBool();
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QToolButton#homeDeviceActionButton {"),
        QStringLiteral("background-color: ") +
            VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceAlt,
                                         homeDeviceActionDark),
        "home device overview action buttons keep their gray background");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QToolButton#homeDeviceActionButton {"),
        QStringLiteral("border: 1px solid ") +
            VaporView::appThemeColorName(VaporView::AppThemeColor::Border,
                                         homeDeviceActionDark),
        "home device overview action buttons keep their border");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QToolButton#homeDeviceActionButton:hover {"),
        QStringLiteral("background-color: ") +
            VaporView::appThemeColorName(VaporView::AppThemeColor::PrimarySubtle,
                                         homeDeviceActionDark),
        "home device overview action buttons keep their primary hover background");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QToolButton#homeDeviceActionButton:hover {"),
        QStringLiteral("border-color: ") +
            VaporView::appThemeColorName(VaporView::AppThemeColor::BorderStrong,
                                         homeDeviceActionDark),
        "home device overview action buttons keep their hover border");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QToolButton#homeDeviceActionButton[deviceConfigAction=\"true\"] {"),
        QStringLiteral("background-color: transparent"),
        "serial configuration action buttons are transparent by default");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QToolButton#homeDeviceActionButton[deviceConfigAction=\"true\"] {"),
        QStringLiteral("border: none"),
        "serial configuration action buttons have no visible edge by default");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QToolButton#homeDeviceActionButton[deviceConfigAction=\"true\"]:hover {"),
        QStringLiteral("background-color: ") +
            VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceAlt,
                                         homeDeviceActionDark),
        "serial configuration action buttons show a gray background only on hover");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QToolButton#homeDeviceActionButton[deviceConfigAction=\"true\"]:hover {"),
        QStringLiteral("border: none"),
        "serial configuration action buttons have no hover edge");
    for (QToolButton *button : homeDeviceActionButtons)
    {
        if (!button->property("deviceConfigAction").toBool())
        {
            require(button->focusPolicy() == Qt::TabFocus,
                    "home device connection actions do not take focus on mouse click");
        }
    }
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
    bool hasAi8HomeCapsule = false;
    for (QLabel *capsule : homeDeviceCapsules)
    {
        if (capsule->text().contains(QStringLiteral("AI-8288八路温控")))
        {
            hasAi8HomeCapsule = true;
            break;
        }
    }
    require(hasAi8HomeCapsule,
            "home device overview includes the AI-8288 eight-channel temperature controller");

    auto *ai8TemperaturePage = window.findChild<QWidget *>(QStringLiteral("temperaturePage"));
    require(ai8TemperaturePage != nullptr, "temperature page exists");
    QGroupBox *ai8TemperatureCard = nullptr;
    const QList<QGroupBox *> topLevelGroups = window.findChildren<QGroupBox *>();
    for (QGroupBox *group : topLevelGroups)
    {
        if (group->property("ai8TemperatureControllerCard").toBool())
        {
            ai8TemperatureCard = group;
            break;
        }
    }
    require(ai8TemperatureCard != nullptr,
            "AI-8 multi-loop controller card exists");
    require(ai8TemperaturePage->isAncestorOf(ai8TemperatureCard),
            "AI-8 controller card belongs to the temperature page");
    require(!homeOverviewSplitter->isAncestorOf(ai8TemperatureCard),
            "AI-8 controller card is not added to the home overview");

    auto *ai8TitlePortCombo = ai8TemperatureCard->findChild<QComboBox *>(
        QStringLiteral("ai8TitlePortCombo"));
    auto *ai8TitleLayout = ai8TitlePortCombo
        ? qobject_cast<QHBoxLayout *>(ai8TitlePortCombo->parentWidget()->layout())
        : nullptr;
    auto *ai8TitleCluster = ai8TitleLayout && ai8TitleLayout->itemAt(0)
        ? ai8TitleLayout->itemAt(0)->widget()
        : nullptr;
    auto *ai8TitleLabel = ai8TitleCluster
        ? ai8TitleCluster->findChild<QLabel *>(QStringLiteral("sectionTitleLabel"))
        : nullptr;
    auto *rd105TitlePortCombo = window.findChild<QComboBox *>(
        QStringLiteral("temperatureTitlePortCombo"));
    require(ai8TitlePortCombo != nullptr && ai8TitleLayout != nullptr &&
                ai8TitleLayout->indexOf(ai8TitlePortCombo) == 1 &&
                ai8TitlePortCombo->count() >= 1 &&
                ai8TitlePortCombo->itemData(0).toString().isEmpty() &&
                ai8TitlePortCombo->itemText(0) == QStringLiteral("未选择串口") &&
                !ai8TitlePortCombo->isEditable() &&
                ai8TitlePortCombo->property("usesSingleLevelPopupMenu").toBool() &&
                ai8TitlePortCombo->cursor().shape() == Qt::PointingHandCursor &&
                ai8TitlePortCombo->focusPolicy() == Qt::TabFocus,
            "AI-8 title is followed by the detected serial-port selector");
    require(ai8TitleLabel != nullptr &&
                ai8TitleLabel->text() == QStringLiteral("AI-8 系列多回路智能温控器 ·"),
            "AI-8 title uses the same separator as the RD105 title");
    require(rd105TitlePortCombo != nullptr &&
                ai8TitlePortCombo->font().weight() == rd105TitlePortCombo->font().weight() &&
                ai8TitlePortCombo->font().pointSizeF() == rd105TitlePortCombo->font().pointSizeF(),
            "AI-8 and RD105 title serial selectors share typography");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QComboBox#ai8TitlePortCombo {"),
        QStringLiteral("background-color: transparent"),
        "AI-8 title serial selector shares the RD105 transparent title style");
    auto *ai8TitleActionButton =
        ai8TemperatureCard->findChild<QToolButton *>(QStringLiteral("ai8TitleActionButton"));
    auto *ai8TitleStatusLabel =
        ai8TemperatureCard->findChild<QLabel *>(QStringLiteral("ai8TitleOutputStatusLabel"));
    auto *ai8ProtocolStatusLabel =
        ai8TemperatureCard->findChild<QLabel *>(QStringLiteral("ai8ProtocolStatus"));
    require(ai8TitleActionButton != nullptr &&
            ai8TemperatureCard->findChild<QToolButton *>(QStringLiteral("ai8TitleConnectButton")) == nullptr &&
                ai8TemperatureCard->findChild<QToolButton *>(QStringLiteral("ai8TitleDisconnectButton")) == nullptr &&
                ai8TitleLayout->indexOf(ai8TitleActionButton) ==
                    ai8TitleLayout->indexOf(ai8TitlePortCombo) + 1 &&
                ai8TitleActionButton->property("temperatureTitleAction").toBool() &&
                ai8TitleActionButton->property("temperatureTitleCommand").toString() == QStringLiteral("connect") &&
                ai8TitleActionButton->text().isEmpty() &&
                !ai8TitleActionButton->icon().isNull() &&
                ai8TitleActionButton->iconSize() == QSize(18, 18) &&
                ai8TitleActionButton->size() == QSize(32, 32),
            "AI-8 title serial selector is followed by one stateful connection icon action");
    require(ai8TitleStatusLabel != nullptr &&
                ai8TitleLayout->indexOf(ai8TitleStatusLabel) ==
                    ai8TitleLayout->indexOf(ai8TitleActionButton) + 1 &&
                ai8TitleStatusLabel->text() == QStringLiteral("输出：--"),
            "AI-8 title shows the selected channel output state after the connection icon");
    require(ai8ProtocolStatusLabel != nullptr &&
                ai8TitleLayout->indexOf(ai8ProtocolStatusLabel) == ai8TitleLayout->count() - 1 &&
                ai8ProtocolStatusLabel->text() == QStringLiteral("通信后端未连接") &&
                !ai8ProtocolStatusLabel->property("protocolReady").toBool(),
            "AI-8 communication backend status sits at the far end of the title bar");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QToolButton[temperatureTitleAction=\"true\"] {"),
        QStringLiteral("background-color: transparent"),
        "temperature title icon actions are transparent by default");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QToolButton[temperatureTitleAction=\"true\"] {"),
        QStringLiteral("border: none"),
        "temperature title icon actions have no visible edge by default");
    const bool temperatureTitleActionDark =
        qApp->property(VaporView::kAppDarkThemeProperty).toBool();
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QToolButton[temperatureTitleAction=\"true\"]:hover {"),
        QStringLiteral("background-color: ") +
            VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceAlt,
                                         temperatureTitleActionDark),
        "temperature title icon actions show a gray background only on hover");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QToolButton[temperatureTitleAction=\"true\"]:hover {"),
        QStringLiteral("border: none"),
        "temperature title icon actions have no hover edge");

    auto *ai8Panel = ai8TemperatureCard->findChild<QWidget *>(
        QStringLiteral("ai8TemperatureControllerPanel"));
    auto *ai8NavigationBar = ai8TemperatureCard->findChild<QFrame *>(
        QStringLiteral("ai8NavigationBar"));
    auto *ai8Stack = ai8TemperatureCard->findChild<QStackedWidget *>(
        QStringLiteral("ai8ParameterStack"));
    auto *ai8DetailStack = ai8TemperatureCard->findChild<QStackedWidget *>(
        QStringLiteral("ai8DetailParametersStack"));
    auto *ai8MainContentCard = ai8TemperatureCard->findChild<QFrame *>(
        QStringLiteral("ai8MainContentCard"));
    auto *ai8TemperaturePlot = ai8TemperatureCard->findChild<QWidget *>(
        QStringLiteral("ai8TemperatureTrendPlot"));
    auto *ai8StatusRow = ai8TemperatureCard->findChild<QWidget *>(
        QStringLiteral("ai8ProtocolStatusRow"));
    require(ai8Panel != nullptr && ai8NavigationBar != nullptr &&
                ai8NavigationBar->testAttribute(Qt::WA_StyledBackground) &&
                ai8Stack != nullptr && ai8Stack->count() == 4 &&
                ai8DetailStack != nullptr && ai8DetailStack->count() == 4 &&
                ai8MainContentCard != nullptr && ai8TemperaturePlot != nullptr &&
                ai8StatusRow != nullptr,
            "AI-8 card exposes channel, input, output, and global parameter pages");
    require(ai8Stack->currentIndex() == 0,
            "AI-8 card opens on channel parameters");
    const QRect ai8MainContentRect(ai8MainContentCard->mapTo(ai8Panel, QPoint(0, 0)),
                                   ai8MainContentCard->size());
    const QRect ai8StackRect(ai8Stack->mapTo(ai8Panel, QPoint(0, 0)), ai8Stack->size());
    const QRect ai8PlotRect(ai8TemperaturePlot->mapTo(ai8Panel, QPoint(0, 0)), ai8TemperaturePlot->size());
    const QRect ai8DetailStackRect(ai8DetailStack->mapTo(ai8Panel, QPoint(0, 0)), ai8DetailStack->size());
    const QRect ai8NavigationRect(ai8NavigationBar->mapTo(ai8Panel, QPoint(0, 0)), ai8NavigationBar->size());
    const QRect ai8StatusRect(ai8StatusRow->mapTo(ai8Panel, QPoint(0, 0)), ai8StatusRow->size());
    auto *ai8ManualOutputSpin = ai8TemperatureCard->findChild<QDoubleSpinBox *>(
        QStringLiteral("ai8ManualOutputSpin"));
    require(ai8ManualOutputSpin != nullptr,
            "AI-8 channel common parameters include the bottom-row manual output editor");
    const QRect ai8ManualOutputRect(ai8ManualOutputSpin->mapTo(ai8Panel, QPoint(0, 0)),
                                    ai8ManualOutputSpin->size());
    constexpr int kSerialConfigComboSpacingPx = 6;
    require(ai8StackRect.left() < ai8PlotRect.left() &&
                ai8StackRect.right() < ai8PlotRect.left() &&
                ai8PlotRect.width() > ai8StackRect.width() &&
                std::abs(ai8PlotRect.bottom() - ai8ManualOutputRect.bottom()) <= 2 &&
                std::abs(ai8StackRect.top() - ai8PlotRect.top()) <= 2 &&
                ai8DetailStackRect.top() > ai8MainContentRect.bottom() &&
                std::abs(ai8StackRect.left() - ai8MainContentRect.left() -
                         kSerialConfigComboSpacingPx) <= 1 &&
                std::abs(ai8PlotRect.left() -
                         (ai8StackRect.left() + ai8StackRect.width()) -
                         kSerialConfigComboSpacingPx) <= 1,
            "AI-8 common parameters stay compact and align the plot x-axis baseline with the bottom-row editor");
    require(ai8StatusRect.left() > ai8NavigationRect.right() &&
                ai8StatusRect.right() <= ai8Panel->rect().right() &&
                ai8StatusRect.bottom() < ai8MainContentRect.top() &&
                !ai8TemperaturePlot->property("forceWhiteBackground").toBool(),
            "AI-8 page action row is right-aligned beside page selectors and the plot follows the active theme");
    auto *ai8GlobalButton = ai8TemperatureCard->findChild<QPushButton *>(
        QStringLiteral("ai8PageSelectorButton4"));
    auto *ai8ChannelButton = ai8TemperatureCard->findChild<QPushButton *>(
        QStringLiteral("ai8PageSelectorButton1"));
    require(ai8GlobalButton != nullptr && ai8ChannelButton != nullptr,
            "AI-8 page selectors exist");
    auto *ai8ChannelDetailToggle = ai8TemperatureCard->findChild<QToolButton *>(
        QStringLiteral("ai8ChannelDetailParametersToggle"));
    auto *rd105TemperaturePanelForAi8Expansion =
        window.findChild<TemperatureControllerPanel *>();
    QWidget *rd105TemperatureCardForAi8Expansion =
        sensorGroupAncestor(rd105TemperaturePanelForAi8Expansion);
    auto *temperatureScrollAreaForAi8Expansion =
        ai8TemperaturePage->findChild<QScrollArea *>(QStringLiteral("mainCardsScrollArea"));
    QWidget *temperatureScrollContentForAi8Expansion =
        temperatureScrollAreaForAi8Expansion ? temperatureScrollAreaForAi8Expansion->widget() : nullptr;
    require(ai8ChannelDetailToggle != nullptr &&
                rd105TemperatureCardForAi8Expansion != nullptr &&
                temperatureScrollContentForAi8Expansion != nullptr &&
                temperatureScrollContentForAi8Expansion->layout() != nullptr,
            "AI-8 detail toggle, RD105 card, and temperature scroll content exist for expansion stability checks");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QWidget#ai8TemperatureControllerPanel QFrame#ai8NavigationBar {"),
                                 QStringLiteral("border-radius: 8px"),
                                 "AI-8 page selectors use the same rounded gray track as temperature parameters");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QWidget#ai8TemperatureControllerPanel QFrame#ai8NavigationBar QPushButton {"),
                                 QStringLiteral("background-color: transparent"),
                                 "AI-8 page selector buttons override the global primary button fill");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QWidget#ai8TemperatureControllerPanel QFrame#ai8NavigationBar QPushButton:checked {"),
                                 QStringLiteral("font-weight: 600"),
                                 "AI-8 page selector marks the selected parameter group like the temperature tabs");
    const bool ai8StyleDark =
        qApp->property(VaporView::kAppDarkThemeProperty).toBool();
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QWidget#ai8TemperatureControllerPanel QFrame#ai8MainContentCard {"),
        QStringLiteral("background-color: %1").arg(
            VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceRaised, ai8StyleDark)),
        "AI-8 common parameters and trend plot use the raised theme surface");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QWidget#ai8TemperatureControllerPanel QFrame#ai8MainContentCard {"),
                                 QStringLiteral("border-radius: 8px"),
                                 "AI-8 common parameters and trend plot share a bordered content card");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QWidget#ai8TemperatureControllerPanel QFrame#ai8DetailParametersCard {"),
        QStringLiteral("background-color: %1").arg(
            VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceRaised, ai8StyleDark)),
        "AI-8 detail parameter cards use the same background as the common parameter card");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QWidget#ai8TemperatureControllerPanel QFrame#ai8ParameterField {"),
                                 QStringLiteral("border: none"),
                                 "AI-8 common parameter fields have no outer frame");
    ai8GlobalButton->click();
    processEventsFor(10);
    require(ai8Stack->currentIndex() == 3 && ai8DetailStack->currentIndex() == 3 &&
                ai8GlobalButton->isChecked(),
            "AI-8 page selector switches to global parameters");
    ai8ChannelButton->click();
    processEventsFor(10);
    require(ai8Stack->currentIndex() == 0 && ai8DetailStack->currentIndex() == 0 &&
                ai8ChannelButton->isChecked(),
            "AI-8 page selector returns to channel parameters");
    const int rd105CardHeightBeforeAi8DetailExpand =
        rd105TemperatureCardForAi8Expansion->height();
    const int ai8MainContentHeightBeforeDetailExpand = ai8MainContentCard->height();
    const QSize ai8TemperaturePlotSizeBeforeDetailExpand = ai8TemperaturePlot->size();
    MinimumHeightRecorder rd105CardHeightDuringAi8DetailExpand(
        rd105TemperatureCardForAi8Expansion);
    MinimumHeightRecorder ai8MainContentHeightDuringDetailExpand(ai8MainContentCard);
    ResizeEventCounter ai8TemperaturePlotResizeDuringDetailExpand(ai8TemperaturePlot);
    clickWidget(ai8ChannelDetailToggle, 120);
    activateLayouts(&window);
    require(rd105CardHeightDuringAi8DetailExpand.minimumHeight() >=
                rd105CardHeightBeforeAi8DetailExpand,
            "AI-8 detail expansion does not transiently shrink the RD105 card above it");
    require(ai8MainContentHeightDuringDetailExpand.minimumHeight() >=
                ai8MainContentHeightBeforeDetailExpand,
            "AI-8 detail expansion does not transiently shrink its main parameter and trend area");
    require(ai8TemperaturePlotResizeDuringDetailExpand.count() == 0 &&
                ai8TemperaturePlot->size() == ai8TemperaturePlotSizeBeforeDetailExpand,
            "AI-8 detail expansion keeps the temperature trend plot geometry stable");
    require(ai8TemperaturePlot->testAttribute(Qt::WA_OpaquePaintEvent),
            "AI-8 temperature trend plot paints opaquely without a background erase flash");
    if (temperatureScrollContentForAi8Expansion->minimumHeight() <
        temperatureScrollContentForAi8Expansion->layout()->sizeHint().height())
    {
        std::cerr << "TEMP_MIN=" << temperatureScrollContentForAi8Expansion->minimumHeight()
                  << " TEMP_HINT=" << temperatureScrollContentForAi8Expansion->layout()->sizeHint().height()
                  << " TEMP_MIN_SIZE=" << temperatureScrollContentForAi8Expansion->layout()->minimumSize().height()
                  << " VIEWPORT=" << temperatureScrollAreaForAi8Expansion->viewport()->height()
                  << '\n';
    }
    require(temperatureScrollContentForAi8Expansion->minimumHeight() >=
                temperatureScrollContentForAi8Expansion->layout()->sizeHint().height(),
            "temperature page scroll content tracks its expanded preferred height instead of compressing cards");

    auto *ai8ChannelSpin = ai8TemperatureCard->findChild<QSpinBox *>(
        QStringLiteral("ai8ChannelSpin"));
    auto *ai8AddressSpin = ai8TemperatureCard->findChild<QSpinBox *>(
        QStringLiteral("ai8DeviceAddressSpin"));
    auto *ai8BaudCombo = ai8TemperatureCard->findChild<QComboBox *>(
        QStringLiteral("ai8BaudCombo"));
    require(ai8ChannelSpin != nullptr && ai8ChannelSpin->minimum() == 1 &&
                ai8ChannelSpin->maximum() == 8,
            "AI-8288 channel selector covers the model's eight loops");
    require(ai8AddressSpin != nullptr && ai8AddressSpin->minimum() == 1 &&
                ai8AddressSpin->maximum() == 88 && ai8AddressSpin->value() == 1,
            "AI-8288 address control uses the usable Modbus range and default");
    require(ai8BaudCombo != nullptr && ai8BaudCombo->currentData().toInt() == 19200,
            "AI-8 baud control defaults to the documented 19.2K setting");
    auto *ai8ReadButton = ai8TemperatureCard->findChild<QPushButton *>(
        QStringLiteral("ai8ReadParametersButton"));
    auto *ai8WriteButton = ai8TemperatureCard->findChild<QPushButton *>(
        QStringLiteral("ai8WriteParametersButton"));
    require(ai8ReadButton != nullptr && ai8WriteButton != nullptr &&
                !ai8ReadButton->isEnabled() && !ai8WriteButton->isEnabled(),
            "AI-8 read and write actions remain disabled until the protocol backend is connected");

    auto *temperaturePortCombo = window.findChild<QComboBox *>(QStringLiteral("temperaturePortCombo"));
    auto *temperatureBaudCombo = window.findChild<QComboBox *>(QStringLiteral("temperatureBaudCombo"));
    auto *temperatureRateCombo = window.findChild<QComboBox *>(QStringLiteral("temperatureRateCombo"));
    require(temperaturePortCombo != nullptr && temperatureBaudCombo != nullptr && temperatureRateCombo != nullptr,
            "RD105 serial controls exist in device configuration");
    const QList<QPair<QString, const char *>> homeLocalSerialCombos = {
        {QStringLiteral("epsilonPortCombo"), "EPSILON local serial port combo uses select-plus-manual behavior"},
        {QStringLiteral("pressurePortCombo"), "PTB local serial port combo uses select-plus-manual behavior"},
        {QStringLiteral("humidityPortCombo"), "HMP local serial port combo uses select-plus-manual behavior"},
        {QStringLiteral("lidarPortCombo"), "Lidar local serial port combo uses select-plus-manual behavior"},
        {QStringLiteral("temperaturePortCombo"), "RD105 local serial port combo uses select-plus-manual behavior"}};
    for (const auto& comboSpec : homeLocalSerialCombos)
    {
        requireLocalSerialPortComboReady(window.findChild<QComboBox *>(comboSpec.first), comboSpec.second);
    }
    requireComboPopupStyled(temperaturePortCombo,
                            "temperature port combo uses the shared popup styling helper");
    requireComboPopupStyled(temperatureBaudCombo,
                            "temperature baud combo uses the shared popup styling helper");
    requireComboPopupStyled(temperatureRateCombo,
                            "temperature rate combo uses the shared popup styling helper");
#ifdef Q_OS_WIN
    require(localSerialPortValue(temperaturePortCombo) == QStringLiteral("COM9"),
            "RD105 local serial port restores remembered COM9 as its actual port value");
#else
    require(localSerialPortValue(temperaturePortCombo) == QStringLiteral("/dev/ttyRD105"),
            "RD105 local serial port restores remembered /dev/ttyRD105 as its actual port value");
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
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QToolButton#temperatureOverviewChannelButton {"),
                                 QStringLiteral("font-weight: 500"),
                                 "temperature overview channel selector uses the normal selector font weight");
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
        require(row->font().weight() <= QFont::Medium,
                "temperature overview channel menu text uses the normal selector weight");
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
                temperatureChannelPopup->property("shadowBottomMargin").toInt() == 50 &&
                temperatureChannelPopup->property("floatingPanelChrome").toBool(),
            "temperature overview channel menu uses the unified horizontal and extended bottom popup margins");
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
    auto *temperatureOutputPercentPill =
        window.findChild<QLabel *>(QStringLiteral("temperatureOverviewOutputPercentPill"));
    auto *temperatureOutputCapsule =
        window.findChild<QFrame *>(QStringLiteral("temperatureOverviewOutputCapsule"));
    auto *temperatureOutputLabel =
        window.findChild<QLabel *>(QStringLiteral("temperatureOverviewOutputLabel"));
    auto *temperatureConfigOutputSwitch =
        window.findChild<QPushButton *>(QStringLiteral("temperatureOutputEnableSwitchChannel1"));
    require(temperatureOutputSwitch != nullptr,
            "temperature overview output enable capsule exists");
    require(temperatureOutputPercentPill != nullptr,
            "temperature overview output percent capsule exists");
    require(temperatureOutputPercentPill->textFormat() == Qt::RichText &&
                temperatureOutputPercentPill->text().contains(QStringLiteral("<br/>")) &&
                temperatureOutputPercentPill->text().contains(QStringLiteral("输出百分比")) &&
                temperatureOutputPercentPill->text().contains(QStringLiteral("---")),
            "temperature overview output percent starts as a two-line unavailable capsule");
    require(temperatureOutputSwitch->property("segmentedSwitchControl").toBool() &&
                temperatureOutputSwitch->focusPolicy() == Qt::TabFocus,
            "temperature overview output uses the keyboard-accessible shared segmented switch");
    require(temperatureOutputCapsule != nullptr &&
                temperatureOutputLabel != nullptr &&
                temperatureOutputLabel->parentWidget() == temperatureOutputCapsule &&
                temperatureOutputSwitch->parentWidget() == temperatureOutputCapsule &&
                temperatureOutputLabel->text() == QStringLiteral("输出使能"),
            "temperature overview keeps the output-enable label above the switch in one capsule");
    require(temperatureConfigOutputSwitch != nullptr &&
                temperatureOutputSwitch->height() == temperatureConfigOutputSwitch->height() &&
                temperatureOutputSwitch->height() == 34,
            "temperature overview and configuration use the same segmented-switch height");
    require(!temperatureOutputSwitch->isEnabled(),
            "temperature overview output enable capsule is disabled without controller data");
    auto *temperatureOverviewSummary =
        window.findChild<QWidget *>(QStringLiteral("temperatureOverviewSummary"));
    require(temperatureOverviewSummary != nullptr,
            "temperature overview summary column exists");
    require(temperatureOverviewSummary->layout() != nullptr,
            "temperature overview summary column has a layout");
    require(temperatureOverviewSummary->layout()->indexOf(temperatureChannelButton) >= 0 &&
                temperatureOverviewSummary->layout()->indexOf(temperatureOutputPercentPill) >
                    temperatureOverviewSummary->layout()->indexOf(temperatureChannelButton) &&
                temperatureOverviewSummary->layout()->indexOf(temperatureOutputPercentPill) <
                    temperatureOverviewSummary->layout()->indexOf(temperatureValuePills.first()),
            "temperature overview output percent capsule sits between channel selection and target temperature");
    processEventsFor(50);
    activateLayouts(&window);
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QLabel#temperatureOverviewValuePill {"),
                                 QStringLiteral("font-size: 13px"),
                                 "temperature overview value pill font matches the other capsules");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QLabel#temperatureOverviewOutputPercentPill {"),
                                 QStringLiteral("font-size: 13px"),
                                 "temperature overview output percent pill matches the value capsule typography");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QFrame#temperatureOverviewOutputCapsule {"),
                                 QStringLiteral("border-radius: 10px"),
                                 "temperature overview output label and switch share one rounded capsule");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QLabel#temperatureOverviewOutputLabel {"),
                                 QStringLiteral("font-size: 12px"),
                                 "temperature overview output capsule uses the value-pill title size");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QPushButton#temperatureOverviewOutputSwitch {"),
                                 QStringLiteral("font-size: 14px"),
                                 "temperature overview output switch font is enlarged for readability");
    const int temperatureSummarySpacing = temperatureOverviewSummary->layout()->spacing();
    int temperatureSummaryControlHeight =
        temperatureChannelButton->height() + temperatureOutputPercentPill->height() +
        temperatureOutputCapsule->height() + temperatureSummarySpacing * 4;
    for (QLabel *pill : temperatureValuePills)
    {
        temperatureSummaryControlHeight += pill->height();
        require(pill->height() >= 44,
                "temperature overview value capsules are taller than the old compact pills");
    }
    require(temperatureOutputPercentPill->height() > temperatureChannelButton->height() &&
                temperatureOutputPercentPill->height() >= 42,
            "temperature overview output percent capsule is taller than the compact channel selector");
    require(temperatureChannelButton->height() <= 30,
            "temperature overview channel selector is shorter than the value and output capsules");
    require(temperatureOutputCapsule->height() == 60 &&
                temperatureOutputCapsule->width() == temperatureChannelButton->width(),
            "temperature overview output capsule keeps the compact summary-column width and height");
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
    const QList<QFrame *> compactLinkRatePills =
        homeTelemetrySections.at(1)->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
    QStringList linkRateNames;
    for (QFrame *pill : compactLinkRatePills)
    {
        if (QLabel *nameLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel")))
        {
            linkRateNames << nameLabel->text();
        }
    }
    require(linkRateNames == QStringList{QStringLiteral("目标"),
                                         QStringLiteral("天→地"),
                                         QStringLiteral("地→天"),
                                         QStringLiteral("合")},
            "home link-rate pills expose target plus compact Chinese field names");
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
        require(valueLabel != nullptr,
                "home data-stream telemetry pill has a value label");
        require(valueLabel->width() >= valueLabel->fontMetrics().horizontalAdvance(QStringLiteral("999.9 Hz")),
                "home data-stream rate value label reserves room for 999.9 Hz");
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
        requireCompactTelemetryPillTextGap(
            pill,
            "home data-stream telemetry field/value gap remains compact");
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
    require(linkRatePills.size() == 4,
            "home link-rate telemetry section includes target plus three link-rate pills");
    QFrame *targetPill = findTelemetryPillByName(homeLinkSection, QStringLiteral("目标"));
    require(targetPill != nullptr,
            "home link-rate telemetry section exposes the current Local/Remote target");
    const int targetPillY = targetPill->mapTo(homeLinkSection, QPoint(0, 0)).y();
    int linkRatePillY = -1;
    for (QFrame *pill : linkRatePills)
    {
        if (pill == targetPill)
        {
            continue;
        }
        const int pillY = pill->mapTo(homeLinkSection, QPoint(0, 0)).y();
        require(pillY > targetPillY,
                "home link-rate target is alone on the first telemetry line");
        if (linkRatePillY < 0)
        {
            linkRatePillY = pillY;
        }
        require(std::abs(pillY - linkRatePillY) <= 2,
                "home link-rate direction and total pills share the second telemetry line");
        QLabel *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
        require(valueLabel != nullptr,
                "home link-rate pill has a value label");
        require(valueLabel->fontMetrics().horizontalAdvance(valueLabel->text()) <= valueLabel->width() + 1,
                "home link-rate value text fits its compact label");
        const int compactKbpsWidth = valueLabel->fontMetrics().horizontalAdvance(QStringLiteral("999.9 kbps"));
        const int oldMbpsReserveWidth = valueLabel->fontMetrics().horizontalAdvance(QStringLiteral("999.9 Mbps")) + 8;
        require(valueLabel->width() >= compactKbpsWidth,
                "home link-rate value label reserves room for 999.9 kbps");
        require(valueLabel->width() < oldMbpsReserveWidth,
                "home link-rate value label no longer reserves the wider 999.9 Mbps width");
        requireCompactTelemetryPillTextGap(
            pill,
            "home link-rate telemetry field/value gap remains compact");
    }
    QFrame *homeDataSection = homeTelemetrySections.at(2);
    QLabel *homeDataTitle =
        homeDataSection->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryTitleLabel"));
    require(homeDataTitle != nullptr &&
                (homeDataTitle->text() == QStringLiteral("数据") ||
                 homeDataTitle->text() == QStringLiteral("Data")),
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
        requireCompactTelemetryPillTextGap(
            pill,
            "home data availability field/value gap remains compact");
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
    require(VaporView::appThemeColor(VaporView::AppThemeColor::Focus, true) ==
                VaporView::appThemeColor(VaporView::AppThemeColor::Primary, true),
            "dark theme focus token uses the orange primary color");
    require(VaporView::appThemeColor(VaporView::AppThemeColor::SurfaceRaised, true) ==
                QColor(24, 24, 24),
            "dark top-level card background uses the requested rgb(24, 24, 24) surface");
    const QString darkOverviewStyleSheet = qApp->styleSheet();
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QGroupBox#sensorGroupBox[vaporViewTopLevelCard=\"true\"],"),
        QStringLiteral("background-color: %1").arg(
            VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceRaised, true)),
        "dark top-level cards use the shared raised surface background");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QGroupBox#sensorGroupBox[vaporViewTopLevelCard=\"true\"] > QWidget#sectionTitleBar,"),
        QStringLiteral("background-color: %1").arg(
            VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceRaised, true)),
        "dark top-level card title bars use the shared raised surface background");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QListView#logListView::item:selected {"),
        QStringLiteral("background-color: %1").arg(
            VaporView::appThemeColorName(VaporView::AppThemeColor::PrimarySubtlePressed, true)),
        "dark log selection uses the dark primary selection background");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QListView#logListView::item:selected {"),
        QStringLiteral("color: %1").arg(
            VaporView::appThemeColorName(VaporView::AppThemeColor::White, true)),
        "dark log selection keeps the text readable");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QListView#logListView::item:hover {"),
        QStringLiteral("background-color: %1").arg(
            VaporView::appThemeColorName(VaporView::AppThemeColor::TitleBarHover, true)),
        "dark log hover uses the neutral gray hover background");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QListView#logListView::item:selected:hover {"),
        QStringLiteral("background-color: %1").arg(
            VaporView::appThemeColorName(VaporView::AppThemeColor::PrimarySubtlePressed, true)),
        "dark selected log hover keeps the pressed selection background");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QMessageBox QLabel {"),
        QStringLiteral("color: %1").arg(VaporView::appThemeColorName(
            VaporView::AppThemeColor::Text, true)),
        "dark message-box body text uses the readable semantic text color");
    require(darkOverviewStyleSheet.contains(
                QStringLiteral("QAbstractSpinBox,\nQPlainTextEdit,\nQTextEdit {\n    background-color: %1")
                    .arg(VaporView::appThemeColorName(
                        VaporView::AppThemeColor::FieldBackground, true))),
            "dark theme keeps every editable control on the semantic field background");
    require(darkOverviewStyleSheet.contains(QStringLiteral("chevron-up-dark.svg")) &&
                darkOverviewStyleSheet.contains(QStringLiteral("chevron-down-dark.svg")) &&
                darkOverviewStyleSheet.contains(QStringLiteral("chevron-up-primary-dark.svg")) &&
                darkOverviewStyleSheet.contains(QStringLiteral("chevron-down-primary-dark.svg")),
            "dark theme arrow styles include white idle and orange highlighted assets");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QComboBox::down-arrow {"),
                                 QStringLiteral("chevron-down-dark.svg"),
                                 "dark theme combo chevron-down uses the white idle asset");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QComboBox::down-arrow:hover,"),
                                 QStringLiteral("chevron-down-primary-dark.svg"),
                                 "dark theme highlighted combo chevron-down uses the orange primary asset");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QAbstractSpinBox::up-arrow {"),
                                 QStringLiteral("chevron-up-dark.svg"),
                                 "dark theme spin chevron-up uses the white idle asset");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QAbstractSpinBox::down-arrow {"),
                                 QStringLiteral("chevron-down-dark.svg"),
                                 "dark theme spin chevron-down uses the white idle asset");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QComboBox#temperatureTitlePortCombo,"),
        QStringLiteral("color: %1").arg(VaporView::appThemeColorName(
            VaporView::AppThemeColor::TextStrong, true)),
        "dark temperature title serial selector uses white text");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QComboBox#temperatureTitlePortCombo:hover,"),
        QStringLiteral("background-color: %1").arg(VaporView::appThemeColorName(
            VaporView::AppThemeColor::TitleBarHover, true)),
        "dark temperature title serial selector uses the dark title hover background");
    require(VaporView::appThemeColor(VaporView::AppThemeColor::Primary, true) !=
                VaporView::appThemeColor(VaporView::AppThemeColor::PrimaryHover, true) &&
                VaporView::appThemeColor(VaporView::AppThemeColor::PrimaryHover, true) !=
                    VaporView::appThemeColor(VaporView::AppThemeColor::PrimaryPressed, true),
            "dark primary button state colors are visibly distinct");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QPushButton:hover {"),
        VaporView::appThemeColorName(VaporView::AppThemeColor::PrimaryHover, true),
        "dark primary buttons use the hover color while highlighted");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QPushButton:pressed,"),
        VaporView::appThemeColorName(VaporView::AppThemeColor::PrimaryPressed, true),
        "dark primary buttons use the pressed color while pressed or checked");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QToolBar QToolButton:hover {"),
        VaporView::appThemeColorName(VaporView::AppThemeColor::PrimaryHover, true),
        "dark primary toolbar buttons use the hover color");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QToolBar QToolButton:pressed,"),
        VaporView::appThemeColorName(VaporView::AppThemeColor::PrimaryPressed, true),
        "dark primary toolbar buttons use the pressed color while pressed or checked");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QPushButton#appSidebarButton:checked:hover,"),
        VaporView::appThemeColorName(VaporView::AppThemeColor::PrimaryHover, true),
        "dark selected sidebar buttons retain visible hover feedback");
    requireSpinArrowHoverUsesPrimary(true,
                                     "dark theme spin arrow hover renders the primary lucide icon");
    requireComboArrowUsesDarkIdleAndPrimaryHighlight(
        "dark theme combo arrow renders white at idle and orange when highlighted");
    requireComboDarkFocusBorderUsesPrimary(
        "dark theme combo focus border renders with the orange primary color");
    requireMenuPopupStyleUnified(darkOverviewStyleSheet,
                                 true,
                                 "dark popup menus use the shared menu hover and rounded panel style");
    requireSidebarCardStyle(darkOverviewStyleSheet, true,
                            "dark sidebar keeps the complete rounded card border");
    const QString darkHomeOverviewSplitterBackground =
        QStringLiteral("background-color: ") +
        VaporView::appThemeColorName(VaporView::AppThemeColor::Window, true);
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QSplitter#homeOverviewSplitter::handle:horizontal:hover {"),
        darkHomeOverviewSplitterBackground,
        "dark home overview splitter keeps its background while hovered");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QSplitter#homeOverviewSplitter::handle:horizontal:pressed {"),
        darkHomeOverviewSplitterBackground,
        "dark home overview splitter keeps its background while pressed");
    requireLastStyleRuleContains(
        qApp->styleSheet(),
        QStringLiteral("QScrollArea#mainCardsScrollArea QScrollBar:vertical {"),
        QStringLiteral("background-color: ") +
            VaporView::appThemeColorName(VaporView::AppThemeColor::Window, true),
        "dark main card scrollbar track blends into the adjacent window background");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QWidget#tcpWaveCardOutline {"),
                                 QStringLiteral("border: 1px solid %1")
                                     .arg(VaporView::appThemeColorName(
                                         VaporView::AppThemeColor::Border, true)),
                                 "dark TCP wave subcard outline uses the shared border color");
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
                                 QStringLiteral("QLabel#temperatureOverviewOutputPercentPill {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceAlt, true),
                                 "dark theme overrides temperature overview output percent pill background");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QFrame#temperatureOverviewOutputCapsule {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceAlt, true),
                                 "dark theme overrides temperature overview output capsule background");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QLabel#temperatureOverviewOutputLabel {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::TextStrong, true),
                                 "dark theme overrides temperature overview output label color");
    requireWidgetInteriorUsesBackground(
        temperatureOutputCapsule,
        VaporView::appThemeColor(VaporView::AppThemeColor::SurfaceAlt, true),
        "dark theme renders the temperature overview output capsule with the alternate surface");
    const QList<QWidget *> darkTemperatureTrendPlots =
        window.findChildren<QWidget *>(QStringLiteral("temperatureTrendPlot"));
    const auto visibleDarkTemperaturePlot = std::find_if(
        darkTemperatureTrendPlots.cbegin(),
        darkTemperatureTrendPlots.cend(),
        [](QWidget *plot) { return plot && plot->isVisible(); });
    require(visibleDarkTemperaturePlot != darkTemperatureTrendPlots.cend(),
            "a temperature trend plot is visible for dark theme rendering checks");
    requireWidgetInteriorUsesBackground(
        *visibleDarkTemperaturePlot,
        VaporView::appThemeColor(VaporView::AppThemeColor::SurfaceRaised, true),
        "dark temperature trend plot matches its raised card background");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QWidget#ai8TemperatureControllerPanel QFrame#ai8MainContentCard {"),
        QStringLiteral("background-color: %1").arg(
            VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceRaised, true)),
        "dark AI-8 common parameters and trend plot use the raised surface background");
    requireLastStyleRuleContains(
        darkOverviewStyleSheet,
        QStringLiteral("QWidget#ai8TemperatureControllerPanel QFrame#ai8DetailParametersCard {"),
        QStringLiteral("background-color: %1").arg(
            VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceRaised, true)),
        "dark AI-8 detail parameter cards use the raised surface background");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QToolButton#temperatureOverviewChannelButton[available=\"false\"] {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceAlt, true),
                                 "dark theme overrides unavailable temperature channel selector background");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QPushButton#appSidebarButton:hover,"),
                                 QStringLiteral("background-color: %1")
                                     .arg(VaporView::appThemeColorName(
                                         VaporView::AppThemeColor::TitleBarHover, true)),
                                 "dark sidebar hover uses the same neutral highlight as title-bar icons");
    requireLastStyleRuleContains(darkOverviewStyleSheet,
                                 QStringLiteral("QPushButton#appSidebarButton:hover,"),
                                 QStringLiteral("color: %1")
                                     .arg(VaporView::appThemeColorName(
                                         VaporView::AppThemeColor::Text, true)),
                                 "dark sidebar hover preserves the normal text color");
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
    validTemperatureData.channels[0].output_percent = 32.5;
    validTemperatureData.channels[0].output_enabled = true;
    validTemperatureData.channels[0].output_mode = 0;
    validTemperatureData.channels[0].max_output_percent = 70;
    validTemperatureData.channels[0].kp = 10;
    validTemperatureData.channels[0].ki = 20;
    validTemperatureData.channels[0].kd = 30;
    validTemperatureData.channels[0].auto_pid_mode = 0;
    validTemperatureData.channels[0].overtemp_upper_c = 500.12345;
    validTemperatureData.channels[0].overtemp_lower_c = -40.54321;
    validTemperatureData.channels[0].temperature_slope_c_per_s = 1.234;
    validTemperatureData.channels[0].startup_delay_s = 15;
    validTemperatureData.channels[0].sensor_resistance_ohm = 11948.4923;
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
    require(temperatureOutputPercentPill->textFormat() == Qt::RichText &&
                temperatureOutputPercentPill->text().contains(QStringLiteral("<br/>")) &&
                temperatureOutputPercentPill->text().contains(QStringLiteral("输出百分比")) &&
                temperatureOutputPercentPill->text().contains(QStringLiteral("32.50%")),
            "temperature overview output percent capsule reflects the selected channel telemetry on two lines");
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
    auto *temperaturePageForLayout = window.findChild<QWidget *>(QStringLiteral("temperaturePage"));
    require(temperaturePageForLayout != nullptr,
            "temperature page exists for controller layout checks");
    auto findTemperatureStatusLabel = [&window](const char *propertyName) -> QLabel * {
        for (QLabel *label : window.findChildren<QLabel *>())
        {
            if (label->property(propertyName).toBool())
            {
                return label;
            }
        }
        return nullptr;
    };
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
        findTemperatureStatusLabel("temperatureControllerRateValue");
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
    QLabel *autoPidLabel = nullptr;
    if (autoPidCombo && autoPidCombo->parentWidget())
    {
        autoPidLabel =
            autoPidCombo->parentWidget()->findChild<QLabel *>(QStringLiteral("fieldLabel"), Qt::FindDirectChildrenOnly);
    }
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
                kdSpin != nullptr && autoPidCombo != nullptr && autoPidLabel != nullptr &&
                addressSpin != nullptr && rs485BaudCombo != nullptr && overtempOutputCombo != nullptr &&
                commonInternalTemperatureEdit != nullptr && factoryResetButton != nullptr &&
                sensorModelSelector1 != nullptr && sensorModelBValueRadio != nullptr &&
                sensorModelPtRadio != nullptr && sensorModelShRadio != nullptr && sensorModelMf501Radio != nullptr,
            "temperature controller editable controls are discoverable for stale telemetry checks");
    require(controllerModeLabel->text() == QStringLiteral("温控器模式"),
            "RD105 controller mode label omits the trailing Chinese colon");
    for (QLabel *fieldLabel : temperaturePanel->findChildren<QLabel *>(QStringLiteral("fieldLabel")))
    {
        if (fieldLabel->wordWrap())
        {
            continue;
        }
        requireSelectableCardTitle(fieldLabel,
                                   "RD105 temperature-controller field names are selectable and copyable");
        require(fieldLabel->focusPolicy() == Qt::ClickFocus,
                "RD105 temperature-controller field names accept mouse focus for selection");
    }
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
    requireCardTitleMouseSelectionAndCopy(controllerModeLabel);
    require(temperaturePageForLayout != nullptr && temperaturePageForLayout->isVisible(),
            "temperature page is visible for controller layout checks");
    auto *temperatureTitleStatusStrip =
        temperaturePageForLayout->findChild<QWidget *>(QStringLiteral("temperatureTitleStatusStrip"));
    QLabel *internalTemperatureStatusLabel =
        findTemperatureStatusLabel("temperatureControllerInternalTemperatureTitle");
    QLabel *internalTemperatureStatusValue =
        findTemperatureStatusLabel("temperatureControllerInternalTemperatureValue");
    QLabel *errorCodeStatusLabel =
        findTemperatureStatusLabel("temperatureControllerErrorCodeTitle");
    QLabel *errorCodeStatusValue =
        findTemperatureStatusLabel("temperatureControllerErrorCodeValue");
    QLabel *pollingRateStatusLabel =
        findTemperatureStatusLabel("temperatureControllerRateTitle");
    QLabel *pollingRateStatusValue = temperatureStatusRateLabel;
    require(internalTemperatureStatusLabel != nullptr && internalTemperatureStatusValue != nullptr &&
                errorCodeStatusLabel != nullptr && errorCodeStatusValue != nullptr &&
                pollingRateStatusLabel != nullptr && pollingRateStatusValue != nullptr &&
                temperatureTitleStatusStrip != nullptr && temperatureTitleStatusStrip->isVisible(),
            "temperature, error, and polling-rate fields exist in the title-bar status strip");
    require((internalTemperatureStatusValue->alignment() & Qt::AlignHorizontal_Mask) == Qt::AlignLeft &&
                (errorCodeStatusValue->alignment() & Qt::AlignHorizontal_Mask) == Qt::AlignLeft &&
                (pollingRateStatusValue->alignment() & Qt::AlignHorizontal_Mask) == Qt::AlignLeft,
            "temperature title status values align toward their labels");
    require(internalTemperatureStatusLabel->width() < controllerModeLabel->width() &&
                errorCodeStatusLabel->width() < controllerModeLabel->width(),
            "temperature and error labels do not reserve controller-mode label width");
    auto leftInTitleStatus = [temperatureTitleStatusStrip](QWidget *widget) {
        return widget->mapTo(temperatureTitleStatusStrip, QPoint(0, 0)).x();
    };
    auto rightInTitleStatus = [temperatureTitleStatusStrip](QWidget *widget) {
        return widget->mapTo(temperatureTitleStatusStrip, QPoint(0, 0)).x() + widget->width();
    };
    require(internalTemperatureStatusValue->x() -
                    (internalTemperatureStatusLabel->x() + internalTemperatureStatusLabel->width()) <= 12 &&
                errorCodeStatusValue->x() -
                    (errorCodeStatusLabel->x() + errorCodeStatusLabel->width()) <= 12,
            "temperature and error values stay close to their labels");
    require(pollingRateStatusLabel->text() == QStringLiteral("轮询频率:") &&
                pollingRateStatusLabel->property("temperatureControllerRateTitle").toBool() &&
                pollingRateStatusValue == temperatureStatusRateLabel &&
                pollingRateStatusValue->property("temperatureControllerRateValue").toBool(),
            "temperature title status strip describes the Hz value as polling rate");
    require(leftInTitleStatus(pollingRateStatusLabel) -
                    rightInTitleStatus(errorCodeStatusValue) <= 16 &&
                pollingRateStatusValue->x() -
                    (pollingRateStatusLabel->x() + pollingRateStatusLabel->width()) <= 12,
            "polling-rate label and value follow the error code in the title bar");
    require(pollingRateStatusValue->fontMetrics().height() >
                errorCodeStatusValue->fontMetrics().height(),
            "temperature polling-rate value and Hz unit use a larger font");
    for (QLabel *titleStatusLabel : {internalTemperatureStatusLabel, errorCodeStatusLabel, pollingRateStatusLabel})
    {
        requireSelectableCardTitle(titleStatusLabel,
                                   "RD105 title-bar status field names remain selectable and copyable");
    }
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
    auto *temperatureTitlePortCombo =
        temperaturePageForLayout->findChild<QComboBox *>(QStringLiteral("temperatureTitlePortCombo"));
    require(temperatureControllerTitleLabel != nullptr &&
                !temperatureControllerTitleLabel->text().contains(expectedTemperaturePortText) &&
                !temperatureControllerTitleLabel->text().contains(QStringLiteral("RD105温控器")),
            "temperature controller title keeps the laser driver board name separate from the serial selector");
    require(temperatureTitlePortCombo != nullptr &&
                temperatureTitlePortCombo->currentText() == expectedTemperaturePortText &&
                !temperatureTitlePortCombo->isEditable() &&
                temperatureTitlePortCombo->property("usesSingleLevelPopupMenu").toBool() &&
                temperatureTitlePortCombo->cursor().shape() == Qt::PointingHandCursor &&
                temperatureTitlePortCombo->focusPolicy() == Qt::TabFocus,
            "temperature controller title exposes the selected serial port as a clickable keyboard-accessible selector");
    auto titlePortTextToArrowGap = [](QComboBox *combo) {
        QStyleOptionComboBox option;
        option.initFrom(combo);
        option.currentText = combo->currentText();
        option.editable = combo->isEditable();
        const QRect textRect = combo->style()->subControlRect(
            QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, combo);
        const QRect arrowRect = combo->style()->subControlRect(
            QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, combo);
        return arrowRect.left() -
            (textRect.left() + combo->fontMetrics().horizontalAdvance(combo->currentText()));
    };
    const int initialTitlePortChromeWidth = temperatureTitlePortCombo->width() -
        temperatureTitlePortCombo->fontMetrics().horizontalAdvance(temperatureTitlePortCombo->currentText());
    const int titlePortCharacterWidth = std::max(
        1, temperatureTitlePortCombo->fontMetrics().horizontalAdvance(QStringLiteral("0")));
    require(titlePortTextToArrowGap(temperatureTitlePortCombo) >= 0 &&
                titlePortTextToArrowGap(temperatureTitlePortCombo) <= titlePortCharacterWidth + 2,
            "temperature title serial selector keeps about one character between the port and chevron");
    const bool temperatureTitlePortDark =
        qApp->property(VaporView::kAppDarkThemeProperty).toBool();
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QComboBox#temperatureTitlePortCombo:hover,"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::TitleBarHover,
                                                              temperatureTitlePortDark),
                                 "temperature title serial selector changes its background on hover");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QComboBox#temperatureTitlePortCombo::down-arrow,"),
                                 temperatureTitlePortDark
                                     ? QStringLiteral("chevron-down-dark.svg")
                                     : QStringLiteral("chevron-down-primary.svg"),
                                 "temperature title serial selector shows the theme idle chevron-down icon");
    if (temperatureTitlePortDark)
    {
        requireLastStyleRuleContains(
            qApp->styleSheet(),
            QStringLiteral("QComboBox#temperatureTitlePortCombo::down-arrow:hover,"),
            QStringLiteral("chevron-down-primary-dark.svg"),
            "dark temperature title serial selector highlights its chevron in orange");
    }
    auto *temperatureTitlePortMenu =
        temperatureTitlePortCombo->findChild<VaporView::SingleLevelPopupMenu *>(
            QStringLiteral("singleLevelComboPopupMenu"));
    require(temperatureTitlePortMenu != nullptr,
            "temperature title serial selector owns the shared single-level popup menu");
    temperatureTitlePortCombo->showPopup();
    processEventsFor(120);
    const QList<VaporView::SingleLevelPopupMenuRow *> temperatureTitlePortRows =
        temperatureTitlePortMenu->rows();
    require(temperatureTitlePortMenu->isVisible() &&
                temperatureTitlePortRows.size() == temperatureTitlePortCombo->count() &&
                !temperatureTitlePortRows.isEmpty() &&
                temperatureTitlePortRows.first()->text() == temperatureTitlePortCombo->itemText(0) &&
                std::all_of(temperatureTitlePortRows.cbegin(),
                            temperatureTitlePortRows.cend(),
                            [](VaporView::SingleLevelPopupMenuRow *row) {
                                return row && !row->property("hasCheckIcon").toBool() &&
                                    row->checkLabel() && row->checkLabel()->width() == 0;
                            }),
            "temperature title serial selector opens its current port choices without a redundant check slot");
    temperatureTitlePortCombo->hidePopup();
    processEventsFor(40);
    auto *temperatureTitleActionButton =
        temperaturePageForLayout->findChild<QToolButton *>(QStringLiteral("temperatureTitleActionButton"));
    auto *temperatureTitleConnectButton =
        temperaturePageForLayout->findChild<QToolButton *>(QStringLiteral("temperatureTitleConnectButton"));
    auto *temperatureTitleDisconnectButton =
        temperaturePageForLayout->findChild<QToolButton *>(QStringLiteral("temperatureTitleDisconnectButton"));
    auto *temperatureTitleReconnectButton =
        temperaturePageForLayout->findChild<QToolButton *>(QStringLiteral("temperatureTitleReconnectButton"));
    auto *temperatureTitleLayout = temperatureTitlePortCombo
        ? qobject_cast<QHBoxLayout *>(temperatureTitlePortCombo->parentWidget()->layout())
        : nullptr;
    require(temperatureTitleActionButton != nullptr &&
                temperatureTitleConnectButton == nullptr &&
                temperatureTitleDisconnectButton == nullptr &&
                temperatureTitleReconnectButton == nullptr &&
                temperatureTitleLayout != nullptr &&
                temperatureTitleLayout->indexOf(temperatureTitleActionButton) ==
                    temperatureTitleLayout->indexOf(temperatureTitlePortCombo) + 1 &&
                temperatureTitleLayout->indexOf(temperatureTitleStatusStrip) ==
                    temperatureTitleLayout->indexOf(temperatureTitleActionButton) + 1 &&
                temperatureTitleActionButton->property("temperatureTitleAction").toBool() &&
                temperatureTitleActionButton->property("temperatureTitleCommand").toString() == QStringLiteral("connect") &&
                temperatureTitleActionButton->text().isEmpty() &&
                !temperatureTitleActionButton->icon().isNull() &&
                temperatureTitleActionButton->iconSize() == QSize(18, 18) &&
                temperatureTitleActionButton->size() == QSize(32, 32),
            "temperature controller title bar places status fields after the icon-only connection action");
    require(temperatureTitleActionButton->isEnabled(),
            "temperature controller title-bar state action is usable in local serial mode");
#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
    std::vector<VaporView::CommandId> temperatureTitleCommands;
    window.testSetLocalTemperatureCommandObserver([&temperatureTitleCommands](VaporView::CommandId command) {
        temperatureTitleCommands.push_back(command);
    });
    const bool temperatureActionWasEnabled = temperatureTitleActionButton->isEnabled();
    temperatureTitleActionButton->setEnabled(true);
    temperatureTitleActionButton->click();
    require(temperatureTitleCommands ==
                std::vector<VaporView::CommandId>{VaporView::CommandId::ConnectDevice},
            "temperature title state button dispatches the current local RD105 device command");
    window.testSetLocalTemperatureCommandObserver({});
    temperatureTitleActionButton->setEnabled(temperatureActionWasEnabled);
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
    auto *temperatureChannelCommonTopControls2 =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureChannelCommonTopControlsChannel2"));
    auto *temperatureControllerModeTopControls =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureControllerModeTopControls"));
    auto *temperatureChannelStack =
        temperaturePanel->findChild<QStackedWidget *>(QStringLiteral("temperatureChannelStack"));
    auto *temperatureControllerContentRow =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureControllerContentRow"));
    auto *temperatureControllerLeftConfigColumn =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureControllerLeftConfigColumn"));
    auto *temperatureControllerControlsCard =
        temperaturePanel->findChild<QFrame *>(QStringLiteral("temperatureControllerControlsCard"));
    auto *temperatureSubPageBarStack =
        temperaturePanel->findChild<QStackedWidget *>(QStringLiteral("temperatureSubPageBarStack"));
    auto *temperatureCommonSettingsPage =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureCommonSettingsPage"));
    auto *temperatureConfigChannelButton1 =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureChannelSelectorButton1"));
    auto *temperatureConfigChannelButton2 =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureChannelSelectorButton2"));
    auto *temperatureCommonSettingsButton =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureCommonSettingsButton"));
    auto *temperatureConfigPlotContainer =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureConfigPlotContainer"));
    auto *temperatureControllerModeField =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureControllerModeField"));
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
                temperatureChannelCommonTopControls2 != nullptr &&
                temperatureControllerModeTopControls != nullptr &&
                temperatureChannelStack != nullptr &&
                temperatureControllerContentRow != nullptr &&
                temperatureControllerLeftConfigColumn != nullptr &&
                temperatureControllerControlsCard != nullptr &&
                temperatureSubPageBarStack != nullptr &&
                temperatureCommonSettingsPage != nullptr &&
                temperatureConfigChannelButton1 != nullptr &&
                temperatureConfigChannelButton2 != nullptr &&
                temperatureCommonSettingsButton != nullptr &&
                temperatureConfigPlotContainer != nullptr &&
                temperatureControllerModeField != nullptr &&
                temperatureConfigPlot != nullptr,
            "temperature controller page exposes a top channel selector and a trend plot with inline mode controls");
    require(temperatureConfigPlot->sizePolicy().verticalPolicy() == QSizePolicy::Fixed &&
                temperatureControllerContentRow->sizePolicy().verticalPolicy() == QSizePolicy::Fixed,
            "temperature controller config plot and content row do not expand vertically with the page");
    require(temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureConfigTabs")) == nullptr,
            "temperature controller no longer uses the native tab widget that drew the gray base bar");
    require(temperatureChannelTopRow->parentWidget() == temperatureConfigCard &&
                temperatureChannelSelectorRow->parentWidget() == temperatureChannelTopRow &&
                temperatureChannelTopBar->parentWidget() == temperatureChannelSelectorRow &&
                temperatureChannelTopControlsStack->parentWidget() == temperatureChannelSelectorRow &&
                temperatureControllerContentRow->parentWidget() == temperatureConfigCard &&
                temperatureControllerLeftConfigColumn->parentWidget() == temperatureControllerContentRow &&
                temperatureSubPageBarStack->parentWidget() == temperatureControllerLeftConfigColumn &&
                temperatureControllerControlsCard->parentWidget() == temperatureControllerLeftConfigColumn &&
                temperatureChannelStack->parentWidget() == temperatureControllerControlsCard &&
                temperatureConfigPlotContainer->parentWidget() == temperatureControllerContentRow &&
                temperatureConfigPlot->parentWidget() == temperatureConfigPlotContainer &&
                temperatureControllerModeField->parentWidget() == temperatureChannelTopControlsStack->currentWidget(),
            "temperature card keeps the controller mode field in the shared top-controls row");
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
                temperatureConfigChannelButton1->focusPolicy() == Qt::TabFocus &&
                temperatureConfigChannelButton2->focusPolicy() == Qt::TabFocus &&
                temperatureCommonSettingsButton->focusPolicy() == Qt::TabFocus &&
                temperatureConfigChannelButton1->isChecked() &&
                !temperatureConfigChannelButton2->isChecked() &&
                !temperatureCommonSettingsButton->isChecked() &&
                temperatureChannelTopControlsStack->currentIndex() == 0 &&
                temperatureChannelStack->currentIndex() == 0,
            "temperature channel top bar defaults to channel 1 without mouse-click focus frames");
    require(temperaturePanel->minimumWidth() == 0 &&
                temperaturePanel->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored &&
                temperatureConfigCard->minimumWidth() == 0 &&
                temperatureConfigCard->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored,
            "temperature controller card width follows the available page width instead of the active page size hint");
    const QRect controllerModeFieldRect(
        temperatureControllerModeField->mapTo(temperatureChannelSelectorRow, QPoint(0, 0)),
        temperatureControllerModeField->size());
    const QRect topEnableFieldRect(
        enableSwitch->parentWidget()->mapTo(temperatureChannelSelectorRow, QPoint(0, 0)),
        enableSwitch->parentWidget()->size());
    const QRect autoPidFieldRectForMode(
        autoPidCombo->parentWidget()->mapTo(temperatureChannelSelectorRow, QPoint(0, 0)),
        autoPidCombo->parentWidget()->size());
    const QRect autoPidLabelRect(autoPidLabel->mapTo(autoPidCombo->parentWidget(), QPoint(0, 0)),
                                 autoPidLabel->size());
    const QRect autoPidComboRect(autoPidCombo->mapTo(autoPidCombo->parentWidget(), QPoint(0, 0)),
                                 autoPidCombo->size());
    const QRect controllerModeLabelRect(controllerModeLabel->mapTo(temperatureControllerModeField, QPoint(0, 0)),
                                        controllerModeLabel->size());
    const QRect controllerModeComboRect(controllerModeCombo->mapTo(temperatureControllerModeField, QPoint(0, 0)),
                                        controllerModeCombo->size());
    const QRect controllerModeComboRectInTopRow(
        controllerModeCombo->mapTo(temperatureChannelSelectorRow, QPoint(0, 0)),
        controllerModeCombo->size());
    int longestControllerModeTextWidth = 0;
    int longestControllerModeIndex = -1;
    for (int index = 0; index < controllerModeCombo->count(); ++index)
    {
        const int textWidth = controllerModeCombo->fontMetrics().horizontalAdvance(
            controllerModeCombo->itemText(index));
        if (textWidth > longestControllerModeTextWidth)
        {
            longestControllerModeTextWidth = textWidth;
            longestControllerModeIndex = index;
        }
    }
    const int initialControllerModeIndex = controllerModeCombo->currentIndex();
    controllerModeCombo->setCurrentIndex(longestControllerModeIndex);
    processEventsFor(20);
    QStyleOptionComboBox controllerModeComboOption;
    controllerModeComboOption.initFrom(controllerModeCombo);
    controllerModeComboOption.rect = controllerModeCombo->rect();
    controllerModeComboOption.currentText = controllerModeCombo->currentText();
    controllerModeComboOption.editable = controllerModeCombo->isEditable();
    controllerModeComboOption.frame = controllerModeCombo->hasFrame();
    const QRect controllerModeComboEditRect = controllerModeCombo->style()->subControlRect(
        QStyle::CC_ComboBox,
        &controllerModeComboOption,
        QStyle::SC_ComboBoxEditField,
        controllerModeCombo);
    require(std::abs(controllerModeFieldRect.top() - autoPidFieldRectForMode.top()) <= 2,
            "temperature controller mode field shares the top row with auto PID");
    const int enableToAutoGap = autoPidFieldRectForMode.left() - topEnableFieldRect.right() - 1;
    const int autoToControllerModeGap = controllerModeFieldRect.left() - autoPidFieldRectForMode.right() - 1;
    require(enableToAutoGap >= 0 && autoToControllerModeGap >= 0,
            "temperature top fields do not overlap when the mode selector keeps its full width");
    const QRect topBarRectForMode(
        temperatureChannelTopBar->mapTo(temperatureChannelSelectorRow, QPoint(0, 0)),
        temperatureChannelTopBar->size());
    const int commonTopBarToEnableGap = topEnableFieldRect.left() - topBarRectForMode.right() - 1;
    require(enableToAutoGap >= 0 && autoToControllerModeGap >= 0,
            "temperature top fields do not overlap when the mode selector keeps its full width");
    require(commonTopBarToEnableGap >= 0 &&
                std::abs(commonTopBarToEnableGap - enableToAutoGap) <= 1 &&
                std::abs(enableToAutoGap - autoToControllerModeGap) <= 1,
            "temperature common-page top controls use one evenly distributed gap");
    require(controllerModeLabelRect.right() < controllerModeComboRect.left(),
            "temperature controller mode label sits before the combo");
    const int autoPidLabelToComboGap = autoPidComboRect.left() - autoPidLabelRect.right() - 1;
    const int controllerModeLabelToComboGap = controllerModeComboRect.left() - controllerModeLabelRect.right() - 1;
    require(autoPidLabelToComboGap >= 4 && autoPidLabelToComboGap <= 8 &&
                std::abs(controllerModeLabelToComboGap - autoPidLabelToComboGap) <= 1,
            "temperature controller mode label keeps the same few-pixel gap as Auto PID");
    require(controllerModeComboRectInTopRow.right() <= controllerModeFieldRect.right() &&
                controllerModeComboRectInTopRow.right() < temperatureChannelSelectorRow->width(),
            "temperature controller mode combo stays inside its field and top row without right-edge clipping");
    require(std::abs(controllerModeLabelRect.center().y() - controllerModeComboRect.center().y()) <= 2,
            "temperature controller mode label and combo are vertically centered together");
    require(controllerModeComboEditRect.width() >= longestControllerModeTextWidth + 18,
            "temperature controller mode combo reserves text padding beyond its longest option");
    int longestAutoPidTextWidth = 0;
    int longestAutoPidIndex = -1;
    for (int index = 0; index < autoPidCombo->count(); ++index)
    {
        const int textWidth = autoPidCombo->fontMetrics().horizontalAdvance(
            autoPidCombo->itemText(index));
        if (textWidth > longestAutoPidTextWidth)
        {
            longestAutoPidTextWidth = textWidth;
            longestAutoPidIndex = index;
        }
    }
    QStyleOptionComboBox autoPidComboOption;
    autoPidComboOption.initFrom(autoPidCombo);
    autoPidComboOption.rect = autoPidCombo->rect();
    autoPidComboOption.currentText = autoPidCombo->itemText(longestAutoPidIndex);
    autoPidComboOption.editable = autoPidCombo->isEditable();
    autoPidComboOption.frame = autoPidCombo->hasFrame();
    const QRect autoPidComboEditRect = autoPidCombo->style()->subControlRect(
        QStyle::CC_ComboBox,
        &autoPidComboOption,
        QStyle::SC_ComboBoxEditField,
        autoPidCombo);
    require(autoPidComboEditRect.width() >= longestAutoPidTextWidth + 36,
            "temperature auto PID combo keeps its longest option fully visible");
    controllerModeCombo->setCurrentIndex(initialControllerModeIndex);
    require(controllerModeFieldRect.right() >= temperatureChannelSelectorRow->width() - 1,
            "temperature controller mode field and combo are right aligned in the shared top row");
    const QRect topRowRectInCard(temperatureChannelTopRow->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                 temperatureChannelTopRow->size());
    const QRect selectorRowRectInTopRow(temperatureChannelSelectorRow->mapTo(temperatureChannelTopRow, QPoint(0, 0)),
                                        temperatureChannelSelectorRow->size());
    const QRect topBarRectInRow(temperatureChannelTopBar->mapTo(temperatureChannelTopRow, QPoint(0, 0)),
                                temperatureChannelTopBar->size());
    const QRect stackRectInCard(temperatureChannelStack->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                 temperatureChannelStack->size());
    const QRect stackRectInControlsCard(
        temperatureChannelStack->mapTo(temperatureControllerControlsCard, QPoint(0, 0)),
        temperatureChannelStack->size());
    auto *temperatureControllerLeftConfigColumnLayout =
        qobject_cast<QVBoxLayout *>(temperatureControllerLeftConfigColumn->layout());
    auto *temperatureControllerControlsCardLayout =
        qobject_cast<QVBoxLayout *>(temperatureControllerControlsCard->layout());
    require(temperatureControllerControlsCard->width() <= 280 &&
                temperatureControllerLeftConfigColumnLayout != nullptr &&
                temperatureControllerLeftConfigColumnLayout->spacing() == 6 &&
                temperatureControllerControlsCardLayout != nullptr &&
                temperatureChannelStack->frameShape() == QFrame::NoFrame &&
                stackRectInControlsCard.left() <= 8 &&
                temperatureControllerControlsCard->width() - stackRectInControlsCard.right() - 1 <= 8,
            "temperature parameter card removes the stacked-page frame and trims the outer horizontal padding");
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
                temperatureConfigChannelButton1->height() == 30 &&
                temperatureConfigChannelButton2->height() == 30 &&
                temperatureCommonSettingsButton->height() == 30,
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
                temperatureChannelStack->isAncestorOf(modeCombo) &&
                temperatureChannelStack->isAncestorOf(targetSpin) &&
                !temperatureChannelTopControlsStack->isAncestorOf(modeCombo) &&
                !temperatureChannelTopControlsStack->isAncestorOf(targetSpin) &&
                !temperatureChannelTopBar->isAncestorOf(modeCombo) &&
                !temperatureChannelTopBar->isAncestorOf(targetSpin),
            "temperature output enable stays beside the selector while mode and target move into common parameters");
    const QRect topCommonRect(temperatureCommonSettingsButton->mapTo(temperatureChannelTopRow, QPoint(0, 0)),
                              temperatureCommonSettingsButton->size());
    require(topCommonRect.right() < stackRectInCard.right(),
            "temperature common settings selector stays inside the compact top bar");
    require(temperatureChannelStack->isAncestorOf(factoryResetButton) &&
                !temperatureChannelSelectorRow->isAncestorOf(factoryResetButton) &&
                !temperatureChannelTopBar->isAncestorOf(factoryResetButton) &&
                !factoryResetButton->isVisible() &&
                !factoryResetButton->icon().isNull(),
            "temperature factory reset button belongs to the common settings page and starts hidden");
    const QColor plotBackgroundColor = VaporView::appThemeColor(VaporView::AppThemeColor::SurfaceRaised, false);
    const QImage plotSnapshot = temperatureConfigPlot->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QImage configCardSnapshot = temperatureConfigCard->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QImage controlsCardSnapshot =
        temperatureControllerControlsCard->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    require(!plotSnapshot.isNull() && !configCardSnapshot.isNull() && !controlsCardSnapshot.isNull() &&
                plotSnapshot.pixelColor(0, 0) == plotBackgroundColor &&
                configCardSnapshot.pixelColor(4, configCardSnapshot.height() / 2) == plotBackgroundColor &&
                controlsCardSnapshot.pixelColor(4, controlsCardSnapshot.height() / 2) == plotBackgroundColor,
            "temperature cards use the same raised white background as the trend plot");
    require(enableSwitch->height() == 34 &&
                enableSwitch->width() == 106 &&
                enableSwitch2->height() == 34 &&
                enableSwitch2->width() == 106 &&
                enableSwitch->property("segmentedSwitchControl").toBool() &&
                enableSwitch2->property("segmentedSwitchControl").toBool() &&
                enableSwitch->focusPolicy() == Qt::TabFocus &&
                enableSwitch2->focusPolicy() == Qt::TabFocus,
            "temperature output enable uses the shared keyboard-accessible segmented switch");
    const int channel1StackHeight = temperatureChannelStack->height();
    const int channel1TopRowHeight = temperatureChannelTopRow->height();
    const int channel1ConfigCardHeight = temperatureConfigCard->height();
    clickWidget(temperatureConfigChannelButton2, 150);
    activateLayouts(&window);
    require(temperatureChannelTopControlsStack->currentIndex() == 1 &&
                temperatureChannelTopControlsStack->isVisible() &&
                temperatureControllerModeField->parentWidget() == temperatureChannelTopControlsStack->currentWidget() &&
                temperatureChannelStack->currentIndex() == 1 &&
                temperatureSubPageBarStack->currentIndex() == 1 &&
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
                temperatureChannelTopControlsStack->currentWidget() == temperatureControllerModeTopControls &&
                temperatureControllerModeField->parentWidget() == temperatureControllerModeTopControls &&
                temperatureChannelStack->currentIndex() == 2 &&
                temperatureSubPageBarStack->currentIndex() == 2 &&
                std::abs(commonStackHeight - channel1StackHeight) <= 1 &&
                std::abs(temperatureChannelTopRow->height() - channel1TopRowHeight) <= 1 &&
                std::abs(temperatureConfigCard->height() - channel1ConfigCardHeight) <= 1 &&
                factoryResetButton->isVisible() &&
                !temperatureConfigChannelButton1->isChecked() &&
                !temperatureConfigChannelButton2->isChecked() &&
                temperatureCommonSettingsButton->isChecked(),
            "temperature top bar switches to a compact three-column common settings page");
    const QRect modeOnlyControllerModeFieldRect(
        temperatureControllerModeField->mapTo(temperatureControllerModeTopControls, QPoint(0, 0)),
        temperatureControllerModeField->size());
    require(modeOnlyControllerModeFieldRect.right() >= temperatureControllerModeTopControls->width() - 1,
            "temperature controller mode field stays right aligned on the mode-only top row");
    require(std::abs(temperatureChannelTopBar->mapTo(temperatureConfigCard, QPoint(0, 0)).x() -
                     temperatureControllerContentRow->mapTo(temperatureConfigCard, QPoint(0, 0)).x()) <= 1,
            "temperature top navigation stays aligned with the left control card on the common settings page");
    auto *commonSettingsFields =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureCommonSettingsFields"));
    auto *commonSettingsGrid =
        commonSettingsFields ? qobject_cast<QGridLayout *>(commonSettingsFields->layout()) : nullptr;
    require(commonSettingsFields != nullptr &&
                commonSettingsGrid != nullptr &&
                commonSettingsGrid->horizontalSpacing() == 6 &&
                commonSettingsGrid->verticalSpacing() == 10 &&
                commonSettingsGrid->itemAtPosition(0, 0) != nullptr &&
                commonSettingsGrid->itemAtPosition(0, 0)->widget() == addressSpin->parentWidget() &&
                commonSettingsGrid->itemAtPosition(0, 1) != nullptr &&
                commonSettingsGrid->itemAtPosition(0, 1)->widget() == rs485BaudCombo->parentWidget() &&
                commonSettingsGrid->itemAtPosition(1, 0) != nullptr &&
                commonSettingsGrid->itemAtPosition(1, 0)->widget() == overtempOutputCombo->parentWidget() &&
                commonSettingsGrid->itemAtPosition(1, 1) != nullptr &&
                commonSettingsGrid->itemAtPosition(1, 1)->widget() == commonInternalTemperatureEdit->parentWidget(),
            "temperature common settings use a compact two-column stacked form");
    QWidget *commonSettingsPage = commonSettingsFields->parentWidget();
    auto *commonSettingsPageLayout = qobject_cast<QVBoxLayout *>(commonSettingsPage->layout());
    const QRect commonSettingsFieldsRectInPage(
        commonSettingsFields->mapTo(commonSettingsPage, QPoint(0, 0)), commonSettingsFields->size());
    require(commonSettingsPageLayout != nullptr &&
                commonSettingsPageLayout->contentsMargins() == QMargins(0, 0, 0, 6) &&
                addressSpin->width() == 130 &&
                rs485BaudCombo->width() == 130 &&
                overtempOutputCombo->width() == 130 &&
                commonInternalTemperatureEdit->width() == 130 &&
                commonSettingsFields->width() ==
                    addressSpin->width() * 2 + commonSettingsGrid->horizontalSpacing() &&
                commonSettingsFieldsRectInPage.left() == commonSettingsPageLayout->contentsMargins().left() &&
                commonSettingsPage->width() - 1 - commonSettingsFieldsRectInPage.right() ==
                    commonSettingsPageLayout->contentsMargins().right(),
            "temperature common settings leave the shared 6px inset to the outer parameter card");
    const QMargins commonSettingsPageMargins = commonSettingsPageLayout->contentsMargins();
    auto *selectedChannelCommonParamsPage = temperaturePanel->findChild<QWidget *>(
        QStringLiteral("temperatureChannelCommonParamsPageChannel2"));
    auto *selectedChannelCommonParamsGrid = selectedChannelCommonParamsPage
        ? qobject_cast<QGridLayout *>(selectedChannelCommonParamsPage->layout())
        : nullptr;
    const QRect selectorBarRectInCard(temperatureChannelTopBar->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                      temperatureChannelTopBar->size());
    const QRect commonAddressRowRect(addressSpin->parentWidget()->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                     addressSpin->parentWidget()->size());
    const QRect commonBaudRowRect(rs485BaudCombo->parentWidget()->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                  rs485BaudCombo->parentWidget()->size());
    const QRect commonOvertempRowRect(overtempOutputCombo->parentWidget()->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                      overtempOutputCombo->parentWidget()->size());
    const QRect commonInternalRowRect(commonInternalTemperatureEdit->parentWidget()->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                      commonInternalTemperatureEdit->parentWidget()->size());
    const QRect factoryResetRectInCard(factoryResetButton->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                       factoryResetButton->size());
    const QRect factoryResetRectInPage(factoryResetButton->mapTo(commonSettingsPage, QPoint(0, 0)),
                                       factoryResetButton->size());
    const QRect commonBaudComboRectInCard(rs485BaudCombo->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                          rs485BaudCombo->size());
    const QRect commonAddressInputRectInCard(addressSpin->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                             addressSpin->size());
    const QRect commonOvertempInputRectInCard(overtempOutputCombo->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                              overtempOutputCombo->size());
    const QRect commonInternalInputRectInCard(commonInternalTemperatureEdit->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                              commonInternalTemperatureEdit->size());
    auto *commonSettingsSubTopBar =
        temperaturePanel->findChild<QFrame *>(QStringLiteral("temperatureCommonSettingsSubTopBar"));
    auto *commonSettingsCommonParamsButton =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureCommonSettingsCommonParamsButton"));
    auto *commonSettingsAdvancedParamsButton =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureCommonSettingsAdvancedParamsButton"));
    auto *commonSettingsSensorConfigButton =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureCommonSettingsSensorConfigButton"));
    require(commonSettingsSubTopBar != nullptr &&
                commonSettingsCommonParamsButton != nullptr &&
                commonSettingsAdvancedParamsButton != nullptr &&
                commonSettingsSensorConfigButton != nullptr &&
                temperatureSubPageBarStack->isAncestorOf(commonSettingsSubTopBar) &&
                !temperatureControllerControlsCard->isAncestorOf(commonSettingsSubTopBar) &&
                !temperatureChannelStack->isAncestorOf(commonSettingsSubTopBar) &&
                commonSettingsCommonParamsButton->isChecked() &&
                commonSettingsCommonParamsButton->property("temperatureChannelSubSelector").toBool() &&
                commonSettingsAdvancedParamsButton->property("temperatureChannelSubSelector").toBool() &&
                commonSettingsSensorConfigButton->property("temperatureChannelSubSelector").toBool(),
            "temperature common settings page keeps the lower common/professional/sensor selector outside the left card");
    const QRect commonSubBarRectInCard(commonSettingsSubTopBar->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                       commonSettingsSubTopBar->size());
    const QRect controlsCardRectInCard(
        temperatureControllerControlsCard->mapTo(temperatureConfigCard, QPoint(0, 0)),
        temperatureControllerControlsCard->size());
    const QRect contentRowRectInCard(temperatureControllerContentRow->mapTo(temperatureConfigCard, QPoint(0, 0)),
                                     temperatureControllerContentRow->size());
    require(commonAddressRowRect.top() > selectorBarRectInCard.bottom() &&
                commonAddressRowRect.right() < commonBaudRowRect.left() &&
                commonBaudRowRect.left() - commonAddressRowRect.right() - 1 <= 8 &&
                std::abs(commonAddressRowRect.center().y() - commonBaudRowRect.center().y()) <= 2 &&
                commonOvertempRowRect.top() > commonAddressRowRect.bottom() &&
                commonOvertempRowRect.right() < commonInternalRowRect.left() &&
                commonInternalRowRect.left() - commonOvertempRowRect.right() - 1 <= 8 &&
                std::abs(commonOvertempRowRect.center().y() - commonInternalRowRect.center().y()) <= 2 &&
                factoryResetRectInCard.top() > commonOvertempRowRect.bottom() &&
                commonSubBarRectInCard.top() > controlsCardRectInCard.bottom() &&
                commonSubBarRectInCard.top() - controlsCardRectInCard.bottom() - 1 ==
                    commonSettingsGrid->horizontalSpacing() &&
                std::abs(commonAddressRowRect.left() - commonOvertempRowRect.left()) <= 2 &&
                factoryResetRectInPage.left() ==
                    commonSettingsPageMargins.left() +
                        (commonSettingsPage->width() - commonSettingsPageMargins.left() -
                         commonSettingsPageMargins.right() - factoryResetButton->width()) /
                            2 &&
                std::abs(commonSubBarRectInCard.left() - contentRowRectInCard.left()) <= 2,
            "temperature common settings center the factory reset action and keep the lower-tab row outside the parameter card");
    require(std::abs(commonAddressInputRectInCard.left() - commonOvertempInputRectInCard.left()) <= 1 &&
                std::abs(commonBaudComboRectInCard.left() - commonInternalInputRectInCard.left()) <= 1 &&
                commonBaudComboRectInCard.left() - commonAddressInputRectInCard.right() - 1 <= 8 &&
                commonInternalInputRectInCard.left() - commonOvertempInputRectInCard.right() - 1 <= 8 &&
                addressSpin->width() == overtempOutputCombo->width() &&
                rs485BaudCombo->width() == commonInternalTemperatureEdit->width(),
            "temperature common settings align equal-width field editors with compact adjacent gaps");
    require(selectedChannelCommonParamsPage != nullptr && selectedChannelCommonParamsGrid != nullptr,
            "temperature channel common parameters remain available after switching into common settings");
    const int controlsCardLeftInCard =
        temperatureControllerControlsCard->mapTo(temperatureConfigCard, QPoint(0, 0)).x();
    const int commonBaudRightInset =
        controlsCardLeftInCard + temperatureControllerControlsCard->width() - 2 - commonBaudComboRectInCard.right();
    require(commonBaudRightInset == 6,
            "temperature common RS485 baud combo keeps the shared right inset inside the left control card");
    auto *overtempOutputMenu = overtempOutputCombo->findChild<VaporView::SingleLevelPopupMenu *>(
        QStringLiteral("singleLevelComboPopupMenu"));
    require(overtempOutputMenu != nullptr,
            "temperature over-temperature selector owns the shared popup menu");
    overtempOutputCombo->showPopup();
    processEventsFor(120);
    const int overtempPopupShadowMargin = overtempOutputMenu->property("shadowMargin").toInt();
    require(overtempOutputMenu->isVisible() &&
                overtempPopupShadowMargin == 22 &&
                overtempOutputMenu->property("shadowBottomMargin").toInt() == 50 &&
                overtempOutputMenu->contentsMargins().bottom() == 50 &&
                overtempOutputMenu->width() - overtempPopupShadowMargin * 2 ==
                    overtempOutputCombo->width(),
            "default single-level combo popups preserve 50px bottom shadow space without widening the trigger");
    overtempOutputCombo->hidePopup();
    processEventsFor(40);
    const QList<QLabel*> overtempLabels =
        overtempOutputCombo->parentWidget()->findChildren<QLabel *>(QStringLiteral("fieldLabel"), Qt::FindDirectChildrenOnly);
    require(!overtempLabels.isEmpty() &&
                overtempLabels.first()->property("temperatureOvertempWarning").toBool() &&
                overtempLabels.first()->palette().color(QPalette::WindowText) ==
                    VaporView::appThemeColor(VaporView::AppThemeColor::Danger, false),
            "temperature over-temperature output label is rendered in danger red");
    const QRect controlsCardRectInContent(
        temperatureControllerControlsCard->mapTo(temperatureControllerContentRow, QPoint(0, 0)),
        temperatureControllerControlsCard->size());
    const QRect plotRectInContent(temperatureConfigPlot->mapTo(temperatureControllerContentRow, QPoint(0, 0)),
                                  temperatureConfigPlot->size());
    const QMargins controlsCardMargins = temperatureControllerControlsCardLayout->contentsMargins();
    const int controlsCardBorderHeight =
        controlsCardRectInContent.height() -
        controlsCardMargins.top() -
        controlsCardMargins.bottom() -
        temperatureChannelStack->height();
    require(controlsCardRectInContent.right() < plotRectInContent.left() &&
                std::abs(controlsCardRectInContent.top() - plotRectInContent.top()) <= 2 &&
                plotRectInContent.right() <= temperatureControllerContentRow->rect().right() &&
                controlsCardRectInContent.height() < plotRectInContent.height() &&
                plotRectInContent.height() == 268 &&
                temperatureControllerLeftConfigColumn->height() == plotRectInContent.height() &&
                temperatureControllerContentRow->height() == plotRectInContent.height() &&
                controlsCardBorderHeight >= 0 &&
                controlsCardBorderHeight <= 2,
            "temperature common settings keep the lowered trend plot flush with the compact left configuration column");
    clickWidget(temperatureConfigChannelButton1, 150);
    activateLayouts(&window);
    require(temperatureChannelTopControlsStack->currentIndex() == 0 &&
                temperatureChannelTopControlsStack->isVisible() &&
                temperatureChannelStack->currentIndex() == 0 &&
                temperatureSubPageBarStack->currentIndex() == 0 &&
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
    const QRect controlsCardRectAfterChannelSwitch(
        temperatureControllerControlsCard->mapTo(temperatureControllerContentRow, QPoint(0, 0)),
        temperatureControllerControlsCard->size());
    const QRect plotRectAfterChannelSwitch(
        temperatureConfigPlot->mapTo(temperatureControllerContentRow, QPoint(0, 0)),
        temperatureConfigPlot->size());
    require(controlsCardRectAfterChannelSwitch.right() < plotRectAfterChannelSwitch.left(),
            "temperature trend plot is laid out to the right of the channel configuration card");
    require(std::abs(controlsCardRectAfterChannelSwitch.top() - plotRectAfterChannelSwitch.top()) <= 2 &&
                plotRectAfterChannelSwitch.right() <= temperatureControllerContentRow->rect().right() &&
                controlsCardRectAfterChannelSwitch.height() < plotRectAfterChannelSwitch.height() &&
                plotRectAfterChannelSwitch.height() == 268 &&
                temperatureControllerLeftConfigColumn->height() == plotRectAfterChannelSwitch.height(),
            "temperature trend plot follows the compact side-by-side layout with the selector below the control card");
    require(plotRectAfterChannelSwitch.width() > 0 &&
                plotRectAfterChannelSwitch.right() >= temperatureControllerContentRow->rect().right() - 1,
            "temperature trend plot expands to the right edge of the remaining controller panel width");
    require(controlsCardRectAfterChannelSwitch.width() <= 280 &&
                plotRectAfterChannelSwitch.width() > controlsCardRectAfterChannelSwitch.width(),
            "temperature controller gives the narrowed parameter card less width than the trend plot");
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
                                 QStringLiteral("TemperatureControllerPanel QPushButton[temperatureChannelSelector=\"true\"] {"),
                                 QStringLiteral("outline: none"),
                                 "temperature channel top bar buttons suppress native dotted focus outlines");
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
                                 QStringLiteral("TemperatureControllerPanel QPushButton[temperatureChannelSubSelector=\"true\"] {"),
                                 QStringLiteral("outline: none"),
                                 "temperature lower parameter selector buttons suppress native dotted focus outlines");
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
                                 QStringLiteral("QPushButton#appSidebarButton:hover,"),
                                 QStringLiteral("background-color: %1")
                                     .arg(VaporView::appThemeColorName(
                                         VaporView::AppThemeColor::TitleBarHover, false)),
                                 "sidebar button hover uses the same neutral highlight as title-bar icons");
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QPushButton#appSidebarButton:hover,"),
                                 QStringLiteral("color: %1")
                                     .arg(VaporView::appThemeColorName(
                                         VaporView::AppThemeColor::Text, false)),
                                 "sidebar button hover preserves the normal text color");
    require(VaporView::appThemeColor(VaporView::AppThemeColor::ScrollbarHandle, false) ==
                    QColor(226, 226, 226) &&
                VaporView::appThemeColor(VaporView::AppThemeColor::ScrollbarHandle, true) ==
                    QColor(39, 39, 39) &&
                VaporView::appThemeColor(VaporView::AppThemeColor::ScrollbarHandleHover, false) ==
                    QColor(87, 89, 90) &&
                VaporView::appThemeColor(VaporView::AppThemeColor::ScrollbarHandleHover, true) ==
                    QColor(190, 190, 191),
            "dark theme scrollbar handles use the requested dim default and light hover colors");
    for (const QString& arrowFile : {QStringLiteral("combo_arrow_up.xpm"),
                                     QStringLiteral("combo_arrow_down.xpm")})
    {
        const QImage arrowImage(
            QCoreApplication::applicationDirPath() + QStringLiteral("/resources/") + arrowFile);
        require(!arrowImage.isNull(), "scrollbar arrow image can be loaded");
        bool hasVisiblePixel = false;
        for (int y = 0; y < arrowImage.height(); ++y)
        {
            for (int x = 0; x < arrowImage.width(); ++x)
            {
                const QColor pixel = arrowImage.pixelColor(x, y);
                if (pixel.alpha() > 0)
                {
                    hasVisiblePixel = true;
                    require(pixel == QColor(217, 217, 218),
                            "scrollbar arrow triangle uses the requested color");
                }
            }
        }
        require(hasVisiblePixel, "scrollbar arrow image contains a visible triangle");
    }
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("TemperatureControllerPanel QPushButton#temperatureFactoryResetButton {"),
                                 VaporView::appThemeColorName(VaporView::AppThemeColor::ToolbarRed, false),
                                 "temperature factory reset button uses the vivid red toolbar token");
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
    auto *temperatureChannelAdvancedParamsButton2 =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureChannelAdvancedParamsButton2"));
    auto *temperatureChannelCommonParamsButton2 =
        temperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureChannelCommonParamsButton2"));
    auto *temperatureChannelConfigSubStack2 =
        temperaturePanel->findChild<QStackedWidget *>(QStringLiteral("temperatureChannelConfigSubStackChannel2"));
    auto *temperatureChannelSubTopBar =
        qobject_cast<QFrame *>(temperatureChannelCommonParamsButton->parentWidget());
    require(temperatureChannelCommonParamsButton != nullptr &&
                temperatureChannelAdvancedParamsButton != nullptr &&
                temperatureChannelSensorConfigButton != nullptr &&
                temperatureChannelConfigSubStack != nullptr &&
                temperatureChannelAdvancedParamsButton2 != nullptr &&
                temperatureChannelCommonParamsButton2 != nullptr &&
                temperatureChannelConfigSubStack2 != nullptr &&
                temperatureChannelSubTopBar != nullptr,
            "temperature channel exposes lower common, advanced, and sensor config tabs");
    require(temperatureChannelCommonParamsButton->focusPolicy() == Qt::TabFocus &&
                temperatureChannelAdvancedParamsButton->focusPolicy() == Qt::TabFocus &&
                temperatureChannelSensorConfigButton->focusPolicy() == Qt::TabFocus,
            "temperature lower parameter tabs keep keyboard tab focus without mouse-click focus frames");
    auto lowerTabHasTextPadding = [](QPushButton *button) {
        return button != nullptr &&
            button->width() >= button->fontMetrics().horizontalAdvance(button->text()) + 28;
    };
    require(lowerTabHasTextPadding(temperatureChannelCommonParamsButton) &&
                lowerTabHasTextPadding(temperatureChannelAdvancedParamsButton) &&
                lowerTabHasTextPadding(temperatureChannelSensorConfigButton),
            "temperature lower parameter tabs reserve horizontal padding for their full labels");
    auto *temperatureChannelSubTopBarLayout =
        qobject_cast<QHBoxLayout *>(temperatureChannelSubTopBar->layout());
    require(temperatureChannelSubTopBarLayout != nullptr &&
                temperatureChannelSubTopBarLayout->contentsMargins() == QMargins(2, 3, 2, 3) &&
                temperatureChannelSubTopBarLayout->spacing() == 2,
            "temperature lower parameter tabs reserve text width without excess outer gaps");
    std::array<QGridLayout *, 3> channelContentGrids{};
    for (int pageIndex = 0; pageIndex < temperatureChannelConfigSubStack->count(); ++pageIndex)
    {
        channelContentGrids[static_cast<size_t>(pageIndex)] =
            qobject_cast<QGridLayout *>(temperatureChannelConfigSubStack->widget(pageIndex)->layout());
    }
    std::array<QGridLayout *, 3> channel2ContentGrids{};
    for (int pageIndex = 0; pageIndex < temperatureChannelConfigSubStack2->count(); ++pageIndex)
    {
        channel2ContentGrids[static_cast<size_t>(pageIndex)] =
            qobject_cast<QGridLayout *>(temperatureChannelConfigSubStack2->widget(pageIndex)->layout());
    }
    require(channelContentGrids[0] != nullptr &&
                channelContentGrids[1] != nullptr &&
                channelContentGrids[2] != nullptr &&
                channel2ContentGrids[0] != nullptr &&
                channel2ContentGrids[1] != nullptr &&
                channel2ContentGrids[2] != nullptr,
            "temperature channel sub-pages share grid-based screenshot layouts on both channels");
    require(channelContentGrids[0]->alignment() == (Qt::AlignTop | Qt::AlignLeft) &&
                channelContentGrids[1]->alignment() == (Qt::AlignTop | Qt::AlignLeft) &&
                channelContentGrids[2]->alignment() == (Qt::AlignTop | Qt::AlignLeft) &&
                channel2ContentGrids[0]->alignment() == (Qt::AlignTop | Qt::AlignLeft) &&
                channel2ContentGrids[1]->alignment() == (Qt::AlignTop | Qt::AlignLeft) &&
                channel2ContentGrids[2]->alignment() == (Qt::AlignTop | Qt::AlignLeft),
            "temperature channel sub-pages top-align their stacked form controls on both channels");
    require(channelContentGrids[0]->verticalSpacing() == 10 &&
                channelContentGrids[1]->verticalSpacing() == 10 &&
                channelContentGrids[2]->verticalSpacing() == 10 &&
                channel2ContentGrids[0]->verticalSpacing() == 10 &&
                channel2ContentGrids[1]->verticalSpacing() == 10 &&
                channel2ContentGrids[2]->verticalSpacing() == 10,
            "temperature channel sub-pages share the sensor-config vertical rhythm on both channels");
    require(channelContentGrids[0]->contentsMargins() == QMargins(0, 0, 0, 6) &&
                channelContentGrids[1]->contentsMargins() == QMargins(0, 0, 0, 6) &&
                channelContentGrids[2]->contentsMargins() == QMargins(0, 0, 0, 6) &&
                channel2ContentGrids[0]->contentsMargins() == QMargins(0, 0, 0, 6) &&
                channel2ContentGrids[1]->contentsMargins() == QMargins(0, 0, 0, 6) &&
                channel2ContentGrids[2]->contentsMargins() == QMargins(0, 0, 0, 6),
            "temperature channel sub-pages leave the shared horizontal inset to the parameter card on both channels");
    require(temperatureChannelConfigSubStack->minimumHeight() == temperatureChannelConfigSubStack->maximumHeight() &&
                temperatureChannelConfigSubStack->height() == temperatureChannelConfigSubStack->minimumHeight() &&
                temperatureChannelConfigSubStack->height() >=
                    temperatureChannelConfigSubStack->currentWidget()->sizeHint().height(),
            "temperature channel content stack reserves stable height for the tallest screenshot page");
    auto *temperatureConfigCardLayout = qobject_cast<QVBoxLayout *>(temperatureConfigCard->layout());
    auto *temperatureChannelPageLayout =
        qobject_cast<QVBoxLayout *>(temperatureChannelStack->currentWidget()->layout());
    require(temperatureConfigCardLayout != nullptr &&
                temperatureConfigCardLayout->spacing() == 6 &&
                temperatureConfigCardLayout->contentsMargins().bottom() == 6 &&
                contentRowRectInCard.top() - topRowRectInCard.bottom() - 1 ==
                    temperatureConfigCardLayout->spacing() &&
                temperatureChannelPageLayout != nullptr &&
                temperatureChannelPageLayout->count() == 1,
            "temperature screenshot layout keeps the upper selector and lower card at the shared compact spacing");
    require(temperatureChannelTopBar->height() == temperatureChannelSubTopBar->height() &&
                temperatureConfigChannelButton1->height() == temperatureChannelCommonParamsButton->height(),
            "temperature screenshot layout keeps the upper and lower segmented bars visually matched");
    require(temperatureChannelConfigSubStack->currentWidget() != nullptr &&
                temperatureChannelConfigSubStack->currentWidget()->objectName() ==
                    QStringLiteral("temperatureChannelCommonParamsPageChannel1") &&
                temperatureChannelCommonParamsButton->isChecked(),
            "temperature channel defaults to the lower common-params page");
    const QRect subTopBarRectInCard(
        temperatureChannelSubTopBar->mapTo(temperatureConfigCard, QPoint(0, 0)),
        temperatureChannelSubTopBar->size());
    const int lowerSelectorToConfigCardBottom =
        temperatureConfigCard->height() - 1 - subTopBarRectInCard.bottom();
    QWidget *initialChannelSubPageRow = temperatureChannelSubTopBar->parentWidget();
    require(initialChannelSubPageRow != nullptr &&
                temperatureSubPageBarStack->isAncestorOf(temperatureChannelSubTopBar) &&
                !temperatureControllerControlsCard->isAncestorOf(temperatureChannelSubTopBar) &&
                !temperatureChannelStack->isAncestorOf(temperatureChannelSubTopBar) &&
                subTopBarRectInCard.top() > controlsCardRectInCard.bottom() &&
                subTopBarRectInCard.top() - controlsCardRectInCard.bottom() - 1 ==
                    temperatureControllerLeftConfigColumnLayout->spacing() &&
                std::abs(subTopBarRectInCard.left() - contentRowRectInCard.left()) <= 2 &&
                subTopBarRectInCard.bottom() == contentRowRectInCard.bottom() &&
                std::abs(lowerSelectorToConfigCardBottom -
                         temperatureConfigCardLayout->contentsMargins().bottom()) <= 1 &&
                temperatureChannelStack->height() >= temperatureChannelStack->currentWidget()->sizeHint().height() &&
                temperatureChannelConfigSubStack->height() >=
                    temperatureChannelConfigSubStack->currentWidget()->sizeHint().height() &&
                std::abs(temperatureChannelConfigSubStack->height() - temperatureChannelStack->height()) <= 1,
            "temperature lower selector stays outside the left parameter card with the shared compact bottom inset");
    clickWidget(temperatureChannelAdvancedParamsButton, 150);
    activateLayouts(&window);
    auto *overtempUpperSpin = temperaturePanel->findChild<QDoubleSpinBox *>(
        QStringLiteral("temperatureOvertempUpperSpinChannel1"));
    auto *overtempLowerSpin = temperaturePanel->findChild<QDoubleSpinBox *>(
        QStringLiteral("temperatureOvertempLowerSpinChannel1"));
    auto *temperatureSlopeSpin = temperaturePanel->findChild<QDoubleSpinBox *>(
        QStringLiteral("temperatureSlopeSpinChannel1"));
    auto *startupDelaySpin = temperaturePanel->findChild<QSpinBox *>(
        QStringLiteral("temperatureStartupDelaySpinChannel1"));
    auto *sensorResistanceEdit = temperaturePanel->findChild<QLineEdit *>(
        QStringLiteral("temperatureSensorResistanceEditChannel1"));
    require(temperatureChannelConfigSubStack->currentWidget() != nullptr &&
                temperatureChannelConfigSubStack->currentWidget()->objectName() ==
                    QStringLiteral("temperatureChannelAdvancedParamsPageChannel1") &&
                temperatureChannelAdvancedParamsButton->isChecked() &&
                temperatureChannelTopControlsStack->isVisible() &&
                temperatureChannelTopControlsStack->currentWidget() == temperatureControllerModeTopControls &&
                temperatureControllerModeField->parentWidget() == temperatureControllerModeTopControls &&
                overtempUpperSpin != nullptr &&
                overtempLowerSpin != nullptr &&
                temperatureSlopeSpin != nullptr &&
                startupDelaySpin != nullptr &&
                sensorResistanceEdit != nullptr,
            "temperature lower advanced tab exposes all RD105 professional parameters");
    const QRect professionalControllerModeFieldRect(
        temperatureControllerModeField->mapTo(temperatureChannelSelectorRow, QPoint(0, 0)),
        temperatureControllerModeField->size());
    require(std::abs(temperatureChannelTopBar->mapTo(temperatureConfigCard, QPoint(0, 0)).x() -
                     temperatureControllerContentRow->mapTo(temperatureConfigCard, QPoint(0, 0)).x()) <= 1,
            "temperature top navigation stays aligned with the left control-card region on the professional parameters page");
    require(overtempUpperSpin->minimum() == -3000.0 &&
                overtempUpperSpin->maximum() == 5000.0 &&
                overtempUpperSpin->decimals() == 5 &&
                overtempLowerSpin->minimum() == -3000.0 &&
                overtempLowerSpin->maximum() == 5000.0 &&
                overtempLowerSpin->decimals() == 5 &&
                temperatureSlopeSpin->minimum() == 0.0 &&
                temperatureSlopeSpin->maximum() == 10.0 &&
                temperatureSlopeSpin->decimals() == 3 &&
                startupDelaySpin->minimum() == 3 &&
                startupDelaySpin->maximum() == 180 &&
                sensorResistanceEdit->isReadOnly(),
            "temperature professional controls follow the RD105 manual ranges and read-only resistance rule");
    require(std::abs(overtempUpperSpin->value() - 500.12345) < 0.00001 &&
                std::abs(overtempLowerSpin->value() + 40.54321) < 0.00001 &&
                std::abs(temperatureSlopeSpin->value() - 1.234) < 0.001 &&
                startupDelaySpin->value() == 15 &&
                sensorResistanceEdit->text() == QStringLiteral("11948.492300"),
            "temperature professional controls show values received from the RD105 status frame");
    QWidget *advancedParamsPage = temperatureChannelConfigSubStack->currentWidget();
    auto *advancedParamsGrid = qobject_cast<QGridLayout *>(advancedParamsPage->layout());
    require(advancedParamsGrid != nullptr &&
                advancedParamsGrid->horizontalSpacing() == 6 &&
                advancedParamsGrid->itemAtPosition(0, 0) != nullptr &&
                advancedParamsGrid->itemAtPosition(0, 0)->widget() == overtempUpperSpin->parentWidget() &&
                advancedParamsGrid->itemAtPosition(0, 1) != nullptr &&
                advancedParamsGrid->itemAtPosition(0, 1)->widget() == overtempLowerSpin->parentWidget() &&
                advancedParamsGrid->itemAtPosition(1, 0) != nullptr &&
                advancedParamsGrid->itemAtPosition(1, 0)->widget() == temperatureSlopeSpin->parentWidget() &&
                advancedParamsGrid->itemAtPosition(1, 1) != nullptr &&
                advancedParamsGrid->itemAtPosition(1, 1)->widget() == startupDelaySpin->parentWidget() &&
                advancedParamsGrid->itemAtPosition(2, 0) != nullptr &&
                advancedParamsGrid->itemAtPosition(2, 0)->widget() == sensorResistanceEdit->parentWidget(),
            "temperature professional parameters use the two-column stacked form from the screenshot");
    const QRect overtempUpperRect(overtempUpperSpin->mapTo(advancedParamsPage, QPoint(0, 0)), overtempUpperSpin->size());
    const QRect overtempLowerRect(overtempLowerSpin->mapTo(advancedParamsPage, QPoint(0, 0)), overtempLowerSpin->size());
    const QRect temperatureSlopeRect(temperatureSlopeSpin->mapTo(advancedParamsPage, QPoint(0, 0)), temperatureSlopeSpin->size());
    const QRect startupDelayRect(startupDelaySpin->mapTo(advancedParamsPage, QPoint(0, 0)), startupDelaySpin->size());
    const QRect sensorResistanceRect(sensorResistanceEdit->mapTo(advancedParamsPage, QPoint(0, 0)), sensorResistanceEdit->size());
    const QMargins advancedParamsMargins = advancedParamsGrid->contentsMargins();
    require(overtempUpperRect.left() == temperatureSlopeRect.left() &&
                overtempLowerRect.left() == startupDelayRect.left() &&
                overtempUpperRect.width() == overtempLowerRect.width() &&
                overtempUpperRect.width() == temperatureSlopeRect.width() &&
                overtempUpperRect.width() == startupDelayRect.width() &&
                overtempUpperRect.width() == sensorResistanceRect.width() &&
                overtempUpperRect.width() == 130 &&
                overtempUpperRect.top() == overtempLowerRect.top() &&
                temperatureSlopeRect.top() == startupDelayRect.top() &&
                sensorResistanceRect.top() > temperatureSlopeRect.bottom() &&
                sensorResistanceRect.left() == overtempUpperRect.left() &&
                overtempUpperRect.left() == advancedParamsMargins.left() &&
                advancedParamsPage->width() - 1 - overtempLowerRect.right() == advancedParamsMargins.right() &&
                advancedParamsPage->width() - 1 - startupDelayRect.right() == advancedParamsMargins.right(),
            "temperature professional input fields fill the two-column parameter area");
    require(overtempLowerRect.left() - overtempUpperRect.right() - 1 == advancedParamsGrid->horizontalSpacing() &&
                startupDelayRect.left() - temperatureSlopeRect.right() - 1 == advancedParamsGrid->horizontalSpacing(),
            "temperature professional adjacent inputs keep the compact reference horizontal gap");
    clickWidget(temperatureChannelCommonParamsButton, 150);
    activateLayouts(&window);
    require(temperatureChannelConfigSubStack->currentWidget() != nullptr &&
                temperatureChannelConfigSubStack->currentWidget()->objectName() ==
                    QStringLiteral("temperatureChannelCommonParamsPageChannel1") &&
                temperatureChannelCommonParamsButton->isChecked() &&
                temperatureChannelTopControlsStack->isVisible() &&
                temperatureChannelCommonTopControls1->isVisible(),
            "temperature lower common tab switches back to channel controls");
    const QRect commonControllerModeFieldRect(
        temperatureControllerModeField->mapTo(temperatureChannelSelectorRow, QPoint(0, 0)),
        temperatureControllerModeField->size());
    require(commonControllerModeFieldRect == professionalControllerModeFieldRect,
            "temperature common-parameter mode field matches the professional-parameter reference position");

    auto *commonParamsGrid = qobject_cast<QGridLayout *>(temperatureChannelConfigSubStack->currentWidget()->layout());
    require(commonParamsGrid != nullptr,
            "temperature lower common tab uses a shared grid for cross-row input alignment");
    auto *pidFields = temperaturePanel->findChild<QWidget *>(QStringLiteral("temperaturePidFieldsChannel1"));
    auto *outputTargetFields =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureOutputTargetFieldsChannel1"));
    require(pidFields != nullptr &&
                outputTargetFields != nullptr &&
                qobject_cast<QHBoxLayout *>(pidFields->layout()) != nullptr &&
                qobject_cast<QHBoxLayout *>(outputTargetFields->layout()) != nullptr &&
                qobject_cast<QHBoxLayout *>(pidFields->layout())->spacing() == 6 &&
                qobject_cast<QHBoxLayout *>(outputTargetFields->layout())->spacing() == 6 &&
                commonParamsGrid->itemAtPosition(0, 0) != nullptr &&
                commonParamsGrid->itemAtPosition(0, 0)->widget() == pidFields &&
                commonParamsGrid->itemAtPosition(1, 0) != nullptr &&
                commonParamsGrid->itemAtPosition(1, 0)->widget() == outputTargetFields &&
                commonParamsGrid->itemAtPosition(2, 0) != nullptr &&
                commonParamsGrid->itemAtPosition(2, 0)->widget() == maxOutputSpin->parentWidget(),
            "temperature lower common tab groups adjacent input columns into compact row containers");
    auto requireStackedChannelFieldLayout = [](QWidget *editor,
                                               const char *message) {
        require(editor != nullptr && editor->parentWidget() != nullptr, message);
        QWidget *cell = editor->parentWidget();
        auto *cellLayout = qobject_cast<QVBoxLayout *>(cell->layout());
        const QList<QLabel*> labels =
            cell->findChildren<QLabel *>(QStringLiteral("fieldLabel"), Qt::FindDirectChildrenOnly);
        require(cell->objectName() == QStringLiteral("temperatureConfigFieldColumn") &&
                    cellLayout != nullptr &&
                    cellLayout->spacing() == 6 &&
                    !labels.isEmpty(),
                message);
        const QRect labelRect(labels.first()->mapTo(cell, QPoint(0, 0)), labels.first()->size());
        const QRect editorRect(editor->mapTo(cell, QPoint(0, 0)), editor->size());
        require(labelRect.left() <= 1 &&
                    editorRect.left() <= 1 &&
                    labelRect.bottom() < editorRect.top(),
                message);
    };
    requireStackedChannelFieldLayout(modeCombo,
                                     "temperature output mode field lives in the lower common-params page");
    requireStackedChannelFieldLayout(targetSpin,
                                     "temperature target temperature field lives in the lower common-params page");
    requireStackedChannelFieldLayout(maxOutputSpin,
                                     "temperature max output field lives in the lower common-params page");
    requireStackedChannelFieldLayout(kpSpin,
                                     "temperature PID P field lives in the lower common-params page");
    requireStackedChannelFieldLayout(kiSpin,
                                     "temperature PID I field lives in the lower common-params page");
    requireStackedChannelFieldLayout(kdSpin,
                                     "temperature PID D field lives in the lower common-params page");
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
    requirePidTextFits(kiSpin,
                       "temperature PID spin boxes leave enough unobscured edit area for a three-digit value");
    requirePidTextFits(kdSpin,
                       "temperature PID spin boxes leave enough unobscured edit area for a three-digit value");
    const QRect modeRowRect(modeCombo->mapTo(temperatureChannelStack, QPoint(0, 0)), modeCombo->size());
    const QRect targetRowRect(targetSpin->mapTo(temperatureChannelStack, QPoint(0, 0)), targetSpin->size());
    const QRect maxOutputRowRect(maxOutputSpin->mapTo(temperatureChannelStack, QPoint(0, 0)), maxOutputSpin->size());
    const QRect kpRowRect(kpSpin->mapTo(temperatureChannelStack, QPoint(0, 0)), kpSpin->size());
    const QRect kiRowRect(kiSpin->mapTo(temperatureChannelStack, QPoint(0, 0)), kiSpin->size());
    const QRect kdRowRect(kdSpin->mapTo(temperatureChannelStack, QPoint(0, 0)), kdSpin->size());
    QWidget *commonParamsPage = temperatureChannelConfigSubStack->currentWidget();
    const QRect pidFieldsRectInCommonParamsPage(pidFields->mapTo(commonParamsPage, QPoint(0, 0)), pidFields->size());
    const QRect outputTargetFieldsRectInCommonParamsPage(
        outputTargetFields->mapTo(commonParamsPage, QPoint(0, 0)), outputTargetFields->size());
    const QMargins commonParamsMargins = commonParamsGrid->contentsMargins();
    const QRect pidFieldsRectInControlsCard(
        pidFields->mapTo(temperatureControllerControlsCard, QPoint(0, 0)), pidFields->size());
    require(kpRowRect.left() == commonParamsMargins.left() &&
                pidFieldsRectInCommonParamsPage.left() == commonParamsMargins.left() &&
                outputTargetFieldsRectInCommonParamsPage.left() == commonParamsMargins.left() &&
                commonParamsPage->width() - 1 - pidFieldsRectInCommonParamsPage.right() == commonParamsMargins.right() &&
                commonParamsPage->width() - 1 - outputTargetFieldsRectInCommonParamsPage.right() ==
                    commonParamsMargins.right(),
            "temperature lower common tab fills the parameter area while preserving the shared card inset");
    const QRect maxOutputFieldRectInControlsCard(
        maxOutputSpin->parentWidget()->mapTo(temperatureControllerControlsCard, QPoint(0, 0)),
        maxOutputSpin->parentWidget()->size());
    require(controlsCardMargins == QMargins(6, 6, 6, 0) &&
                std::abs(pidFieldsRectInControlsCard.left() - 6) <= 1 &&
                std::abs(temperatureControllerControlsCard->width() - 1 - pidFieldsRectInControlsCard.right() - 6) <= 1 &&
                std::abs(pidFieldsRectInControlsCard.top() - 6) <= 1 &&
                std::abs(temperatureControllerControlsCard->height() - 1 - maxOutputFieldRectInControlsCard.bottom() - 6) <= 1,
            "temperature common parameters keep one visible 6px inset on all four card edges");
    require(std::abs(kpRowRect.top() - kiRowRect.top()) <= 2 &&
                std::abs(kiRowRect.top() - kdRowRect.top()) <= 2 &&
                kpRowRect.right() < kiRowRect.left() &&
                kiRowRect.right() < kdRowRect.left() &&
                kiRowRect.left() - kpRowRect.right() - 1 <= 8 &&
                kdRowRect.left() - kiRowRect.right() - 1 <= 8,
            "temperature lower common tab lays P, I, and D on the first row with compact adjacent gaps");
    require(modeRowRect.top() > kdRowRect.bottom(),
            "temperature lower common tab places output mode below the PID row");
    require(std::abs(modeRowRect.top() - targetRowRect.top()) <= 2,
            "temperature lower common tab aligns output mode and target on the same row");
    require(maxOutputRowRect.top() > modeRowRect.bottom(),
            "temperature lower common tab places max output below the output/target row");
    require(modeRowRect.right() < targetRowRect.left(),
            "temperature lower common tab keeps target to the right of output mode");
    require(targetRowRect.left() - modeRowRect.right() - 1 <= 8,
            "temperature output mode and target inputs use the compact reference horizontal gap");
    require(maxOutputRowRect.left() == modeRowRect.left(),
            "temperature lower common tab keeps max output aligned under output mode");
    require(std::abs(kpRowRect.left() - modeRowRect.left()) <= 1 &&
                std::abs(modeRowRect.left() - maxOutputRowRect.left()) <= 1,
            "temperature common-parameter rows align their first input column while keeping row-local compact gaps");
    require(temperaturePanel->findChild<QWidget *>(QStringLiteral("temperaturePidHeadingChannel1")) == nullptr,
            "temperature common parameters omit the redundant PID heading");
    require(modeRowRect.bottom() <= temperatureChannelStack->rect().bottom() &&
                targetRowRect.bottom() <= temperatureChannelStack->rect().bottom() &&
                maxOutputRowRect.bottom() <= temperatureChannelStack->rect().bottom() &&
                kpRowRect.bottom() <= temperatureChannelStack->rect().bottom() &&
                kiRowRect.bottom() <= temperatureChannelStack->rect().bottom() &&
                kdRowRect.bottom() <= temperatureChannelStack->rect().bottom(),
            "temperature channel fields fit inside the stack without clipping");
    require(maxOutputSpin->property("temperatureMaxOutputWarning").toBool(),
            "temperature max output value carries warning styling");
    QList<QLabel*> maxOutputLabels;
    for (QLabel *label : temperatureChannelConfigSubStack->currentWidget()->findChildren<QLabel *>(
             QStringLiteral("fieldLabel")))
    {
        if (label->property("temperatureMaxOutputWarning").toBool())
        {
            maxOutputLabels.append(label);
        }
    }
    const QColor warningTextColor = VaporView::appThemeColor(VaporView::AppThemeColor::Danger, false);
    require(!maxOutputLabels.isEmpty() &&
                maxOutputLabels.first()->text() == QStringLiteral("最大输出电压百分比(%)") &&
                maxOutputLabels.first()->property("temperatureMaxOutputWarning").toBool(),
            "temperature max output label is renamed and marked red");
    require(maxOutputSpin->parentWidget()->width() == 266 &&
                maxOutputLabels.first()->width() >=
                    maxOutputLabels.first()->fontMetrics().horizontalAdvance(maxOutputLabels.first()->text()),
            "temperature max output label uses the full parameter row without narrowing its input");
    require(maxOutputSpin->palette().color(QPalette::Text) == warningTextColor,
            "temperature max output value palette is actually painted red");
    require(modeCombo->width() == 130 &&
                targetSpin->width() == 130 &&
                maxOutputSpin->width() == 130,
            "temperature common parameter inputs use the widened field width");
    require(kpSpin->width() == 85 &&
                kiSpin->width() == 84 &&
                kdSpin->width() == 85,
            "temperature PID spin boxes divide the widened row across three fields");
    auto requireTopBarFieldLayout = [temperatureChannelTopControlsStack](QWidget *editor, const char *message) {
        require(editor != nullptr && editor->parentWidget() != nullptr, message);
        QWidget *row = editor->parentWidget();
        require(row->objectName() == QStringLiteral("temperatureTopBarField"), message);
        require(temperatureChannelTopControlsStack->isAncestorOf(row), message);
    };
    require(!temperatureChannelTopControlsStack->isAncestorOf(modeCombo) &&
                !temperatureChannelTopControlsStack->isAncestorOf(targetSpin) &&
                temperatureChannelStack->isAncestorOf(modeCombo) &&
                temperatureChannelStack->isAncestorOf(targetSpin),
            "temperature output mode and target temperature live in the lower common-params page");
    requireTopBarFieldLayout(enableSwitch,
                              "temperature output enable switch lives beside the channel selectors");
    requireTopBarFieldLayout(autoPidCombo,
                             "temperature auto PID field lives beside the output enable switch");
    require(temperatureChannelTopControlsStack->isAncestorOf(temperatureControllerModeField) &&
                temperatureControllerModeField->parentWidget() == temperatureChannelTopControlsStack->currentWidget(),
            "temperature controller mode field lives in the shared top-controls row");
    const QRect enableFieldRect(enableSwitch->parentWidget()->mapTo(temperatureChannelTopControlsStack, QPoint(0, 0)),
                                enableSwitch->parentWidget()->size());
    const QRect autoPidFieldRect(autoPidCombo->parentWidget()->mapTo(temperatureChannelTopControlsStack, QPoint(0, 0)),
                                 autoPidCombo->parentWidget()->size());
    require(std::abs(enableFieldRect.top() - autoPidFieldRect.top()) <= 2 &&
                enableFieldRect.right() < autoPidFieldRect.left(),
            "temperature output enable and auto PID share the channel selector top row");
    requireTopBarFieldLayout(sensorModelSelector1,
                             "temperature sensor model radio selector lives in the channel top row");
    require(addressSpin->parentWidget() != nullptr &&
                rs485BaudCombo->parentWidget() != nullptr &&
                addressSpin->parentWidget()->objectName() == QStringLiteral("temperatureCommonFieldRow") &&
                rs485BaudCombo->parentWidget()->objectName() == QStringLiteral("temperatureCommonFieldRow") &&
                temperatureChannelStack->isAncestorOf(addressSpin) &&
                temperatureChannelStack->isAncestorOf(rs485BaudCombo) &&
                !temperatureChannelTopControlsStack->isAncestorOf(addressSpin) &&
                !temperatureChannelTopControlsStack->isAncestorOf(rs485BaudCombo),
            "temperature common RS485 fields live in the common-settings grid");
    auto requireCommonFieldRowLayout = [temperatureChannelStack](QWidget *editor, const char *message) {
        require(editor != nullptr && editor->parentWidget() != nullptr, message);
        QWidget *row = editor->parentWidget();
        require(row->objectName() == QStringLiteral("temperatureCommonFieldRow"), message);
        require(temperatureChannelStack->isAncestorOf(row), message);
        auto *rowLayout = qobject_cast<QVBoxLayout *>(row->layout());
        const QList<QLabel*> labels =
            row->findChildren<QLabel *>(QStringLiteral("fieldLabel"), Qt::FindDirectChildrenOnly);
        require(rowLayout != nullptr &&
                    rowLayout->spacing() == 6 &&
                    !labels.isEmpty(),
                message);
        const QRect labelRect(labels.first()->mapTo(row, QPoint(0, 0)), labels.first()->size());
        const QRect editorRect(editor->mapTo(row, QPoint(0, 0)), editor->size());
        require(labelRect.left() <= 1 &&
                    labelRect.right() <= row->rect().right() &&
                    editorRect.left() <= 1 &&
                    labelRect.bottom() < editorRect.top() &&
                    editorRect.right() <= row->rect().right(),
                message);
    };
    requireCommonFieldRowLayout(addressSpin,
                                "temperature common address field uses aligned left label and right value layout");
    requireCommonFieldRowLayout(rs485BaudCombo,
                                "temperature common baud field uses aligned left label and right value layout");
    requireCommonFieldRowLayout(overtempOutputCombo,
                                "temperature common over-temperature output field uses left label and right value layout");
    requireCommonFieldRowLayout(commonInternalTemperatureEdit,
                                "temperature common internal temperature field uses left label and right value layout");

    clickWidget(temperatureChannelCommonParamsButton, 150);
    clickWidget(temperatureConfigChannelButton2, 150);
    activateLayouts(&window);
    require(temperatureChannelConfigSubStack2->currentWidget() != nullptr &&
                temperatureChannelConfigSubStack2->currentWidget()->objectName() ==
                    QStringLiteral("temperatureChannelCommonParamsPageChannel2") &&
                temperatureChannelCommonParamsButton2->isChecked() &&
                temperatureConfigChannelButton2->isChecked(),
            "switching channels preserves the globally selected lower common-params page");
    clickWidget(temperatureChannelAdvancedParamsButton2, 150);
    activateLayouts(&window);
    require(temperatureChannelConfigSubStack2->currentWidget() != nullptr &&
                temperatureChannelConfigSubStack2->currentWidget()->objectName() ==
                    QStringLiteral("temperatureChannelAdvancedParamsPageChannel2") &&
                temperatureChannelAdvancedParamsButton2->isChecked() &&
                temperatureConfigChannelButton2->isChecked(),
            "switching the lower page does not change the selected top channel");
    clickWidget(temperatureConfigChannelButton1, 150);
    activateLayouts(&window);
    require(temperatureChannelConfigSubStack->currentWidget() != nullptr &&
                temperatureChannelConfigSubStack->currentWidget()->objectName() ==
                    QStringLiteral("temperatureChannelAdvancedParamsPageChannel1") &&
                temperatureChannelAdvancedParamsButton->isChecked() &&
                temperatureChannelConfigSubStack2->currentIndex() ==
                    temperatureChannelConfigSubStack->currentIndex() &&
                temperatureConfigChannelButton1->isChecked(),
            "switching back to channel 1 keeps the globally selected lower advanced-params page");

    clickWidget(temperatureChannelSensorConfigButton, 150);
    activateLayouts(&window);
    require(temperatureChannelConfigSubStack->currentWidget() != nullptr &&
                temperatureChannelConfigSubStack->currentWidget()->objectName() ==
                    QStringLiteral("temperatureChannelSensorConfigPageChannel1") &&
                temperatureChannelSensorConfigButton->isChecked() &&
                temperatureChannelTopControlsStack->isVisible() &&
                !temperatureChannelCommonTopControls1->isVisible() &&
                temperatureControllerModeField->parentWidget() == temperatureChannelTopControlsStack->currentWidget() &&
                sensorModelSelector1->parentWidget()->isVisible(),
            "temperature sensor config tab switches to the sensor config page");
    const QRect sensorControllerModeFieldRect(
        temperatureControllerModeField->mapTo(temperatureChannelSelectorRow, QPoint(0, 0)),
        temperatureControllerModeField->size());
    require(sensorControllerModeFieldRect == professionalControllerModeFieldRect,
            "temperature sensor-config mode field matches the professional-parameter reference position");
    const QRect sensorModelFieldRect(
        sensorModelSelector1->parentWidget()->mapTo(temperatureChannelSelectorRow, QPoint(0, 0)),
        sensorModelSelector1->parentWidget()->size());
    const int sensorTopBarToModelGap = sensorModelFieldRect.left() - topBarRectForMode.right() - 1;
    const int sensorModelToModeGap = sensorControllerModeFieldRect.left() - sensorModelFieldRect.right() - 1;
    require(sensorTopBarToModelGap >= 0 && sensorModelToModeGap >= 0 &&
                std::abs(sensorTopBarToModelGap - sensorModelToModeGap) <= 1,
            "temperature sensor-page top controls use one evenly distributed gap");

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
    auto *sensorConfigGrid = qobject_cast<QGridLayout *>(temperatureChannelConfigSubStack->currentWidget()->layout());
    auto *polynomialFields =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperaturePolynomialFieldsChannel1"));
    auto *polynomialFieldsGrid =
        polynomialFields ? qobject_cast<QGridLayout *>(polynomialFields->layout()) : nullptr;
    auto *temperatureCalibrationDrawer =
        temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureCalibrationSideDrawerChannel1"));
    auto *temperatureCalibrationOverlay =
        qobject_cast<QFrame *>(temperaturePanel->findChild<QWidget *>(
            QStringLiteral("temperaturePolynomialFieldsChannel1")));
    auto *ntcFields = temperaturePanel->findChild<QWidget *>(QStringLiteral("temperatureNtcFieldsChannel1"));
    auto *ptR0Fields = temperaturePanel->findChild<QWidget *>(QStringLiteral("temperaturePtR0FieldsChannel1"));
    auto *ptBFields = temperaturePanel->findChild<QWidget *>(QStringLiteral("temperaturePtBFieldsChannel1"));
    require(sensorConfigGrid != nullptr &&
                sensorConfigGrid->horizontalSpacing() == 6 &&
                ntcFields != nullptr &&
                ptR0Fields != nullptr &&
                ptBFields != nullptr &&
                qobject_cast<QHBoxLayout *>(ntcFields->layout()) != nullptr &&
                qobject_cast<QHBoxLayout *>(ptR0Fields->layout()) != nullptr &&
                qobject_cast<QHBoxLayout *>(ptBFields->layout()) != nullptr &&
                qobject_cast<QHBoxLayout *>(ntcFields->layout())->spacing() == 6 &&
                qobject_cast<QHBoxLayout *>(ptR0Fields->layout())->spacing() == 6 &&
                qobject_cast<QHBoxLayout *>(ptBFields->layout())->spacing() == 6 &&
                sensorConfigGrid->itemAtPosition(0, 0) != nullptr &&
                sensorConfigGrid->itemAtPosition(0, 0)->widget() == ntcFields &&
                sensorConfigGrid->itemAtPosition(1, 0) != nullptr &&
                sensorConfigGrid->itemAtPosition(1, 0)->widget() == ptR0Fields &&
                sensorConfigGrid->itemAtPosition(2, 0) != nullptr &&
                sensorConfigGrid->itemAtPosition(2, 0)->widget() == ptBFields &&
                sensorConfigGrid->itemAtPosition(3, 0) == nullptr &&
                polynomialFields != nullptr &&
                polynomialFieldsGrid != nullptr &&
                polynomialFieldsGrid->horizontalSpacing() == 6 &&
                polynomialFieldsGrid->contentsMargins().left() == 6 &&
                polynomialFieldsGrid->contentsMargins().right() == 6 &&
                temperatureCalibrationDrawer != nullptr &&
                temperatureCalibrationOverlay != nullptr &&
                temperatureCalibrationDrawer->parentWidget() == temperatureControllerControlsCard &&
                temperatureCalibrationDrawer->focusPolicy() == Qt::TabFocus &&
                temperatureCalibrationDrawer->width() ==
                    temperatureCalibrationDrawer->property("temperatureCalibrationHandleWidth").toInt() &&
                temperatureCalibrationDrawer->height() ==
                    temperatureCalibrationDrawer->property("temperatureCalibrationHandleHeight").toInt() &&
                temperatureCalibrationDrawer->property("temperatureCalibrationSideDrawer").toBool() &&
                temperatureCalibrationDrawer->property("temperatureCalibrationHandleText").toString() ==
                    QStringLiteral("校\n准\n系\n数\nA0\nA7") &&
                !temperatureCalibrationDrawer->property("temperatureCalibrationHandleText").toString().contains(
                    QLatin1Char('-')) &&
                temperatureCalibrationDrawer->property("temperatureCalibrationHandleChevronIconName").toString() ==
                    QStringLiteral("chevron-left") &&
                QFile::exists(QCoreApplication::applicationDirPath() +
                              QStringLiteral("/resources/lucide/chevron-left.svg")) &&
                temperaturePanel->findChild<QPushButton *>(
                    QStringLiteral("temperatureCalibrationPullButtonChannel1")) == nullptr &&
                temperatureCalibrationOverlay->property("temperatureSensorCalibrationOverlay").toBool() &&
                !temperatureCalibrationOverlay->isVisible(),
            "temperature sensor config keeps the six primary fields in the base grid and hides A0-A7 in an edge calibration drawer");
    temperatureCalibrationDrawer->setFocus(Qt::MouseFocusReason);
    processEventsFor(20);
    require(temperatureCalibrationDrawer->hasFocus(),
            "temperature calibration handle accepts focus after selection");
    const QImage focusedCalibrationHandleImage =
        temperatureCalibrationDrawer->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    require(countPixelsNearColor(
                focusedCalibrationHandleImage,
                focusedCalibrationHandleImage.rect(),
                VaporView::appThemeColor(VaporView::AppThemeColor::Focus,
                                          VaporView::isDarkThemeEnabled()),
                0) == 0,
            "temperature calibration handle does not draw a focus-colored border after selection");
    auto sensorFieldLabel = [](QWidget *editor) -> QLabel * {
        return editor && editor->parentWidget()
            ? editor->parentWidget()->findChild<QLabel *>(QStringLiteral("fieldLabel"), Qt::FindDirectChildrenOnly)
            : nullptr;
    };
    auto requireStackedSensorFieldLayout = [sensorConfigGrid](QWidget *editor,
                                                              int expectedWidth,
                                                              const char *message) {
        require(sensorConfigGrid != nullptr && editor != nullptr && editor->parentWidget() != nullptr, message);
        QWidget *cell = editor->parentWidget();
        auto *cellLayout = qobject_cast<QVBoxLayout *>(cell->layout());
        const QList<QLabel*> labels =
            cell->findChildren<QLabel *>(QStringLiteral("fieldLabel"), Qt::FindDirectChildrenOnly);
        require(cell->objectName() == QStringLiteral("temperatureConfigFieldRow") &&
                    cellLayout != nullptr &&
                    cellLayout->spacing() == 6 &&
                    !labels.isEmpty() &&
                    cell->width() == expectedWidth &&
                    editor->width() == expectedWidth,
                message);
        const QRect labelRect(labels.first()->mapTo(cell, QPoint(0, 0)), labels.first()->size());
        const QRect editorRect(editor->mapTo(cell, QPoint(0, 0)), editor->size());
        require(labelRect.left() <= 1 &&
                    editorRect.left() <= 1 &&
                    labelRect.bottom() < editorRect.top(),
                message);
    };
    auto requirePolynomialFieldLayout = [polynomialFieldsGrid](QWidget *editor,
                                                               int row,
                                                               int column,
                                                               int expectedWidth,
                                                               const char *message) {
        require(polynomialFieldsGrid != nullptr && editor != nullptr && editor->parentWidget() != nullptr, message);
        QWidget *cell = editor->parentWidget();
        QLayoutItem *cellItem = polynomialFieldsGrid->itemAtPosition(row, column);
        auto *cellLayout = qobject_cast<QVBoxLayout *>(cell->layout());
        const QList<QLabel*> labels =
            cell->findChildren<QLabel *>(QStringLiteral("fieldLabel"), Qt::FindDirectChildrenOnly);
        require(cell->objectName() == QStringLiteral("temperatureConfigFieldRow") &&
                    cellItem != nullptr &&
                    cellItem->widget() == cell &&
                    cellLayout != nullptr &&
                    cellLayout->spacing() == 6 &&
                    !labels.isEmpty() &&
                    cell->width() == expectedWidth &&
                    editor->width() == expectedWidth,
                message);
        const QRect labelRect(labels.first()->mapTo(cell, QPoint(0, 0)), labels.first()->size());
        const QRect editorRect(editor->mapTo(cell, QPoint(0, 0)), editor->size());
        require(labelRect.left() <= 1 &&
                    editorRect.left() <= 1 &&
                    labelRect.bottom() < editorRect.top(),
                message);
    };
    requireStackedSensorFieldLayout(ntcR0Edit, 110,
                                    "temperature NTC R0 field uses the narrowed stacked layout");
    requireStackedSensorFieldLayout(ntcBEdit, 110,
                                    "temperature NTC B field uses the narrowed stacked layout");
    requireStackedSensorFieldLayout(ptR0Edit, 110,
                                    "temperature PT R0 field uses the narrowed stacked layout");
    requireStackedSensorFieldLayout(ptAEdit, 110,
                                    "temperature PT A field uses the narrowed stacked layout");
    requireStackedSensorFieldLayout(ptBEdit, 110,
                                    "temperature PT B field uses the narrowed stacked layout");
    requireStackedSensorFieldLayout(ptCEdit, 110,
                                    "temperature PT C field uses the narrowed stacked layout");
    requirePolynomialFieldLayout(polynomialA0Edit, 0, 0, 58,
                                 "temperature polynomial field uses the compact nested stacked layout");
    require(sensorFieldLabel(ntcR0Edit) && sensorFieldLabel(ntcR0Edit)->text() == QStringLiteral("NTC R0(Ohm)") &&
                sensorFieldLabel(ntcBEdit) && sensorFieldLabel(ntcBEdit)->text() == QStringLiteral("NTC B") &&
                sensorFieldLabel(ptR0Edit) && sensorFieldLabel(ptR0Edit)->text() == QStringLiteral("PT R0(Ohm)") &&
                sensorFieldLabel(ptAEdit) && sensorFieldLabel(ptAEdit)->text() == QStringLiteral("PT A(E-3)") &&
                sensorFieldLabel(ptBEdit) && sensorFieldLabel(ptBEdit)->text() == QStringLiteral("PT B(E-7)") &&
                sensorFieldLabel(ptCEdit) && sensorFieldLabel(ptCEdit)->text() == QStringLiteral("PT C(E-12)"),
            "temperature sensor fields retain their unit and exponent annotations");
    require(ntcR0Edit->width() == 110 &&
                ntcBEdit->width() == 110 &&
                ptR0Edit->width() == 110 &&
                ptAEdit->width() == 110 &&
                ptBEdit->width() == 110 &&
                ptCEdit->width() == 110,
            "temperature primary sensor inputs use the narrowed half-width fields");
    require(temperatureCalibrationOverlay->isAncestorOf(polynomialA0Edit) &&
                temperatureCalibrationOverlay->isAncestorOf(ntcR0Edit) == false,
            "temperature calibration coefficients share the overlay without moving the six primary sensor fields");
    auto requireCompactHorizontalInputGap = [](QWidget *left,
                                               QWidget *right,
                                               QWidget *host,
                                               const char *message) {
        require(left != nullptr && right != nullptr && host != nullptr, message);
        const QRect leftRect(left->mapTo(host, QPoint(0, 0)), left->size());
        const QRect rightRect(right->mapTo(host, QPoint(0, 0)), right->size());
        require(std::abs(leftRect.top() - rightRect.top()) <= 2 &&
                    leftRect.right() < rightRect.left() &&
                    rightRect.left() - leftRect.right() - 1 <= 8,
                message);
    };
    requireCompactHorizontalInputGap(ntcR0Edit,
                                     ntcBEdit,
                                     temperatureChannelConfigSubStack->currentWidget(),
                                     "temperature NTC adjacent inputs use the compact reference horizontal gap");
    requireCompactHorizontalInputGap(ptR0Edit,
                                     ptAEdit,
                                     temperatureChannelConfigSubStack->currentWidget(),
                                     "temperature PT R0/PT A adjacent inputs use the compact reference horizontal gap");
    requireCompactHorizontalInputGap(ptBEdit,
                                     ptCEdit,
                                     temperatureChannelConfigSubStack->currentWidget(),
                                     "temperature PT B/PT C adjacent inputs use the compact reference horizontal gap");

    const QRect sensorPageRectInControlsCard(
        temperatureChannelConfigSubStack->currentWidget()->mapTo(temperatureControllerControlsCard, QPoint(0, 0)),
        temperatureChannelConfigSubStack->currentWidget()->size());
    const QRect collapsedDrawerRectInControlsCard(
        temperatureCalibrationDrawer->mapTo(temperatureControllerControlsCard, QPoint(0, 0)),
        temperatureCalibrationDrawer->size());
    const int calibrationHandleHeight =
        temperatureCalibrationDrawer->property("temperatureCalibrationHandleHeight").toInt();
    require(calibrationHandleHeight > 0 &&
                calibrationHandleHeight < sensorPageRectInControlsCard.height() &&
                temperatureCalibrationDrawer->height() == calibrationHandleHeight &&
                std::abs(collapsedDrawerRectInControlsCard.center().y() -
                         temperatureControllerControlsCard->rect().center().y()) <= 1 &&
                collapsedDrawerRectInControlsCard.right() == temperatureControllerControlsCard->width() - 1,
            "temperature calibration handle fits its seven rows, stays vertically centered, and touches the controls-card edge");

    const int calibrationHandleWidth =
        temperatureCalibrationDrawer->property("temperatureCalibrationHandleWidth").toInt();
    auto clickCalibrationHandle = [temperatureCalibrationDrawer, calibrationHandleWidth](int waitMs) {
        clickWidgetAt(temperatureCalibrationDrawer,
                      QPoint(calibrationHandleWidth / 2,
                             temperatureCalibrationDrawer->height() / 2),
                      waitMs);
    };
    auto *calibrationAnimation = temperatureCalibrationDrawer->findChild<QVariantAnimation *>(
        QStringLiteral("temperatureCalibrationDrawerAnimation"));
    require(calibrationAnimation != nullptr && calibrationAnimation->duration() == 320,
            "temperature calibration drawer owns the smooth 320 ms animation");
    const int collapsedHandleCenterY = collapsedDrawerRectInControlsCard.center().y();

    clickCalibrationHandle(0);
    activateLayouts(&window);
    calibrationAnimation->setCurrentTime(qRound(calibrationAnimation->duration() * 0.36));
    const int expandingWidth = temperatureCalibrationDrawer->width();
    const QRect expandingDrawerRectInControlsCard(
        temperatureCalibrationDrawer->mapTo(temperatureControllerControlsCard, QPoint(0, 0)),
        temperatureCalibrationDrawer->size());
    require(temperatureCalibrationDrawer->property("expanded").toBool() &&
                expandingWidth > calibrationHandleWidth &&
                temperatureCalibrationDrawer->property("temperatureCalibrationHandleChevronIconName").toString() ==
                    QStringLiteral("chevron-right") &&
                expandingWidth < temperatureChannelConfigSubStack->currentWidget()->width(),
            "temperature calibration drawer starts expanding from the handle without occupying the full page immediately");
    require(std::abs(expandingDrawerRectInControlsCard.center().y() - collapsedHandleCenterY) <= 1,
            "temperature calibration handle keeps a fixed vertical center during expansion");
    clickCalibrationHandle(0);
    calibrationAnimation->setCurrentTime(qRound(calibrationAnimation->duration() * 0.20));
    const QRect reversingDrawerRectInControlsCard(
        temperatureCalibrationDrawer->mapTo(temperatureControllerControlsCard, QPoint(0, 0)),
        temperatureCalibrationDrawer->size());
    require(!temperatureCalibrationDrawer->property("expanded").toBool() &&
                temperatureCalibrationDrawer->property("temperatureCalibrationHandleChevronIconName").toString() ==
                    QStringLiteral("chevron-left") &&
                temperatureCalibrationDrawer->width() < expandingWidth,
            "temperature calibration drawer reverses smoothly from its current visual width");
    require(std::abs(reversingDrawerRectInControlsCard.center().y() - collapsedHandleCenterY) <= 1,
            "temperature calibration handle keeps a fixed vertical center during reversal");
    clickCalibrationHandle(0);
    calibrationAnimation->setCurrentTime(calibrationAnimation->duration());
    const QRect calibrationDrawerRect(
        temperatureCalibrationDrawer->mapTo(temperatureControllerControlsCard, QPoint(0, 0)),
        temperatureCalibrationDrawer->size());
    require(temperatureCalibrationDrawer->property("expanded").toBool() &&
                temperatureCalibrationOverlay->isVisible() &&
                calibrationDrawerRect.width() > calibrationHandleWidth &&
                calibrationDrawerRect.left() >= 0 &&
                calibrationDrawerRect.top() == 0 &&
                calibrationDrawerRect.bottom() == temperatureControllerControlsCard->height() - 1 &&
                temperatureCalibrationOverlay->geometry().left() == calibrationHandleWidth &&
                temperatureCalibrationOverlay->geometry().top() == 0 &&
                temperatureCalibrationOverlay->geometry().bottom() ==
                    temperatureCalibrationDrawer->height() - 1 &&
                temperatureCalibrationOverlay->geometry().right() <
                    temperatureCalibrationDrawer->width() &&
                calibrationDrawerRect.right() ==
                    temperatureControllerControlsCard->width() - 1,
            "temperature calibration drawer expands left inside the sensor-config page while its right edge stays anchored");
    clickCalibrationHandle(0);
    calibrationAnimation->setCurrentTime(calibrationAnimation->duration());
    const QRect collapsedCalibrationDrawerRect(
        temperatureCalibrationDrawer->mapTo(temperatureControllerControlsCard, QPoint(0, 0)),
        temperatureCalibrationDrawer->size());
    require(!temperatureCalibrationDrawer->property("expanded").toBool() &&
                !temperatureCalibrationOverlay->isVisible() &&
                collapsedCalibrationDrawerRect.width() == calibrationHandleWidth &&
                collapsedCalibrationDrawerRect.right() ==
                    temperatureControllerControlsCard->width() - 1 &&
                !collapsedCalibrationDrawerRect.contains(
                    ntcR0Edit->mapTo(temperatureChannelConfigSubStack->currentWidget(),
                                     ntcR0Edit->rect().center())),
            "temperature calibration drawer collapses back to its edge handle without blocking primary sensor input");
    std::array<QLineEdit *, 8> polynomialEdits{};
    for (int coefficient = 0; coefficient < 8; ++coefficient)
    {
        auto *edit = temperaturePanel->findChild<QLineEdit *>(
            QStringLiteral("temperaturePolynomialA%1EditChannel1").arg(coefficient));
        polynomialEdits[static_cast<size_t>(coefficient)] = edit;
        require(edit != nullptr,
                "temperature polynomial inputs all exist");
        requirePolynomialFieldLayout(edit,
                                     coefficient / 3,
                                     coefficient % 3,
                                     58,
                                     "temperature polynomial inputs stay in three compact rows inside the drawer");
    }
    for (int coefficient = 0; coefficient < 6; ++coefficient)
    {
        if (coefficient == 2 || coefficient == 5)
        {
            continue;
        }
        QWidget *leftCell = polynomialEdits[static_cast<size_t>(coefficient)]->parentWidget();
        QWidget *rightCell = polynomialEdits[static_cast<size_t>(coefficient + 1)]->parentWidget();
        const QRect leftRect(leftCell->mapTo(polynomialFields, QPoint(0, 0)), leftCell->size());
        const QRect rightRect(rightCell->mapTo(polynomialFields, QPoint(0, 0)), rightCell->size());
        require(rightRect.left() - leftRect.right() - 1 <= 8,
                "temperature polynomial A0-A7 inputs use the compact AI-8-style horizontal gap");
    }
    QWidget *temperatureChannelSubPageRow = temperatureChannelSubTopBar->parentWidget();
    require(temperatureChannelSubPageRow != nullptr &&
                temperaturePanel->findChild<QWidget *>(
                    QStringLiteral("temperatureSensorTopPolynomialFieldsChannel1")) == nullptr,
            "temperature sensor navigation no longer owns a separate A4-A7 top-row container");
    clickWidget(temperatureConfigChannelButton2, 100);
    activateLayouts(&window);
    require(temperatureChannelConfigSubStack2->currentWidget() != nullptr &&
                temperatureChannelConfigSubStack2->currentWidget()->objectName() ==
                    QStringLiteral("temperatureChannelSensorConfigPageChannel2"),
            "switching to channel 2 preserves the sensor-config subpage");
    selectedChannelCommonParamsPage->resize(420, selectedChannelCommonParamsPage->height());
    selectedChannelCommonParamsGrid->invalidate();
    selectedChannelCommonParamsGrid->activate();
    clickWidget(temperatureCommonSettingsButton, 150);
    activateLayouts(&window);
    const QRect commonBaudRowAfterChannel2Sensor(
        rs485BaudCombo->parentWidget()->mapTo(temperatureConfigCard, QPoint(0, 0)),
        rs485BaudCombo->parentWidget()->size());
    const QRect commonInternalRowAfterChannel2Sensor(
        commonInternalTemperatureEdit->parentWidget()->mapTo(temperatureConfigCard, QPoint(0, 0)),
        commonInternalTemperatureEdit->parentWidget()->size());
    require(std::abs(commonBaudRowAfterChannel2Sensor.left() - commonBaudRowRect.left()) <= 1 &&
                std::abs(commonInternalRowAfterChannel2Sensor.left() - commonInternalRowRect.left()) <= 1 &&
                commonBaudRowAfterChannel2Sensor.left() > commonAddressRowRect.right() &&
                commonInternalRowAfterChannel2Sensor.left() > commonOvertempRowRect.right(),
            "channel 2 sensor config switches to the same non-overlapping common-settings layout");
    clickWidget(temperatureConfigChannelButton1, 100);
    activateLayouts(&window);
    require(temperatureChannelConfigSubStack->currentWidget() != nullptr &&
                temperatureChannelConfigSubStack->currentWidget()->objectName() ==
                    QStringLiteral("temperatureChannelSensorConfigPageChannel1"),
            "switching back to channel 1 restores the sensor-config subpage");
    clickWidget(temperatureChannelCommonParamsButton, 100);
    activateLayouts(&window);
    for (int coefficient = 4; coefficient < 8; ++coefficient)
    {
        require(!polynomialEdits[static_cast<size_t>(coefficient)]->parentWidget()->isVisible(),
                "temperature polynomial fields hide outside the sensor config tab");
    }
    clickWidget(temperatureChannelSensorConfigButton, 100);
    activateLayouts(&window);
    require(sensorConfigGrid != nullptr &&
                sensorConfigGrid->horizontalSpacing() == 6 &&
                sensorConfigGrid->verticalSpacing() == 10 &&
                sensorConfigGrid->itemAtPosition(3, 0) == nullptr &&
                temperatureCalibrationDrawer->isVisible() &&
                !polynomialFields->isVisible(),
            "temperature sensor grid keeps the primary rows visible while A0-A7 stays in the collapsed calibration card");
    QWidget *sensorLastRow = ptCEdit ? ptCEdit->parentWidget() : nullptr;
    const QRect sensorFirstRowRect(ntcR0Edit->parentWidget()->mapTo(
                                       temperatureChannelConfigSubStack->currentWidget(), QPoint(0, 0)),
                                   ntcR0Edit->parentWidget()->size());
    const QRect sensorLastRowRect = sensorLastRow
        ? QRect(sensorLastRow->mapTo(temperatureChannelConfigSubStack->currentWidget(), QPoint(0, 0)),
                sensorLastRow->size())
        : QRect();
    const int sensorPageBottomUnusedHeight =
        temperatureChannelConfigSubStack->currentWidget()->height() - 1 - sensorLastRowRect.bottom();
    const QRect lowerBarRectForSensor(
        temperatureChannelSubTopBar->mapTo(temperatureConfigCard, QPoint(0, 0)),
        temperatureChannelSubTopBar->size());
    const QRect sensorLastRowRectInControlsCard(
        sensorLastRow->mapTo(temperatureControllerControlsCard, QPoint(0, 0)),
        sensorLastRow->size());
    const int sensorCardBottomGap =
        temperatureControllerControlsCard->height() - 1 - sensorLastRowRectInControlsCard.bottom();
    const int sensorLowerBarGap = lowerBarRectForSensor.top() - controlsCardRectInCard.bottom() - 1;
    require(polynomialEdits[7] != nullptr &&
                temperatureCalibrationOverlay->isAncestorOf(polynomialEdits[7]),
            "temperature sensor config retains the final polynomial input in the calibration card");
    require(temperatureChannelStack->height() >= temperatureChannelStack->currentWidget()->sizeHint().height() &&
                temperatureConfigCard->height() >= temperatureConfigCard->sizeHint().height(),
            "temperature primary sensor card reserves enough total height");
    require(sensorPageBottomUnusedHeight == sensorConfigGrid->horizontalSpacing() &&
                sensorCardBottomGap <= sensorConfigGrid->horizontalSpacing() + 2 &&
                sensorLowerBarGap == sensorConfigGrid->horizontalSpacing(),
            "temperature sensor card and lower selector keep the shared compact spacing");
    require(sensorConfigGrid->alignment() == (Qt::AlignTop | Qt::AlignLeft) &&
                sensorFirstRowRect.top() >= 0 &&
                sensorFirstRowRect.top() <= 4,
            "temperature sensor content starts at the shared first content row");
    require(sensorLastRowRect.bottom() < temperatureChannelConfigSubStack->currentWidget()->height() &&
                sensorPageBottomUnusedHeight >= 0,
            "temperature sensor primary rows remain inside the reserved content height");
    require(temperatureChannelStack->isAncestorOf(factoryResetButton) &&
                !temperatureChannelSelectorRow->isAncestorOf(factoryResetButton),
            "temperature factory reset button lives on the last common-settings row");
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
        require(plot->property("yAxisMinC").toDouble() == 24.0 &&
                    plot->property("yAxisMaxC").toDouble() == 26.0,
                "temperature trend plot centers the default axis range around the target temperature");
        require(plot->property("axisLabelsVisible").toBool(),
                "temperature trend plot exposes visible axis labels");
        const int xAxisTickCount = plot->property("xAxisTickCount").toInt();
        const bool timeAxisMode = plot->property("xAxisTimeMode").toBool();
        const bool xAxisTickCountIsValid = timeAxisMode
            ? xAxisTickCount >= 2 &&
                std::abs(plot->property("xAxisTimeSpanSeconds").toDouble() -
                         static_cast<double>(xAxisTickCount - 1)) < 1e-6
            : xAxisTickCount == 5;
        require(plot->property("yAxisTickCount").toInt() == 7 &&
                    xAxisTickCountIsValid,
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
                    plot->property("yAxisMaxC").toDouble() == 26.0,
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
        if (label->text() == QStringLiteral("波形监控") ||
            label->text() == QStringLiteral("Wave Monitor"))
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
    for (QGroupBox *group : {rawWaveGroup, harmonicWaveGroup, peakTrendGroup})
    {
        auto *outline = group->findChild<QWidget *>(QStringLiteral("tcpWaveCardOutline"),
                                                   Qt::FindDirectChildrenOnly);
        require(outline != nullptr && outline->property("tcpWaveCardOutline").toBool(),
                "each TCP wave subcard owns a dedicated outline layer");
        require(outline->geometry() == group->rect(),
                "each TCP wave subcard outline covers all four card edges");
        require(outline->testAttribute(Qt::WA_TransparentForMouseEvents),
                "TCP wave subcard outline does not intercept plot interaction");
    }
    requireLastStyleRuleContains(qApp->styleSheet(),
                                 QStringLiteral("QWidget#tcpWaveCardOutline {"),
                                 QStringLiteral("border: 1px solid %1")
                                     .arg(VaporView::appThemeColorName(
                                         VaporView::AppThemeColor::Border, false)),
                                 "light TCP wave subcard outline uses the shared border color");
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
        require(menu->property("shadowMargin").toInt() == 22 &&
                    menu->property("shadowBottomMargin").toInt() == 50,
                "TCP wave display menu reserves the shared horizontal and extended bottom popup shadow margins");
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
    constexpr int kExpectedResizeHandleSpacerGap =
        VaporView::Ground::MainSupport::kTopLevelCardSpacerAfterResizeHandle;
    require(std::abs((tcpWaveCardRect.top() - previousCardBottom - 1) -
                     kExpectedResizeHandleSpacerGap) <= 1,
            "collapsed TCP wave card keeps the shared resize-handle-adjusted gap to the card above it");
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
    require(std::abs((tcpWaveCardRect.top() - previousCardBottom - 1) -
                     kExpectedResizeHandleSpacerGap) <= 1,
            "raw-only TCP wave card keeps the shared resize-handle-adjusted gap to the card above it");
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
    auto *temperatureScrollAreaForTopGap =
        temperaturePage->findChild<QScrollArea *>(QStringLiteral("mainCardsScrollArea"));
    if (temperatureScrollAreaForTopGap && temperatureScrollAreaForTopGap->verticalScrollBar())
    {
        temperatureScrollAreaForTopGap->verticalScrollBar()->setValue(0);
        activateLayouts(&window);
    }
    QWidget *temperatureControllerCard = sensorGroupAncestor(temperaturePanel);
    require(temperatureControllerCard != nullptr,
            "temperature controller card can be identified from the controller panel");
    require(std::abs(widgetRectInCentral(temperatureControllerCard).left() - homePrimaryCardLeft) <= 1,
            "temperature page aligns its first card with the home page card left edge");
    require(std::abs(widgetRectInCentral(temperatureControllerCard).top() -
                     homePrimaryCardRect.top()) <= 1,
            "temperature page aligns its first card with the home page 12px top gap");
    auto *temperatureScrollAreaForSpacing =
        temperaturePage->findChild<QScrollArea *>(QStringLiteral("mainCardsScrollArea"));
    require(temperatureScrollAreaForSpacing != nullptr,
            "temperature page scroll area exists for horizontal margin checks");
    const bool temperaturePageScrollable =
        temperatureScrollAreaForSpacing->verticalScrollBar()->maximum() > 0;
    require(temperatureScrollAreaForSpacing->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded &&
                temperatureScrollAreaForSpacing->verticalScrollBar()->isVisible() == temperaturePageScrollable,
            "temperature page shows its vertical scrollbar only when content needs scrolling");
    const QRect temperatureScrollRect = widgetRectInCentral(temperatureScrollAreaForSpacing);
    require(std::abs(temperatureScrollRect.left() - mainPageStackCentralRect.left()) <= 1 &&
                std::abs(temperatureScrollRect.top() - mainPageStackCentralRect.top()) <= 1 &&
                std::abs(rightEdge(mainPageStackCentralRect) - rightEdge(temperatureScrollRect)) <= 1,
            "temperature scroll viewport fills the page while content reserves the card shadow inset");
    require(temperatureScrollAreaForSpacing->widget() != nullptr &&
                temperatureScrollAreaForSpacing->widget()->layout() != nullptr &&
                temperatureScrollAreaForSpacing->widget()->layout()->contentsMargins() ==
                    QMargins(kExpectedPageLeftInset,
                             kExpectedPageTopInset,
                             temperaturePageScrollable
                                 ? kExpectedHomeShadowSafeRightInset
                                 : kExpectedNoScrollPageRightInset,
                             VaporView::Ground::MainSupport::kMainContentBottomShadowSafeInset),
            "temperature page keeps the right-side card shadow gap in both scroll states");
    const int kExpectedMainCardToRightSidebarGap =
        kExpectedNoScrollPageRightInset +
        kExpectedHomeShadowSafeRightInset;
    require(std::abs((recordingCardRect.left() -
                      rightEdge(widgetRectInCentral(temperatureControllerCard))) -
                     kExpectedMainCardToRightSidebarGap) <= 1,
            "temperature page keeps the shared 18px card-to-right-sidebar gap");
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
    const bool deviceConfigPageScrollable = deviceConfigScrollArea->verticalScrollBar()->maximum() > 0;
    require(deviceConfigScrollArea->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded &&
                (!deviceConfigPageScrollable || deviceConfigScrollArea->verticalScrollBar()->isVisible()),
            "device configuration page only exposes scrolling when the remaining content needs it");
    const QRect deviceConfigScrollRect = widgetRectInCentral(deviceConfigScrollArea);
    require(std::abs(deviceConfigScrollRect.left() - mainPageStackCentralRect.left()) <= 1 &&
                std::abs(deviceConfigScrollRect.top() - mainPageStackCentralRect.top()) <= 1 &&
                std::abs(rightEdge(mainPageStackCentralRect) - rightEdge(deviceConfigScrollRect)) <= 1,
            "device configuration scroll viewport fills the page while content reserves the card shadow inset");
    require(deviceConfigScrollArea->widget() != nullptr &&
                deviceConfigScrollArea->widget()->layout() != nullptr &&
                deviceConfigScrollArea->widget()->layout()->contentsMargins() ==
                    QMargins(kExpectedPageLeftInset,
                             kExpectedPageTopInset,
                             deviceConfigPageScrollable
                                 ? kExpectedHomeShadowSafeRightInset
                                 : kExpectedNoScrollPageRightInset,
                             VaporView::Ground::MainSupport::kMainContentBottomShadowSafeInset),
            "device configuration page keeps the card shadow inset when its scrollbar is visible");
    require(deviceConfigScrollArea->horizontalScrollBar() != nullptr &&
                deviceConfigScrollArea->horizontalScrollBar()->maximum() == 0,
            "device configuration page fits horizontally at default window size");
    auto *deviceFirstCard =
        deviceConfigPage->findChild<QFrame *>(QStringLiteral("epsilonSectionCard"));
    require(deviceFirstCard != nullptr &&
                std::abs(widgetRectInCentral(deviceFirstCard).top() -
                         homePrimaryCardRect.top()) <= 1,
            "device configuration page places the link-status card on the first row");

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
    QToolButton *ai8TemperatureDeviceActionButton = nullptr;
    QToolButton *tcpWaveDeviceActionButton = nullptr;
    QPushButton *deviceAutoDetectButton = nullptr;
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
                        button->statusTip().isEmpty(),
                    "device configuration icon-only remote actions keep tooltip and accessibility text without redundant status-tip text");
            const bool tcpWaveAction = button->property("deviceConfigRemoteDevice").toInt() ==
                static_cast<int>(VaporView::SkyDeviceId::WaveTcp);
            require(button->toolTip().contains(QStringLiteral("本地串口设备")) ||
                        button->toolTip().contains(QStringLiteral("local serial device")) ||
                        (tcpWaveAction &&
                         (button->toolTip().contains(QStringLiteral("本地 TCP 波形")) ||
                          button->toolTip().contains(QStringLiteral("local TCP waveform")))),
                    "device configuration actions identify their local device mode");
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
            if (button->property("deviceConfigRemoteDevice").toInt() ==
                static_cast<int>(VaporView::SkyDeviceId::Ai8TemperatureController))
            {
                ai8TemperatureDeviceActionButton = button;
            }
            if (tcpWaveAction)
            {
                tcpWaveDeviceActionButton = button;
            }
            ++localDeviceActionCount;
        }
    }
    require(localDeviceActionCount == 7,
            "device configuration keeps one local action per device row");
    require(connectRemoteActionCount == 7 && disconnectRemoteActionCount == 0,
            "device configuration shows one connect action for every disconnected device");
    require(temperatureDeviceActionButton != nullptr,
            "device configuration exposes the RD105 connection action button");
    require(ai8TemperatureDeviceActionButton != nullptr,
            "device configuration exposes the AI-8288 connection action button");
    require(tcpWaveDeviceActionButton != nullptr,
            "device configuration exposes the TCP waveform connection action button");
    require(deviceAutoDetectButton != nullptr && deviceAutoDetectButton->width() <= 145,
            "device configuration auto-detect button uses compact title-bar width");

    QComboBox *devicePortCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceTemperaturePortCombo"));
    QComboBox *deviceRateCombo = nullptr;
    for (QComboBox *combo : deviceConfigPage->findChildren<QComboBox *>())
    {
        if (!combo->isVisible() || !combo->isEditable())
        {
            continue;
        }
        if (combo->currentText() == QStringLiteral("5") &&
            combo->objectName() != QStringLiteral("deviceAi8TemperatureRateCombo"))
        {
            deviceRateCombo = combo;
        }
    }
    require(devicePortCombo != nullptr && devicePortCombo->width() <= 112,
            "device configuration serial combo is sized for COM999");
    if (devicePortCombo)
    {
        const QColor expectedComboBg =
            VaporView::appThemeColor(VaporView::AppThemeColor::FieldBackground, false);
        const QImage comboImage = devicePortCombo->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        const QRect comboInterior = comboImage.rect().adjusted(2, 2, -2, -2);
        const int expectedPixels = countPixelsNearColor(comboImage, comboInterior, expectedComboBg, 0);
        if (expectedPixels < comboInterior.width() * comboInterior.height() / 4)
        {
            QComboBox *diagnosticSourceModeCombo = findSourceModeCombo(deviceConfigPage);
            std::cerr << "Device combo background diagnostic:"
                      << " enabled=" << devicePortCombo->isEnabled()
                      << " text=" << devicePortCombo->currentText().toStdString()
                      << " sourceMode="
                      << (diagnosticSourceModeCombo ? diagnosticSourceModeCombo->currentData().toString().toStdString() : std::string("<null>"))
                      << " appDark=" << VaporView::isDarkThemePalette(devicePortCombo->palette())
                      << " expectedPixels=" << expectedPixels
                      << " required=" << (comboInterior.width() * comboInterior.height() / 4)
                      << '\n';
        }
    }
    requireWidgetInteriorUsesBackground(
        devicePortCombo,
        VaporView::appThemeColor(VaporView::AppThemeColor::FieldBackground, false),
        "device configuration combo renders a pure white field interior");
    const QList<QPair<QString, const char *>> deviceLocalSerialCombos = {
        {QStringLiteral("deviceSkyTelemetryPortCombo"), "device sky telemetry serial combo uses select-plus-manual behavior"},
        {QStringLiteral("deviceEpsilonPortCombo"), "device EPSILON local serial combo uses select-plus-manual behavior"},
        {QStringLiteral("devicePressurePortCombo"), "device PTB local serial combo uses select-plus-manual behavior"},
        {QStringLiteral("deviceHumidityPortCombo"), "device HMP local serial combo uses select-plus-manual behavior"},
        {QStringLiteral("deviceLidarPortCombo"), "device Lidar local serial combo uses select-plus-manual behavior"},
        {QStringLiteral("deviceTemperaturePortCombo"), "device RD105 local serial combo uses select-plus-manual behavior"},
        {QStringLiteral("deviceAi8TemperaturePortCombo"), "device AI-8288 local serial combo uses select-plus-manual behavior"}};
    for (const auto& comboSpec : deviceLocalSerialCombos)
    {
        requireLocalSerialPortComboReady(deviceConfigPage->findChild<QComboBox *>(comboSpec.first), comboSpec.second);
    }
    requireLocalSerialPortComboReady(
        window.findChild<QComboBox *>(QStringLiteral("skyTelemetryPortCombo")),
        "home sky telemetry serial combo uses select-plus-manual behavior");
    const int manualAddIndex = devicePortCombo->findText(QStringLiteral("手动添加"));
    require(manualAddIndex >= 0,
            "device configuration local serial combo exposes the manual-add option");
    require(deviceRateCombo != nullptr && deviceRateCombo->width() <= 92,
            "device configuration rate combo is sized for 9999");
    require(devicePortCombo->isEnabled(),
            "device configuration serial combo is enabled in local mode");
    const QString originalDevicePort = localSerialPortValue(devicePortCombo);
    devicePortCombo->setCurrentIndex(0);
    processEventsFor(20);
    require(!temperatureDeviceActionButton->isEnabled(),
            "device configuration disables the local action when its serial port is cleared");
    devicePortCombo->setCurrentIndex(
        devicePortCombo->findData(QStringLiteral("__vv_manual_serial_port__")));
    processEventsFor(20);
    require(devicePortCombo->isEditable() && devicePortCombo->lineEdit() != nullptr,
            "device configuration local serial combo becomes editable only after manual-add is selected");
    require(devicePortCombo->lineEdit()->placeholderText() == QStringLiteral("输入串口..."),
            "local manual serial input uses the shortened Chinese placeholder");
    devicePortCombo->lineEdit()->setText(QStringLiteral("COM99"));
    QKeyEvent acceptManualPort(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(devicePortCombo->lineEdit(), &acceptManualPort);
    processEventsFor(20);
    require(!devicePortCombo->isEditable() &&
                localSerialPortValue(devicePortCombo) == QStringLiteral("COM99"),
            "device configuration manual serial entry is accepted and returns to select-only mode");
    require(temperatureDeviceActionButton->isEnabled(),
            "device configuration immediately enables the local action after selecting a serial port");
    devicePortCombo->setCurrentIndex(
        devicePortCombo->findData(QStringLiteral("__vv_manual_serial_port__")));
    processEventsFor(20);
    devicePortCombo->setCurrentIndex(0);
    processEventsFor(20);
    require(!devicePortCombo->isEditable() && localSerialPortValue(devicePortCombo).isEmpty(),
            "selecting unselected while entering a local serial port restores select-only mode");
    devicePortCombo->setCurrentIndex(
        devicePortCombo->findData(QStringLiteral("__vv_manual_serial_port__")));
    processEventsFor(20);
    require(devicePortCombo->isEditable() && devicePortCombo->lineEdit() != nullptr,
            "local serial manual entry can restart after selecting unselected");
    devicePortCombo->lineEdit()->setText(QStringLiteral("COM101"));
    QApplication::sendEvent(devicePortCombo->lineEdit(), &acceptManualPort);
    processEventsFor(20);
    require(!devicePortCombo->isEditable() &&
                localSerialPortValue(devicePortCombo) == QStringLiteral("COM101"),
            "restarted local serial manual entry accepts the new port");
    int originalDevicePortIndex = devicePortCombo->findData(originalDevicePort);
    if (originalDevicePortIndex < 0)
    {
        originalDevicePortIndex = devicePortCombo->findText(originalDevicePort);
    }
    devicePortCombo->setCurrentIndex(originalDevicePortIndex);
    processEventsFor(20);
    devicePortCombo->setCurrentIndex(devicePortCombo->findData(QStringLiteral("__vv_manual_serial_port__")));
    QMetaObject::invokeMethod(
        devicePortCombo,
        "activated",
        Qt::DirectConnection,
        Q_ARG(int, devicePortCombo->currentIndex()));
    processEventsFor(20);
    devicePortCombo->lineEdit()->setText(QString());
    QKeyEvent rejectEmptyManualPort(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(devicePortCombo->lineEdit(), &rejectEmptyManualPort);
    processEventsFor(20);
    require(!devicePortCombo->isEditable() &&
                localSerialPortValue(devicePortCombo) == originalDevicePort,
            "empty manual serial entry cancels and restores the previous local port");
    devicePortCombo->setCurrentIndex(devicePortCombo->findData(QStringLiteral("__vv_manual_serial_port__")));
    QMetaObject::invokeMethod(
        devicePortCombo,
        "activated",
        Qt::DirectConnection,
        Q_ARG(int, devicePortCombo->currentIndex()));
    processEventsFor(20);
    devicePortCombo->lineEdit()->setText(QStringLiteral("COM100"));
    QKeyEvent escapeManualPort(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(devicePortCombo->lineEdit(), &escapeManualPort);
    processEventsFor(20);
    require(!devicePortCombo->isEditable() &&
                localSerialPortValue(devicePortCombo) == originalDevicePort,
            "Esc cancels manual serial entry and restores the previous local port");
    devicePortCombo->setCurrentIndex(devicePortCombo->findData(QStringLiteral("__vv_manual_serial_port__")));
    QMetaObject::invokeMethod(
        devicePortCombo,
        "activated",
        Qt::DirectConnection,
        Q_ARG(int, devicePortCombo->currentIndex()));
    processEventsFor(20);
    devicePortCombo->lineEdit()->setText(QStringLiteral("/dev/ttyUSB0"));
    QFocusEvent manualFocusOut(QEvent::FocusOut, Qt::OtherFocusReason);
    QApplication::sendEvent(devicePortCombo->lineEdit(), &manualFocusOut);
    processEventsFor(60);
    require(!devicePortCombo->isEditable() &&
                localSerialPortValue(devicePortCombo) == QStringLiteral("/dev/ttyUSB0"),
            "manual serial entry accepts Linux-style paths on focus loss");
    originalDevicePortIndex = devicePortCombo->findData(originalDevicePort);
    if (originalDevicePortIndex < 0)
    {
        originalDevicePortIndex = devicePortCombo->findText(originalDevicePort);
    }
    devicePortCombo->setCurrentIndex(originalDevicePortIndex);
    processEventsFor(20);
    auto *deviceTemperaturePortCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceTemperaturePortCombo"));
    auto *deviceTemperatureBaudCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceTemperatureBaudCombo"));
    auto *deviceAi8TemperaturePortCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceAi8TemperaturePortCombo"));
    auto *deviceAi8TemperatureBaudCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceAi8TemperatureBaudCombo"));
    auto *deviceAi8TemperatureRateCombo =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceAi8TemperatureRateCombo"));
    auto *deviceSerialGrid = deviceAi8TemperaturePortCombo
        ? qobject_cast<QGridLayout *>(deviceAi8TemperaturePortCombo->parentWidget()->layout())
        : nullptr;
    auto labelInComboRow = [deviceSerialGrid](QWidget *combo, int column) -> QLabel * {
        if (!deviceSerialGrid || !combo)
        {
            return nullptr;
        }
        const int index = deviceSerialGrid->indexOf(combo);
        if (index < 0)
        {
            return nullptr;
        }
        int row = 0;
        int itemColumn = 0;
        int rowSpan = 0;
        int columnSpan = 0;
        deviceSerialGrid->getItemPosition(index, &row, &itemColumn, &rowSpan, &columnSpan);
        QLayoutItem *item = deviceSerialGrid->itemAtPosition(row, column);
        return item ? qobject_cast<QLabel *>(item->widget()) : nullptr;
    };
    QLabel *deviceAi8TemperatureLabel =
        labelInComboRow(deviceAi8TemperaturePortCombo, 0);
    QLabel *deviceAi8TemperatureRateLabel =
        labelInComboRow(deviceAi8TemperaturePortCombo, 3);
    QLabel *deviceTemperatureLabel =
        labelInComboRow(deviceTemperaturePortCombo, 0);
    require(deviceTemperaturePortCombo != nullptr,
            "device configuration temperature serial-port combo exists");
    require(deviceTemperatureBaudCombo != nullptr,
            "device configuration temperature baud-rate combo exists");
    require(deviceAi8TemperatureLabel != nullptr && deviceTemperatureLabel != nullptr &&
                deviceAi8TemperatureLabel->text().contains(QStringLiteral("AI-8288")) &&
                deviceAi8TemperatureLabel->objectName() == QStringLiteral("fieldLabel") &&
                deviceTemperatureLabel->objectName() == QStringLiteral("fieldLabel") &&
                deviceAi8TemperatureLabel->font().weight() == deviceTemperatureLabel->font().weight(),
            "device configuration AI-8288 label matches the RD105 field-label typography");
    require(deviceAi8TemperatureBaudCombo != nullptr &&
                deviceAi8TemperatureBaudCombo->count() == 6 &&
                deviceAi8TemperatureBaudCombo->currentData().toInt() == 19200 &&
                deviceAi8TemperatureBaudCombo->findData(4800) >= 0 &&
                deviceAi8TemperatureBaudCombo->findData(115200) >= 0,
            "device configuration exposes the documented AI-8288 baud-rate choices");
    require(deviceAi8TemperatureRateLabel != nullptr &&
                deviceAi8TemperatureRateLabel->text() == QStringLiteral("轮询:") &&
                deviceAi8TemperatureRateCombo != nullptr &&
                deviceAi8TemperatureRateCombo->currentData().toInt() == 5,
            "device configuration exposes the AI-8288 polling-rate selector");
    const QPoint deviceTemperaturePortPosition =
        deviceTemperaturePortCombo->mapTo(deviceConfigPage, QPoint(0, 0));
    const QPoint deviceAi8TemperaturePortPosition =
        deviceAi8TemperaturePortCombo != nullptr
            ? deviceAi8TemperaturePortCombo->mapTo(deviceConfigPage, QPoint(0, 0))
            : QPoint();
    require(deviceAi8TemperaturePortCombo != nullptr &&
                deviceAi8TemperaturePortPosition.y() > deviceTemperaturePortPosition.y() &&
                std::abs(deviceAi8TemperaturePortPosition.x() - deviceTemperaturePortPosition.x()) <= 1,
            "device configuration places the AI-8288 serial selector directly below RD105");
    require(std::abs(deviceAi8TemperatureBaudCombo->mapTo(deviceConfigPage, QPoint(0, 0)).x() -
                     deviceTemperatureBaudCombo->mapTo(deviceConfigPage, QPoint(0, 0)).x()) <= 1 &&
                deviceAi8TemperatureRateCombo->mapTo(deviceConfigPage, QPoint(0, 0)).x() >
                    deviceAi8TemperatureBaudCombo->mapTo(deviceConfigPage, QPoint(0, 0)).x(),
            "device configuration aligns all AI-8288 serial controls with the RD105 row");
    deviceAi8TemperatureBaudCombo->setCurrentIndex(
        deviceAi8TemperatureBaudCombo->findData(57600));
    processEventsFor(20);
    require(ai8BaudCombo->currentData().toInt() == 57600,
            "device configuration AI-8288 baud selection updates the global parameter editor");
    ai8BaudCombo->setCurrentIndex(ai8BaudCombo->findData(19200));
    processEventsFor(20);
    require(deviceAi8TemperatureBaudCombo->currentData().toInt() == 19200,
            "AI-8 global baud parameter writes back to device configuration");
    deviceConfigScrollArea->ensureWidgetVisible(deviceTemperaturePortCombo, 20, 20);
    processEventsFor(80);
    requireComboPopupFloatingContainer(deviceTemperaturePortCombo,
                                       "device serial-port combo opens with the shared rounded shadow popup");
    requireComboPopupStyled(deviceTemperatureBaudCombo,
                            "device baud-rate combo uses the shared native popup style");
    QComboBox *homePortCombo = temperaturePortCombo;
    require(homePortCombo != nullptr,
            "home serial combo matching the device configuration combo exists");
    if (homePortCombo->findText(expectedTemperaturePortText) < 0)
    {
        homePortCombo->addItem(expectedTemperaturePortText, expectedTemperaturePortText);
    }
    const QString syntheticPort = QStringLiteral("COM123");
    const int ai8ManualIndex = deviceAi8TemperaturePortCombo->findData(
        QStringLiteral("__vv_manual_serial_port__"));
    deviceAi8TemperaturePortCombo->insertItem(
        ai8ManualIndex >= 0 ? ai8ManualIndex : deviceAi8TemperaturePortCombo->count(),
        syntheticPort,
        syntheticPort);
    deviceAi8TemperaturePortCombo->setCurrentIndex(
        deviceAi8TemperaturePortCombo->findData(syntheticPort));
    processEventsFor(50);
    require(ai8TitlePortCombo->currentData().toString() == syntheticPort,
            "device configuration AI-8288 serial selection updates the temperature-page title selector");
    ai8TitlePortCombo->setCurrentIndex(0);
    processEventsFor(50);
    require(localSerialPortValue(deviceAi8TemperaturePortCombo).isEmpty(),
            "temperature-page AI-8 title selector writes back to device configuration");
    if (homePortCombo->findText(syntheticPort) < 0)
    {
        homePortCombo->addItem(syntheticPort, syntheticPort);
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
                localSerialPortValue(devicePortCombo) == syntheticPort,
            "device configuration serial combo can select an existing serial item");
    require(localSerialPortValue(homePortCombo) == syntheticPort,
            "device configuration serial selection mirrors back to the home combo");
    require(temperatureTitlePortCombo->currentText() == syntheticPort,
            "temperature title serial selector follows the canonical RD105 port selection");
    require(std::abs((temperatureTitlePortCombo->width() -
                      temperatureTitlePortCombo->fontMetrics().horizontalAdvance(syntheticPort)) -
                     initialTitlePortChromeWidth) <= 1 &&
                titlePortTextToArrowGap(temperatureTitlePortCombo) >= 0 &&
                titlePortTextToArrowGap(temperatureTitlePortCombo) <= titlePortCharacterWidth + 2,
            "temperature title serial selector stays compact for a longer COM port name");
    temperatureTitlePortCombo->showPopup();
    processEventsFor(120);
    VaporView::SingleLevelPopupMenuRow *syntheticPortRow = nullptr;
    for (VaporView::SingleLevelPopupMenuRow *row : temperatureTitlePortMenu->rows())
    {
        if (row && row->text() == syntheticPort)
        {
            syntheticPortRow = row;
            break;
        }
    }
    require(syntheticPortRow != nullptr &&
                syntheticPortRow->width() >=
                    syntheticPortRow->textLabel()->fontMetrics().horizontalAdvance(syntheticPort) + 32 &&
                !syntheticPortRow->property("hasCheckIcon").toBool() &&
                syntheticPortRow->checkLabel()->width() == 0,
            "temperature title serial popup keeps longer port options unclipped without a selection-check slot");
    temperatureTitlePortCombo->hidePopup();
    processEventsFor(40);
    const int originalTitlePortIndex = temperatureTitlePortCombo->findText(expectedTemperaturePortText);
    require(originalTitlePortIndex >= 0,
            "temperature title serial selector retains the original RD105 port option");
    temperatureTitlePortCombo->setCurrentIndex(originalTitlePortIndex);
    processEventsFor(50);
    activateLayouts(&window);
    require(localSerialPortValue(homePortCombo) == expectedTemperaturePortText &&
                localSerialPortValue(deviceTemperaturePortCombo) == expectedTemperaturePortText,
            "temperature title serial selection writes back to home and device configuration controls");
    const int syntheticTitlePortIndex = temperatureTitlePortCombo->findText(syntheticPort);
    require(syntheticTitlePortIndex >= 0,
            "temperature title serial selector retains refreshed synthetic port options");
    temperatureTitlePortCombo->setCurrentIndex(syntheticTitlePortIndex);
    processEventsFor(50);
    activateLayouts(&window);
    require(localSerialPortValue(homePortCombo) == syntheticPort &&
                localSerialPortValue(deviceTemperaturePortCombo) == syntheticPort,
            "temperature title serial selector can restore the refreshed RD105 port");

    QGroupBox *serialConfigCard = qobject_cast<QGroupBox *>(sensorGroupAncestor(devicePortCombo));
    require(serialConfigCard != nullptr,
            "device configuration serial card can be identified from the serial controls");
    requireTopLevelCardElevation(serialConfigCard,
                                 1.0,
                                 "device serial configuration card uses the shared soft elevation");
    require(serialConfigCard->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding,
            "device configuration serial card expands to fill its row");
    const QRect serialConfigPageRect(serialConfigCard->mapTo(deviceConfigPage, QPoint(0, 0)),
                                     serialConfigCard->size());
    require(std::abs(widgetRectInCentral(serialConfigCard).left() - homePrimaryCardLeft) <= 1,
            "device configuration page aligns the serial card with the home page card left edge");
    const int expectedDeviceConfigCardWidth =
        deviceConfigScrollArea->viewport()->width() -
        kExpectedPageLeftInset -
        (deviceConfigPageScrollable
             ? kExpectedHomeShadowSafeRightInset
             : kExpectedNoScrollPageRightInset);
    require(std::abs(serialConfigCard->width() - expectedDeviceConfigCardWidth) <= 4,
            "device configuration serial card fills the main content row");
    requireCardTitleBar(serialConfigCard,
                        QStringList{QStringLiteral("设备配置 [本机]"), QStringLiteral("Device Configuration [Local]")},
                        QStringLiteral("usb"),
                        "device serial configuration card uses the standard icon title bar");
    const QString appStyleSheet = qApp->styleSheet();
    const int serialCardStyleIndex = appStyleSheet.indexOf(
        QStringLiteral("QGroupBox#sensorGroupBox[vaporViewTopLevelCard=\"true\"]"));
    require(serialCardStyleIndex >= 0 &&
                appStyleSheet.mid(serialCardStyleIndex, 480).contains(QStringLiteral("border-radius: 12px")) &&
                appStyleSheet.mid(serialCardStyleIndex, 480).contains(QStringLiteral("rgba(0, 0, 0, 0.04)")),
            "serial configuration card uses the shared 12px radius and subtle outline");

    const QRect deviceRateRect(deviceRateCombo->mapTo(deviceConfigPage, QPoint(0, 0)),
                               deviceRateCombo->size());
    bool foundRemoteButtonsToRightOfRate = false;
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
        if (buttonRect.left() > deviceRateRect.right() &&
            std::abs(buttonRect.center().y() - deviceRateRect.center().y()) <= 2)
        {
            foundRemoteButtonsToRightOfRate = true;
        }
    }
    require(foundRemoteButtonsToRightOfRate,
            "device configuration remote actions sit to the right of the rate selector");

    auto *combinationPageForEpsilonGeometry =
        window.findChild<VaporView::Ground::Navigation::CombinationNavigationPage *>();
    auto *epsilonPanelForGeometry = combinationPageForEpsilonGeometry
        ? combinationPageForEpsilonGeometry->epsilonConfigPanel()
        : nullptr;
    require(combinationPageForEpsilonGeometry != nullptr && epsilonPanelForGeometry != nullptr,
            "combination navigation exposes its EPSILON configuration panel for geometry checks");
    QWidget *pageBeforeEpsilonGeometryCheck = mainPageStackForScroll->currentWidget();
    combinationPageForEpsilonGeometry->setCurrentSection(
        VaporView::Ground::Navigation::CombinationNavigationPage::Section::Epsilon);
    mainPageStackForScroll->setCurrentWidget(combinationPageForEpsilonGeometry);
    processEventsFor(120);
    activateLayouts(epsilonPanelForGeometry);
    QFrame *epsilonConfigCard = epsilonPanelForGeometry;
    auto *epsilonStatusCard = epsilonPanelForGeometry->findChild<QFrame *>(
        QStringLiteral("epsilonStatusCard"));
    auto *epsilonOutputCard = epsilonPanelForGeometry->findChild<QFrame *>(
        QStringLiteral("epsilonOutputCard"));
    auto *epsilonDeviceSettingsCard = epsilonPanelForGeometry->findChild<QFrame *>(
        QStringLiteral("epsilonDeviceSettingsCard"));
    require(epsilonConfigCard != nullptr &&
                combinationPageForEpsilonGeometry->isAncestorOf(epsilonConfigCard) &&
                !deviceConfigPage->isAncestorOf(epsilonConfigCard) &&
                epsilonStatusCard != nullptr && epsilonOutputCard != nullptr &&
                epsilonDeviceSettingsCard != nullptr,
            "combination navigation owns the EPSILON packet-rate panel exclusively");
    require(!epsilonConfigCard->property(VaporView::kTopLevelCardProperty).toBool(),
            "EPSILON panel root is an unframed layout container");
    for (QFrame *sectionCard : {epsilonStatusCard, epsilonOutputCard, epsilonDeviceSettingsCard})
    {
        requireTopLevelCardElevation(sectionCard, 1.0,
                                     "EPSILON business section uses the shared soft elevation");
    }
    requireCardTitleBar(epsilonStatusCard,
                        QStringList{QStringLiteral("配置摘要"), QStringLiteral("Configuration Summary")},
                        QStringLiteral("satellite"),
                        "EPSILON summary uses the standard icon title bar");
    requireCardTitleBar(epsilonOutputCard,
                        QStringList{QStringLiteral("报文通信频率"), QStringLiteral("Packet Communication Rates")},
                        QStringLiteral("activity"),
                        "EPSILON output uses the standard icon title bar");
    requireCardTitleBar(epsilonDeviceSettingsCard,
                        QStringList{QStringLiteral("设备设置"), QStringLiteral("Device Settings")},
                        QStringLiteral("sliders-vertical"),
                        "EPSILON device settings use the standard icon title bar");
    QList<uint> packetRateIds;
    for (QComboBox *combo : epsilonConfigCard->findChildren<QComboBox *>())
    {
        if (combo->property("epsilonPacketId").isValid())
        {
            packetRateIds.append(combo->property("epsilonPacketId").toUInt());
        }
    }
    std::sort(packetRateIds.begin(), packetRateIds.end());
    const QList<uint> expectedPacketRateIds = {
        0x40, 0x41, 0x42, 0x50, 0x53, 0x59, 0x5A, 0x5C, 0x5D, 0x63, 0x64};
    require(packetRateIds == expectedPacketRateIds,
            "EPSILON configuration panel exposes all 11 packet-rate controls");
    require(epsilonConfigCard->isVisible(), "EPSILON configuration panel is visible in local mode");
    require(epsilonPanelForGeometry->styleSheet().contains(
                QStringLiteral("QFrame[epsilonConfigCard=\"true\"]")) &&
                epsilonPanelForGeometry->styleSheet().contains(
                    VaporView::appThemeColorName(VaporView::AppThemeColor::SurfaceRaised,
                                                  VaporView::isDarkThemeEnabled())),
            "EPSILON section styling uses local theme tokens without a parallel color system");
    const QRect epsilonStatusBounds(epsilonStatusCard->mapTo(epsilonConfigCard, QPoint(0, 0)),
                                    epsilonStatusCard->size());
    const QRect epsilonOutputBounds(epsilonOutputCard->mapTo(epsilonConfigCard, QPoint(0, 0)),
                                    epsilonOutputCard->size());
    const QRect epsilonDeviceSettingsBounds(
        epsilonDeviceSettingsCard->mapTo(epsilonConfigCard, QPoint(0, 0)),
        epsilonDeviceSettingsCard->size());
    require(epsilonStatusBounds.top() < epsilonOutputBounds.top() &&
                epsilonOutputBounds.top() < epsilonDeviceSettingsBounds.top(),
            "EPSILON section cards follow summary, output, device-settings order");
    auto *epsilonActionsContainer = epsilonConfigCard->findChild<QWidget *>(
        QStringLiteral("epsilonActionsContainer"));
    require(epsilonActionsContainer != nullptr,
            "EPSILON panel exposes a separate primary-actions footer");
    const QRect epsilonActionsBounds(
        epsilonActionsContainer->mapTo(epsilonConfigCard, QPoint(0, 0)),
        epsilonActionsContainer->size());
    require(epsilonActionsBounds.top() > epsilonDeviceSettingsBounds.bottom(),
            "EPSILON primary-actions footer follows device settings");
    auto *epsilonConfigScrollArea = combinationPageForEpsilonGeometry->findChild<QScrollArea *>(
        QStringLiteral("epsilonConfigScrollArea"));
    require(epsilonConfigScrollArea != nullptr &&
                epsilonConfigScrollArea->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff &&
                epsilonConfigScrollArea->horizontalScrollBar()->maximum() == 0,
            "EPSILON page avoids a horizontal scrollbar at the default window size");
    constexpr int kEpsilonPacketVisualColumnCount = 4;
    std::array<int, kEpsilonPacketVisualColumnCount> packetComboColumnLefts;
    packetComboColumnLefts.fill(-1);
    for (QComboBox *combo : epsilonOutputCard->findChildren<QComboBox *>())
    {
        if (!combo->isVisible() || !combo->property("epsilonPacketId").isValid())
        {
            continue;
        }
        const QRect comboRect(combo->mapTo(epsilonOutputCard, QPoint(0, 0)), combo->size());
        require(QRect(QPoint(0, 0), epsilonOutputCard->size()).contains(comboRect),
                "EPSILON packet-rate combos stay inside the panel");
        require(combo->width() >= combo->fontMetrics().horizontalAdvance(combo->currentText()) + 44,
                "EPSILON packet-rate combo text is not clipped");
        QLabel *packetLabel = nullptr;
        for (QLabel *label : epsilonOutputCard->findChildren<QLabel *>())
        {
            if (label->property("epsilonPacketId").isValid() &&
                label->property("epsilonPacketId").toUInt() == combo->property("epsilonPacketId").toUInt())
            {
                packetLabel = label;
                break;
            }
        }
        require(packetLabel != nullptr && packetLabel->isVisible(),
                "EPSILON packet-rate row has a matching label");
        require(!packetLabel->text().contains(QStringLiteral("最大")) &&
                    !packetLabel->text().contains(QStringLiteral("Max")),
                "EPSILON packet-rate labels omit max-rate text");
        const QRect labelRect(packetLabel->mapTo(epsilonOutputCard, QPoint(0, 0)),
                              packetLabel->size());
        require(comboRect.left() > labelRect.right(),
                "EPSILON packet-rate combo sits to the right of its label");
        require(std::abs(comboRect.center().y() - labelRect.center().y()) <= 3,
                "EPSILON packet-rate label and combo are vertically aligned");
        require(combo->property("epsilonPacketGridColumn").isValid(),
                "EPSILON packet-rate combo reports its visual column");
        const int visualColumn = combo->property("epsilonPacketGridColumn").toInt();
        require(visualColumn >= 0 && visualColumn < kEpsilonPacketVisualColumnCount,
                "EPSILON packet-rate combo column is valid");
        int& expectedColumnLeft = packetComboColumnLefts[visualColumn];
        if (expectedColumnLeft < 0)
        {
            expectedColumnLeft = comboRect.left();
        }
        else
        {
            require(std::abs(comboRect.left() - expectedColumnLeft) <= 2,
                    "EPSILON packet-rate combos align vertically within each column");
        }
    }
    int visiblePacketColumns = 0;
    int previousPacketColumnLeft = -1;
    for (int columnLeft : packetComboColumnLefts)
    {
        if (columnLeft < 0)
        {
            continue;
        }
        require(previousPacketColumnLeft < 0 || columnLeft > previousPacketColumnLeft,
                "EPSILON packet-rate visual columns progress left-to-right");
        previousPacketColumnLeft = columnLeft;
        ++visiblePacketColumns;
    }
    require(visiblePacketColumns >= 2,
            "EPSILON packet-rate layout exposes multiple aligned visual columns");
    auto requireActionButton = [](QWidget *owner,
                                  const QString& objectName,
                                  const QStringList& expectedTexts,
                                  const QRect& ownerBounds,
                                  const char *message) {
        auto *button = owner->findChild<QPushButton *>(objectName);
        require(button != nullptr && button->isVisible(), message);
        require(expectedTexts.contains(button->text()), message);
        const QRect buttonRect(button->mapTo(owner, QPoint(0, 0)), button->size());
        require(ownerBounds.contains(buttonRect), message);
        require(button->width() >= button->fontMetrics().horizontalAdvance(button->text()) + 32,
                "EPSILON action button text is not clipped");
        require(button->focusPolicy() == Qt::TabFocus && !button->accessibleName().isEmpty(),
                "EPSILON action button remains keyboard accessible");
        return buttonRect;
    };
    const QRect epsilonDeviceLocalBounds(QPoint(0, 0), epsilonDeviceSettingsCard->size());
    const QRect recommendedRect = requireActionButton(
        epsilonOutputCard, QStringLiteral("epsilonRecommendedConfigButton"),
        {QStringLiteral("恢复推荐"), QStringLiteral("Recommended")},
        QRect(QPoint(0, 0), epsilonOutputCard->size()), "EPSILON packet communication title bar exposes the recommended action");
    const QRect rtcmRect = requireActionButton(
        epsilonDeviceSettingsCard, QStringLiteral("epsilonRtcmPortButton"),
        {QStringLiteral("配置RTCM串口"), QStringLiteral("RTCM Port")},
        epsilonDeviceLocalBounds, "EPSILON device settings expose the RTCM action");
    auto *epsilonRtcmDevicePortCombo =
        epsilonDeviceSettingsCard->findChild<QComboBox *>(QStringLiteral("epsilonRtcmDevicePortCombo"));
    require(epsilonRtcmDevicePortCombo != nullptr &&
                epsilonRtcmDevicePortCombo->property("epsilonRtcmDevicePortControl").toBool() &&
                epsilonRtcmDevicePortCombo->count() == 4,
            "EPSILON device settings expose the RTCM input selector directly on the card");
    const QRect epsilonRtcmDevicePortRect(
        epsilonRtcmDevicePortCombo->mapTo(epsilonDeviceSettingsCard, QPoint(0, 0)),
        epsilonRtcmDevicePortCombo->size());
    require(epsilonDeviceLocalBounds.contains(epsilonRtcmDevicePortRect) &&
                epsilonRtcmDevicePortRect.right() < rtcmRect.left(),
            "EPSILON RTCM input selector stays inline before the configure action");
    require(epsilonRtcmDevicePortCombo->focusPolicy() == Qt::TabFocus &&
                !epsilonRtcmDevicePortCombo->accessibleName().isEmpty(),
            "EPSILON RTCM input selector remains keyboard accessible");
    const QRect reconfigureRect = requireActionButton(
        epsilonDeviceSettingsCard, QStringLiteral("epsilonReconfigureButton"),
        {QStringLiteral("应用已保存配置"), QStringLiteral("Apply Saved Configuration")},
        epsilonDeviceLocalBounds, "EPSILON device settings expose the reconfigure action");
    require(epsilonDeviceSettingsCard->findChild<QPushButton *>(QStringLiteral("epsilonRtkConfigButton")) == nullptr,
            "EPSILON device settings route differential navigation through the top navigation bar");
    const QRect saveRect = requireActionButton(
        epsilonActionsContainer, QStringLiteral("epsilonSaveButton"),
        {QStringLiteral("保存并应用"), QStringLiteral("Save + Apply")},
        QRect(QPoint(0, 0), epsilonActionsContainer->size()),
        "EPSILON footer exposes the save-and-apply primary action");
    Q_UNUSED(recommendedRect);
    Q_UNUSED(rtcmRect);
    Q_UNUSED(reconfigureRect);
    require(saveRect.top() + epsilonActionsBounds.top() > epsilonDeviceSettingsBounds.bottom(),
            "EPSILON save-and-apply action follows device settings");
    require(epsilonOutputCard->findChild<QPushButton *>(QStringLiteral("epsilonSaveButton")) == nullptr,
            "EPSILON actions remain in their business sections");

    mainPageStackForScroll->setCurrentWidget(pageBeforeEpsilonGeometryCheck);
    processEventsFor(80);

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
            if (label->text().contains(QStringLiteral("数据源与天地链路")) ||
                label->text().contains(QStringLiteral("Data Source / Sky Link")))
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
                        QStringList{QStringLiteral("数据源与天地链路"), QStringLiteral("Data Source / Sky Link")},
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
        return a->mapTo(a->parentWidget(), QPoint(0, 0)).x() <
               b->mapTo(b->parentWidget(), QPoint(0, 0)).x();
    });
    const QVector<QStringList> expectedTelemetrySubCardTitles = {
        {QStringLiteral("数据频率"), QStringLiteral("Data stream rates")},
        {QStringLiteral("链路速率"), QStringLiteral("Link rate")},
        {QStringLiteral("数据"), QStringLiteral("Data")},
    };
    int previousSubCardRight = -1;
    int previousSubCardTop = -1;
    for (int i = 0; i < telemetrySubCards.size(); ++i)
    {
        QFrame *subCard = telemetrySubCards.at(i);
        const QRect subCardRect(subCard->mapTo(deviceTelemetrySummaryCard, QPoint(0, 0)), subCard->size());
        require(previousSubCardRight < 0 || subCardRect.left() > previousSubCardRight,
                "device telemetry summary subcards are arranged horizontally");
        require(previousSubCardTop < 0 || std::abs(subCardRect.top() - previousSubCardTop) <= 2,
                "device telemetry summary subcards share a top edge");
        require(subCard->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed &&
                    std::abs(subCardRect.width() - subCard->sizeHint().width()) <= 2,
                "device telemetry summary subcard width follows its widest content row");
        previousSubCardRight = subCardRect.right();
        previousSubCardTop = subCardRect.top();

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
        require(titleRect.left() >= titlePaneRect.left() &&
                    titleRect.right() <= titlePaneRect.right(),
                "device telemetry summary subcard title fits its left-side pane");
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
            QStringList dataFieldNames;
            const QList<QFrame*> dataPills =
                subCard->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
            require(!dataPills.isEmpty(),
                    "device telemetry availability subcard has value pills");
            for (QFrame *pill : dataPills)
            {
                QLabel *nameLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel"));
                QLabel *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
                require(nameLabel != nullptr && valueLabel != nullptr,
                        "device telemetry data pill has a name and value");
                dataFieldNames << nameLabel->text();
                const QString name = nameLabel->text();
                const QString text = valueLabel->text();
                const bool statusField =
                    name == QStringLiteral("记录") ||
                    name == QStringLiteral("Record") ||
                    name == QStringLiteral("磁盘") ||
                    name == QStringLiteral("Disk") ||
                    name == QStringLiteral("CRC");
                if (!statusField)
                {
                    require(text == QStringLiteral("有") ||
                                text == QStringLiteral("无") ||
                                text == QStringLiteral("Yes") ||
                                text == QStringLiteral("No"),
                            "device telemetry availability values use compact yes/no text");
                }
            }
            require(dataFieldNames.contains(QStringLiteral("记录")) ||
                        dataFieldNames.contains(QStringLiteral("Record")),
                    "device telemetry data subcard shows the remote recording state");
            require(dataFieldNames.contains(QStringLiteral("磁盘")) ||
                        dataFieldNames.contains(QStringLiteral("Disk")),
                    "device telemetry data subcard shows remaining sky disk capacity");
            require(dataFieldNames.contains(QStringLiteral("CRC")),
                    "device telemetry data subcard shows CRC error count");
        }
        require(leftmostPillLeft > titlePaneRect.right(),
                "device telemetry summary subcard title sits in a separate left area");
    }
    const QRect telemetrySummaryPageRect(deviceTelemetrySummaryCard->mapTo(deviceConfigPage, QPoint(0, 0)),
                                         deviceTelemetrySummaryCard->size());
    require(std::abs(widgetRectInCentral(deviceTelemetrySummaryCard).left() - homePrimaryCardLeft) <= 1,
            "device telemetry summary card aligns with the home page card left edge");
    require(std::abs(deviceTelemetrySummaryCard->width() - expectedDeviceConfigCardWidth) <= 4,
            "device telemetry summary card fills the main content row");
    require(telemetrySummaryPageRect.top() < serialConfigPageRect.top() &&
                serialConfigPageRect.top() - telemetrySummaryPageRect.bottom() <=
                    kExpectedTopLevelCardGap + 2,
            "device telemetry summary card sits on the first row above the serial configuration card");
    require(std::abs(telemetrySummaryPageRect.left() - serialConfigPageRect.left()) <= 2 &&
                std::abs(telemetrySummaryPageRect.right() - serialConfigPageRect.right()) <= 2,
            "device telemetry summary and serial configuration cards share the full-row width");
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
    require(deviceSourceModeCombo == nullptr,
            "device configuration no longer exposes the title-bar source mode combo");
    auto *deviceSourceModeSwitch =
        deviceConfigPage->findChild<QPushButton *>(QStringLiteral("deviceConfigSourceModeOverviewSwitch"));
    require(deviceSourceModeSwitch != nullptr &&
                deviceSourceModeSwitch->property("segmentedSwitchControl").toBool() &&
                deviceSourceModeSwitch->focusPolicy() == Qt::TabFocus,
            "device configuration source mode uses the shared segmented switch");
    auto *homeSourceModeSwitch =
        window.findChild<QPushButton *>(QStringLiteral("sourceModeOverviewSwitch"));
    require(homeSourceModeSwitch != nullptr &&
                deviceSourceModeSwitch->size() == homeSourceModeSwitch->size(),
            "device configuration source switch reuses the home title-bar switch size");
    auto setDeviceSourceModeRemote = [&](bool remote) {
        if (deviceSourceModeSwitch->isChecked() != remote)
        {
            const QPoint target(remote ? deviceSourceModeSwitch->width() * 3 / 4
                                       : deviceSourceModeSwitch->width() / 4,
                                deviceSourceModeSwitch->height() / 2);
            clickWidgetAt(deviceSourceModeSwitch, target, 0);
        }
        processEventsFor(150);
        activateLayouts(&window);
        require(deviceSourceModeSwitch->isChecked() == remote,
                "device configuration source switch reaches the requested source mode");
    };
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
    require(deviceSkyTelemetry.row && !deviceSkyTelemetry.row->isVisible(),
            "local device configuration hides sky-ground telemetry edit controls");
    setDeviceSourceModeRemote(true);
    require(deviceSkyTelemetry.row->isVisible(),
            "remote device configuration shows sky-ground telemetry edit controls");
    setSkyTelemetryTransport(deviceSkyTelemetry.transportCombo, QStringLiteral("tcp"));
    processEventsFor(100);
    activateLayouts(&window);
    requireSkyTelemetryTcpMode(deviceSkyTelemetry);
    requireWidgetInteriorUsesBackground(
        deviceSkyTelemetry.tcpHostEdit,
        VaporView::appThemeColor(VaporView::AppThemeColor::FieldBackground, false),
        "device configuration line edit renders a pure white field interior");
    requireWidgetInteriorUsesBackground(
        deviceSkyTelemetry.tcpPortSpin,
        VaporView::appThemeColor(VaporView::AppThemeColor::FieldBackground, false),
        "device configuration spin box renders a pure white field interior");
    setSkyTelemetryTransport(deviceSkyTelemetry.transportCombo, QStringLiteral("serial"));
    processEventsFor(100);
    activateLayouts(&window);
    requireSkyTelemetrySerialMode(deviceSkyTelemetry);
    setSkyTelemetryTransport(deviceSkyTelemetry.transportCombo, QStringLiteral("tcp"));
    processEventsFor(100);
    activateLayouts(&window);
    require(epsilonConfigCard->isEnabled(),
            "EPSILON configuration panel keeps the shared detailed UI available in sky-ground remote mode");
    require(deviceTelemetrySummaryCard->isVisible(),
            "device telemetry summary remains visible after switching to sky-ground remote mode");
    requireSameRect(epsilonConfigCard->geometry(), localEpsilonConfigRect, 2,
                    "EPSILON configuration panel geometry is stable in sky-ground remote mode");
    requireSameRect(deviceTelemetrySummaryCard->geometry(), localTelemetrySummaryRect, 2,
                    "device telemetry summary geometry is stable in sky-ground remote mode");
    setDeviceSourceModeRemote(false);
    require(recordingStatusView->toolTip().contains(QStringLiteral("记录：未记录")) &&
                !recordingStatusView->toolTip().contains(QStringLiteral("天空端记录")),
            "recording status returns to local text after switching back from remote mode");
    const int restoredRecordingStatusBottomGap = recordingStatusBottomGap();
    require(restoredRecordingStatusBottomGap >= 4 && restoredRecordingStatusBottomGap <= 6,
            "local recording status bottom padding is restored after remote mode");
    require(epsilonConfigCard->isEnabled(),
            "EPSILON configuration panel is available after switching back to local mode");
    require(deviceTelemetrySummaryCard->isVisible(),
            "device telemetry summary remains visible after switching back to local mode");
    requireSameRect(epsilonConfigCard->geometry(), localEpsilonConfigRect, 2,
                    "EPSILON configuration panel geometry is stable after switching back to local mode");
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
    require(environmentGroup->findChild<QWidget *>(QStringLiteral("environmentTrendPanel")) == nullptr,
            "environment card does not use a separate trend-title panel");
    QStringList environmentTrendPlotNames = {
        QStringLiteral("environmentTemperatureTrendPlot"),
        QStringLiteral("environmentHumidityTrendPlot"),
        QStringLiteral("environmentPressureTrendPlot")
    };
    const auto isTwoDigitNumber = [](const QString& text) {
        return text.size() == 2 && text.at(0).isDigit() && text.at(1).isDigit();
    };
    const auto isHourNumber = [](const QString& text) {
        return (text.size() == 1 || text.size() == 2) &&
            std::all_of(text.cbegin(), text.cend(), [](const QChar& character) {
                return character.isDigit();
            });
    };
    for (const QString& plotName : environmentTrendPlotNames)
    {
        QWidget *plot = environmentGroup->findChild<QWidget *>(plotName);
        require(plot != nullptr && plot->height() >= 30,
                "environment trend plot exists with a readable compact height");
        require(plot->width() >= environmentGroup->width() * 4 / 5,
                "environment trend plot uses most of the environment card width");
        require(plot->mapTo(environmentGroup, QPoint(0, plot->height())).y() <=
                    environmentGroup->contentsRect().bottom() + 1,
                "environment trend plot stays inside the environment card");
        require(plot->property("xAxisLabelText").toString().isEmpty() &&
                    plot->property("xAxisLeftLabel").toString().contains(QLatin1Char(':')) &&
                    plot->property("xAxisRightLabel").toString().contains(QLatin1Char(':')),
                "environment trend plot exposes clock-time x-axis tick labels");
        const QStringList xAxisTickLabels = plot->property("xAxisTickLabels").toStringList();
        bool subsequentLabelsUseMinutesAndSeconds = true;
        for (int index = 1; index < xAxisTickLabels.size(); ++index)
        {
            const QStringList parts = xAxisTickLabels.at(index).split(QLatin1Char(':'));
            if (parts.size() != 2 || !isTwoDigitNumber(parts.at(0)) ||
                !isTwoDigitNumber(parts.at(1)))
            {
                subsequentLabelsUseMinutesAndSeconds = false;
                break;
            }
        }
        const QStringList firstLabelParts = xAxisTickLabels.isEmpty()
            ? QStringList()
            : xAxisTickLabels.first().split(QLatin1Char(':'));
        const bool firstLabelIncludesHour = firstLabelParts.size() == 3 &&
            isHourNumber(firstLabelParts.at(0)) &&
            isTwoDigitNumber(firstLabelParts.at(1)) &&
            isTwoDigitNumber(firstLabelParts.at(2));
        require(plot->property("xAxisTickCount").toInt() == xAxisTickLabels.size() &&
                    xAxisTickLabels.size() > 2 &&
                    firstLabelIncludesHour && subsequentLabelsUseMinutesAndSeconds,
                "environment trend plot shows hour only on the first tick and compact minute-second labels after it");
        require(std::abs(plot->property("xAxisTimeSpanSeconds").toDouble() -
                         static_cast<double>(xAxisTickLabels.size() - 1)) < 1e-6,
                "environment trend plot uses temperature-style one-second x-axis intervals");
        require(!plot->property("yAxisUnitLabel").toString().isEmpty(),
                "environment trend plot keeps y-axis unit metadata");
        require(!plot->property("yAxisTopLabel").toString().isEmpty() &&
                    !plot->property("yAxisMiddleLabel").toString().isEmpty() &&
                    !plot->property("yAxisBottomLabel").toString().isEmpty(),
                "environment trend plot exposes numeric y-axis tick labels");
    }
    QWidget *pressurePlot = environmentGroup->findChild<QWidget *>(QStringLiteral("environmentPressureTrendPlot"));
    QWidget *temperaturePlot = environmentGroup->findChild<QWidget *>(QStringLiteral("environmentTemperatureTrendPlot"));
    QWidget *humidityPlot = environmentGroup->findChild<QWidget *>(QStringLiteral("environmentHumidityTrendPlot"));
    require(pressurePlot && qobject_cast<PtbPanel *>(pressurePlot->parentWidget()) != nullptr,
            "pressure trend plot sits under the pressure data panel");
    require(temperaturePlot && qobject_cast<HmpPanel *>(temperaturePlot->parentWidget()) != nullptr,
            "temperature trend plot sits under the temperature data panel");
    require(humidityPlot && qobject_cast<HmpPanel *>(humidityPlot->parentWidget()) != nullptr,
            "humidity trend plot sits under the humidity data panel");
    requireHomeEnvironmentCardLayout(window, false);

    const QRect compactEpsilonGeometry = epsilonGroup->geometry();
    const QRect compactEnvironmentGeometry = environmentGroup->geometry();
    const int compactSideBySideRequiredWidth =
        epsilonGroup->minimumSizeHint().width() +
        VaporView::Ground::MainSupport::kTopLevelCardGap +
        environmentGroup->minimumSizeHint().width();
    const bool compactSideBySideFits =
        dataGroup->contentsRect().width() >= compactSideBySideRequiredWidth;
    const bool compactSideBySide =
        std::abs(compactEnvironmentGeometry.top() - compactEpsilonGeometry.top()) <= 1 &&
        compactEnvironmentGeometry.left() > compactEpsilonGeometry.right();
    const bool compactStacked =
        std::abs(compactEnvironmentGeometry.left() - compactEpsilonGeometry.left()) <= 4 &&
        compactEnvironmentGeometry.top() > compactEpsilonGeometry.bottom();
    require(compactSideBySideFits ? compactSideBySide : (compactSideBySide || compactStacked),
            "default home data cards either place environment to the right when width permits, or stack cleanly when narrow");
    require(compactEnvironmentGeometry.right() <= dataGroup->contentsRect().right() + 1,
            "default environment and lidar card stays inside the home data card edge");
    window.resize(1920, 1000);
    require(processEventsUntil(1000, [&window,
                                      epsilonGroup,
                                      environmentGroup,
                                      compactEpsilonGeometry,
                                      compactEnvironmentGeometry]() {
                activateLayouts(&window);
                return window.size() == QSize(1920, 1000) &&
                       (epsilonGroup->geometry() != compactEpsilonGeometry ||
                        environmentGroup->geometry() != compactEnvironmentGeometry);
            }),
            "sensor-card layout responds to the wide window size");
    requireHomeDeviceGeometryStableAcrossCardResize(&window,
                                                    homeOverviewSplitter,
                                                    deviceOverviewCard,
                                                    temperatureOverviewCard);
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
    auto *wideEpsilonPanel = dataGroup->findChild<QWidget *>(QStringLiteral("epsilonPanel"));
    require(wideEpsilonPanel != nullptr, "wide EPSILON panel exists");
    QList<QFrame*> wideCards = sortedEpsilonSectionCards(wideEpsilonPanel);
    require(wideCards.size() == 3, "three EPSILON section cards at wide window size");
    requireEpsilonSectionCardsStayInsidePanel(
        wideEpsilonPanel,
        "wide EPSILON section cards stay inside the EPSILON panel");
    requireEpsilonSectionCardsUseWrappedMotionRow(
        wideEpsilonPanel,
        "wide EPSILON panel defaults to the wrapped motion row at maximized-style width");

    const QRect wideEpsilonGeometry = epsilonGroup->geometry();
    const QRect wideEnvironmentGeometry = environmentGroup->geometry();
    require(std::abs(wideEnvironmentGeometry.top() - wideEpsilonGeometry.top()) <= 1 &&
                wideEnvironmentGeometry.left() > wideEpsilonGeometry.right(),
            "wide home data cards place environment and lidar to the right of EPSILON");
    require(wideEnvironmentGeometry.right() <= dataGroup->contentsRect().right() + 1,
            "wide environment and lidar card stays inside the home data card edge");
    if (compactSideBySide &&
        wideEnvironmentGeometry.width() + wideEpsilonGeometry.width() >
            compactEnvironmentGeometry.width() + compactEpsilonGeometry.width() + 20)
    {
        require(wideEnvironmentGeometry.width() > compactEnvironmentGeometry.width() + 4,
                "environment and lidar card expands beyond its minimum as the window grows");
    }
    window.resize(originalWindowSize);
    require(processEventsUntil(1000, [&window,
                                      epsilonGroup,
                                      environmentGroup,
                                      originalWindowSize,
                                      wideEpsilonGeometry,
                                      wideEnvironmentGeometry]() {
                activateLayouts(&window);
                return window.size() == originalWindowSize &&
                       (epsilonGroup->geometry() != wideEpsilonGeometry ||
                        environmentGroup->geometry() != wideEnvironmentGeometry);
            }),
            "sensor-card layout returns to the original window size");

    auto *epsilonPanel = dataGroup->findChild<QWidget *>(QStringLiteral("epsilonPanel"));
    require(epsilonPanel != nullptr, "EPSILON panel exists");
    require(epsilonPanel->layout() != nullptr, "EPSILON panel layout exists");
    requireMargins(epsilonPanel->layout()->contentsMargins(),
                   QMargins(2, 2, 2, 2),
                   "EPSILON panel content rhythm remains the reference");
    const bool compactSensorCardsStacked =
        std::abs(epsilonGroup->x() - environmentGroup->x()) <= 4 &&
        environmentGroup->y() > epsilonGroup->y();
    if (compactSensorCardsStacked)
    {
        const int epsilonMinimumWidth = epsilonGroup->minimumSizeHint().width();
        require(epsilonGroup->width() <= epsilonMinimumWidth + 24,
                "compact EPSILON outer card follows the narrowed content width with a small reserve");
        if (dataGroup->width() > epsilonMinimumWidth + 48)
        {
            require(epsilonGroup->width() < dataGroup->width() - 8,
                    "compact EPSILON outer card no longer expands to the full row width");
        }
    }

    QList<QFrame*> cards = sortedEpsilonSectionCards(epsilonPanel);
    require(cards.size() == 3, "three EPSILON section cards");

    requireEpsilonSectionCardsUseWrappedMotionRow(
        epsilonPanel,
        "EPSILON motion details use the second row at the default window size");
    requireEpsilonSectionCardsFillWrappedTopRow(
        epsilonPanel,
        "EPSILON first-row cards fill the outer card width without a center gap");

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
        triggerFontScaleAction(scaledWindow, 130);
        scaledWindow.resize(1664, 1040);
        scaledWindow.show();
        require(waitForWindowExposed(&scaledWindow),
                "scaled main window becomes exposed for first-layout validation");
        activateLayouts(&scaledWindow);
        requireHomeDeviceColumnsAligned(&scaledWindow);
        requireHomeDeviceMinimumWidthMatchesControls(&scaledWindow);
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
        QGroupBox *scaledTemperatureCard = nullptr;
        for (QWidget *ancestor = scaledTemperaturePanel;
             ancestor && !scaledTemperatureCard;
             ancestor = ancestor->parentWidget())
        {
            auto *group = qobject_cast<QGroupBox *>(ancestor);
            if (group &&
                group->property(VaporView::kTopLevelCardProperty).toBool())
            {
                scaledTemperatureCard = group;
            }
        }
        requireTopLevelCardElevation(
            scaledTemperatureCard,
            1.3,
            "top-level card shadow scales with the 130 percent UI setting");
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
        auto *scaledCommonSettingsButton =
            scaledTemperaturePanel->findChild<QPushButton *>(QStringLiteral("temperatureCommonSettingsButton"));
        auto *scaledAddressSpin =
            scaledTemperaturePanel->findChild<QSpinBox *>(QStringLiteral("temperatureDeviceAddressSpin"));
        auto *scaledBaudCombo =
            scaledTemperaturePanel->findChild<QComboBox *>(QStringLiteral("temperatureRs485BaudCombo"));
        auto *scaledOvertempCombo =
            scaledTemperaturePanel->findChild<QComboBox *>(QStringLiteral("temperatureOvertempOutputModeCombo"));
        auto *scaledInternalTemperatureEdit =
            scaledTemperaturePanel->findChild<QLineEdit *>(
                QStringLiteral("temperatureCommonInternalTemperatureEdit"));
        require(scaledCommonSettingsButton != nullptr &&
                    scaledAddressSpin != nullptr &&
                    scaledBaudCombo != nullptr &&
                    scaledOvertempCombo != nullptr &&
                    scaledInternalTemperatureEdit != nullptr,
                "scaled temperature common settings controls exist");
        clickWidget(scaledCommonSettingsButton, 150);
        activateLayouts(&scaledWindow);
        for (QWidget *editor : {static_cast<QWidget *>(scaledAddressSpin),
                                static_cast<QWidget *>(scaledBaudCombo),
                                static_cast<QWidget *>(scaledOvertempCombo),
                                static_cast<QWidget *>(scaledInternalTemperatureEdit)})
        {
            QWidget *row = editor->parentWidget();
            const QList<QLabel *> labels = row
                ? row->findChildren<QLabel *>(QStringLiteral("fieldLabel"),
                                              Qt::FindDirectChildrenOnly)
                : QList<QLabel *>();
            require(!labels.isEmpty(),
                    "scaled temperature common field has a visible label");
            QLabel *label = labels.first();
            const QFontMetrics metrics = label->fontMetrics();
            const int textWidth = std::max(metrics.horizontalAdvance(label->text()),
                                           metrics.boundingRect(label->text()).width());
            const QRect labelRect(label->mapTo(row, QPoint(0, 0)), label->size());
            const QRect editorRect(editor->mapTo(row, QPoint(0, 0)), editor->size());
            require(label->width() >= textWidth &&
                        labelRect.bottom() < editorRect.top() &&
                        editorRect.left() <= 1,
                    "scaled temperature common label keeps its last character clear above the editor");
        }
        triggerFontScaleAction(scaledWindow, 100);
        scaledWindow.close();
        processEventsFor(100);
        settings.setValue(QStringLiteral("font_scale_percent"), 100);
        settings.sync();
    }

    window.close();
    processEventsFor(100);

    requireHomeOverviewLanguageWidthRoundTrip();

    std::cout << "main_window_layout_test passed\n";
    return 0;
}
