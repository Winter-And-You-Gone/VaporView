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
        const QColor color = VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark);
        if (isChecked())
            VaporView::drawSquareCheckBig(painter, indicator, color);
        else
            VaporView::drawSquare(painter, indicator, color);
    }
};
