#include "WindowSizing.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace
{

QSize validOrFallback(QSize size, const QSize& fallback)
{
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
    {
        return fallback.isValid() ? fallback : QSize(1440, 860);
    }
    return size;
}

QSize screenAvailableSize(const QWidget *contextWidget, const QSize& fallbackAvailableSize)
{
    const QScreen *screen = contextWidget ? contextWidget->screen() : nullptr;
    if (!screen)
    {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen)
    {
        return validOrFallback(QSize(), fallbackAvailableSize);
    }
    return validOrFallback(screen->availableGeometry().size(), fallbackAvailableSize);
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

}
