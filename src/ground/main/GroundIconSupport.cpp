#include "ground/main/GroundMainWindowSupport.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>
#include <QTransform>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace VaporView::Ground::MainSupport
{

QString findResourceFile(const QString& relativePath)
{
    const QString appDir = QApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(relativePath),
        QDir(appDir).filePath(QStringLiteral("../") + relativePath),
        QDir(appDir).filePath(QStringLiteral("../../") + relativePath)
    };

    for (const QString& path : candidates)
    {
        if (QFileInfo::exists(path))
        {
            return path;
        }
    }
    return QString();
}


QPixmap renderLucidePixmap(const QByteArray& svgData, const QColor& color, qreal devicePixelRatio)
{
    QByteArray tinted = svgData;
    tinted.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatio);
    constexpr int kLogicalSize = 32;
    const int physicalSize = std::max(1, static_cast<int>(std::ceil(kLogicalSize * dpr)));
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(tinted);
    if (!renderer.isValid())
    {
        return QPixmap();
    }

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(2, 2, 28, 28));
    return pixmap;
}

void addLucideIconPixmaps(QIcon& icon, const QByteArray& svgData, const QColor& color, QIcon::Mode mode)
{
    for (const qreal dpr : {1.0, 1.25, 1.5, 2.0, 3.0})
    {
        icon.addPixmap(renderLucidePixmap(svgData, color, dpr), mode);
    }
}

bool isDarkToolbarTheme();
QColor toolbarColor(AppThemeColor color);

QIcon createLucideIcon(const QString& iconName, const QColor& color)
{
    static QHash<QString, QIcon> cache;
    const QColor disabledColor = toolbarColor(AppThemeColor::ToolbarDisabled);
    const QString cacheKey = QStringLiteral("%1:%2:%3")
        .arg(iconName)
        .arg(color.rgba(), 0, 16)
        .arg(disabledColor.rgba(), 0, 16);
    auto it = cache.constFind(cacheKey);
    if (it != cache.constEnd())
    {
        return it.value();
    }

    QFile file(findResourceFile(QStringLiteral("resources/lucide/%1.svg").arg(iconName)));
    if (!file.open(QIODevice::ReadOnly))
    {
        return QIcon();
    }

    const QByteArray svgData = file.readAll();
    QIcon icon;
    addLucideIconPixmaps(icon, svgData, color, QIcon::Normal);
    addLucideIconPixmaps(icon, svgData, disabledColor, QIcon::Disabled);
    cache.insert(cacheKey, icon);
    return icon;
}


QIcon createRotatedLucideIcon(const QString& iconName, const QColor& color, int degrees)
{
    static QHash<QString, QIcon> cache;
    const int normalizedDegrees = ((degrees % 360) + 360) % 360;
    const QString cacheKey = QStringLiteral("%1:%2:%3")
        .arg(iconName)
        .arg(color.rgba(), 0, 16)
        .arg(normalizedDegrees);
    auto it = cache.constFind(cacheKey);
    if (it != cache.constEnd())
    {
        return it.value();
    }

    const QPixmap source = createLucideIcon(iconName, color).pixmap(QSize(32, 32));
    if (source.isNull())
    {
        return QIcon();
    }

    QPixmap rotated(source.size());
    rotated.setDevicePixelRatio(source.devicePixelRatio());
    rotated.fill(Qt::transparent);

    const QSizeF logicalSize = source.deviceIndependentSize();
    QPainter painter(&rotated);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.translate(logicalSize.width() / 2.0, logicalSize.height() / 2.0);
    painter.rotate(normalizedDegrees);
    painter.translate(-logicalSize.width() / 2.0, -logicalSize.height() / 2.0);
    painter.drawPixmap(QPointF(0.0, 0.0), source);

    QIcon icon;
    icon.addPixmap(rotated, QIcon::Normal);
    icon.addPixmap(rotated, QIcon::Disabled);
    cache.insert(cacheKey, icon);
    return icon;
}


void setDangerTextPalette(QWidget *widget)
{
    if (!widget)
    {
        return;
    }

    QPalette palette = widget->palette();
    const QColor danger = appThemeColor(AppThemeColor::Danger, VaporView::isDarkThemeEnabled());
    palette.setColor(QPalette::WindowText, danger);
    palette.setColor(QPalette::Text, danger);
    widget->setPalette(palette);
}


QString vaporViewLogoResourcePath(bool dark)
{
    return findResourceFile(dark
        ? QStringLiteral("resources/VaproViewLOGO/VaporViewLOGO_rgb217_119_87.svg")
        : QStringLiteral("resources/VaproViewLOGO/VaporViewLOGO_black.svg"));
}

