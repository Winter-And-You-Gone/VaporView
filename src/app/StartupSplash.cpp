#include "app/StartupSplash.h"

#include <QAbstractAnimation>
#include <QCloseEvent>
#include <QCursor>
#include <QDebug>
#include <QEasingCurve>
#include <QFont>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>
#include <QHideEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kDefaultSplashCardExtent = 220;
constexpr int kMinimumSplashCardExtent = 180;
constexpr int kMaximumSplashCardExtent = 240;
constexpr qreal kSplashCardFraction = 0.20;
constexpr int kSplashShadowMargin = 30;
constexpr qreal kCardCornerRadiusFraction = 0.22;
constexpr qreal kLogoExtentFraction = 0.72;
constexpr int kShadowBlurRadius = 22;
constexpr int kShadowVerticalOffset = 10;
constexpr int kShadowOpacity = 90;

int shadowMarginForExtent(int extent)
{
    return std::min(kSplashShadowMargin, std::max(0, (extent - 1) / 2));
}
}

namespace VaporView
{

class StartupSplash::SplashCardWidget final : public QWidget
{
public:
    explicit SplashCardWidget(const QString& logoPath, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);
        if (!renderer_.load(logoPath) || !renderer_.isValid())
        {
            fallback_text_ = true;
            qWarning().noquote() << "Startup splash logo could not be loaded from" << logoPath;
        }
    }

    bool isUsingFallbackText() const
    {
        return fallback_text_;
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QRectF cardRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal cornerRadius = cardRect.width() * kCardCornerRadiusFraction;
        QPainterPath cardPath;
        cardPath.addRoundedRect(cardRect, cornerRadius, cornerRadius);
        painter.fillPath(cardPath, QColor(7, 8, 9));
        painter.setPen(QPen(QColor(48, 52, 56, 190), 1.0));
        painter.drawPath(cardPath);

        const QSizeF maximumLogoSize(
            cardRect.width() * kLogoExtentFraction,
            cardRect.height() * kLogoExtentFraction);

        if (fallback_text_)
        {
            painter.setPen(Qt::white);
            QFont fallbackFont = font();
            fallbackFont.setWeight(QFont::DemiBold);
            fallbackFont.setPixelSize(std::max(16, width() / 9));
            painter.setFont(fallbackFont);
            painter.drawText(cardRect, Qt::AlignCenter, QStringLiteral("VaporView"));
            return;
        }

        QSizeF logoSize = renderer_.defaultSize();
        if (logoSize.isEmpty())
        {
            logoSize = maximumLogoSize;
        }
        logoSize.scale(maximumLogoSize, Qt::KeepAspectRatio);
        const QRectF targetRect(
            QPointF(cardRect.center().x() - logoSize.width() / 2.0,
                    cardRect.center().y() - logoSize.height() / 2.0),
            logoSize);
        renderer_.render(&painter, targetRect);
    }

private:
    QSvgRenderer renderer_;
    bool fallback_text_ = false;
};

StartupSplash::StartupSplash(const QString& logoPath, QWidget *parent)
    : QWidget(parent,
              Qt::SplashScreen |
                  Qt::FramelessWindowHint |
                  Qt::WindowStaysOnTopHint |
                  Qt::WindowDoesNotAcceptFocus)
{
    setObjectName(QStringLiteral("vaporViewStartupSplash"));
    setWindowTitle(QStringLiteral("Startup Splash"));
    setAccessibleName(QStringLiteral("VaporView startup"));
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setFixedSize(kDefaultSplashCardExtent + 2 * kSplashShadowMargin,
                 kDefaultSplashCardExtent + 2 * kSplashShadowMargin);
    setWindowOpacity(1.0);

    card_widget_ = new SplashCardWidget(logoPath, this);
    auto *shadow = new QGraphicsDropShadowEffect(card_widget_);
    shadow->setBlurRadius(kShadowBlurRadius);
    shadow->setOffset(0, kShadowVerticalOffset);
    shadow->setColor(QColor(0, 0, 0, kShadowOpacity));
    card_widget_->setGraphicsEffect(shadow);
    updateLogoSize();

    window_fade_animation_ = new QPropertyAnimation(this, "windowOpacity", this);
    window_fade_animation_->setEasingCurve(QEasingCurve::InCubic);
    connect(window_fade_animation_, &QPropertyAnimation::finished, this, [this]() {
        if (!fading_out_)
        {
            return;
        }
        stopAnimations();
        hide();
        emit fadeOutFinished();
    });
}

