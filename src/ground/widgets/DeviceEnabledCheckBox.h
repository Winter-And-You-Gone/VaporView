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
        setAttribute(Qt::WA_Hover, true);
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
        if (isEnabled() && (option.state & QStyle::State_MouseOver))
            VaporView::drawCheckedIndicatorHover(painter, indicator, VaporView::appThemeColor(VaporView::AppThemeColor::TitleBarHover, dark));
        QColor color = VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark);
        if (!isEnabled()) color.setAlphaF(0.45);
        if (isChecked())
            VaporView::drawSquareCheckBig(painter, indicator, color);
        else
            VaporView::drawSquare(painter, indicator, color);
    }
};
