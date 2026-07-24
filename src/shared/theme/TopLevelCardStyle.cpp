#include "shared/theme/TopLevelCardStyle.h"

#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QWidget>

#include <algorithm>

namespace VaporView
{
namespace
{

void updateTopLevelCardShadow(QWidget *card, qreal uiScale)
{
    if (!card)
    {
        return;
    }

    auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(card->graphicsEffect());
    if (!shadow ||
        shadow->objectName() != QString::fromLatin1(kTopLevelCardShadowEffectName))
    {
        return;
    }

    const qreal scale = std::max<qreal>(0.5, uiScale);
    shadow->setBlurRadius(kTopLevelCardShadowBlurRadius * scale);
    shadow->setOffset(0.0, kTopLevelCardShadowOffsetY * scale);
    shadow->setColor(QColor(0, 0, 0, kTopLevelCardShadowAlpha));
}

} // namespace

void configureTopLevelCard(QWidget *card)
{
    if (!card)
    {
        return;
    }

    card->setProperty(kTopLevelCardProperty, true);
    card->setAttribute(Qt::WA_StyledBackground, true);

    if (card->graphicsEffect())
    {
        return;
    }

    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setObjectName(QString::fromLatin1(kTopLevelCardShadowEffectName));
    card->setGraphicsEffect(shadow);
    updateTopLevelCardShadow(card, 1.0);
}

void updateTopLevelCardShadows(QWidget *scope, qreal uiScale)
{
    if (!scope)
    {
        return;
    }

    if (scope->property(kTopLevelCardProperty).toBool())
    {
        updateTopLevelCardShadow(scope, uiScale);
    }

    const QList<QWidget *> descendants = scope->findChildren<QWidget *>();
    for (QWidget *widget : descendants)
    {
        if (widget->property(kTopLevelCardProperty).toBool())
        {
            updateTopLevelCardShadow(widget, uiScale);
        }
    }
}

} // namespace VaporView
