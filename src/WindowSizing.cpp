#include "WindowSizing.h"

#include <QGuiApplication>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int kChildWindowOpenOffsetPx = 20;

QSize validOrFallback(QSize size, const QSize& fallback)
{
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
    {
        return fallback.isValid() ? fallback : QSize(1440, 860);
    }
    return size;
}

const QScreen *screenForWidget(const QWidget *contextWidget)
{
    const QScreen *screen = contextWidget ? contextWidget->screen() : nullptr;
    if (!screen)
    {
        screen = QGuiApplication::primaryScreen();
    }
    return screen;
}

int childWindowOpenOffsetForScreen(const QWidget *contextWidget)
{
    const QScreen *screen = screenForWidget(contextWidget);
    const qreal devicePixelRatio = screen ? std::max<qreal>(1.0, screen->devicePixelRatio()) : 1.0;
    return std::max(1, static_cast<int>(std::lround(kChildWindowOpenOffsetPx / devicePixelRatio)));
}

QRect fallbackAvailableGeometry(const QSize& fallbackAvailableSize)
{
    return QRect(QPoint(0, 0), validOrFallback(QSize(), fallbackAvailableSize));
}

QRect screenAvailableGeometry(const QWidget *contextWidget, const QSize& fallbackAvailableSize)
{
    const QScreen *screen = screenForWidget(contextWidget);
    if (!screen)
    {
        return fallbackAvailableGeometry(fallbackAvailableSize);
    }

    const QRect availableGeometry = screen->availableGeometry();
    if (!availableGeometry.isValid() || availableGeometry.width() <= 0 || availableGeometry.height() <= 0)
    {
        return fallbackAvailableGeometry(fallbackAvailableSize);
    }
    return availableGeometry;
}

QSize screenAvailableSize(const QWidget *contextWidget, const QSize& fallbackAvailableSize)
{
    return screenAvailableGeometry(contextWidget, fallbackAvailableSize).size();
}

QSize boundedMinimumSize(const QSize& minimumSize, const QSize& maximumSize)
{
    if (!minimumSize.isValid())
    {
        return QSize(1, 1);
    }
    return QSize(
        std::clamp(minimumSize.width(), 1, maximumSize.width()),
        std::clamp(minimumSize.height(), 1, maximumSize.height()));
}

}

namespace VaporView
{

QSize screenFractionSize(const QWidget *contextWidget, qreal fraction, const QSize& fallbackAvailableSize)
{
    if (fraction <= 0.0)
    {
        fraction = 0.5;
    }

    const QSize availableSize = screenAvailableSize(contextWidget, fallbackAvailableSize);
    return QSize(
        std::max(1, static_cast<int>(std::floor(availableSize.width() * fraction))),
        std::max(1, static_cast<int>(std::floor(availableSize.height() * fraction))));
}

QSize defaultWindowSizeWithinScreenFraction(const QWidget *contextWidget,
                                            const QSize& preferredSize,
                                            qreal fraction,
                                            const QSize& minimumSize,
                                            const QSize& fallbackAvailableSize)
{
    const QSize maximumSize = screenFractionSize(contextWidget, fraction, fallbackAvailableSize);
    const QSize normalizedMinimumSize = boundedMinimumSize(minimumSize, maximumSize);
    const QSize normalizedPreferredSize = preferredSize.isValid() ? preferredSize : maximumSize;
    return normalizedPreferredSize.boundedTo(maximumSize).expandedTo(normalizedMinimumSize).boundedTo(maximumSize);
}

void centerWindowOnScreen(QWidget *window, const QWidget *contextWidget, const QSize& fallbackAvailableSize)
{
    if (!window)
    {
        return;
    }
    if (window->isMaximized() || window->isFullScreen())
    {
        return;
    }

    const QRect availableGeometry = screenAvailableGeometry(contextWidget ? contextWidget : window, fallbackAvailableSize);
    const QRect windowFrame(QPoint(0, 0), window->size().isValid()
        ? window->size()
        : window->frameGeometry().size());
    const QPoint centeredTopLeft(
        availableGeometry.left() + std::max(0, (availableGeometry.width() - windowFrame.width()) / 2),
        availableGeometry.top() + std::max(0, (availableGeometry.height() - windowFrame.height()) / 2));
    QPoint targetTopLeft = centeredTopLeft;
    if (contextWidget && contextWidget != window)
    {
        const int childWindowOpenOffset = childWindowOpenOffsetForScreen(contextWidget);
        targetTopLeft += QPoint(childWindowOpenOffset, childWindowOpenOffset);
    }

    const int maxLeft = availableGeometry.left() + std::max(0, availableGeometry.width() - windowFrame.width());
    const int maxTop = availableGeometry.top() + std::max(0, availableGeometry.height() - windowFrame.height());
    targetTopLeft.setX(std::clamp(targetTopLeft.x(), availableGeometry.left(), maxLeft));
    targetTopLeft.setY(std::clamp(targetTopLeft.y(), availableGeometry.top(), maxTop));
    window->move(targetTopLeft);
}

int defaultFontScalePercentForScreen(const QWidget *contextWidget,
                                     int normalPercent,
                                     int minimumPercent,
                                     const QSize& fallbackAvailableSize)
{
    minimumPercent = std::clamp(minimumPercent, 50, normalPercent);
    const QSize availableSize = screenAvailableSize(contextWidget, fallbackAvailableSize);

    int percent = normalPercent;
    if (availableSize.height() <= 720 || availableSize.width() <= 1280)
    {
        percent = 80;
    }
    else if (availableSize.height() <= 900 || availableSize.width() <= 1440)
    {
        percent = 90;
    }

    const QScreen *screen = screenForWidget(contextWidget);
    if (screen)
    {
        const qreal logicalDpi = std::max(screen->logicalDotsPerInch(), 1.0);
        if (logicalDpi >= 144.0)
        {
            percent = std::min(percent, 85);
        }
        else if (logicalDpi >= 120.0)
        {
            percent = std::min(percent, 90);
        }
    }

    return std::clamp(percent, minimumPercent, normalPercent);
}

}
