#include "CustomTitleBar.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QSizePolicy>
#include <QSvgRenderer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QVector>
#include <QWidget>
#include <QWindow>
#include <QtMath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>
#endif

#include <algorithm>

namespace VaporView
{
namespace
{
constexpr int kTitleBarHeight = 48;
constexpr int kTitleBarButtonSize = 34;
constexpr int kTitleBarIconSize = 24;
constexpr int kTitleBarMaximizeIconSize = 21;
constexpr int kResizeBorderWidth = 8;
constexpr const char *kMainWindowProperty = "vaporViewMainWindow";
constexpr const char *kEnglishProperty = "vaporViewEnglish";
constexpr const char *kDarkThemeProperty = "vaporViewDarkTheme";
const QColor kToolbarBlue(40, 105, 190);
const QColor kToolbarAmber(220, 150, 35);

QString findResourceFile(const QString& relativePath)
{
    const QString appDir = QApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(relativePath),
        QDir(appDir).filePath(QStringLiteral("../") + relativePath),
        QDir(appDir).filePath(QStringLiteral("../../") + relativePath)
    };

    for (const QString& path : candidates)
    {
        if (QFileInfo::exists(path))
        {
            return path;
        }
    }
    return QString();
}

bool isDarkPalette()
{
    const QPalette palette = qApp->palette();
    return palette.color(QPalette::Window).lightness() < 128 ||
           palette.color(QPalette::Base).lightness() < 128;
}

QPixmap renderLucidePixmap(const QByteArray& svgData, const QColor& color)
{
    QByteArray tinted = svgData;
    tinted.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(tinted);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(2, 2, 28, 28));
    return pixmap;
}

QIcon createLucideIcon(const QString& iconName, const QColor& iconColor)
{
    QFile file(findResourceFile(QStringLiteral("resources/lucide/%1.svg").arg(iconName)));
    if (!file.open(QIODevice::ReadOnly))
    {
        return QIcon();
    }

    QIcon icon;
    icon.addPixmap(renderLucidePixmap(file.readAll(), iconColor), QIcon::Normal);
    return icon;
}

QIcon createTitleBarIcon(const QString& iconName, bool dark)
{
    return createLucideIcon(iconName, dark ? QColor("#d8dee9") : QColor("#111827"));
}

QIcon createToolbarIcon(const QString& iconName, const QColor& color = kToolbarBlue)
{
    return createLucideIcon(iconName, color);
}

QString logoResourcePath(bool dark)
{
    return findResourceFile(dark
        ? QStringLiteral("resources/VaproViewLOGO/VaporViewLOGO_rgb217_119_87.svg")
        : QStringLiteral("resources/VaproViewLOGO/VaporViewLOGO_black.svg"));
}

QPixmap renderLogo(bool dark, int size, qreal devicePixelRatio)
{
    const QString path = logoResourcePath(dark);
    if (path.isEmpty())
    {
        return QPixmap();
    }

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatio);
    const int logicalSize = std::max(1, size);
    const int physicalSize = std::max(1, qCeil(logicalSize * dpr));
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(path);
    if (!renderer.isValid())
    {
        return QPixmap();
    }

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(0, 0, logicalSize, logicalSize));
    return pixmap;
}

QRect availableGeometryFor(QWidget *window)
{
    QScreen *screen = window && window->windowHandle() ? window->windowHandle()->screen() : nullptr;
    if (!screen && window)
    {
        screen = window->screen();
    }
    if (!screen)
    {
        screen = QGuiApplication::primaryScreen();
    }
    return screen ? screen->availableGeometry() : QRect();
}

#ifdef Q_OS_WIN
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWA_COLOR_DEFAULT
#define DWMWA_COLOR_DEFAULT 0xFFFFFFFF
#endif

