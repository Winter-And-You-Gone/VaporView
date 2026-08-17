#include "shared/theme/TopLevelCardStyle.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QEvent>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollBar>
#include <QSet>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <utility>

namespace VaporView
{
namespace
{

inline constexpr const char *kShadowBlurRadiusProperty =
    "vaporViewTopLevelCardShadowBlurRadius";
inline constexpr const char *kShadowOffsetYProperty =
    "vaporViewTopLevelCardShadowOffsetY";
inline constexpr const char *kShadowColorProperty =
    "vaporViewTopLevelCardShadowColor";
inline constexpr const char *kShadowCardCountProperty =
    "vaporViewTopLevelCardShadowCardCount";
inline constexpr const char *kShadowUsesOpaqueScrollBarTrackProperty =
    "vaporViewTopLevelCardShadowUsesOpaqueScrollBarTrack";
inline constexpr const char *kShadowClipsScrollBarTrackProperty =
    "vaporViewTopLevelCardShadowClipsScrollBarTrack";
inline constexpr const char *kShadowUiScaleProperty =
    "vaporViewTopLevelCardShadowUiScale";

class TopLevelCardShadowLayer final : public QWidget
{
public:
    explicit TopLevelCardShadowLayer(QWidget *host)
        : QWidget(host)
    {
        setObjectName(QString::fromLatin1(kTopLevelCardShadowLayerName));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(host->rect());
        host->installEventFilter(this);
    }

