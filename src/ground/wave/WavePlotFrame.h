#pragma once

#include "shared/theme/AppTheme.h"

#include <QPainter>
#include <QWidget>

namespace VaporView::WavePlotFrame
{
struct PlotTheme
{
    QColor background;
    QColor grid;
    QColor border;
    QColor text;
    QColor mutedText;
};

inline PlotTheme plotThemeFor(const QWidget *widget)
{
    const QPalette palette = widget->palette();
    QColor background = palette.color(QPalette::Base);
    if (!background.isValid() || background.alpha() == 0)
    {
        background = palette.color(QPalette::Window);
    }
    const bool dark = background.lightness() < 128;
    const QColor neutralGrid = dark ? QColor(QStringLiteral("#2A2A2A")) : QColor(QStringLiteral("#E6E8EC"));
    const QColor neutralBorder = dark ? QColor(QStringLiteral("#3D3D3D")) : QColor(QStringLiteral("#D7DCE3"));
    return {
        background,
        neutralGrid,
        neutralBorder,
        appThemeColor(AppThemeColor::PlotText, dark),
        appThemeColor(AppThemeColor::PlotMutedText, dark)
    };
}

inline void drawGrid(QPainter &painter, const QRectF &plotRect, int intervals, const PlotTheme &theme)
{
    painter.setPen(QPen(theme.grid, 1));
    for (int i = 0; i <= intervals; ++i)
    {
        const qreal x = plotRect.left() + plotRect.width() * i / static_cast<qreal>(intervals);
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
    }
    for (int i = 0; i <= 6; ++i)
    {
        const qreal y = plotRect.top() + plotRect.height() * i / 6.0;
        painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
    }

    painter.setPen(QPen(theme.border, 1));
    painter.drawRect(plotRect);
}
}