void setWindowsTitleBarDark(QWidget *window, bool dark)
{
    if (!window)
    {
        return;
    }

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd)
    {
        return;
    }

    const BOOL useDark = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));

    const COLORREF captionColor = dark ? RGB(18, 18, 18) : DWMWA_COLOR_DEFAULT;
    const COLORREF textColor = dark ? RGB(229, 231, 235) : DWMWA_COLOR_DEFAULT;
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
}
#else
void setWindowsTitleBarDark(QWidget *, bool)
{
}
#endif

class CustomTitleBarController : public QObject
{
public:
    explicit CustomTitleBarController(QWidget *window, bool showMaximizeButton)
        : QObject(window)
        , window_(window)
        , content_(nullptr)
        , title_bar_(nullptr)
        , logo_label_(nullptr)
        , title_label_(nullptr)
        , language_button_(nullptr)
        , theme_button_(nullptr)
        , minimize_button_(nullptr)
        , maximize_button_(nullptr)
        , close_button_(nullptr)
        , show_maximize_button_(showMaximizeButton)
    {
        if (!window_)
        {
            return;
        }

        window_->setProperty("customTitleBarWindow", true);
        window_->setAttribute(Qt::WA_StyledBackground, true);
        Qt::WindowFlags flags = window_->windowFlags();
        flags |= Qt::FramelessWindowHint;
        flags |= Qt::WindowCloseButtonHint;
        if (show_maximize_button_)
        {
            flags |= Qt::WindowMinimizeButtonHint;
            flags |= Qt::WindowMaximizeButtonHint;
        }
        flags &= ~Qt::WindowContextHelpButtonHint;
        window_->setWindowFlags(flags);

        build();
        createResizeHandles();
        window_->installEventFilter(this);
        if (qApp)
        {
            qApp->installEventFilter(this);
        }
        refreshTheme();
        updateResizeHandles();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == window_)
        {
            if (event->type() == QEvent::WindowTitleChange)
            {
                updateTitle();
            }
            else if (event->type() == QEvent::PaletteChange ||
                     event->type() == QEvent::ApplicationPaletteChange ||
                     event->type() == QEvent::StyleChange)
            {
                refreshTheme();
            }
            else if (event->type() == QEvent::WindowStateChange ||
                     event->type() == QEvent::Resize)
            {
                updateMaximizeButton();
                updateResizeHandles();
            }
        }
        else if (watched == qApp)
        {
            if (event->type() == QEvent::ApplicationPaletteChange ||
                event->type() == QEvent::PaletteChange ||
                event->type() == QEvent::DynamicPropertyChange)
            {
                refreshTheme();
            }
        }

        if (watched == title_bar_ || watched == logo_label_ || watched == title_label_)
        {
            if (event->type() == QEvent::MouseButtonDblClick && show_maximize_button_)
            {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                if (mouseEvent->button() == Qt::LeftButton)
                {
                    toggleMaximized();
                    return true;
                }
            }
            if (event->type() == QEvent::MouseButtonPress)
            {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                if (mouseEvent->button() == Qt::LeftButton && window_->windowHandle())
                {
                    if (!isWindowStateMaximized())
                    {
                        normal_geometry_ = window_->geometry();
                    }
                    window_->windowHandle()->startSystemMove();
                    return true;
                }
            }
        }

        if (auto *handle = qobject_cast<QWidget *>(watched))
        {
            if (handle->property("customTitleBarResizeHandle").toBool() &&
                event->type() == QEvent::MouseButtonPress)
            {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                if (mouseEvent->button() == Qt::LeftButton && window_ && window_->windowHandle())
                {
                    const auto edgeValue = handle->property("customTitleBarResizeEdges").toInt();
                    const Qt::Edges edges(static_cast<Qt::Edges::Int>(edgeValue));
                    window_->windowHandle()->startSystemResize(edges);
                    return true;
                }
            }
        }

        return QObject::eventFilter(watched, event);
    }

