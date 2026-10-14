#pragma once

#include "shared/theme/AppTheme.h"
#include <QCheckBox>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionButton>

class DeviceEnabledCheckBox final : public QCheckBox
{
public:
    explicit DeviceEnabledCheckBox(QWidget *parent) : QCheckBox(parent)
    {
        setStyleSheet(QStringLiteral("QCheckBox::indicator, QCheckBox::indicator:checked, "
            "QCheckBox::indicator:hover, QCheckBox::indicator:disabled "
            "{ background: transparent; border: none; image: none; }"));
    }
protected:
    void paintEvent(QPaintEvent *event) override
    {
        QCheckBox::paintEvent(event);
        QStyleOptionButton option;
        initStyleOption(&option);
        const QRect indicator = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this);
        const bool dark = VaporView::isDarkThemePalette(palette());
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(indicator.topLeft());
        painter.scale(indicator.width() / 20.0, indicator.height() / 20.0);
        painter.setPen(QPen(VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark),
            1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(VaporView::appThemeColor(isChecked() ? VaporView::AppThemeColor::PrimarySubtle
            : VaporView::AppThemeColor::Surface, dark));
        painter.drawRoundedRect(QRectF(1, 1, 18, 18), 3, 3);
        if (isChecked()) {
            QPainterPath check;
            check.moveTo(5, 10); check.lineTo(8, 13); check.lineTo(15, 6);
            painter.drawPath(check);
        }
    }
};
