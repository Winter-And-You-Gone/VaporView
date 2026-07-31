#include "app/StartupSplash.h"

#include <QAbstractAnimation>
#include <QCloseEvent>
#include <QCursor>
#include <QDebug>
#include <QEasingCurve>
#include <QFont>
#include <QGuiApplication>
#include <QHideEvent>
#include <QPainter>
#include <QPalette>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QSvgRenderer>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kDefaultSplashWidth = 720;
constexpr int kDefaultSplashHeight = 405;
constexpr int kMinimumSplashWidth = 560;
constexpr int kMaximumSplashWidth = 800;
constexpr qreal kSplashWidthFraction = 0.35;
constexpr qreal kSplashAspectRatio = 16.0 / 9.0;
constexpr qreal kLogoWidthFraction = 0.50;
constexpr qreal kInitialLogoOpacity = 0.18;
constexpr qreal kMinimumBreathOpacity = 0.88;
constexpr int kLogoFadeDurationMs = 220;
constexpr int kLogoBreathDurationMs = 1200;
}

namespace VaporView
{

class StartupSplash::LogoWidget final : public QWidget
{
public:
    explicit LogoWidget(const QString& logoPath, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
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

    void setLogoOpacity(qreal opacity)
    {
        const qreal boundedOpacity = std::clamp(opacity, 0.0, 1.0);
        if (qFuzzyCompare(logo_opacity_, boundedOpacity))
        {
            return;
        }
        logo_opacity_ = boundedOpacity;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.setOpacity(logo_opacity_);

        if (fallback_text_)
        {
            painter.setPen(Qt::white);
            QFont fallbackFont = font();
            fallbackFont.setWeight(QFont::DemiBold);
            fallbackFont.setPixelSize(std::max(24, width() / 7));
            painter.setFont(fallbackFont);
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("VaporView"));
            return;
        }

        QSizeF logoSize = renderer_.defaultSize();
        if (logoSize.isEmpty())
        {
            logoSize = size();
        }
        logoSize.scale(size(), Qt::KeepAspectRatio);
        const QRectF targetRect(
            QPointF((width() - logoSize.width()) / 2.0,
                    (height() - logoSize.height()) / 2.0),
            logoSize);
        renderer_.render(&painter, targetRect);
    }

private:
    QSvgRenderer renderer_;
    qreal logo_opacity_ = kInitialLogoOpacity;
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
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(true);

    QPalette splashPalette = palette();
    splashPalette.setColor(QPalette::Window, QColor(0, 0, 0));
    setPalette(splashPalette);
    setFixedSize(kDefaultSplashWidth, kDefaultSplashHeight);

    logo_widget_ = new LogoWidget(logoPath, this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(logo_widget_, 0, Qt::AlignCenter);
    updateLogoSize();

    logo_fade_animation_ = new QVariantAnimation(this);
    logo_fade_animation_->setDuration(kLogoFadeDurationMs);
    logo_fade_animation_->setStartValue(kInitialLogoOpacity);
    logo_fade_animation_->setEndValue(1.0);
    logo_fade_animation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(logo_fade_animation_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                logo_widget_->setLogoOpacity(value.toReal());
            });

    logo_breath_animation_ = new QVariantAnimation(this);
    logo_breath_animation_->setDuration(kLogoBreathDurationMs);
    logo_breath_animation_->setLoopCount(-1);
    logo_breath_animation_->setKeyValueAt(0.0, 1.0);
    logo_breath_animation_->setKeyValueAt(0.5, kMinimumBreathOpacity);
    logo_breath_animation_->setKeyValueAt(1.0, 1.0);
    logo_breath_animation_->setEasingCurve(QEasingCurve::InOutSine);
    connect(logo_breath_animation_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                logo_widget_->setLogoOpacity(value.toReal());
            });
    connect(logo_fade_animation_, &QVariantAnimation::finished, this, [this]() {
        if (!fading_out_ && isVisible())
        {
            logo_breath_animation_->start();
        }
    });

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

QSize StartupSplash::calculateWindowSize(const QSize& availableSize)
{
    if (!availableSize.isValid() || availableSize.width() <= 0 || availableSize.height() <= 0)
    {
        return QSize(kDefaultSplashWidth, kDefaultSplashHeight);
    }

    const int fractionWidth = static_cast<int>(std::lround(
        availableSize.width() * kSplashWidthFraction));
    const int preferredWidth = std::clamp(
        fractionWidth, kMinimumSplashWidth, kMaximumSplashWidth);
    const int maximumWidthForHeight = std::max(
        1, static_cast<int>(std::floor(availableSize.height() * kSplashAspectRatio)));
    const int width = std::max(
        1, std::min({preferredWidth, availableSize.width(), maximumWidthForHeight}));
    const int height = std::max(
        1, std::min(availableSize.height(),
                    static_cast<int>(std::lround(width / kSplashAspectRatio))));
    return QSize(width, height);
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
    const QPoint topLeft(
        availableGeometry.left() + std::max(0, (availableGeometry.width() - splashSize.width()) / 2),
        availableGeometry.top() + std::max(0, (availableGeometry.height() - splashSize.height()) / 2));
    move(topLeft);

    fading_out_ = false;
    visible_timer_.invalidate();
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
    logo_fade_animation_->stop();
    logo_breath_animation_->stop();

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
    return logo_widget_->isUsingFallbackText();
}

bool StartupSplash::animationsRunning() const
{
    return logo_fade_animation_->state() == QAbstractAnimation::Running ||
           logo_breath_animation_->state() == QAbstractAnimation::Running ||
           window_fade_animation_->state() == QAbstractAnimation::Running;
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
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0));
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
    startLogoAnimation();
}

void StartupSplash::startLogoAnimation()
{
    logo_fade_animation_->stop();
    logo_breath_animation_->stop();
    logo_widget_->setLogoOpacity(kInitialLogoOpacity);
    logo_fade_animation_->start();
}

void StartupSplash::stopAnimations()
{
    logo_fade_animation_->stop();
    logo_breath_animation_->stop();
    window_fade_animation_->stop();
}

void StartupSplash::updateLogoSize()
{
    if (!logo_widget_)
    {
        return;
    }
    const int logoExtent = std::max(
        1, std::min(static_cast<int>(std::lround(width() * kLogoWidthFraction)),
                    static_cast<int>(std::lround(height() * 0.90))));
    logo_widget_->setFixedSize(logoExtent, logoExtent);
}

}