    void setCards(const QList<QWidget *>& cards, qreal uiScale)
    {
        for (QObject *object : std::as_const(observedObjects_))
        {
            if (object && object != parentWidget())
            {
                object->removeEventFilter(this);
            }
        }
        observedObjects_.clear();
        observedObjects_.insert(parentWidget());

        cards_.clear();
        for (QWidget *card : cards)
        {
            if (!card)
            {
                continue;
            }

            cards_.append(card);
            for (QWidget *ancestor = card;
                 ancestor && ancestor != parentWidget();
                 ancestor = ancestor->parentWidget())
            {
                if (!observedObjects_.contains(ancestor))
                {
                    observedObjects_.insert(ancestor);
                    ancestor->installEventFilter(this);
                }
            }
        }

        uiScale_ = std::max<qreal>(0.5, uiScale);
        setProperty(kShadowBlurRadiusProperty,
                    kTopLevelCardShadowBlurRadius * uiScale_);
        setProperty(kShadowOffsetYProperty,
                    kTopLevelCardShadowOffsetY * uiScale_);
        setProperty(kShadowColorProperty,
                    QColor(0, 0, 0, kTopLevelCardShadowAlpha));
        setProperty(kShadowCardCountProperty, cards_.size());
        setProperty(kShadowUsesOpaqueScrollBarTrackProperty,
                    qobject_cast<QAbstractScrollArea *>(parentWidget()) != nullptr);
        setProperty(kShadowClipsScrollBarTrackProperty,
                    qobject_cast<QAbstractScrollArea *>(parentWidget()) != nullptr);

        setGeometry(parentWidget()->rect());
        setVisible(!cards_.isEmpty());
        if (!cards_.isEmpty())
        {
            raiseAboveContent();
        }
        update();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type())
        {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::LayoutRequest:
        case QEvent::ParentChange:
        case QEvent::StyleChange:
            if (watched == parentWidget())
            {
                setGeometry(parentWidget()->rect());
            }
            if (!cards_.isEmpty())
            {
                raiseAboveContent();
            }
            update();
            break;
        default:
            break;
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *) override
    {
        struct CardPaintInfo
        {
            QRectF rect;
        };

        QList<CardPaintInfo> visibleCards;
        QPainterPath cardSurfaces;
        cardSurfaces.setFillRule(Qt::WindingFill);

        QWidget *host = parentWidget();
        for (const QPointer<QWidget>& card : std::as_const(cards_))
        {
            if (!card ||
                card->graphicsEffect() ||
                !card->isVisibleTo(host) ||
                card->width() <= 0 ||
                card->height() <= 0)
            {
                continue;
            }

            const QPoint topLeft = card->mapTo(host, QPoint(0, 0));
            const QRectF cardRect(topLeft, card->size());
            if (!cardRect.intersects(QRectF(rect())))
            {
                continue;
            }

            visibleCards.append({cardRect});
            QPainterPath surface;
            surface.addRoundedRect(cardRect,
                                   kTopLevelCardCornerRadius,
                                   kTopLevelCardCornerRadius);
            cardSurfaces.addPath(surface);
        }

        if (visibleCards.isEmpty())
        {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const qreal blurRadius = kTopLevelCardShadowBlurRadius * uiScale_;
        const qreal offsetY = kTopLevelCardShadowOffsetY * uiScale_;
        const int spreadCount = std::max(1, static_cast<int>(std::ceil(blurRadius * 0.6)));
        const qreal verticalSpreadExtra =
            kTopLevelCardShadowVerticalSpreadExtra * uiScale_;

        qreal weightSum = 0.0;
        for (int spread = spreadCount; spread >= 1; --spread)
        {
            const qreal proximity =
                static_cast<qreal>(spreadCount - spread) /
                std::max(1, spreadCount - 1);
            weightSum += 0.55 + proximity * 0.9;
        }

        QPainterPath shadowClip;
        shadowClip.addRect(QRectF(rect()));
        shadowClip = shadowClip.subtracted(cardSurfaces);
        if (auto *scrollArea = qobject_cast<QAbstractScrollArea *>(host))
        {
            QPainterPath scrollBarTrackClip;
            addScrollBarTrackClip(scrollArea->verticalScrollBar(), scrollBarTrackClip);
            addScrollBarTrackClip(scrollArea->horizontalScrollBar(), scrollBarTrackClip);
            shadowClip = shadowClip.subtracted(scrollBarTrackClip);
        }
        painter.save();
        painter.setClipPath(shadowClip);

        for (int spread = spreadCount; spread >= 1; --spread)
        {
            const qreal proximity =
                static_cast<qreal>(spreadCount - spread) /
                std::max(1, spreadCount - 1);
            const qreal weight = 0.55 + proximity * 0.9;
            const int layerAlpha = std::max(
                1,
                static_cast<int>(std::round(
                    kTopLevelCardShadowAlpha * weight / weightSum)));

            QPainterPath shadowPath;
            shadowPath.setFillRule(Qt::WindingFill);
            for (const CardPaintInfo& card : std::as_const(visibleCards))
            {
                const qreal verticalSpread = spread + verticalSpreadExtra;
                QRectF shadowRect = card.rect.adjusted(-spread,
                                                       -verticalSpread,
                                                       spread,
                                                       verticalSpread);
                shadowRect.translate(0.0, offsetY);
                shadowPath.addRoundedRect(
                    shadowRect,
                    kTopLevelCardCornerRadius + spread,
                    kTopLevelCardCornerRadius + spread);
            }
            painter.fillPath(shadowPath,
                             QColor(0, 0, 0, layerAlpha));
        }
        painter.restore();

    }

private:
    void raiseAboveContent()
    {
        raise();
        if (auto *scrollArea = qobject_cast<QAbstractScrollArea *>(parentWidget()))
        {
            // The native scrollbars use an opaque, fixed background. Keep the
            // shadow layer above the content and out of the scrollbar rect;
            // repeatedly raising the native widgets during layout updates
            // causes Windows backing-store z-order flicker.
            Q_UNUSED(scrollArea);
            return;
        }

        if (QWidget *bottomFade = parentWidget()->findChild<QWidget *>(
                QStringLiteral("mainContentBottomFade"),
                Qt::FindDirectChildrenOnly))
        {
            bottomFade->raise();
        }
    }

    void addScrollBarTrackClip(QScrollBar *scrollBar, QPainterPath& clip) const
    {
        if (!scrollBar ||
            !scrollBar->isVisible() ||
            scrollBar->width() <= 0 ||
            scrollBar->height() <= 0 ||
            !parentWidget())
        {
            return;
        }

        const QRectF hostTrackRect(scrollBar->mapTo(parentWidget(), QPoint(0, 0)),
                                   scrollBar->size());
        clip.addRect(hostTrackRect.adjusted(-1.0, -1.0, 1.0, 1.0));
    }

    QList<QPointer<QWidget>> cards_;
    QSet<QObject *> observedObjects_;
    qreal uiScale_ = 1.0;
};

QWidget *shadowHostForCard(QWidget *card, QWidget *window)
{
    QWidget *windowContent = card;
    for (QWidget *ancestor = card; ancestor && ancestor != window;
         ancestor = ancestor->parentWidget())
    {
        windowContent = ancestor;
        auto *scrollArea =
            qobject_cast<QAbstractScrollArea *>(ancestor->parentWidget());
        if (scrollArea && scrollArea->viewport() == ancestor)
        {
            return scrollArea;
        }
    }
    return windowContent;
}

TopLevelCardShadowLayer *shadowLayerForHost(QWidget *host)
{
    const QList<QWidget *> children =
        host->findChildren<QWidget *>(
            QString::fromLatin1(kTopLevelCardShadowLayerName),
            Qt::FindDirectChildrenOnly);
    for (QWidget *child : children)
    {
        if (auto *layer = dynamic_cast<TopLevelCardShadowLayer *>(child))
        {
            return layer;
        }
    }
    return new TopLevelCardShadowLayer(host);
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
}

void updateTopLevelCardShadows(QWidget *scope, qreal uiScale)
{
    if (!scope)
    {
        return;
    }

    QWidget *window = scope->window();
    if (!window)
    {
        return;
    }

    if (uiScale <= 0.0)
    {
        const double storedScale = window->property(kShadowUiScaleProperty).toDouble();
        uiScale = storedScale > 0.0 ? storedScale : 1.0;
    }
    else
    {
        window->setProperty(kShadowUiScaleProperty, uiScale);
    }

    QList<QWidget *> cards = window->findChildren<QWidget *>();
    if (window->property(kTopLevelCardProperty).toBool())
    {
        cards.prepend(window);
    }

    QHash<QWidget *, QList<QWidget *>> cardsByHost;
    for (QWidget *card : std::as_const(cards))
    {
        if (card->window() == window &&
            card->property(kTopLevelCardProperty).toBool() &&
            !card->graphicsEffect())
        {
            cardsByHost[shadowHostForCard(card, window)].append(card);
        }
    }

    const QList<QWidget *> existingLayers =
        window->findChildren<QWidget *>(
            QString::fromLatin1(kTopLevelCardShadowLayerName));
    for (QWidget *widget : existingLayers)
    {
        if (auto *layer = dynamic_cast<TopLevelCardShadowLayer *>(widget))
        {
            layer->setCards(cardsByHost.take(layer->parentWidget()), uiScale);
        }
    }

    for (auto it = cardsByHost.cbegin(); it != cardsByHost.cend(); ++it)
    {
        shadowLayerForHost(it.key())->setCards(it.value(), uiScale);
    }
}

} // namespace VaporView
