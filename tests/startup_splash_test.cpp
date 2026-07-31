#include "app/StartupSplash.h"

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void requireSizeWithin(const QSize& availableSize)
{
    const QSize splashSize = VaporView::StartupSplash::calculateWindowSize(availableSize);
    require(splashSize.width() > 0 && splashSize.height() > 0,
            "startup splash size must be positive");
    require(splashSize.width() <= availableSize.width() &&
                splashSize.height() <= availableSize.height(),
            "startup splash must fit inside the available screen geometry");
    if (splashSize.width() >= 16 && splashSize.height() >= 9)
    {
        const qreal ratio = static_cast<qreal>(splashSize.width()) / splashSize.height();
        require(std::abs(ratio - (16.0 / 9.0)) < 0.02,
                "startup splash aspect ratio must stay close to 16:9");
    }
}

}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QFile logoFile(VaporView::StartupSplash::defaultLogoResourcePath());
    require(logoFile.open(QIODevice::ReadOnly),
            "embedded startup logo resource must be accessible");
    const QByteArray logoData = logoFile.readAll();
    require(!logoData.isEmpty(), "embedded startup logo resource must not be empty");
    QSvgRenderer logoRenderer(logoData);
    require(logoRenderer.isValid(), "embedded startup logo must contain valid SVG data");

    VaporView::StartupSplash splash;
    require(!splash.isUsingFallbackText(), "embedded startup logo must render without fallback text");
    require(splash.palette().color(QPalette::Window) == QColor(0, 0, 0),
            "startup splash palette must use a black window background");
    require(splash.autoFillBackground(), "startup splash must fill its background");
    require(!splash.testAttribute(Qt::WA_TranslucentBackground),
            "startup splash must not use a translucent backing store");
    require(splash.windowFlags().testFlag(Qt::FramelessWindowHint),
            "startup splash must be frameless");
    require((splash.windowFlags() & Qt::WindowType_Mask) == Qt::SplashScreen,
            "startup splash must use the splash-screen window type");

    QImage renderedBackground(splash.size(), QImage::Format_ARGB32_Premultiplied);
    renderedBackground.fill(Qt::magenta);
    QPainter backgroundPainter(&renderedBackground);
    splash.render(&backgroundPainter);
    backgroundPainter.end();
    require(renderedBackground.pixelColor(0, 0) == QColor(0, 0, 0),
            "startup splash corner pixel must render as pure black");
    bool firstFrameContainsLogo = false;
    for (int y = 0; y < renderedBackground.height() && !firstFrameContainsLogo; ++y)
    {
        for (int x = 0; x < renderedBackground.width(); ++x)
        {
            if (renderedBackground.pixelColor(x, y) != QColor(0, 0, 0))
            {
                firstFrameContainsLogo = true;
                break;
            }
        }
    }
    require(firstFrameContainsLogo, "startup splash first frame must already contain the logo");

    requireSizeWithin(QSize(3840, 2160));
    requireSizeWithin(QSize(1920, 1040));
    requireSizeWithin(QSize(1280, 720));
    requireSizeWithin(QSize(800, 600));
    requireSizeWithin(QSize(480, 270));

    splash.showCentered();
    QCoreApplication::processEvents();
    require(splash.isVisible(), "startup splash must become visible when shown");
    require(splash.minimumSize() == splash.maximumSize(),
            "startup splash must not be resizable");
    require(splash.animationsRunning(), "startup logo animation must run while visible");
    splash.fadeOutAndClose(0);
    QCoreApplication::processEvents();
    require(!splash.isVisible(), "startup splash must hide after fade-out completion");
    require(!splash.animationsRunning(), "startup splash animations must stop after closing");

    VaporView::StartupSplash fallbackSplash(QStringLiteral(":/vaporview/startup/missing.svg"));
    require(fallbackSplash.isUsingFallbackText(),
            "missing startup logo must fall back to VaporView text");

    std::cout << "startup_splash_test passed\n";
    return 0;
}
