#ifndef VAPORVIEW_STARTUP_SPLASH_H_
#define VAPORVIEW_STARTUP_SPLASH_H_

#include <QElapsedTimer>
#include <QSize>
#include <QString>
#include <QWidget>

class QCloseEvent;
class QHideEvent;
class QPaintEvent;
class QPropertyAnimation;
class QResizeEvent;
class QShowEvent;

namespace VaporView
{

class StartupSplash final : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal contentOpacity READ contentOpacity WRITE setContentOpacity)

public:
    explicit StartupSplash(
        const QString& logoPath = defaultLogoResourcePath(),
        QWidget *parent = nullptr);
    ~StartupSplash() override;

    static QString defaultLogoResourcePath();
    static int calculateCardExtent(const QSize& availableSize);
    static QSize calculateWindowSize(const QSize& availableSize);

    void showCentered();
    void fadeOutAndClose(int durationMs = 200);
    void closeImmediately();

    bool isUsingFallbackText() const;
    bool animationsRunning() const;
    qint64 visibleElapsedMilliseconds() const;

signals:
    void fadeOutFinished();

protected:
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    class SplashCardWidget;

    qreal contentOpacity() const;
    void setContentOpacity(qreal opacity);
    void stopAnimations();
    void updateLogoSize();

    SplashCardWidget *card_widget_ = nullptr;
    QPropertyAnimation *fade_animation_ = nullptr;
    QElapsedTimer visible_timer_;
    qreal content_opacity_ = 1.0;
    bool fading_out_ = false;
};

}

#endif