QPixmap renderVaporViewLogo(bool dark, int size, qreal devicePixelRatio)
{
    const QString path = vaporViewLogoResourcePath(dark);
    if (path.isEmpty())
    {
        return QPixmap();
    }

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatio);
    const int logicalSize = std::max(1, size);
    const int physicalSize = std::max(1, qCeil(logicalSize * dpr));
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(path);
    if (!renderer.isValid())
    {
        return QPixmap();
    }

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(0, 0, logicalSize, logicalSize));
    return pixmap;
}

QIcon createVaporViewLogoIcon(bool dark)
{
    const QString path = vaporViewLogoResourcePath(dark);
    return path.isEmpty() ? QIcon() : QIcon(path);
}

QIcon createRefreshIcon()
{
    return createLucideIcon(QStringLiteral("refresh-cw"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createConnectIcon()
{
    return createLucideIcon(QStringLiteral("link"), toolbarColor(AppThemeColor::ToolbarGreen));
}

QIcon createCancelIcon()
{
    return createLucideIcon(QStringLiteral("circle-x"), toolbarColor(AppThemeColor::ToolbarRed));
}

QIcon createDisconnectIcon()
{
    return createLucideIcon(QStringLiteral("unlink"), toolbarColor(AppThemeColor::ToolbarRed));
}

QIcon createPlayIcon()
{
    return createLucideIcon(QStringLiteral("play"), toolbarColor(AppThemeColor::ToolbarGreen));
}

QIcon createPauseIcon()
{
    return createLucideIcon(QStringLiteral("pause"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createStopIcon()
{
    return createLucideIcon(QStringLiteral("square"), toolbarColor(AppThemeColor::ToolbarRed));
}

QIcon createTimerIcon()
{
    return createLucideIcon(QStringLiteral("timer"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createRtkSatelliteIcon(bool running)
{
    return createLucideIcon(
        QStringLiteral("satellite"),
        running ? toolbarColor(AppThemeColor::ToolbarGreen)
                : appThemeColor(AppThemeColor::Primary, isDarkToolbarTheme()));
}

QIcon createClearLogIcon()
{
    return createLucideIcon(QStringLiteral("trash-2"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createLogSearchIcon()
{
    return createLucideIcon(QStringLiteral("search"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createLogFilterIcon()
{
    return createLucideIcon(QStringLiteral("funnel"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createLogSidePanelToggleIcon(bool collapsed)
{
    return createLucideIcon(collapsed ? QStringLiteral("panel-right-open")
                                      : QStringLiteral("panel-right-close"),
                            toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createAppSidebarToggleIcon(bool collapsed)
{
    QIcon icon;
    const QIcon source = createLogSidePanelToggleIcon(collapsed);
    for (const QSize size : {QSize(32, 32), QSize(48, 48), QSize(64, 64), QSize(96, 96)})
    {
        QPixmap pixmap = source.pixmap(size);
        if (!pixmap.isNull())
        {
            icon.addPixmap(pixmap.transformed(QTransform().scale(-1, 1)));
        }
    }
    return icon;
}

QIcon createMenuCheckIcon()
{
    return createLucideIcon(QStringLiteral("check"), appThemeColor(AppThemeColor::MenuCheckText, true));
}

QIcon createMenuCheckIcon(bool dark)
{
    return createLucideIcon(QStringLiteral("check"), appThemeColor(AppThemeColor::MenuCheckText, dark));
}

QIcon createWaveformViewerIcon()
{
    return createLucideIcon(QStringLiteral("audio-waveform"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createLanguageIcon()
{
    return createLucideIcon(QStringLiteral("languages"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createDarkThemeIcon()
{
    return createLucideIcon(QStringLiteral("moon"), toolbarColor(AppThemeColor::ToolbarBlue));
}

QIcon createLightThemeIcon()
{
    return createLucideIcon(QStringLiteral("sun"), toolbarColor(AppThemeColor::ToolbarAmber));
}

QIcon createTitleBarIcon(const QString& iconName, bool dark)
{
    return createLucideIcon(iconName, dark
        ? appThemeColor(AppThemeColor::TextTitle, true)
        : appThemeColor(AppThemeColor::TextStrong, false));
}

bool isDarkToolbarTheme()
{
    if (qApp)
    {
        const QVariant value = qApp->property(kAppDarkThemeProperty);
        if (value.isValid())
        {
            return value.toBool();
        }
        const QPalette palette = qApp->palette();
        return palette.color(QPalette::Window).lightness() < 128 ||
               palette.color(QPalette::Base).lightness() < 128;
    }
    return false;
}

QColor toolbarColor(AppThemeColor color)
{
    return appThemeColor(color, isDarkToolbarTheme());
}


} // namespace VaporView::Ground::MainSupport
