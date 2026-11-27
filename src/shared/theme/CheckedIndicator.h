#pragma once

#include <algorithm>
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>

namespace VaporView
{

// Draw resources/lucide/square.svg on the same 24-unit canvas as the checked icon.
inline void drawSquare(QPainter& painter, const QRectF& rect, const QColor& color)
{
    const qreal side = qMin(rect.width(), rect.height());
    if (side <= 0) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(rect.center() - QPointF(side / 2, side / 2));
    painter.scale(side / 24.0, side / 24.0);
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
    painter.restore();
}

// Draw the two paths from resources/lucide/square-check-big.svg, tinted for the current theme.
inline void drawSquareCheckBig(QPainter& painter, const QRectF& rect, const QColor& color)
{
    const qreal side = qMin(rect.width(), rect.height());
    if (side <= 0) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(rect.center() - QPointF(side / 2, side / 2));
    painter.scale(side / 24.0, side / 24.0);
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(21, 10.656);
    path.lineTo(21, 19);
    path.arcTo(QRectF(17, 17, 4, 4), 0, -90);
    path.lineTo(5, 21);
    path.arcTo(QRectF(3, 17, 4, 4), -90, -90);
    path.lineTo(3, 5);
    path.arcTo(QRectF(3, 3, 4, 4), 180, -90);
    path.lineTo(17.344, 3);
    path.moveTo(9, 11);
    path.lineTo(12, 14);
    path.lineTo(22, 4);
    painter.drawPath(path);
    painter.restore();
}

inline QRectF checkedIndicatorIconRect(const QRectF& canvasRect)
{
    const qreal side = qMin(canvasRect.width(), canvasRect.height());
    const qreal inset = std::min(3.0, side * 0.15);
    const qreal iconSide = side >= 26.0 ? side * (24.0 / 34.0) : std::max(8.0, side - inset * 2.0);
    return QRectF(canvasRect.center().x() - iconSide / 2.0, canvasRect.center().y() - iconSide / 2.0,
                  iconSide, iconSide);
}

inline void drawCheckedIndicatorHover(QPainter& painter, const QRectF& rect, const QColor& hoverColor)
{
    if (rect.width() <= 0 || rect.height() <= 0 || !hoverColor.isValid()) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(hoverColor);
    const qreal side = qMin(rect.width(), rect.height());
    const QRectF box(rect.center().x() - side / 2.0, rect.center().y() - side / 2.0, side, side);
    const qreal radius = std::clamp(side * (6.0 / 34.0), 4.0, 10.0);
    painter.drawRoundedRect(box, radius, radius);
    painter.restore();
}

} // namespace VaporView