private:
    void build()
    {
        if (auto *mainWindow = qobject_cast<QMainWindow *>(window_))
        {
            content_ = mainWindow->takeCentralWidget();

            auto *wrapper = new QWidget(mainWindow);
            wrapper->setObjectName(QStringLiteral("customTitleBarWrapper"));
            wrapper->setAttribute(Qt::WA_StyledBackground, true);
            auto *rootLayout = new QVBoxLayout(wrapper);
            rootLayout->setContentsMargins(0, 0, 0, 0);
            rootLayout->setSpacing(0);

            title_bar_ = createTitleBar(wrapper);
            rootLayout->addWidget(title_bar_);
            if (content_)
            {
                rootLayout->addWidget(content_, 1);
            }

            mainWindow->setMenuWidget(nullptr);
            mainWindow->setCentralWidget(wrapper);
            return;
        }

        content_ = new QWidget(window_);
        content_->setObjectName(QStringLiteral("customTitleBarContent"));
        content_->setAttribute(Qt::WA_StyledBackground, true);

        QLayout *oldLayout = window_->layout();
        QList<QLayoutItem *> oldItems;
        QMargins oldMargins(12, 12, 12, 12);
        int oldSpacing = 8;
        if (oldLayout)
        {
            oldMargins = oldLayout->contentsMargins();
            oldSpacing = oldLayout->spacing();
            while (QLayoutItem *item = oldLayout->takeAt(0))
            {
                oldItems.append(item);
            }
            delete oldLayout;
        }

        auto *rootLayout = new QVBoxLayout(window_);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(0);
        title_bar_ = createTitleBar(window_);
        rootLayout->addWidget(title_bar_);

        auto *contentLayout = new QVBoxLayout(content_);
        contentLayout->setContentsMargins(oldMargins);
        contentLayout->setSpacing(oldSpacing);
        for (QLayoutItem *item : oldItems)
        {
            if (QWidget *widget = item->widget())
            {
                contentLayout->addWidget(widget);
                delete item;
            }
            else if (QLayout *layout = item->layout())
            {
                contentLayout->addLayout(layout);
                delete item;
            }
            else
            {
                contentLayout->addItem(item);
            }
        }
        rootLayout->addWidget(content_, 1);
    }

    QWidget *createTitleBar(QWidget *parent)
    {
        auto *bar = new QWidget(parent);
        bar->setObjectName(QStringLiteral("customTitleBar"));
        bar->setFixedHeight(kTitleBarHeight);
        bar->installEventFilter(this);

        auto *layout = new QHBoxLayout(bar);
        layout->setContentsMargins(10, 0, 8, 0);
        layout->setSpacing(6);

        logo_label_ = new QLabel(bar);
        logo_label_->setObjectName(QStringLiteral("customTitleLogo"));
        logo_label_->setFixedSize(44, 44);
        logo_label_->setAlignment(Qt::AlignCenter);
        logo_label_->installEventFilter(this);
        layout->addWidget(logo_label_, 0, Qt::AlignVCenter);

        title_label_ = new QLabel(bar);
        title_label_->setObjectName(QStringLiteral("customTitleLabel"));
        title_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        title_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        title_label_->installEventFilter(this);
        layout->addWidget(title_label_, 0, Qt::AlignVCenter);
        layout->addStretch(1);

        language_button_ = createWindowButton(QStringLiteral("titleBarButton"), bar);
        language_button_->setAccessibleName(QStringLiteral("titleLanguageButton"));
        QObject::connect(language_button_, &QToolButton::clicked, this, [this]() {
            invokeMainWindowSlot("onSwitchLanguage");
        });
        layout->addWidget(language_button_, 0, Qt::AlignVCenter);

        theme_button_ = createWindowButton(QStringLiteral("titleBarButton"), bar);
        theme_button_->setAccessibleName(QStringLiteral("titleThemeButton"));
        QObject::connect(theme_button_, &QToolButton::clicked, this, [this]() {
            invokeMainWindowSlot("onToggleTheme");
        });
        layout->addWidget(theme_button_, 0, Qt::AlignVCenter);

        addSeparator(layout, bar);

        if (show_maximize_button_)
        {
            minimize_button_ = createWindowButton(QStringLiteral("windowMinimizeButton"), bar);
            QObject::connect(minimize_button_, &QToolButton::clicked, window_, &QWidget::showMinimized);
            layout->addWidget(minimize_button_, 0, Qt::AlignVCenter);

            maximize_button_ = createWindowButton(QStringLiteral("windowMaximizeButton"), bar);
            maximize_button_->setIconSize(QSize(kTitleBarMaximizeIconSize, kTitleBarMaximizeIconSize));
            QObject::connect(maximize_button_, &QToolButton::clicked, this, [this]() {
                toggleMaximized();
            });
            layout->addWidget(maximize_button_, 0, Qt::AlignVCenter);
        }

        close_button_ = createWindowButton(QStringLiteral("windowCloseButton"), bar);
        QObject::connect(close_button_, &QToolButton::clicked, window_, &QWidget::close);
        layout->addWidget(close_button_, 0, Qt::AlignVCenter);

        updateTitle();
        return bar;
    }

    QToolButton *createWindowButton(const QString& objectName, QWidget *parent) const
    {
        auto *button = new QToolButton(parent);
        button->setObjectName(objectName);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(false);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(kTitleBarButtonSize, kTitleBarButtonSize);
        button->setIconSize(QSize(kTitleBarIconSize, kTitleBarIconSize));
        return button;
    }

    void addSeparator(QHBoxLayout *layout, QWidget *parent) const
    {
        auto *separator = new QFrame(parent);
        separator->setObjectName(QStringLiteral("titleBarSeparator"));
        separator->setFixedWidth(1);
        separator->setFixedHeight(28);
        layout->addWidget(separator, 0, Qt::AlignVCenter);
    }

    void updateTitle()
    {
        if (title_label_ && window_)
        {
            title_label_->setText(window_->windowTitle());
        }
    }

    void refreshTheme()
    {
        const bool dark = isDarkPalette();
        const QColor background = dark ? QColor("#0D0D0D") : QColor("#FDFDFC");
        setWindowsTitleBarDark(window_, dark);
        for (QWidget *widget : {window_, content_})
        {
            if (!widget)
            {
                continue;
            }
            QPalette palette = widget->palette();
            palette.setColor(QPalette::Window, background);
            widget->setPalette(palette);
            widget->setAutoFillBackground(true);
            widget->update();
        }
        if (logo_label_)
        {
            logo_label_->setPixmap(renderLogo(dark, 44, logo_label_->devicePixelRatioF()));
        }
        if (minimize_button_)
        {
            minimize_button_->setIcon(createTitleBarIcon(QStringLiteral("minus"), dark));
        }
        if (close_button_)
        {
            close_button_->setIcon(createTitleBarIcon(QStringLiteral("x"), dark));
        }
        updateLanguageAndThemeButtons();
        updateWindowButtonTexts();
        updateMaximizeButton();
    }

    QObject *mainWindowObject() const
    {
        for (QWidget *widget = window_; widget; widget = widget->parentWidget())
        {
            if (widget->property(kMainWindowProperty).toBool())
            {
                return widget;
            }
        }

        if (!qApp)
        {
            return nullptr;
        }
        for (QWidget *widget : QApplication::topLevelWidgets())
        {
            if (widget && widget->property(kMainWindowProperty).toBool())
            {
                return widget;
            }
        }
        return nullptr;
    }

    void invokeMainWindowSlot(const char *slotName)
    {
        QObject *mainWindow = mainWindowObject();
        if (!mainWindow || !slotName)
        {
            return;
        }
        QMetaObject::invokeMethod(mainWindow, slotName, Qt::QueuedConnection);
    }

    bool isEnglish() const
    {
        return qApp && qApp->property(kEnglishProperty).toBool();
    }

    bool isDarkThemeEnabled() const
    {
        if (qApp)
        {
            const QVariant value = qApp->property(kDarkThemeProperty);
            if (value.isValid())
            {
                return value.toBool();
            }
        }
        return isDarkPalette();
    }

    void updateLanguageAndThemeButtons()
    {
        const bool english = isEnglish();
        const bool dark = isDarkThemeEnabled();
        if (language_button_)
        {
            language_button_->setIcon(createToolbarIcon(QStringLiteral("languages")));
            language_button_->setToolTip(english ? QStringLiteral("Switch to Chinese") : QStringLiteral("切换到英文"));
            language_button_->setStatusTip(english ? QStringLiteral("Switch interface language") : QStringLiteral("切换界面语言"));
        }
        if (theme_button_)
        {
            theme_button_->setIcon(dark
                ? createToolbarIcon(QStringLiteral("sun"), kToolbarAmber)
                : createToolbarIcon(QStringLiteral("moon")));
            theme_button_->setToolTip(dark
                ? (english ? QStringLiteral("Switch to light theme") : QStringLiteral("切换到亮色模式"))
                : (english ? QStringLiteral("Switch to dark theme") : QStringLiteral("切换到暗色模式")));
            theme_button_->setStatusTip(theme_button_->toolTip());
        }
    }

    void updateWindowButtonTexts()
    {
        const bool english = isEnglish();
        if (minimize_button_)
        {
            minimize_button_->setToolTip(english ? QStringLiteral("Minimize") : QStringLiteral("最小化"));
            minimize_button_->setStatusTip(minimize_button_->toolTip());
        }
        if (close_button_)
        {
            close_button_->setToolTip(english ? QStringLiteral("Close") : QStringLiteral("关闭"));
            close_button_->setStatusTip(close_button_->toolTip());
        }
    }

    void createResizeHandles()
    {
        if (!window_)
        {
            return;
        }

        const QList<Qt::Edges> edgesList = {
            Qt::Edges(Qt::TopEdge),
            Qt::Edges(Qt::BottomEdge),
            Qt::Edges(Qt::LeftEdge),
            Qt::Edges(Qt::RightEdge),
            Qt::Edges(Qt::TopEdge | Qt::LeftEdge),
            Qt::Edges(Qt::TopEdge | Qt::RightEdge),
            Qt::Edges(Qt::BottomEdge | Qt::LeftEdge),
            Qt::Edges(Qt::BottomEdge | Qt::RightEdge)
        };

        for (Qt::Edges edges : edgesList)
        {
            auto *handle = new QWidget(window_);
            handle->setProperty("customTitleBarResizeHandle", true);
            handle->setProperty("customTitleBarResizeEdges", static_cast<int>(edges));
            handle->setFocusPolicy(Qt::NoFocus);
            handle->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            handle->setCursor(cursorForEdges(edges));
            handle->installEventFilter(this);
            handle->raise();
            resize_handles_.append(handle);
        }
    }

    QCursor cursorForEdges(Qt::Edges edges) const
    {
        const bool horizontal = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
        const bool vertical = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
        if (horizontal && vertical)
        {
            const bool topLeftBottomRight =
                (edges.testFlag(Qt::TopEdge) && edges.testFlag(Qt::LeftEdge)) ||
                (edges.testFlag(Qt::BottomEdge) && edges.testFlag(Qt::RightEdge));
            return QCursor(topLeftBottomRight ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
        }
        if (horizontal)
        {
            return QCursor(Qt::SizeHorCursor);
        }
        return QCursor(Qt::SizeVerCursor);
    }

    void updateResizeHandles()
    {
        if (!window_)
        {
            return;
        }

        const bool visible = !window_->isFullScreen() && !isWindowStateMaximized();
        const int width = window_->width();
        const int height = window_->height();
        const int border = kResizeBorderWidth;
        const int corner = border * 2;

        for (QWidget *handle : resize_handles_)
        {
            if (!handle)
            {
                continue;
            }
            handle->setVisible(visible);
            if (!visible)
            {
                continue;
            }

            const Qt::Edges edges(static_cast<Qt::Edges::Int>(handle->property("customTitleBarResizeEdges").toInt()));
            if (edges == Qt::Edges(Qt::TopEdge))
            {
                handle->setGeometry(corner, 0, std::max(0, width - 2 * corner), border);
            }
            else if (edges == Qt::Edges(Qt::BottomEdge))
            {
                handle->setGeometry(corner, height - border, std::max(0, width - 2 * corner), border);
            }
            else if (edges == Qt::Edges(Qt::LeftEdge))
            {
                handle->setGeometry(0, corner, border, std::max(0, height - 2 * corner));
            }
            else if (edges == Qt::Edges(Qt::RightEdge))
            {
                handle->setGeometry(width - border, corner, border, std::max(0, height - 2 * corner));
            }
            else if (edges == Qt::Edges(Qt::TopEdge | Qt::LeftEdge))
            {
                handle->setGeometry(0, 0, corner, corner);
            }
            else if (edges == Qt::Edges(Qt::TopEdge | Qt::RightEdge))
            {
                handle->setGeometry(width - corner, 0, corner, corner);
            }
            else if (edges == Qt::Edges(Qt::BottomEdge | Qt::LeftEdge))
            {
                handle->setGeometry(0, height - corner, corner, corner);
            }
            else if (edges == Qt::Edges(Qt::BottomEdge | Qt::RightEdge))
            {
                handle->setGeometry(width - corner, height - corner, corner, corner);
            }
            handle->raise();
        }
    }

    bool isWindowStateMaximized() const
    {
        return window_ &&
               !window_->isFullScreen() &&
               (window_->isMaximized() || window_->windowState().testFlag(Qt::WindowMaximized));
    }

    void updateMaximizeButton()
    {
        if (!maximize_button_)
        {
            return;
        }

        const bool dark = isDarkPalette();
        const bool restore = isWindowStateMaximized();
        const bool english = isEnglish();
        maximize_button_->setIcon(createTitleBarIcon(restore ? QStringLiteral("copy") : QStringLiteral("square"), dark));
        maximize_button_->setToolTip(restore
            ? (english ? QStringLiteral("Restore") : QStringLiteral("还原"))
            : (english ? QStringLiteral("Maximize") : QStringLiteral("最大化")));
        maximize_button_->setStatusTip(maximize_button_->toolTip());
    }

    void toggleMaximized()
    {
        if (!show_maximize_button_ || !window_ || window_->isFullScreen())
        {
            return;
        }

        if (isWindowStateMaximized())
        {
            const QRect restoreGeometry = normal_geometry_.isValid()
                ? normal_geometry_
                : QRect(availableGeometryFor(window_).center() - QPoint(window_->width() / 2, window_->height() / 2),
                        window_->size());
            window_->setWindowState(window_->windowState() & ~Qt::WindowMaximized);
            window_->showNormal();
            if (restoreGeometry.isValid())
            {
                window_->setGeometry(restoreGeometry);
            }
        }
        else
        {
            normal_geometry_ = window_->geometry();
            window_->showMaximized();
        }
        updateMaximizeButton();
        updateResizeHandles();
    }

    QWidget *window_;
    QWidget *content_;
    QWidget *title_bar_;
    QLabel *logo_label_;
    QLabel *title_label_;
    QToolButton *language_button_;
    QToolButton *theme_button_;
    QToolButton *minimize_button_;
    QToolButton *maximize_button_;
    QToolButton *close_button_;
    bool show_maximize_button_;
    QRect normal_geometry_;
    QList<QWidget *> resize_handles_;
};
}  // namespace

void installCustomTitleBar(QWidget *window, bool showMaximizeButton)
{
    if (!window || window->property("customTitleBarWindow").toBool())
    {
        return;
    }

    new CustomTitleBarController(window, showMaximizeButton);
}

}  // namespace VaporView
