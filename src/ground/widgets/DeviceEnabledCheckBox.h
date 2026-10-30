#pragma once

#include "shared/theme/AppTheme.h"
#include "shared/theme/CheckedIndicator.h"
#include <QCheckBox>
#include <QPainter>
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
    DeviceEnabledCheckBox(const QString &text, QWidget *parent) : DeviceEnabledCheckBox(parent)
    {
        setText(text);
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
        if (isChecked()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(VaporView::appThemeColor(VaporView::AppThemeColor::PrimarySubtle, dark));
            painter.drawRoundedRect(QRectF(indicator).adjusted(2, 2, -2, -2), 3, 3);
            VaporView::drawSquareCheckBig(painter, indicator,
                VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark));
            return;
        }
        painter.translate(indicator.topLeft());
        painter.scale(indicator.width() / 20.0, indicator.height() / 20.0);
        painter.setPen(QPen(VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark),
            1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(VaporView::appThemeColor(VaporView::AppThemeColor::Surface, dark));
        painter.drawRoundedRect(QRectF(1, 1, 18, 18), 3, 3);
    }
};
