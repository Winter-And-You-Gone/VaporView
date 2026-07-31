#include "app/StartupSplash.h"

#include <QApplication>
#include <QFile>
#include <QImage>
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
    require(splashSize.width() == splashSize.height(),
            "startup splash must use a square window");
    require(splashSize.width() <= availableSize.width() &&
                splashSize.height() <= availableSize.height(),
            "startup splash must fit inside the available screen geometry");
    const int cardExtent = VaporView::StartupSplash::calculateCardExtent(availableSize);
    require(cardExtent > 0 && cardExtent <= 240,
            "startup splash card extent must stay bounded");
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
    require(splash.testAttribute(Qt::WA_TranslucentBackground),
            "startup splash must use a translucent backing store");
    require(!splash.autoFillBackground(),
            "startup splash must not auto-fill its transparent window");
    require(!splash.testAttribute(Qt::WA_OpaquePaintEvent),
            "startup splash must not claim an opaque paint event");
    require(splash.windowFlags().testFlag(Qt::FramelessWindowHint),
            "startup splash must be frameless");
    require((splash.windowFlags() & Qt::WindowType_Mask) == Qt::SplashScreen,
            "startup splash must use the splash-screen window type");

    requireSizeWithin(QSize(3840, 2160));
    requireSizeWithin(QSize(1920, 1040));
    requireSizeWithin(QSize(1280, 720));
    requireSizeWithin(QSize(800, 600));
    requireSizeWithin(QSize(480, 270));
    const QSize hugeScreenSize = VaporView::StartupSplash::calculateWindowSize(QSize(7680, 4320));
    require(hugeScreenSize == QSize(300, 300),
            "startup splash must cap its large-screen extent");
    const QSize tinyScreenSize = VaporView::StartupSplash::calculateWindowSize(QSize(120, 90));
    require(tinyScreenSize.width() == tinyScreenSize.height() &&
                tinyScreenSize.width() <= 90,
            "startup splash must fit a very small screen");

    splash.showCentered();
    QCoreApplication::processEvents();
    require(splash.isVisible(), "startup splash must become visible when shown");
    require(splash.minimumSize() == splash.maximumSize(),
            "startup splash must not be resizable");
    require(!splash.animationsRunning(),
            "startup splash must not animate the logo while visible");

    const QImage renderedFrame = splash.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    require(renderedFrame.width() == renderedFrame.height(),
            "startup splash render must remain square");
    require(renderedFrame.pixelColor(0, 0).alpha() < 16,
            "startup splash corner must remain transparent outside the card");
    const QColor cardPixel = renderedFrame.pixelColor(
        renderedFrame.width() / 2, std::max(1, renderedFrame.height() / 7));
    require(cardPixel.red() < 24 && cardPixel.green() < 24 && cardPixel.blue() < 24,
            "startup splash card interior must be near-black");

    bool firstFrameContainsWhiteLogo = false;
    for (int y = 0; y < renderedFrame.height() && !firstFrameContainsWhiteLogo; ++y)
    {
        for (int x = 0; x < renderedFrame.width(); ++x)
        {
            const QColor pixel = renderedFrame.pixelColor(x, y);
            if (pixel.alpha() > 220 && pixel.red() > 220 && pixel.green() > 220 && pixel.blue() > 220)
            {
                firstFrameContainsWhiteLogo = true;
                break;
            }
        }
    }
    require(firstFrameContainsWhiteLogo,
            "startup splash first frame must already contain a white logo");

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