StartupSplash::~StartupSplash()
{
    stopAnimations();
}

QString StartupSplash::defaultLogoResourcePath()
{
    return QStringLiteral(":/vaporview/startup/logo-white.svg");
}

int StartupSplash::calculateCardExtent(const QSize& availableSize)
{
    if (!availableSize.isValid() || availableSize.width() <= 0 || availableSize.height() <= 0)
    {
        return kDefaultSplashCardExtent;
    }

    const int shorterSide = std::min(availableSize.width(), availableSize.height());
    const int margin = shadowMarginForExtent(shorterSide);
    const int preferredCardExtent = std::clamp(
        static_cast<int>(std::lround(shorterSide * kSplashCardFraction)),
        kMinimumSplashCardExtent,
        kMaximumSplashCardExtent);
    const int maximumCardExtent = std::max(1, shorterSide - 2 * margin);
    return std::min(preferredCardExtent, maximumCardExtent);
}

QSize StartupSplash::calculateWindowSize(const QSize& availableSize)
{
    if (!availableSize.isValid() || availableSize.width() <= 0 || availableSize.height() <= 0)
    {
        const int extent = kDefaultSplashCardExtent + 2 * kSplashShadowMargin;
        return QSize(extent, extent);
    }

    const int shorterSide = std::min(availableSize.width(), availableSize.height());
    const int margin = shadowMarginForExtent(shorterSide);
    const int cardExtent = calculateCardExtent(availableSize);
    const int extent = std::min(shorterSide, cardExtent + 2 * margin);
    return QSize(extent, extent);
}

void StartupSplash::showCentered()
{
    QScreen *targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!targetScreen)
    {
        targetScreen = QGuiApplication::primaryScreen();
    }

    const QRect availableGeometry = targetScreen
        ? targetScreen->availableGeometry()
        : QRect(QPoint(0, 0), QSize(1920, 1080));
    const QSize splashSize = calculateWindowSize(availableGeometry.size());
    setFixedSize(splashSize);
    updateLogoSize();
    const QPoint topLeft(
        availableGeometry.left() + std::max(0, (availableGeometry.width() - splashSize.width()) / 2),
        availableGeometry.top() + std::max(0, (availableGeometry.height() - splashSize.height()) / 2));
    move(topLeft);

    fading_out_ = false;
    visible_timer_.invalidate();
    setWindowOpacity(1.0);
    show();
    raise();
}

void StartupSplash::fadeOutAndClose(int durationMs)
{
    if (fading_out_)
    {
        return;
    }

    fading_out_ = true;

    if (durationMs <= 0 || !isVisible())
    {
        stopAnimations();
        hide();
        emit fadeOutFinished();
        return;
    }

    window_fade_animation_->stop();
    window_fade_animation_->setDuration(durationMs);
    window_fade_animation_->setStartValue(windowOpacity());
    window_fade_animation_->setEndValue(0.0);
    window_fade_animation_->start();
}

void StartupSplash::closeImmediately()
{
    fading_out_ = true;
    stopAnimations();
    hide();
}

bool StartupSplash::isUsingFallbackText() const
{
    return card_widget_->isUsingFallbackText();
}

bool StartupSplash::animationsRunning() const
{
    return window_fade_animation_->state() == QAbstractAnimation::Running;
}

qint64 StartupSplash::visibleElapsedMilliseconds() const
{
    return visible_timer_.isValid() ? visible_timer_.elapsed() : 0;
}

void StartupSplash::closeEvent(QCloseEvent *event)
{
    closeImmediately();
    QWidget::closeEvent(event);
}

void StartupSplash::hideEvent(QHideEvent *event)
{
    stopAnimations();
    QWidget::hideEvent(event);
}

void StartupSplash::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
}

void StartupSplash::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateLogoSize();
}

void StartupSplash::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    visible_timer_.start();
}

void StartupSplash::stopAnimations()
{
    window_fade_animation_->stop();
}

void StartupSplash::updateLogoSize()
{
    if (!card_widget_)
    {
        return;
    }

    const int extent = std::min(width(), height());
    const int margin = shadowMarginForExtent(extent);
    const int cardExtent = std::max(1, extent - 2 * margin);
    card_widget_->setGeometry(margin, margin, cardExtent, cardExtent);
}

}
