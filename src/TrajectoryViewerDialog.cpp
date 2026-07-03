#include "AppTheme.h"
#include "TrajectoryViewerDialog.h"
#include "CustomTitleBar.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QComboBox>
#include <QIcon>
#include <QLabel>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStandardPaths>
#include <QStringConverter>
#include <QSvgRenderer>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QVector>
#include <QWidgetAction>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWidget>
#include <QtMath>
#include <QSslError>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

using VaporView::AppThemeColor;
using VaporView::appThemeColor;
using VaporView::appThemeColorName;
using VaporView::isDarkThemePalette;

namespace
{
constexpr int kTileSize = 256;
constexpr int kDefaultZoom = 16;
constexpr int kMaxZoom = 19;
constexpr int kTianDiTuMaxZoom = 18;
constexpr int kMaxDisplayZoom = 30;
constexpr int kMinZoom = 1;
constexpr int kTitleBarButtonSize = 34;
constexpr int kTitleBarIconSize = 24;
constexpr int kMaxConcurrentTileRequests = 8;
constexpr int kTrajectorySidebarWidth = 340;
constexpr int kTrajectoryMapMinimumHeight = 360;
constexpr int kTileCurrentPriority = 0;
constexpr int kTilePrefetchPriority = 10;
constexpr int kTileAdjacentZoomPriority = 20;
constexpr int kTilePanPrefetchExpansion = 2;
constexpr int kTileZoomRequestDebounceMs = 90;
constexpr int kTilePanRequestDebounceMs = 50;
constexpr double kDefaultTrackWidth = 2.8;
constexpr double kDefaultTrackPointRadius = 4.0;
constexpr int kTrackStyleSliderScale = 10;
constexpr int kTrackWorldCacheZoom = 20;
constexpr int kTrackSpatialCellSize = 4096;
constexpr double kTrackPointMinPixelStep = 9.0;
constexpr double kTrackBucketSize = 36.0;
constexpr qint64 kTrackMaxCellScanCount = 20000;
constexpr auto kTileRequestTimeout = std::chrono::seconds(15);

enum class TileProvider
{
    OpenStreetMap,
    TianDiTuVector,
    TianDiTuSatellite
};

enum class HeatPalette
{
    Turbo,
    Neon,
    Sunset,
    Ocean,
    Candy
};

QString tileProviderSettingKey()
{
    return QStringLiteral("map/source");
}

QString tiandituKeySettingKey()
{
    return QStringLiteral("map/tianditu_key");
}

double clampLatitude(double latitude)
{
    return std::clamp(latitude, -85.05112878, 85.05112878);
}

QPointF latLonToPixel(double latitude, double longitude, int zoom)
{
    const double lat = clampLatitude(latitude);
    const double sinLat = std::sin(qDegreesToRadians(lat));
    const double worldSize = static_cast<double>(kTileSize) * std::pow(2.0, zoom);
    const double x = (longitude + 180.0) / 360.0 * worldSize;
    const double y = (0.5 - std::log((1.0 + sinLat) / (1.0 - sinLat)) / (4.0 * M_PI)) * worldSize;
    return QPointF(x, y);
}

QString tileKey(int zoom, int tileX, int tileY)
{
    return QStringLiteral("%1/%2/%3").arg(zoom).arg(tileX).arg(tileY);
}

QString tileProviderKey(TileProvider provider)
{
    switch (provider)
    {
    case TileProvider::TianDiTuVector:
        return QStringLiteral("tianditu_vec");
    case TileProvider::TianDiTuSatellite:
        return QStringLiteral("tianditu_img");
    case TileProvider::OpenStreetMap:
    default:
        return QStringLiteral("osm");
    }
}

bool isTianDiTuProvider(TileProvider provider)
{
    return provider == TileProvider::TianDiTuVector || provider == TileProvider::TianDiTuSatellite;
}

int tileProviderComboIndex(TileProvider provider)
{
    switch (provider)
    {
    case TileProvider::TianDiTuVector:
        return 1;
    case TileProvider::TianDiTuSatellite:
        return 2;
    case TileProvider::OpenStreetMap:
    default:
        return 0;
    }
}

TileProvider tileProviderFromComboIndex(int index)
{
    if (index == 1)
    {
        return TileProvider::TianDiTuVector;
    }
    if (index == 2)
    {
        return TileProvider::TianDiTuSatellite;
    }
    return TileProvider::OpenStreetMap;
}

int heatPaletteComboIndex(HeatPalette palette)
{
    switch (palette)
    {
    case HeatPalette::Neon:
        return 1;
    case HeatPalette::Sunset:
        return 2;
    case HeatPalette::Ocean:
        return 3;
    case HeatPalette::Candy:
        return 4;
    case HeatPalette::Turbo:
    default:
        return 0;
    }
}

HeatPalette heatPaletteFromComboIndex(int index)
{
    switch (index)
    {
    case 1:
        return HeatPalette::Neon;
    case 2:
        return HeatPalette::Sunset;
    case 3:
        return HeatPalette::Ocean;
    case 4:
        return HeatPalette::Candy;
    case 0:
    default:
        return HeatPalette::Turbo;
    }
}

QString heatPaletteName(HeatPalette palette, bool english)
{
    switch (palette)
    {
    case HeatPalette::Neon:
        return english ? QStringLiteral("Neon") : QStringLiteral("霓虹");
    case HeatPalette::Sunset:
        return english ? QStringLiteral("Sunset") : QStringLiteral("日落");
    case HeatPalette::Ocean:
        return english ? QStringLiteral("Ocean") : QStringLiteral("海洋");
    case HeatPalette::Candy:
        return english ? QStringLiteral("Candy") : QStringLiteral("糖果");
    case HeatPalette::Turbo:
    default:
        return english ? QStringLiteral("Turbo vivid") : QStringLiteral("Turbo 明艳");
    }
}

int providerMaxZoom(TileProvider provider)
{
    return isTianDiTuProvider(provider) ? kTianDiTuMaxZoom : kMaxZoom;
}

struct TileLayerSpec
{
    QString cache_suffix;
    QString endpoint_path;
    QString layer;
    QString format;
};

struct TileFetchRequest
{
    QString key;
    QNetworkRequest request;
    int generation;
    int priority;
};

struct TileRange
{
    int zoom;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
};

struct ProjectedTrackPoint
{
    QPointF world_pixel;
    int cell_x;
    int cell_y;
};

struct ScreenTrackPoint
{
    int index;
    QPointF screen;
};

struct ScreenTrackSegment
{
    int first_index;
    int second_index;
    QPointF first_screen;
    QPointF second_screen;
};

struct TrackRenderContext
{
    QVector<ScreenTrackPoint> point_points;
    QHash<qint64, QVector<int>> hit_buckets;
};

QVector<TileLayerSpec> tileLayerSpecs(TileProvider provider)
{
    switch (provider)
    {
    case TileProvider::TianDiTuVector:
        return {
            {QStringLiteral("vec"), QStringLiteral("vec_w"), QStringLiteral("vec"), QStringLiteral("tiles")},
            {QStringLiteral("cva"), QStringLiteral("cva_w"), QStringLiteral("cva"), QStringLiteral("tiles")}
        };
    case TileProvider::TianDiTuSatellite:
        return {
            {QStringLiteral("img"), QStringLiteral("img_w"), QStringLiteral("img"), QStringLiteral("tiles")},
            {QStringLiteral("cia"), QStringLiteral("cia_w"), QStringLiteral("cia"), QStringLiteral("tiles")}
        };
    case TileProvider::OpenStreetMap:
    default:
        return {
            {QStringLiteral("osm"), QString(), QString(), QString()}
        };
    }
}

QString tiandituHostForTile(int tileX, int tileY, const QString& layerSuffix)
{
    const int layerSeed = qHash(layerSuffix) & 0x7fffffff;
    const int shard = std::abs(tileX * 31 + tileY * 17 + layerSeed) % 8;
    return QStringLiteral("t%1.tianditu.gov.cn").arg(shard);
}

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

bool isDarkPalette()
{
    return qApp && isDarkThemePalette(qApp->palette());
}

QPixmap renderLucidePixmap(const QByteArray& svgData, const QColor& color)
{
    QByteArray tinted = svgData;
    tinted.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(tinted);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(2, 2, 28, 28));
    return pixmap;
}

QIcon createLucideIcon(const QString& iconName, const QColor& color)
{
    QFile file(findResourceFile(QStringLiteral("resources/lucide/%1.svg").arg(iconName)));
    if (!file.open(QIODevice::ReadOnly))
    {
        return QIcon();
    }

    QIcon icon;
    icon.addPixmap(renderLucidePixmap(file.readAll(), color), QIcon::Normal);
    return icon;
}

QIcon createTitleBarIcon(const QString& iconName, bool dark)
{
    return createLucideIcon(iconName, dark
        ? appThemeColor(AppThemeColor::TextTitle, true)
        : appThemeColor(AppThemeColor::TextStrong, false));
}

QColor defaultTrackColor()
{
    return appThemeColor(AppThemeColor::TrackDefault, false);
}

QColor interpolateColor(const QColor& first, const QColor& second, double ratio)
{
    const double clampedRatio = std::clamp(ratio, 0.0, 1.0);
    return QColor(
        static_cast<int>(std::lround(first.red() + (second.red() - first.red()) * clampedRatio)),
        static_cast<int>(std::lround(first.green() + (second.green() - first.green()) * clampedRatio)),
        static_cast<int>(std::lround(first.blue() + (second.blue() - first.blue()) * clampedRatio)));
}

QColor heatmapColorAt(double normalized, HeatPalette palette)
{
    static const std::array<std::pair<double, QColor>, 8> turbo = {{
        {0.00, QColor(QStringLiteral("#30123B"))},
        {0.14, QColor(QStringLiteral("#445BFF"))},
        {0.28, QColor(QStringLiteral("#18B7FF"))},
        {0.42, QColor(QStringLiteral("#1DFFB3"))},
        {0.56, QColor(QStringLiteral("#B6FF1D"))},
        {0.70, QColor(QStringLiteral("#FFD21A"))},
        {0.84, QColor(QStringLiteral("#FF6A00"))},
        {1.00, QColor(QStringLiteral("#E60026"))}
    }};
    static const std::array<std::pair<double, QColor>, 7> neon = {{
        {0.00, QColor(QStringLiteral("#2400FF"))},
        {0.18, QColor(QStringLiteral("#008CFF"))},
        {0.34, QColor(QStringLiteral("#00F5FF"))},
        {0.50, QColor(QStringLiteral("#00FF66"))},
        {0.66, QColor(QStringLiteral("#F7FF00"))},
        {0.82, QColor(QStringLiteral("#FF7A00"))},
        {1.00, QColor(QStringLiteral("#FF005D"))}
    }};
    static const std::array<std::pair<double, QColor>, 6> sunset = {{
        {0.00, QColor(QStringLiteral("#3B0CA3"))},
        {0.20, QColor(QStringLiteral("#8A1CFF"))},
        {0.40, QColor(QStringLiteral("#FF2DB2"))},
        {0.60, QColor(QStringLiteral("#FF5C00"))},
        {0.80, QColor(QStringLiteral("#FFD000"))},
        {1.00, QColor(QStringLiteral("#FFF44F"))}
    }};
    static const std::array<std::pair<double, QColor>, 6> ocean = {{
        {0.00, QColor(QStringLiteral("#001E9A"))},
        {0.20, QColor(QStringLiteral("#006CFF"))},
        {0.40, QColor(QStringLiteral("#00D4FF"))},
        {0.60, QColor(QStringLiteral("#00FFB2"))},
        {0.80, QColor(QStringLiteral("#A8FF00"))},
        {1.00, QColor(QStringLiteral("#FFF200"))}
    }};
    static const std::array<std::pair<double, QColor>, 6> candy = {{
        {0.00, QColor(QStringLiteral("#0057FF"))},
        {0.20, QColor(QStringLiteral("#00F0FF"))},
        {0.40, QColor(QStringLiteral("#44FF00"))},
        {0.60, QColor(QStringLiteral("#FFF500"))},
        {0.80, QColor(QStringLiteral("#FF7A00"))},
        {1.00, QColor(QStringLiteral("#FF00B8"))}
    }};

    const double clamped = std::clamp(normalized, 0.0, 1.0);
    const auto colorAtStop = [clamped](const auto& stops) {
        for (size_t index = 1; index < stops.size(); ++index)
        {
            const auto& previous = stops[index - 1];
            const auto& current = stops[index];
            if (clamped <= current.first)
            {
                const double localRatio = (clamped - previous.first) / std::max(1e-6, current.first - previous.first);
                return interpolateColor(previous.second, current.second, localRatio);
            }
        }
        return stops.back().second;
    };

    switch (palette)
    {
    case HeatPalette::Neon:
        return colorAtStop(neon);
    case HeatPalette::Sunset:
        return colorAtStop(sunset);
    case HeatPalette::Ocean:
        return colorAtStop(ocean);
    case HeatPalette::Candy:
        return colorAtStop(candy);
    case HeatPalette::Turbo:
    default:
        return colorAtStop(turbo);
    }
}

QColor heatmapColorAt(double normalized)
{
    return heatmapColorAt(normalized, HeatPalette::Turbo);
}

QColor trackHeatColor(float peakValue, float minPeak, float maxPeak, HeatPalette palette)
{
    const double totalRange = static_cast<double>(maxPeak) - static_cast<double>(minPeak);
    if (!(totalRange > 1e-6))
    {
        return heatmapColorAt(0.5, palette);
    }

    const double normalized = std::clamp(
        (static_cast<double>(peakValue) - static_cast<double>(minPeak)) / totalRange,
        0.0,
        1.0);
    return heatmapColorAt(normalized, palette);
}

QColor trackHeatColor(float peakValue, float minPeak, float maxPeak)
{
    const double totalRange = static_cast<double>(maxPeak) - static_cast<double>(minPeak);
    if (!(totalRange > 1e-6))
    {
        return heatmapColorAt(0.5);
    }

    const double normalized = std::clamp(
        (static_cast<double>(peakValue) - static_cast<double>(minPeak)) / totalRange,
        0.0,
        1.0);
    return heatmapColorAt(normalized);
}

class HeatGradientBarWidget : public QWidget
{
public:
    explicit HeatGradientBarWidget(QWidget *parent = nullptr)
        : QWidget(parent)
        , palette_(HeatPalette::Turbo)
    {
        setObjectName(QStringLiteral("trajectoryHeatGradientBar"));
        setFixedHeight(10);
        setMinimumWidth(260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setHeatPalette(HeatPalette palette)
    {
        if (palette_ == palette)
        {
            return;
        }
        palette_ = palette;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF barRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient gradient(barRect.left(), barRect.top(), barRect.right(), barRect.top());
        for (int stop = 0; stop <= 14; ++stop)
        {
            const double ratio = stop / 14.0;
            gradient.setColorAt(ratio, heatmapColorAt(ratio, palette_));
        }

        QPainterPath barPath;
        barPath.addRoundedRect(barRect, 4.0, 4.0);
        painter.fillPath(barPath, gradient);
        painter.setPen(QPen(appThemeColor(AppThemeColor::BorderStrong, isDarkPalette()), 1));
        painter.drawPath(barPath);
    }

private:
    HeatPalette palette_;
};

QString formatPeakValue(double value)
{
    return QString::number(value, 'f', 6);
}

QString formatTimestampUs(quint64 timestampUs)
{
    if (timestampUs == 0)
    {
        return QStringLiteral("--");
    }
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestampUs / 1000), QTimeZone::UTC)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz 'UTC'"));
}

QString formatDistanceMeters(double meters)
{
    if (!std::isfinite(meters))
    {
        return QStringLiteral("--");
    }
    if (std::abs(meters) >= 1000.0)
    {
        return QStringLiteral("%1 km").arg(QString::number(meters / 1000.0, 'f', 3));
    }
    return QStringLiteral("%1 m").arg(QString::number(meters, 'f', 2));
}

QString formatSpeed(double metersPerSecond)
{
    if (!std::isfinite(metersPerSecond))
    {
        return QStringLiteral("--");
    }
    return QStringLiteral("%1 m/s (%2 km/h)")
        .arg(QString::number(metersPerSecond, 'f', 2),
             QString::number(metersPerSecond * 3.6, 'f', 2));
}

QString formatSignedDeltaMs(quint64 deltaUs)
{
    return QStringLiteral("%1 ms").arg(QString::number(static_cast<double>(deltaUs) / 1000.0, 'f', 3));
}

QString trajectoryInfoRow(const QString& label, const QString& value, bool dark)
{
    return QStringLiteral(
        "<tr>"
        "<td style=\"padding:0 10px 5px 0; color:%1; font-weight:600; white-space:nowrap; vertical-align:top;\">%2</td>"
        "<td style=\"padding:0 0 5px 0; color:%3; font-weight:600; vertical-align:top;\">%4</td>"
        "</tr>")
        .arg(appThemeColorName(AppThemeColor::TextMuted, dark),
             label.toHtmlEscaped(),
             appThemeColorName(AppThemeColor::TextStrong, dark),
             value.toHtmlEscaped());
}

QString trajectoryInfoTable(const QString& title,
                            const QVector<QPair<QString, QString>>& rows,
                            bool dark)
{
    QString html = QStringLiteral(
        "<div style=\"color:%1; font-weight:700; margin-bottom:6px;\">%2</div>"
        "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" style=\"border-collapse:collapse;\">")
        .arg(appThemeColorName(AppThemeColor::TextStrong, dark), title.toHtmlEscaped());
    for (const auto& row : rows)
    {
        html += trajectoryInfoRow(row.first, row.second, dark);
    }
    html += QStringLiteral("</table>");
    return html;
}

QString csvCell(QString value)
{
    value.replace('"', QStringLiteral("\"\""));
    if (value.contains(',') || value.contains('"') || value.contains('\n') || value.contains('\r'))
    {
        return QStringLiteral("\"%1\"").arg(value);
    }
    return value;
}

QString mapAttributionText(TileProvider provider, bool english)
{
    switch (provider)
    {
    case TileProvider::TianDiTuVector:
        return english ? QStringLiteral("Map data © Tianditu Vector") : QStringLiteral("底图数据 © 天地图矢量");
    case TileProvider::TianDiTuSatellite:
        return english ? QStringLiteral("Map data © Tianditu Imagery") : QStringLiteral("底图数据 © 天地图影像");
    case TileProvider::OpenStreetMap:
    default:
        return english ? QStringLiteral("Map data © OpenStreetMap contributors")
                       : QStringLiteral("底图数据 © OpenStreetMap contributors");
    }
}

class TrajectoryMapWidget : public QWidget
{
public:
    explicit TrajectoryMapWidget(QWidget *parent = nullptr)
        : QWidget(parent)
        , manager_(new QNetworkAccessManager(this))
        , is_english_(false)
        , tile_provider_(TileProvider::OpenStreetMap)
        , heat_palette_(HeatPalette::Turbo)
        , tianditu_key_()
        , zoom_(kDefaultZoom)
        , center_world_pixel_(0.0, 0.0)
        , fit_zoom_(kDefaultZoom)
        , fit_center_world_pixel_(0.0, 0.0)
        , manual_view_active_(false)
        , dragging_(false)
        , drag_moved_(false)
        , drag_start_pos_()
        , drag_start_center_world_pixel_()
        , drag_current_delta_()
        , drag_frame_cache_()
        , selected_track_index_(-1)
        , hovered_track_index_(-1)
        , track_width_(kDefaultTrackWidth)
        , point_radius_(kDefaultTrackPointRadius)
        , show_route_(true)
        , show_track_points_(true)
        , has_peak_range_(false)
        , min_peak_(0.0f)
        , max_peak_(0.0f)
        , peak_count_(0)
        , active_tile_request_count_(0)
        , tile_request_generation_(0)
        , visible_tile_request_scheduled_(false)
        , feedback_update_scheduled_(false)
        , repaint_update_requested_(false)
        , english_track_label_(QStringLiteral("RTK trajectory"))
        , chinese_track_label_(QStringLiteral("RTK轨迹"))
    {
        setMinimumSize(720, 420);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover, true);

        auto *cache = new QNetworkDiskCache(manager_);
        const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        cache->setCacheDirectory(QDir(cacheRoot).filePath(QStringLiteral("map-tiles")));
        cache->setMaximumCacheSize(128LL * 1024LL * 1024LL);
        manager_->setCache(cache);
        connect(manager_, &QNetworkAccessManager::sslErrors, this,
                [this](QNetworkReply *, const QList<QSslError>& errors) {
            if (!errors.isEmpty())
            {
                last_tile_error_ = errors.first().errorString();
                scheduleLoadFeedbackUpdate(false);
            }
        });
    }

    ~TrajectoryMapWidget() override
    {
        ++tile_request_generation_;
        visible_tile_request_scheduled_ = false;
        feedback_update_scheduled_ = false;
        repaint_update_requested_ = false;
        status_callback_ = nullptr;
        progress_callback_ = nullptr;
        selection_callback_ = nullptr;

        if (manager_)
        {
            manager_->disconnect(this);
        }

        abortActiveTileReplies();
        pending_tiles_.clear();
        queued_tile_requests_.clear();
        current_visible_tile_keys_.clear();
        current_requested_tile_keys_.clear();
        failed_tiles_.clear();
    }

    void cancelTileActivity()
    {
        ++tile_request_generation_;
        visible_tile_request_scheduled_ = false;
        feedback_update_scheduled_ = false;
        repaint_update_requested_ = false;
        pending_tiles_.clear();
        queued_tile_requests_.clear();
        current_visible_tile_keys_.clear();
        current_requested_tile_keys_.clear();
        failed_tiles_.clear();
        abortActiveTileReplies();
    }

    void setStatusCallback(std::function<void(const QString&)> callback)
    {
        status_callback_ = std::move(callback);
        updateLoadFeedback();
    }

    void setProgressCallback(std::function<void(int, int, int)> callback)
    {
        progress_callback_ = std::move(callback);
        updateLoadFeedback();
    }

    void setSelectionCallback(std::function<void(int)> callback)
    {
        selection_callback_ = std::move(callback);
    }

    void setEnglish(bool english)
    {
        is_english_ = english;
        updateLoadFeedback();
        update();
    }

    void setTrackPoints(const QVector<RtkTrackPoint>& points)
    {
        track_points_ = points;
        rebuildTrackCaches();
        hovered_track_index_ = -1;
        selected_track_index_ = track_points_.isEmpty()
            ? -1
            : std::clamp(selected_track_index_, 0, static_cast<int>(track_points_.size()) - 1);
        if (track_points_.isEmpty())
        {
            unsetCursor();
        }
        manual_view_active_ = false;
        resetTileLoadingState(true);
        refreshViewport();
        update();
    }

    void setSelectedTrackIndex(int index)
    {
        const int clamped = track_points_.isEmpty()
            ? -1
            : std::clamp(index, 0, static_cast<int>(track_points_.size()) - 1);
        if (selected_track_index_ == clamped)
        {
            return;
        }
        selected_track_index_ = clamped;
        update();
    }

    void setTrackLabel(const QString& englishLabel, const QString& chineseLabel)
    {
        english_track_label_ = englishLabel;
        chinese_track_label_ = chineseLabel;
        update();
    }

    void setTileProvider(TileProvider provider)
    {
        if (tile_provider_ == provider)
        {
            return;
        }
        tile_provider_ = provider;
        resetTileLoadingState(true);
        requestVisibleTiles();
        updateLoadFeedback();
        update();
    }

    TileProvider tileProvider() const
    {
        return tile_provider_;
    }

    void setHeatPalette(HeatPalette palette)
    {
        if (heat_palette_ == palette)
        {
            return;
        }
        heat_palette_ = palette;
        update();
    }

    HeatPalette heatPalette() const
    {
        return heat_palette_;
    }

    void setTrackWidth(double width)
    {
        const double clamped = std::clamp(width, 1.0, 8.0);
        if (qFuzzyCompare(track_width_, clamped))
        {
            return;
        }
        track_width_ = clamped;
        update();
    }

    double trackWidth() const
    {
        return track_width_;
    }

    void setPointRadius(double radius)
    {
        const double clamped = std::clamp(radius, 2.0, 12.0);
        if (qFuzzyCompare(point_radius_, clamped))
        {
            return;
        }
        point_radius_ = clamped;
        last_render_context_ = TrackRenderContext();
        update();
    }

    double pointRadius() const
    {
        return point_radius_;
    }

    void setShowRoute(bool show)
    {
        if (show_route_ == show)
        {
            return;
        }
        show_route_ = show;
        update();
    }

    void setShowTrackPoints(bool show)
    {
        if (show_track_points_ == show)
        {
            return;
        }
        show_track_points_ = show;
        last_render_context_ = TrackRenderContext();
        if (!show_track_points_)
        {
            setHoveredTrackPoint(-1);
        }
        update();
    }

    void setTianDiTuKey(const QString& key)
    {
        const QString trimmed = key.trimmed();
        if (tianditu_key_ == trimmed)
        {
            return;
        }
        tianditu_key_ = trimmed;
        if (isTianDiTuProvider(tile_provider_))
        {
            resetTileLoadingState(true);
            requestVisibleTiles();
            updateLoadFeedback();
            update();
        }
    }

    QString tiandituKey() const
    {
        return tianditu_key_;
    }

    void zoomIn()
    {
        adjustZoom(+1);
    }

    void zoomOut()
    {
        adjustZoom(-1);
    }

    void resetView()
    {
        manual_view_active_ = false;
        zoom_ = fit_zoom_;
        center_world_pixel_ = fit_center_world_pixel_;
        drag_frame_cache_ = QPixmap();
        last_render_context_ = TrackRenderContext();
        requestVisibleTiles();
        update();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        drag_frame_cache_ = QPixmap();
        last_render_context_ = TrackRenderContext();
        refreshViewport();
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (track_points_.isEmpty())
        {
            event->ignore();
            return;
        }

        const QPoint angleDelta = event->angleDelta();
        if (angleDelta.y() > 0)
        {
            adjustZoom(+1);
            event->accept();
            return;
        }
        if (angleDelta.y() < 0)
        {
            adjustZoom(-1);
            event->accept();
            return;
        }
        event->ignore();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !track_points_.isEmpty())
        {
            drag_frame_cache_ = renderDragFrameCache();
            dragging_ = true;
            drag_moved_ = false;
            drag_start_pos_ = event->position();
            drag_current_delta_ = QPointF();
            drag_start_center_world_pixel_ = center_world_pixel_;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (dragging_)
        {
            const QPointF delta = event->position() - drag_start_pos_;
            if (std::abs(delta.x()) > 3.0 || std::abs(delta.y()) > 3.0)
            {
                drag_moved_ = true;
            }
            center_world_pixel_ = drag_start_center_world_pixel_ - delta;
            manual_view_active_ = true;
            drag_current_delta_ = delta;
            scheduleVisibleTileRequest(kTilePanRequestDebounceMs);
            update();
            event->accept();
            return;
        }
        updateHoveredTrackPoint(event->position());
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (dragging_ && event->button() == Qt::LeftButton)
        {
            const bool wasClick = !drag_moved_;
            dragging_ = false;
            drag_frame_cache_ = QPixmap();
            drag_current_delta_ = QPointF();
            last_render_context_ = TrackRenderContext();
            requestVisibleTiles();
            updateHoveredTrackPoint(event->position());
            if (wasClick)
            {
                const int index = closestTrackPointIndex(event->position());
                if (index >= 0)
                {
                    selected_track_index_ = index;
                    update();
                    if (selection_callback_)
                    {
                        selection_callback_(index);
                    }
                }
            }
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        setHoveredTrackPoint(-1);
        QWidget::leaveEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, !dragging_);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, !dragging_);
        painter.fillRect(rect(), appThemeColor(AppThemeColor::MapCanvas, false));

        const QRectF mapRect = mapViewportRect();
        const QRectF roundedMapRect = mapRect.adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath mapClip;
        mapClip.addRoundedRect(roundedMapRect, 8.0, 8.0);
        painter.fillPath(mapClip, appThemeColor(AppThemeColor::MapViewport, false));

        if (track_points_.isEmpty())
        {
            painter.setPen(appThemeColor(AppThemeColor::MapMutedText, false));
            painter.drawRoundedRect(roundedMapRect, 8.0, 8.0);
            painter.drawText(mapRect, Qt::AlignCenter,
                is_english_
                    ? QStringLiteral("No %1 available in this session.").arg(english_track_label_)
                    : QStringLiteral("当前会话中没有可用的%1。").arg(chinese_track_label_));
            return;
        }

        painter.save();
        painter.setClipPath(mapClip);
        if (dragging_ && !drag_frame_cache_.isNull())
        {
            drawTiles(painter, mapRect);
            painter.drawPixmap(mapRect.topLeft() + drag_current_delta_, drag_frame_cache_);
        }
        else
        {
            drawMapBody(painter, mapRect);
        }
        painter.restore();

        drawMapFooter(painter, mapRect);

        painter.setPen(QPen(appThemeColor(AppThemeColor::MapTileBorder, false), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(roundedMapRect, 8.0, 8.0);
    }

private:
    QPixmap renderDragFrameCache()
    {
        const qreal dpr = devicePixelRatioF();
        QPixmap pixmap(size() * dpr);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter cachePainter(&pixmap);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        cachePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QRectF mapRect = mapViewportRect();
        QPainterPath mapClip;
        mapClip.addRoundedRect(mapRect.adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);
        cachePainter.setClipPath(mapClip);
        drawMapBody(cachePainter, mapRect);
        return pixmap;
    }

    void drawMapBody(QPainter& painter, const QRectF& mapRect)
    {
        drawTiles(painter, mapRect);
        drawTrack(painter, mapRect);
    }

    void resetTileLoadingState(bool clearCache)
    {
        ++tile_request_generation_;
        if (clearCache)
        {
            tile_cache_.clear();
        }
        pending_tiles_.clear();
        queued_tile_requests_.clear();
        current_requested_tile_keys_.clear();
        failed_tiles_.clear();
        last_tile_error_.clear();
        abortActiveTileReplies();
    }

    void rebuildTrackCaches()
    {
        projected_track_points_.clear();
        track_spatial_index_.clear();
        last_render_context_ = TrackRenderContext();
        has_peak_range_ = false;
        min_peak_ = std::numeric_limits<float>::max();
        max_peak_ = std::numeric_limits<float>::lowest();
        peak_count_ = 0;

        projected_track_points_.reserve(track_points_.size());
        for (int index = 0; index < track_points_.size(); ++index)
        {
            const RtkTrackPoint& point = track_points_.at(index);
            const QPointF worldPixel = latLonToPixel(point.latitude, point.longitude, kTrackWorldCacheZoom);
            const int cellX = static_cast<int>(std::floor(worldPixel.x() / kTrackSpatialCellSize));
            const int cellY = static_cast<int>(std::floor(worldPixel.y() / kTrackSpatialCellSize));
            projected_track_points_.push_back({worldPixel, cellX, cellY});
            track_spatial_index_[trackCellKey(cellX, cellY)].push_back(index);

            if (point.has_peak_value && std::isfinite(point.peak_value))
            {
                has_peak_range_ = true;
                ++peak_count_;
                min_peak_ = std::min(min_peak_, point.peak_value);
                max_peak_ = std::max(max_peak_, point.peak_value);
            }
        }
    }

    void abortActiveTileReplies()
    {
        const QSet<QNetworkReply*> replies = active_tile_replies_;
        for (QNetworkReply *reply : replies)
        {
            if (reply)
            {
                reply->abort();
            }
        }
    }

    void abortStaleActiveTileReplies()
    {
        const QSet<QNetworkReply*> replies = active_tile_replies_;
        for (QNetworkReply *reply : replies)
        {
            if (!reply)
            {
                continue;
            }

            const QString key = active_tile_reply_keys_.value(reply);
            if (!current_requested_tile_keys_.contains(key))
            {
                reply->abort();
            }
        }
    }

    void enqueueTileRequest(const QString& key, const QNetworkRequest& request, int priority)
    {
        if (tile_cache_.contains(key) || pending_tiles_.contains(key))
        {
            for (TileFetchRequest& queuedRequest : queued_tile_requests_)
            {
                if (queuedRequest.key == key && queuedRequest.generation == tile_request_generation_)
                {
                    queuedRequest.priority = std::min(queuedRequest.priority, priority);
                    return;
                }
            }
            return;
        }

        pending_tiles_.insert(key);
        queued_tile_requests_.append({key, request, tile_request_generation_, priority});
    }

    void pruneQueuedTileRequests()
    {
        QVector<TileFetchRequest> retainedRequests;
        retainedRequests.reserve(queued_tile_requests_.size());
        for (const TileFetchRequest& request : std::as_const(queued_tile_requests_))
        {
            if (request.generation == tile_request_generation_ &&
                current_requested_tile_keys_.contains(request.key) &&
                pending_tiles_.contains(request.key))
            {
                retainedRequests.append(request);
                continue;
            }
            pending_tiles_.remove(request.key);
        }
        queued_tile_requests_ = std::move(retainedRequests);
    }

    void processTileRequestQueue()
    {
        while (active_tile_request_count_ < kMaxConcurrentTileRequests && !queued_tile_requests_.isEmpty())
        {
            int bestIndex = 0;
            for (int index = 1; index < queued_tile_requests_.size(); ++index)
            {
                if (queued_tile_requests_.at(index).priority < queued_tile_requests_.at(bestIndex).priority)
                {
                    bestIndex = index;
                }
            }

            const TileFetchRequest tileRequest = queued_tile_requests_.takeAt(bestIndex);
            if (tileRequest.generation != tile_request_generation_ ||
                !current_requested_tile_keys_.contains(tileRequest.key) ||
                !pending_tiles_.contains(tileRequest.key))
            {
                pending_tiles_.remove(tileRequest.key);
                continue;
            }

            ++active_tile_request_count_;
            QNetworkReply *reply = manager_->get(tileRequest.request);
            active_tile_replies_.insert(reply);
            active_tile_reply_keys_.insert(reply, tileRequest.key);
            QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, key = tileRequest.key, generation = tileRequest.generation]() {
                const bool wasActive = active_tile_replies_.contains(reply);
                active_tile_replies_.remove(reply);
                active_tile_reply_keys_.remove(reply);
                reply->deleteLater();
                if (wasActive)
                {
                    active_tile_request_count_ = std::max(0, active_tile_request_count_ - 1);
                }
                if (generation != tile_request_generation_)
                {
                    processTileRequestQueue();
                    return;
                }

                pending_tiles_.remove(key);
                const bool stillRequested = current_requested_tile_keys_.contains(key);
                if (!reply->error())
                {
                    QPixmap tile;
                    tile.loadFromData(reply->readAll());
                    if (!tile.isNull())
                    {
                        failed_tiles_.remove(key);
                        tile_cache_.insert(key, tile);
                    }
                    else
                    {
                        if (stillRequested)
                        {
                            failed_tiles_.insert(key);
                            last_tile_error_ = is_english_
                                ? QStringLiteral("The tile response could not be decoded as an image.")
                                : QStringLiteral("收到的瓦片响应无法解码为图像。");
                        }
                    }
                }
                else
                {
                    if (stillRequested)
                    {
                        failed_tiles_.insert(key);
                        last_tile_error_ = reply->errorString();
                    }
                }

                scheduleLoadFeedbackUpdate(stillRequested);
                processTileRequestQueue();
            });
        }
    }

    void scheduleVisibleTileRequest(int delayMs)
    {
        if (delayMs <= 0)
        {
            visible_tile_request_scheduled_ = false;
            requestVisibleTiles();
            return;
        }
        if (visible_tile_request_scheduled_)
        {
            return;
        }

        visible_tile_request_scheduled_ = true;
        QTimer::singleShot(delayMs, this, [this]() {
            visible_tile_request_scheduled_ = false;
            requestVisibleTiles();
        });
    }

    void scheduleLoadFeedbackUpdate(bool repaint)
    {
        repaint_update_requested_ = repaint_update_requested_ || repaint;
        if (feedback_update_scheduled_)
        {
            return;
        }

        feedback_update_scheduled_ = true;
        QTimer::singleShot(0, this, [this]() {
            feedback_update_scheduled_ = false;
            updateLoadFeedback();
            if (repaint_update_requested_)
            {
                repaint_update_requested_ = false;
                update();
            }
        });
    }

    void refreshViewport()
    {
        if (track_points_.isEmpty())
        {
            zoom_ = std::min(kDefaultZoom, kMaxDisplayZoom);
            center_world_pixel_ = QPointF();
            fit_zoom_ = zoom_;
            fit_center_world_pixel_ = center_world_pixel_;
            current_visible_tile_keys_.clear();
            updateLoadFeedback();
            return;
        }

        const QSizeF mapSize = mapViewportRect().size();
        const double availableWidth = std::max(200.0, mapSize.width());
        const double availableHeight = std::max(160.0, mapSize.height());

        if (track_points_.size() == 1)
        {
            fit_zoom_ = std::min(kDefaultZoom, providerMaxZoom(tile_provider_));
            fit_center_world_pixel_ = latLonToPixel(track_points_.first().latitude, track_points_.first().longitude, fit_zoom_);
            if (!manual_view_active_)
            {
                zoom_ = fit_zoom_;
                center_world_pixel_ = fit_center_world_pixel_;
            }
            requestVisibleTiles();
            return;
        }

        for (int candidateZoom = providerMaxZoom(tile_provider_); candidateZoom >= kMinZoom; --candidateZoom)
        {
            double minX = std::numeric_limits<double>::infinity();
            double maxX = -std::numeric_limits<double>::infinity();
            double minY = std::numeric_limits<double>::infinity();
            double maxY = -std::numeric_limits<double>::infinity();
            for (const RtkTrackPoint& point : track_points_)
            {
                const QPointF pixel = latLonToPixel(point.latitude, point.longitude, candidateZoom);
                minX = std::min(minX, pixel.x());
                maxX = std::max(maxX, pixel.x());
                minY = std::min(minY, pixel.y());
                maxY = std::max(maxY, pixel.y());
            }

            if ((maxX - minX) <= availableWidth * 0.8 && (maxY - minY) <= availableHeight * 0.8)
            {
                fit_zoom_ = candidateZoom;
                fit_center_world_pixel_ = QPointF((minX + maxX) * 0.5, (minY + maxY) * 0.5);
                if (!manual_view_active_)
                {
                    zoom_ = fit_zoom_;
                    center_world_pixel_ = fit_center_world_pixel_;
                }
                requestVisibleTiles();
                return;
            }
        }

        fit_zoom_ = kMinZoom;
        double minX = std::numeric_limits<double>::infinity();
        double maxX = -std::numeric_limits<double>::infinity();
        double minY = std::numeric_limits<double>::infinity();
        double maxY = -std::numeric_limits<double>::infinity();
        for (const RtkTrackPoint& point : track_points_)
        {
            const QPointF pixel = latLonToPixel(point.latitude, point.longitude, fit_zoom_);
            minX = std::min(minX, pixel.x());
            maxX = std::max(maxX, pixel.x());
            minY = std::min(minY, pixel.y());
            maxY = std::max(maxY, pixel.y());
        }
        fit_center_world_pixel_ = QPointF((minX + maxX) * 0.5, (minY + maxY) * 0.5);
        if (!manual_view_active_)
        {
            zoom_ = fit_zoom_;
            center_world_pixel_ = fit_center_world_pixel_;
        }
        requestVisibleTiles();
    }

    QRectF mapViewportRect() const
    {
        return rect();
    }

    int tileZoom() const
    {
        return std::min(zoom_, providerMaxZoom(tile_provider_));
    }

    QPointF centerWorldPixelForZoom(int targetZoom) const
    {
        if (targetZoom == zoom_)
        {
            return center_world_pixel_;
        }

        return latLonToPixel(
            pixelToLatitude(center_world_pixel_, zoom_),
            pixelToLongitude(center_world_pixel_, zoom_),
            targetZoom);
    }

    TileRange tileRangeForZoom(int targetZoom, int expansionTiles) const
    {
        const int clampedTargetZoom = std::clamp(targetZoom, kMinZoom, providerMaxZoom(tile_provider_));
        const QRectF mapRect = mapViewportRect();
        const double targetScale = std::pow(2.0, clampedTargetZoom - zoom_);
        const double width = std::max(1.0, mapRect.width() * targetScale);
        const double height = std::max(1.0, mapRect.height() * targetScale);
        const QPointF center = centerWorldPixelForZoom(clampedTargetZoom);
        const QPointF topLeft = center - QPointF(width * 0.5, height * 0.5);
        const QPointF bottomRight = center + QPointF(width * 0.5, height * 0.5);
        const int tileCount = 1 << clampedTargetZoom;

        return {
            clampedTargetZoom,
            std::max(0, static_cast<int>(std::floor(topLeft.x() / kTileSize)) - expansionTiles),
            std::min(tileCount - 1, static_cast<int>(std::floor(bottomRight.x() / kTileSize)) + expansionTiles),
            std::max(0, static_cast<int>(std::floor(topLeft.y() / kTileSize)) - expansionTiles),
            std::min(tileCount - 1, static_cast<int>(std::floor(bottomRight.y() / kTileSize)) + expansionTiles)
        };
    }

    QString tileCacheKey(const TileLayerSpec& layer, int zoom, int tileX, int tileY) const
    {
        return QStringLiteral("%1:%2:%3")
            .arg(tileProviderKey(tile_provider_), layer.cache_suffix, tileKey(zoom, tileX, tileY));
    }

    QNetworkRequest createTileRequest(const TileLayerSpec& layer, int zoom, int tileX, int tileY) const
    {
        QUrl tileUrl;
        if (tile_provider_ == TileProvider::OpenStreetMap)
        {
            tileUrl = QUrl(QStringLiteral("https://tile.openstreetmap.org/%1/%2/%3.png").arg(zoom).arg(tileX).arg(tileY));
        }
        else
        {
            tileUrl = QUrl(QStringLiteral("https://%1/%2/wmts")
                .arg(tiandituHostForTile(tileX, tileY, layer.cache_suffix), layer.endpoint_path));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("SERVICE"), QStringLiteral("WMTS"));
            query.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("GetTile"));
            query.addQueryItem(QStringLiteral("VERSION"), QStringLiteral("1.0.0"));
            query.addQueryItem(QStringLiteral("LAYER"), layer.layer);
            query.addQueryItem(QStringLiteral("STYLE"), QStringLiteral("default"));
            query.addQueryItem(QStringLiteral("TILEMATRIXSET"), QStringLiteral("w"));
            query.addQueryItem(QStringLiteral("FORMAT"), layer.format);
            query.addQueryItem(QStringLiteral("TILEMATRIX"), QString::number(zoom));
            query.addQueryItem(QStringLiteral("TILEROW"), QString::number(tileY));
            query.addQueryItem(QStringLiteral("TILECOL"), QString::number(tileX));
            query.addQueryItem(QStringLiteral("tk"), tianditu_key_.trimmed());
            tileUrl.setQuery(query);
        }

        QNetworkRequest request(tileUrl);
        request.setTransferTimeout(kTileRequestTimeout);
        if (isTianDiTuProvider(tile_provider_))
        {
            request.setRawHeader(
                "User-Agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36");
        }
        else
        {
            request.setRawHeader("User-Agent", "VaporView/1.0");
        }
        return request;
    }

    void enqueueTileRange(const TileRange& range,
                          int priority,
                          QSet<QString>& requestedKeys,
                          QSet<QString> *visibleKeys = nullptr)
    {
        const QVector<TileLayerSpec> layers = tileLayerSpecs(tile_provider_);
        for (int tileX = range.min_x; tileX <= range.max_x; ++tileX)
        {
            for (int tileY = range.min_y; tileY <= range.max_y; ++tileY)
            {
                for (const TileLayerSpec& layer : layers)
                {
                    const QString key = tileCacheKey(layer, range.zoom, tileX, tileY);
                    requestedKeys.insert(key);
                    if (visibleKeys)
                    {
                        visibleKeys->insert(key);
                    }
                    if (tile_cache_.contains(key))
                    {
                        continue;
                    }

                    enqueueTileRequest(key, createTileRequest(layer, range.zoom, tileX, tileY), priority);
                }
            }
        }
    }

    void requestVisibleTiles()
    {
        if (track_points_.isEmpty())
        {
            current_visible_tile_keys_.clear();
            current_requested_tile_keys_.clear();
            updateLoadFeedback();
            return;
        }
        if (isTianDiTuProvider(tile_provider_) && tianditu_key_.trimmed().isEmpty())
        {
            current_visible_tile_keys_.clear();
            current_requested_tile_keys_.clear();
            updateLoadFeedback();
            return;
        }

        QSet<QString> visibleKeys;
        QSet<QString> requestedKeys;
        const int currentTileZoom = tileZoom();
        enqueueTileRange(tileRangeForZoom(currentTileZoom, 0), kTileCurrentPriority, requestedKeys, &visibleKeys);
        enqueueTileRange(tileRangeForZoom(currentTileZoom, kTilePanPrefetchExpansion), kTilePrefetchPriority, requestedKeys);
        if (currentTileZoom > kMinZoom)
        {
            enqueueTileRange(tileRangeForZoom(currentTileZoom - 1, 1), kTileAdjacentZoomPriority, requestedKeys);
        }
        if (currentTileZoom < providerMaxZoom(tile_provider_))
        {
            enqueueTileRange(tileRangeForZoom(currentTileZoom + 1, 0), kTileAdjacentZoomPriority, requestedKeys);
        }

        current_visible_tile_keys_ = std::move(visibleKeys);
        current_requested_tile_keys_ = std::move(requestedKeys);
        pruneQueuedTileRequests();
        abortStaleActiveTileReplies();
        processTileRequestQueue();
        failed_tiles_ = failed_tiles_.intersect(current_visible_tile_keys_);
        updateLoadFeedback();
    }

    void drawTiles(QPainter& painter, const QRectF& mapRect)
    {
        const int currentTileZoom = tileZoom();
        const double tileScale = std::pow(2.0, zoom_ - currentTileZoom);
        const double tileScreenSize = kTileSize * tileScale;
        const QPointF tileCenter = centerWorldPixelForZoom(currentTileZoom);
        const QPointF topLeft = tileCenter - QPointF(mapRect.width() * 0.5 / tileScale, mapRect.height() * 0.5 / tileScale);
        const QPointF bottomRight = tileCenter + QPointF(mapRect.width() * 0.5 / tileScale, mapRect.height() * 0.5 / tileScale);
        const int tileCount = 1 << currentTileZoom;

        const int minTileX = std::max(0, static_cast<int>(std::floor(topLeft.x() / kTileSize)));
        const int maxTileX = std::min(tileCount - 1, static_cast<int>(std::floor(bottomRight.x() / kTileSize)));
        const int minTileY = std::max(0, static_cast<int>(std::floor(topLeft.y() / kTileSize)));
        const int maxTileY = std::min(tileCount - 1, static_cast<int>(std::floor(bottomRight.y() / kTileSize)));
        const QVector<TileLayerSpec> layers = tileLayerSpecs(tile_provider_);

        for (int tileX = minTileX; tileX <= maxTileX; ++tileX)
        {
            for (int tileY = minTileY; tileY <= maxTileY; ++tileY)
            {
                const QRectF tileRect(
                    mapRect.left() + (tileX * kTileSize - topLeft.x()) * tileScale,
                    mapRect.top() + (tileY * kTileSize - topLeft.y()) * tileScale,
                    tileScreenSize,
                    tileScreenSize);
                painter.fillRect(tileRect, appThemeColor(AppThemeColor::MapTileBackground, false));
                for (const TileLayerSpec& layer : layers)
                {
                    drawTileLayer(painter, layer, currentTileZoom, tileX, tileY, tileRect);
                }
            }
        }
    }

    bool drawTileLayer(QPainter& painter,
                       const TileLayerSpec& layer,
                       int zoom,
                       int tileX,
                       int tileY,
                       const QRectF& tileRect)
    {
        const QString key = tileCacheKey(layer, zoom, tileX, tileY);
        const auto tile = tile_cache_.constFind(key);
        if (tile != tile_cache_.constEnd())
        {
            painter.drawPixmap(tileRect, *tile, QRectF(0, 0, tile->width(), tile->height()));
            return true;
        }

        if (drawParentTileFallback(painter, layer, zoom, tileX, tileY, tileRect))
        {
            return true;
        }
        return drawChildTileFallback(painter, layer, zoom, tileX, tileY, tileRect);
    }

    bool drawParentTileFallback(QPainter& painter,
                                const TileLayerSpec& layer,
                                int zoom,
                                int tileX,
                                int tileY,
                                const QRectF& tileRect)
    {
        if (zoom <= kMinZoom)
        {
            return false;
        }

        const int parentZoom = zoom - 1;
        const int parentTileX = tileX / 2;
        const int parentTileY = tileY / 2;
        const QString key = tileCacheKey(layer, parentZoom, parentTileX, parentTileY);
        const auto parentTile = tile_cache_.constFind(key);
        if (parentTile == tile_cache_.constEnd())
        {
            return false;
        }

        const double sourceWidth = parentTile->width() * 0.5;
        const double sourceHeight = parentTile->height() * 0.5;
        const QRectF sourceRect(
            (tileX % 2) * sourceWidth,
            (tileY % 2) * sourceHeight,
            sourceWidth,
            sourceHeight);
        painter.drawPixmap(tileRect, *parentTile, sourceRect);
        return true;
    }

    bool drawChildTileFallback(QPainter& painter,
                               const TileLayerSpec& layer,
                               int zoom,
                               int tileX,
                               int tileY,
                               const QRectF& tileRect)
    {
        const int childZoom = zoom + 1;
        if (childZoom > providerMaxZoom(tile_provider_))
        {
            return false;
        }

        bool drewChildTile = false;
        const int childTileCount = 1 << childZoom;
        const double destWidth = tileRect.width() * 0.5;
        const double destHeight = tileRect.height() * 0.5;
        for (int dx = 0; dx < 2; ++dx)
        {
            for (int dy = 0; dy < 2; ++dy)
            {
                const int childTileX = tileX * 2 + dx;
                const int childTileY = tileY * 2 + dy;
                if (childTileX >= childTileCount || childTileY >= childTileCount)
                {
                    continue;
                }

                const QString key = tileCacheKey(layer, childZoom, childTileX, childTileY);
                const auto childTile = tile_cache_.constFind(key);
                if (childTile == tile_cache_.constEnd())
                {
                    continue;
                }

                const QRectF destRect(
                    tileRect.left() + dx * destWidth,
                    tileRect.top() + dy * destHeight,
                    destWidth,
                    destHeight);
                painter.drawPixmap(destRect, *childTile, QRectF(0, 0, childTile->width(), childTile->height()));
                drewChildTile = true;
            }
        }
        return drewChildTile;
    }

    QPointF worldToScreen(const QPointF& worldPixel, const QRectF& mapRect) const
    {
        const QPointF topLeft = center_world_pixel_ - QPointF(mapRect.width() * 0.5, mapRect.height() * 0.5);
        return QPointF(
            mapRect.left() + (worldPixel.x() - topLeft.x()),
            mapRect.top() + (worldPixel.y() - topLeft.y()));
    }

    QPointF cachedWorldToScreen(const QPointF& cachedWorldPixel, const QRectF& mapRect) const
    {
        const double scale = std::pow(2.0, zoom_ - kTrackWorldCacheZoom);
        return worldToScreen(cachedWorldPixel * scale, mapRect);
    }

    qint64 trackCellKey(int cellX, int cellY) const
    {
        return (static_cast<qint64>(static_cast<quint32>(cellX)) << 32) |
               static_cast<quint32>(cellY);
    }

    qint64 screenBucketKey(int bucketX, int bucketY) const
    {
        return (static_cast<qint64>(static_cast<quint32>(bucketX)) << 32) |
               static_cast<quint32>(bucketY);
    }

    QRectF visibleWorldRect(const QRectF& mapRect, double marginPixels) const
    {
        const double scale = std::pow(2.0, kTrackWorldCacheZoom - zoom_);
        const QPointF topLeft = center_world_pixel_ - QPointF(mapRect.width() * 0.5, mapRect.height() * 0.5);
        return QRectF(
            (topLeft.x() - marginPixels) * scale,
            (topLeft.y() - marginPixels) * scale,
            (mapRect.width() + marginPixels * 2.0) * scale,
            (mapRect.height() + marginPixels * 2.0) * scale);
    }

    bool isForcedTrackPoint(int index) const
    {
        return index == 0 ||
               index == track_points_.size() - 1 ||
               index == hovered_track_index_ ||
               index == selected_track_index_;
    }

    bool appendScreenPointIfSeparated(QVector<ScreenTrackPoint>& points,
                                      const ScreenTrackPoint& candidate,
                                      double minDistanceSquared) const
    {
        if (points.isEmpty() || isForcedTrackPoint(candidate.index))
        {
            points.push_back(candidate);
            return true;
        }

        const QPointF delta = candidate.screen - points.constLast().screen;
        if (delta.x() * delta.x() + delta.y() * delta.y() >= minDistanceSquared)
        {
            points.push_back(candidate);
            return true;
        }
        return false;
    }

    bool isVisibleTrackSegment(int firstIndex, int secondIndex, const QRectF& visibleWorld) const
    {
        if (firstIndex < 0 ||
            secondIndex < 0 ||
            firstIndex >= projected_track_points_.size() ||
            secondIndex >= projected_track_points_.size())
        {
            return false;
        }

        const QPointF& firstWorld = projected_track_points_.at(firstIndex).world_pixel;
        const QPointF& secondWorld = projected_track_points_.at(secondIndex).world_pixel;
        const QRectF segmentWorldRect(
            std::min(firstWorld.x(), secondWorld.x()),
            std::min(firstWorld.y(), secondWorld.y()),
            std::abs(firstWorld.x() - secondWorld.x()),
            std::abs(firstWorld.y() - secondWorld.y()));
        return visibleWorld.intersects(segmentWorldRect.adjusted(-1.0, -1.0, 1.0, 1.0));
    }

    QColor segmentColorForPoints(int firstIndex, int secondIndex) const
    {
        QColor segmentColor = defaultTrackColor();
        if (!has_peak_range_ ||
            firstIndex < 0 ||
            secondIndex < 0 ||
            firstIndex >= track_points_.size() ||
            secondIndex >= track_points_.size())
        {
            return segmentColor;
        }

        const RtkTrackPoint& firstPoint = track_points_.at(firstIndex);
        const RtkTrackPoint& secondPoint = track_points_.at(secondIndex);
        if (firstPoint.has_peak_value && secondPoint.has_peak_value)
        {
            return trackHeatColor((firstPoint.peak_value + secondPoint.peak_value) * 0.5f,
                min_peak_,
                max_peak_,
                heat_palette_);
        }
        if (firstPoint.has_peak_value)
        {
            return trackHeatColor(firstPoint.peak_value, min_peak_, max_peak_, heat_palette_);
        }
        if (secondPoint.has_peak_value)
        {
            return trackHeatColor(secondPoint.peak_value, min_peak_, max_peak_, heat_palette_);
        }
        return segmentColor;
    }

    QColor pointColorForIndex(int index) const
    {
        if (index < 0 || index >= track_points_.size())
        {
            return defaultTrackColor();
        }

        const RtkTrackPoint& point = track_points_.at(index);
        if (has_peak_range_ && point.has_peak_value)
        {
            return trackHeatColor(point.peak_value, min_peak_, max_peak_, heat_palette_);
        }
        return defaultTrackColor();
    }

    QVector<int> visibleTrackCandidateIndices(const QRectF& mapRect) const
    {
        QVector<int> indices;
        if (projected_track_points_.isEmpty())
        {
            return indices;
        }

        const QRectF visibleWorld = visibleWorldRect(mapRect, std::max(48.0, point_radius_ * 4.0));
        const int minCellX = static_cast<int>(std::floor(visibleWorld.left() / kTrackSpatialCellSize));
        const int maxCellX = static_cast<int>(std::floor(visibleWorld.right() / kTrackSpatialCellSize));
        const int minCellY = static_cast<int>(std::floor(visibleWorld.top() / kTrackSpatialCellSize));
        const int maxCellY = static_cast<int>(std::floor(visibleWorld.bottom() / kTrackSpatialCellSize));
        const qint64 cellScanCount = static_cast<qint64>(maxCellX - minCellX + 1) *
                                     static_cast<qint64>(maxCellY - minCellY + 1);

        if (cellScanCount > kTrackMaxCellScanCount ||
            cellScanCount > static_cast<qint64>(track_spatial_index_.size()) * 4)
        {
            for (int index = 0; index < projected_track_points_.size(); ++index)
            {
                const QPointF& world = projected_track_points_.at(index).world_pixel;
                if (!visibleWorld.contains(world))
                {
                    continue;
                }
                if (index > 0)
                {
                    indices.push_back(index - 1);
                }
                indices.push_back(index);
                if (index + 1 < projected_track_points_.size())
                {
                    indices.push_back(index + 1);
                }
            }
        }
        else
        {
            for (int cellX = minCellX; cellX <= maxCellX; ++cellX)
            {
                for (int cellY = minCellY; cellY <= maxCellY; ++cellY)
                {
                    const auto cell = track_spatial_index_.constFind(trackCellKey(cellX, cellY));
                    if (cell == track_spatial_index_.constEnd())
                    {
                        continue;
                    }
                    for (int index : cell.value())
                    {
                        const QPointF& world = projected_track_points_.at(index).world_pixel;
                        if (visibleWorld.contains(world))
                        {
                            if (index > 0)
                            {
                                indices.push_back(index - 1);
                            }
                            indices.push_back(index);
                            if (index + 1 < projected_track_points_.size())
                            {
                                indices.push_back(index + 1);
                            }
                        }
                    }
                }
            }
        }

        if (!track_points_.isEmpty())
        {
            indices.push_back(0);
            indices.push_back(track_points_.size() - 1);
            if (hovered_track_index_ >= 0)
            {
                indices.push_back(hovered_track_index_);
            }
            if (selected_track_index_ >= 0)
            {
                indices.push_back(selected_track_index_);
            }
        }

        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        return indices;
    }

    QVector<ScreenTrackSegment> buildVisibleTrackSegments(const QVector<int>& visibleIndices,
                                                         const QRectF& mapRect) const
    {
        QVector<ScreenTrackSegment> segments;
        if (visibleIndices.size() < 2)
        {
            return segments;
        }

        const QRectF visibleWorld = visibleWorldRect(mapRect, std::max(72.0, track_width_ * 6.0));
        for (int index : visibleIndices)
        {
            const int nextIndex = index + 1;
            if (nextIndex >= projected_track_points_.size())
            {
                continue;
            }
            if (!isVisibleTrackSegment(index, nextIndex, visibleWorld))
            {
                continue;
            }

            const QPointF firstScreen = cachedWorldToScreen(projected_track_points_.at(index).world_pixel, mapRect);
            const QPointF secondScreen = cachedWorldToScreen(projected_track_points_.at(nextIndex).world_pixel, mapRect);
            segments.push_back({index, nextIndex, firstScreen, secondScreen});
        }

        return segments;
    }

    TrackRenderContext buildTrackRenderContext(const QVector<int>& visibleIndices,
                                               const QRectF& mapRect) const
    {
        TrackRenderContext context;
        if (!show_track_points_)
        {
            return context;
        }

        context.point_points.reserve(std::min<int>(visibleIndices.size(), 12000));

        const double pointMinStep = std::max(point_radius_ * 2.2, kTrackPointMinPixelStep);
        const double pointMinDistanceSquared = pointMinStep * pointMinStep;

        for (int index : visibleIndices)
        {
            if (index < 0 || index >= projected_track_points_.size())
            {
                continue;
            }
            const QPointF screen = cachedWorldToScreen(projected_track_points_.at(index).world_pixel, mapRect);
            if (!mapRect.adjusted(-64.0, -64.0, 64.0, 64.0).contains(screen))
            {
                if (!isForcedTrackPoint(index))
                {
                    continue;
                }
            }

            const ScreenTrackPoint screenPoint{index, screen};
            appendScreenPointIfSeparated(context.point_points, screenPoint, pointMinDistanceSquared);
        }

        for (const ScreenTrackPoint& screenPoint : std::as_const(context.point_points))
        {
            const int bucketX = static_cast<int>(std::floor(screenPoint.screen.x() / kTrackBucketSize));
            const int bucketY = static_cast<int>(std::floor(screenPoint.screen.y() / kTrackBucketSize));
            QVector<int>& bucket = context.hit_buckets[screenBucketKey(bucketX, bucketY)];
            if (!bucket.contains(screenPoint.index))
            {
                bucket.push_back(screenPoint.index);
            }
        }

        return context;
    }

    void drawTrack(QPainter& painter, const QRectF& mapRect)
    {
        if (!show_route_ && !show_track_points_)
        {
            last_render_context_ = TrackRenderContext();
            painter.setPen(QPen(appThemeColor(AppThemeColor::MapBoundary, false), 1));
            painter.drawRoundedRect(mapRect, 8.0, 8.0);
            return;
        }

        const QVector<int> visibleIndices = visibleTrackCandidateIndices(mapRect);
        last_render_context_ = buildTrackRenderContext(visibleIndices, mapRect);
        const QVector<ScreenTrackSegment> lineSegments = show_route_
            ? buildVisibleTrackSegments(visibleIndices, mapRect)
            : QVector<ScreenTrackSegment>();

        painter.save();
        painter.setClipRect(mapRect, Qt::IntersectClip);
        if (!lineSegments.isEmpty())
        {
            for (const ScreenTrackSegment& segment : lineSegments)
            {
                const QColor segmentColor = segmentColorForPoints(segment.first_index, segment.second_index);
                painter.setPen(QPen(segmentColor, track_width_, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawLine(segment.first_screen, segment.second_screen);
            }
        }

        if (show_track_points_ && !last_render_context_.point_points.isEmpty())
        {
            for (const ScreenTrackPoint& screenPoint : std::as_const(last_render_context_.point_points))
            {
                if (screenPoint.index == selected_track_index_ || screenPoint.index == hovered_track_index_)
                {
                    continue;
                }
                painter.setPen(QPen(appThemeColor(AppThemeColor::MapViewport, false), 1.5));
                painter.setBrush(pointColorForIndex(screenPoint.index));
                painter.drawEllipse(screenPoint.screen, point_radius_, point_radius_);
            }

            painter.setPen(Qt::NoPen);
            painter.setBrush(appThemeColor(AppThemeColor::TrackStart, false));
            const double endpointRadius = std::max(point_radius_ + 1.0, 5.0);
            painter.drawEllipse(cachedWorldToScreen(projected_track_points_.first().world_pixel, mapRect), endpointRadius, endpointRadius);
            painter.setBrush(appThemeColor(AppThemeColor::TrackEnd, false));
            painter.drawEllipse(cachedWorldToScreen(projected_track_points_.last().world_pixel, mapRect), endpointRadius, endpointRadius);
        }
        if (show_track_points_ && hovered_track_index_ >= 0 && hovered_track_index_ < projected_track_points_.size())
        {
            const QPointF hoveredPoint = cachedWorldToScreen(projected_track_points_.at(hovered_track_index_).world_pixel, mapRect);
            const double hoverRadius = point_radius_ * 1.55;
            painter.setPen(QPen(appThemeColor(AppThemeColor::TextStrong, false), 2.0));
            painter.setBrush(pointColorForIndex(hovered_track_index_));
            painter.drawEllipse(hoveredPoint, hoverRadius, hoverRadius);
        }
        if (show_track_points_ && selected_track_index_ >= 0 && selected_track_index_ < projected_track_points_.size())
        {
            const QPointF selectedPoint = cachedWorldToScreen(projected_track_points_.at(selected_track_index_).world_pixel, mapRect);
            const double selectedRadius = std::max(point_radius_ * 1.85, 8.0);
            painter.setPen(QPen(appThemeColor(AppThemeColor::TrackEnd, false), 3.0));
            painter.setBrush(appThemeColor(AppThemeColor::MapViewport, false));
            painter.drawEllipse(selectedPoint, selectedRadius, selectedRadius);
            painter.setPen(Qt::NoPen);
            painter.setBrush(appThemeColor(AppThemeColor::TrackEnd, false));
            painter.drawEllipse(selectedPoint, std::max(point_radius_ * 0.75, 4.0), std::max(point_radius_ * 0.75, 4.0));
        }
        painter.restore();

        painter.setPen(QPen(appThemeColor(AppThemeColor::MapBoundary, false), 1));
        painter.drawRoundedRect(mapRect, 8.0, 8.0);
    }

    void drawMapFooter(QPainter& painter, const QRectF& mapRect) const
    {
        const QRectF footerRect(mapRect.left() + 10.0, mapRect.bottom() - 28.0, mapRect.width() - 20.0, 20.0);
        if (footerRect.width() <= 80.0)
        {
            return;
        }

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath footerPath;
        footerPath.addRoundedRect(footerRect, 6.0, 6.0);
        QColor footerColor = appThemeColor(AppThemeColor::SurfaceRaised, false);
        footerColor.setAlpha(220);
        painter.fillPath(footerPath, footerColor);

        QFont footerFont = painter.font();
        footerFont.setPointSize(8);
        painter.setFont(footerFont);
        painter.setPen(appThemeColor(AppThemeColor::MapText, false));
        painter.drawText(footerRect.adjusted(8, 0, -8, 0),
            Qt::AlignLeft | Qt::AlignVCenter,
            mapAttributionText(tile_provider_, is_english_));
        painter.drawText(footerRect.adjusted(8, 0, -8, 0),
            Qt::AlignRight | Qt::AlignVCenter,
            QString(is_english_ ? "%1 points: %2" : "%1点数: %2")
                .arg(is_english_ ? english_track_label_ : chinese_track_label_)
                .arg(track_points_.size()));
        painter.restore();
    }

    int closestTrackPointIndex(const QPointF& pos)
    {
        if (track_points_.isEmpty() || projected_track_points_.isEmpty() || !show_track_points_)
        {
            return -1;
        }

        const QRectF mapRect = mapViewportRect();
        if (!mapRect.adjusted(-8.0, -8.0, 8.0, 8.0).contains(pos))
        {
            return -1;
        }

        if (last_render_context_.hit_buckets.isEmpty())
        {
            const QVector<int> visibleIndices = visibleTrackCandidateIndices(mapRect);
            last_render_context_ = buildTrackRenderContext(visibleIndices, mapRect);
        }
        int bestIndex = -1;
        double bestDistanceSquared = std::numeric_limits<double>::infinity();
        const double pickRadius = std::max(18.0, point_radius_ * 2.4);
        const int bucketX = static_cast<int>(std::floor(pos.x() / kTrackBucketSize));
        const int bucketY = static_cast<int>(std::floor(pos.y() / kTrackBucketSize));
        QVector<int> candidates;
        for (int dx = -1; dx <= 1; ++dx)
        {
            for (int dy = -1; dy <= 1; ++dy)
            {
                const auto bucket = last_render_context_.hit_buckets.constFind(screenBucketKey(bucketX + dx, bucketY + dy));
                if (bucket != last_render_context_.hit_buckets.constEnd())
                {
                    candidates += bucket.value();
                }
            }
        }

        for (int index : std::as_const(candidates))
        {
            if (index < 0 || index >= projected_track_points_.size())
            {
                continue;
            }
            const QPointF screenPoint = cachedWorldToScreen(projected_track_points_.at(index).world_pixel, mapRect);
            const QPointF delta = screenPoint - pos;
            const double distanceSquared = delta.x() * delta.x() + delta.y() * delta.y();
            if (distanceSquared < bestDistanceSquared)
            {
                bestDistanceSquared = distanceSquared;
                bestIndex = index;
            }
        }

        return bestDistanceSquared <= pickRadius * pickRadius ? bestIndex : -1;
    }

    void updateHoveredTrackPoint(const QPointF& pos)
    {
        setHoveredTrackPoint(closestTrackPointIndex(pos));
    }

    void setHoveredTrackPoint(int index)
    {
        const int clamped = (index >= 0 && index < track_points_.size()) ? index : -1;
        if (hovered_track_index_ == clamped)
        {
            if (!dragging_)
            {
                setCursor(hovered_track_index_ >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            }
            return;
        }
        hovered_track_index_ = clamped;
        if (!dragging_)
        {
            setCursor(hovered_track_index_ >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
        update();
    }

    void adjustZoom(int delta)
    {
        if (track_points_.isEmpty() || delta == 0)
        {
            return;
        }

        const int newZoom = std::clamp(zoom_ + delta, kMinZoom, kMaxDisplayZoom);
        if (newZoom == zoom_)
        {
            return;
        }

        const QPointF centerLatLonPixelAtNewZoom = latLonToPixel(
            pixelToLatitude(center_world_pixel_, zoom_),
            pixelToLongitude(center_world_pixel_, zoom_),
            newZoom);
        zoom_ = newZoom;
        center_world_pixel_ = centerLatLonPixelAtNewZoom;
        manual_view_active_ = true;
        last_render_context_ = TrackRenderContext();
        scheduleVisibleTileRequest(kTileZoomRequestDebounceMs);
        update();
    }

    double pixelToLongitude(const QPointF& pixel, int zoom) const
    {
        const double worldSize = static_cast<double>(kTileSize) * std::pow(2.0, zoom);
        return pixel.x() / worldSize * 360.0 - 180.0;
    }

    double pixelToLatitude(const QPointF& pixel, int zoom) const
    {
        const double worldSize = static_cast<double>(kTileSize) * std::pow(2.0, zoom);
        const double mercatorY = M_PI * (1.0 - 2.0 * pixel.y() / worldSize);
        return qRadiansToDegrees(std::atan(std::sinh(mercatorY)));
    }

    void updateLoadFeedback()
    {
        const int totalVisible = current_visible_tile_keys_.size();
        int loaded = 0;
        int failed = 0;
        int pending = 0;
        for (const QString& key : current_visible_tile_keys_)
        {
            if (tile_cache_.contains(key))
            {
                ++loaded;
            }
            else if (pending_tiles_.contains(key))
            {
                ++pending;
            }
            else if (failed_tiles_.contains(key))
            {
                ++failed;
            }
        }

        if (progress_callback_)
        {
            progress_callback_(loaded, failed, totalVisible);
        }

        if (!status_callback_)
        {
            return;
        }

        QString statusText;
        if (track_points_.isEmpty())
        {
            statusText = is_english_
                ? QStringLiteral("No track data is available, so no base map tiles need to be loaded.")
                : QStringLiteral("当前没有轨迹数据，因此无需加载底图瓦片。");
        }
        else if (isTianDiTuProvider(tile_provider_) && tianditu_key_.trimmed().isEmpty())
        {
            statusText = is_english_
                ? QStringLiteral("Tianditu is selected but no key is configured. Use the key button in the title bar to add one.")
                : QStringLiteral("当前已选择天地图，但尚未配置 Key。请点击标题栏钥匙按钮添加 Key。");
        }
        else if (totalVisible <= 0)
        {
            statusText = is_english_
                ? QStringLiteral("Preparing visible map tiles...")
                : QStringLiteral("正在准备可见区域底图瓦片...");
        }
        else if (pending > 0)
        {
            statusText = QString(is_english_
                ? "Loading %1 tiles: %2/%3 loaded, %4 pending, %5 failed."
                : "正在加载%1底图：已加载 %2/%3，待完成 %4，失败 %5。")
                .arg(tile_provider_ == TileProvider::TianDiTuSatellite
                    ? (is_english_ ? QStringLiteral("Tianditu Satellite") : QStringLiteral("天地图卫星"))
                    : (tile_provider_ == TileProvider::TianDiTuVector
                        ? (is_english_ ? QStringLiteral("Tianditu Vector") : QStringLiteral("天地图矢量"))
                        : QStringLiteral("OSM")))
                .arg(loaded)
                .arg(totalVisible)
                .arg(pending)
                .arg(failed);
        }
        else if (failed > 0)
        {
            statusText = QString(is_english_
                ? "Base map finished with %1/%2 tiles loaded and %3 failures. Last error: %4"
                : "底图加载完成：成功 %1/%2，失败 %3。最近错误：%4")
                .arg(loaded)
                .arg(totalVisible)
                .arg(failed)
                .arg(last_tile_error_.isEmpty()
                    ? (is_english_ ? QStringLiteral("unknown") : QStringLiteral("未知"))
                    : last_tile_error_);
        }
        else
        {
            statusText = QString(is_english_
                ? "Base map loaded successfully: %1/%2 tiles ready."
                : "底图加载成功：%1/%2 个瓦片已就绪。")
                .arg(loaded)
                .arg(totalVisible);
        }

        status_callback_(statusText);
    }

    QNetworkAccessManager *manager_;
    QVector<RtkTrackPoint> track_points_;
    QVector<ProjectedTrackPoint> projected_track_points_;
    QHash<qint64, QVector<int>> track_spatial_index_;
    mutable TrackRenderContext last_render_context_;
    QHash<QString, QPixmap> tile_cache_;
    QSet<QString> pending_tiles_;
    QVector<TileFetchRequest> queued_tile_requests_;
    QSet<QNetworkReply*> active_tile_replies_;
    QHash<QNetworkReply*, QString> active_tile_reply_keys_;
    QSet<QString> current_visible_tile_keys_;
    QSet<QString> current_requested_tile_keys_;
    QSet<QString> failed_tiles_;
    bool is_english_;
    TileProvider tile_provider_;
    HeatPalette heat_palette_;
    QString tianditu_key_;
    QString last_tile_error_;
    int zoom_;
    QPointF center_world_pixel_;
    int fit_zoom_;
    QPointF fit_center_world_pixel_;
    bool manual_view_active_;
    bool dragging_;
    bool drag_moved_;
    QPointF drag_start_pos_;
    QPointF drag_start_center_world_pixel_;
    QPointF drag_current_delta_;
    QPixmap drag_frame_cache_;
    int selected_track_index_;
    int hovered_track_index_;
    double track_width_;
    double point_radius_;
    bool show_route_;
    bool show_track_points_;
    bool has_peak_range_;
    float min_peak_;
    float max_peak_;
    int peak_count_;
    int active_tile_request_count_;
    int tile_request_generation_;
    bool visible_tile_request_scheduled_;
    bool feedback_update_scheduled_;
    bool repaint_update_requested_;
    QString english_track_label_;
    QString chinese_track_label_;
    std::function<void(const QString&)> status_callback_;
    std::function<void(int, int, int)> progress_callback_;
    std::function<void(int)> selection_callback_;
};
}

TrajectoryViewerDialog::TrajectoryViewerDialog(QWidget *parent)
    : QDialog(parent)
    , summary_label_(new QLabel(this))
    , sidebar_title_label_(new QLabel(this))
    , sidebar_icon_label_(new QLabel(this))
    , detail_label_(new QLabel(this))
    , map_status_label_(new QLabel(this))
    , map_progress_bar_(new QProgressBar(this))
    , map_widget_(new TrajectoryMapWidget(this))
    , track_width_label_(new QLabel(this))
    , track_width_slider_(new QSlider(Qt::Horizontal, this))
    , point_size_label_(new QLabel(this))
    , point_size_slider_(new QSlider(Qt::Horizontal, this))
    , heat_palette_card_(new QFrame(this))
    , heat_palette_title_label_(new QLabel(this))
    , heat_gradient_bar_(new HeatGradientBarWidget(this))
    , heat_min_label_(new QLabel(this))
    , heat_mid_label_(new QLabel(this))
    , heat_max_label_(new QLabel(this))
    , heat_palette_button_(new QToolButton(this))
    , heat_palette_menu_(new QMenu(this))
    , map_tools_card_(new QFrame(this))
    , point_detail_card_(new QFrame(this))
    , point_detail_close_button_(new QToolButton(this))
    , show_route_button_(new QPushButton(this))
    , show_points_button_(new QPushButton(this))
    , export_button_(new QPushButton(this))
    , copy_point_button_(new QPushButton(this))
    , map_source_combo_(new QComboBox(this))
    , tianditu_key_edit_(new QLineEdit(this))
    , tianditu_key_button_(new QToolButton(this))
    , tianditu_key_menu_(new QMenu(this))
    , tianditu_key_menu_label_(new QLabel(this))
    , zoom_in_button_(new QToolButton(this))
    , zoom_out_button_(new QToolButton(this))
    , reset_view_button_(new QToolButton(this))
    , is_english_(false)
    , updating_theme_styles_(false)
    , english_track_label_(QStringLiteral("RTK trajectory"))
    , chinese_track_label_(QStringLiteral("RTK轨迹"))
    , track_points_()
    , track_stats_()
    , selected_track_index_(-1)
    , point_detail_visible_(false)
{
    setObjectName(QStringLiteral("trajectoryViewerDialog"));
    setWindowFlag(Qt::Window, true);
    setModal(false);
    resize(1080, 680);

    auto *dialogLayout = new QVBoxLayout(this);
    dialogLayout->setContentsMargins(12, 12, 12, 12);
    dialogLayout->setSpacing(8);

    auto *content = new QWidget(this);
    content->setObjectName(QStringLiteral("trajectoryViewerContent"));
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *mainLayout = new QHBoxLayout(content);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(12);
    dialogLayout->addWidget(content, 1);

    auto *sidebarCard = new QFrame(this);
    sidebarCard->setObjectName(QStringLiteral("trajectoryViewerSidebarCard"));
    sidebarCard->setFrameShape(QFrame::NoFrame);
    sidebarCard->setAttribute(Qt::WA_StyledBackground, true);
    sidebarCard->setFixedWidth(kTrajectorySidebarWidth);
    sidebarCard->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto *sidebarCardLayout = new QVBoxLayout(sidebarCard);
    sidebarCardLayout->setContentsMargins(1, 1, 1, 1);
    sidebarCardLayout->setSpacing(0);

    auto *sidebarTitleBar = new QWidget(sidebarCard);
    sidebarTitleBar->setObjectName(QStringLiteral("sectionTitleBar"));
    sidebarTitleBar->setFixedHeight(40);
    auto *sidebarTitleLayout = new QHBoxLayout(sidebarTitleBar);
    sidebarTitleLayout->setContentsMargins(12, 0, 12, 0);
    sidebarTitleLayout->setSpacing(8);
    sidebar_icon_label_->setObjectName(QStringLiteral("trajectorySidebarTitleIcon"));
    sidebar_icon_label_->setFixedSize(24, 24);
    sidebar_icon_label_->setAlignment(Qt::AlignCenter);
    sidebarTitleLayout->addWidget(sidebar_icon_label_, 0, Qt::AlignVCenter);
    sidebar_title_label_->setObjectName(QStringLiteral("sectionTitleLabel"));
    sidebar_title_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    sidebarTitleLayout->addWidget(sidebar_title_label_, 1, Qt::AlignVCenter);
    sidebarCardLayout->addWidget(sidebarTitleBar);

    auto *sidebar = new QScrollArea(sidebarCard);
    sidebar->setObjectName(QStringLiteral("trajectoryViewerSidebar"));
    sidebar->setWidgetResizable(true);
    sidebar->viewport()->setObjectName(QStringLiteral("trajectoryViewerSidebarViewport"));
    sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sidebar->setFrameShape(QFrame::NoFrame);
    sidebar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *sidebarContent = new QWidget(sidebar);
    sidebarContent->setObjectName(QStringLiteral("trajectoryViewerSidebarContent"));
    auto *sidebarLayout = new QVBoxLayout(sidebarContent);
    sidebarLayout->setContentsMargins(12, 12, 12, 12);
    sidebarLayout->setSpacing(8);
    sidebar->setWidget(sidebarContent);
    sidebarCardLayout->addWidget(sidebar, 1);

    auto *mapPanel = new QFrame(this);
    mapPanel->setObjectName(QStringLiteral("trajectoryViewerMapPanel"));
    mapPanel->setFrameShape(QFrame::NoFrame);
    mapPanel->setAttribute(Qt::WA_StyledBackground, true);
    mapPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *mapPanelLayout = new QVBoxLayout(mapPanel);
    mapPanelLayout->setContentsMargins(0, 0, 0, 0);
    mapPanelLayout->setSpacing(0);

    map_widget_->setObjectName(QStringLiteral("trajectoryViewerMap"));
    map_widget_->setMinimumHeight(kTrajectoryMapMinimumHeight);
    map_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    summary_label_->setWordWrap(true);
    summary_label_->setTextFormat(Qt::RichText);
    summary_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary_label_->setObjectName(QStringLiteral("trajectorySidebarSummaryLabel"));
    sidebarLayout->addWidget(summary_label_);
    detail_label_->setWordWrap(true);
    detail_label_->setTextFormat(Qt::RichText);
    detail_label_->setObjectName(QStringLiteral("trajectoryPointDetailLabel"));
    detail_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    map_status_label_->setWordWrap(true);
    map_status_label_->setObjectName(QStringLiteral("trajectorySidebarStatusLabel"));
    sidebarLayout->addWidget(map_status_label_);
    map_progress_bar_->setTextVisible(true);
    map_progress_bar_->setMinimum(0);
    map_progress_bar_->setMaximum(1);
    map_progress_bar_->setValue(0);
    sidebarLayout->addWidget(map_progress_bar_);

    auto *actionLayout = new QHBoxLayout();
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);
    track_width_label_->setObjectName(QStringLiteral("trajectoryControlLabel"));
    track_width_label_->setMinimumWidth(52);
    point_size_label_->setObjectName(QStringLiteral("trajectoryControlLabel"));
    point_size_label_->setMinimumWidth(52);
    for (auto *button : {show_route_button_, show_points_button_})
    {
        button->setObjectName(QStringLiteral("trajectoryVisibilityToggle"));
        button->setCheckable(true);
        button->setChecked(true);
        button->setFixedSize(24, 24);
        button->setIconSize(QSize(18, 18));
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setText(QString());
    }
    show_route_button_->setProperty("visibilityRole", QStringLiteral("route"));
    show_points_button_->setProperty("visibilityRole", QStringLiteral("points"));
    track_width_slider_->setObjectName(QStringLiteral("trajectoryTrackWidthSlider"));
    track_width_slider_->setRange(10, 80);
    track_width_slider_->setValue(static_cast<int>(std::lround(kDefaultTrackWidth * kTrackStyleSliderScale)));
    track_width_slider_->setTracking(true);
    track_width_slider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    point_size_slider_->setObjectName(QStringLiteral("trajectoryPointSizeSlider"));
    point_size_slider_->setRange(20, 120);
    point_size_slider_->setValue(static_cast<int>(std::lround(kDefaultTrackPointRadius * kTrackStyleSliderScale)));
    point_size_slider_->setTracking(true);
    point_size_slider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    export_button_->setEnabled(false);
    copy_point_button_->setEnabled(false);
    for (auto *button : {copy_point_button_, export_button_})
    {
        button->setObjectName(QStringLiteral("trajectorySidebarActionButton"));
        button->setFixedHeight(32);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    actionLayout->addWidget(copy_point_button_);
    actionLayout->addWidget(export_button_);
    sidebarLayout->addLayout(actionLayout);

    auto *mapWidget = static_cast<TrajectoryMapWidget*>(map_widget_);
    mapWidget->setStatusCallback([this](const QString& text) {
        map_status_label_->setText(text);
    });
    mapWidget->setProgressCallback([this](int loaded, int failed, int total) {
        map_progress_bar_->setMaximum(std::max(1, total));
        map_progress_bar_->setValue(std::min(total, loaded + failed));
        map_progress_bar_->setFormat(total > 0
            ? QStringLiteral("%1/%2").arg(std::min(total, loaded + failed)).arg(total)
            : QStringLiteral("--"));
    });
    mapWidget->setSelectionCallback([this](int index) {
        point_detail_visible_ = true;
        setSelectedTrackIndex(index, true);
    });

    map_source_combo_->setObjectName(QStringLiteral("trajectoryMapSourceCombo"));
    map_source_combo_->setMinimumWidth(160);
    map_source_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    map_source_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    map_source_combo_->setToolTip(is_english_ ? QStringLiteral("Map source") : QStringLiteral("底图来源"));

    heat_palette_button_->setObjectName(QStringLiteral("trajectoryHeatPaletteButton"));
    heat_palette_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    heat_palette_button_->setPopupMode(QToolButton::InstantPopup);
    heat_palette_button_->setAutoRaise(false);
    heat_palette_button_->setArrowType(Qt::NoArrow);
    heat_palette_button_->setFixedSize(28, 24);
    heat_palette_button_->setIconSize(QSize(18, 18));
    heat_palette_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    heat_palette_button_->setAccessibleName(is_english_ ? QStringLiteral("Heat palette") : QStringLiteral("热力图色带"));
    heat_palette_button_->setToolTip(is_english_
        ? QStringLiteral("Choose the peak heatmap color ramp.")
        : QStringLiteral("选择峰值热力图色带。"));
    heat_palette_menu_->setObjectName(QStringLiteral("trajectoryHeatPaletteMenu"));
    heat_palette_menu_->setAttribute(Qt::WA_TranslucentBackground, true);
    heat_palette_menu_->setWindowFlag(Qt::FramelessWindowHint, true);
    heat_palette_menu_->setWindowFlag(Qt::NoDropShadowWindowHint, true);
    heat_palette_button_->setMenu(heat_palette_menu_);

    heat_palette_card_->setObjectName(QStringLiteral("trajectoryHeatLegendCard"));
    heat_palette_card_->setAttribute(Qt::WA_StyledBackground, true);
    heat_palette_card_->setFixedWidth(390);
    heat_palette_card_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    heat_palette_title_label_->setObjectName(QStringLiteral("trajectoryHeatLegendTitle"));
    heat_palette_title_label_->setFixedWidth(34);
    for (auto *label : {heat_min_label_, heat_mid_label_, heat_max_label_})
    {
        label->setObjectName(QStringLiteral("trajectoryHeatLegendCaption"));
    }
    heat_min_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    heat_mid_label_->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);
    heat_max_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *heatCardLayout = new QVBoxLayout(heat_palette_card_);
    heatCardLayout->setContentsMargins(8, 5, 8, 5);
    heatCardLayout->setSpacing(3);
    auto *heatTopLayout = new QHBoxLayout();
    heatTopLayout->setContentsMargins(0, 0, 0, 0);
    heatTopLayout->setSpacing(6);
    heatTopLayout->addWidget(heat_palette_title_label_, 0);
    heatTopLayout->addWidget(heat_gradient_bar_, 1);
    heatTopLayout->addWidget(heat_palette_button_, 0);
    heatCardLayout->addLayout(heatTopLayout);
    auto *heatCaptionLayout = new QHBoxLayout();
    heatCaptionLayout->setContentsMargins(0, 0, 34, 0);
    heatCaptionLayout->setSpacing(6);
    heatCaptionLayout->addSpacing(40);
    heatCaptionLayout->addWidget(heat_min_label_, 1);
    heatCaptionLayout->addWidget(heat_mid_label_, 1);
    heatCaptionLayout->addWidget(heat_max_label_, 1);
    heatCardLayout->addLayout(heatCaptionLayout);

    map_tools_card_->setObjectName(QStringLiteral("trajectoryMapToolsCard"));
    map_tools_card_->setAttribute(Qt::WA_StyledBackground, true);
    map_tools_card_->setFixedWidth(280);
    map_tools_card_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *mapToolsLayout = new QVBoxLayout(map_tools_card_);
    mapToolsLayout->setContentsMargins(8, 6, 8, 6);
    mapToolsLayout->setSpacing(4);
    auto addMapToolRow = [mapToolsLayout](QLabel *label, QPushButton *button, QSlider *slider) {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        row->addWidget(label, 0);
        row->addWidget(button, 0);
        row->addWidget(slider, 1);
        mapToolsLayout->addLayout(row);
    };
    addMapToolRow(track_width_label_, show_route_button_, track_width_slider_);
    addMapToolRow(point_size_label_, show_points_button_, point_size_slider_);

    point_detail_card_->setObjectName(QStringLiteral("trajectoryPointDetailCard"));
    point_detail_card_->setAttribute(Qt::WA_StyledBackground, true);
    point_detail_card_->setFixedWidth(380);
    point_detail_card_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    point_detail_card_->hide();
    point_detail_close_button_->setObjectName(QStringLiteral("trajectoryPointDetailCloseButton"));
    point_detail_close_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    point_detail_close_button_->setAutoRaise(false);
    point_detail_close_button_->setFocusPolicy(Qt::NoFocus);
    point_detail_close_button_->setFixedSize(24, 24);
    point_detail_close_button_->setIconSize(QSize(16, 16));
    auto *pointDetailLayout = new QVBoxLayout(point_detail_card_);
    pointDetailLayout->setContentsMargins(10, 6, 10, 8);
    pointDetailLayout->setSpacing(2);
    auto *pointDetailHeaderLayout = new QHBoxLayout();
    pointDetailHeaderLayout->setContentsMargins(0, 0, 0, 0);
    pointDetailHeaderLayout->setSpacing(0);
    pointDetailHeaderLayout->addStretch(1);
    pointDetailHeaderLayout->addWidget(point_detail_close_button_);
    pointDetailLayout->addLayout(pointDetailHeaderLayout);
    pointDetailLayout->addWidget(detail_label_);

    tianditu_key_edit_->setObjectName(QStringLiteral("trajectoryTiandituKeyEdit"));
    tianditu_key_edit_->setFixedWidth(390);
    tianditu_key_edit_->setClearButtonEnabled(true);
    tianditu_key_edit_->setToolTip(is_english_ ? QStringLiteral("Tianditu tile key") : QStringLiteral("天地图瓦片 Key"));

    tianditu_key_menu_->setObjectName(QStringLiteral("trajectoryTiandituKeyMenu"));
    auto *keyMenuWidget = new QWidget(tianditu_key_menu_);
    auto *keyMenuLayout = new QVBoxLayout(keyMenuWidget);
    keyMenuLayout->setContentsMargins(10, 10, 10, 10);
    keyMenuLayout->setSpacing(6);
    tianditu_key_menu_label_->setObjectName(QStringLiteral("trajectoryTiandituKeyMenuLabel"));
    tianditu_key_menu_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    keyMenuLayout->addWidget(tianditu_key_menu_label_);
    tianditu_key_edit_->setParent(keyMenuWidget);
    keyMenuLayout->addWidget(tianditu_key_edit_);
    auto *keyWidgetAction = new QWidgetAction(tianditu_key_menu_);
    keyWidgetAction->setDefaultWidget(keyMenuWidget);
    tianditu_key_menu_->addAction(keyWidgetAction);
    connect(tianditu_key_menu_, &QMenu::aboutToShow, this, [this]() {
        QTimer::singleShot(0, this, [this]() {
            if (!tianditu_key_edit_)
            {
                return;
            }
            tianditu_key_edit_->setFocus(Qt::PopupFocusReason);
            tianditu_key_edit_->selectAll();
        });
    });

    auto configureTitleBarButton = [](QToolButton *button, const QString& objectName, int width) {
        button->setObjectName(objectName);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(false);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(width, 34);
        button->setIconSize(QSize(kTitleBarIconSize, kTitleBarIconSize));
    };
    configureTitleBarButton(tianditu_key_button_, QStringLiteral("titleBarButton"), kTitleBarButtonSize);
    configureTitleBarButton(zoom_in_button_, QStringLiteral("titleBarButton"), kTitleBarButtonSize);
    configureTitleBarButton(zoom_out_button_, QStringLiteral("titleBarButton"), kTitleBarButtonSize);
    configureTitleBarButton(reset_view_button_, QStringLiteral("titleBarButton"), kTitleBarButtonSize);
    tianditu_key_button_->setPopupMode(QToolButton::InstantPopup);
    tianditu_key_button_->setMenu(tianditu_key_menu_);

    auto *mapControlsLayout = new QHBoxLayout();
    mapControlsLayout->setContentsMargins(0, 0, 0, 0);
    mapControlsLayout->setSpacing(6);
    mapControlsLayout->addWidget(map_source_combo_, 1);
    mapControlsLayout->addWidget(tianditu_key_button_, 0);
    mapControlsLayout->addWidget(zoom_in_button_, 0);
    mapControlsLayout->addWidget(zoom_out_button_, 0);
    mapControlsLayout->addWidget(reset_view_button_, 0);
    sidebarLayout->addLayout(mapControlsLayout);
    sidebarLayout->addStretch(1);

    auto *mapOverlayLayout = new QVBoxLayout(map_widget_);
    mapOverlayLayout->setContentsMargins(14, 14, 14, 14);
    mapOverlayLayout->setSpacing(0);
    auto *mapTopOverlayLayout = new QHBoxLayout();
    mapTopOverlayLayout->setContentsMargins(0, 0, 0, 0);
    mapTopOverlayLayout->setSpacing(10);
    mapTopOverlayLayout->addWidget(heat_palette_card_, 0, Qt::AlignTop);
    mapTopOverlayLayout->addWidget(map_tools_card_, 0, Qt::AlignTop);
    mapTopOverlayLayout->addStretch(1);
    mapOverlayLayout->addLayout(mapTopOverlayLayout);
    mapOverlayLayout->addStretch(1);
    auto *mapBottomOverlayLayout = new QHBoxLayout();
    mapBottomOverlayLayout->setContentsMargins(0, 0, 0, 0);
    mapBottomOverlayLayout->setSpacing(0);
    mapBottomOverlayLayout->addWidget(point_detail_card_, 0, Qt::AlignLeft | Qt::AlignBottom);
    mapBottomOverlayLayout->addStretch(1);
    mapOverlayLayout->addLayout(mapBottomOverlayLayout);

    mapPanelLayout->addWidget(map_widget_, 1);
    mainLayout->addWidget(sidebarCard);
    mainLayout->addWidget(mapPanel, 1);

    connect(map_source_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TrajectoryViewerDialog::applyMapSourceSelection);
    connect(heat_palette_menu_, &QMenu::triggered, this, [this, mapWidget](QAction *action) {
        if (!action)
        {
            return;
        }
        const bool ok = action->data().isValid();
        if (ok)
        {
            mapWidget->setHeatPalette(heatPaletteFromComboIndex(action->data().toInt()));
            updateTexts();
                updateHeatLegend();
        }
    });
    connect(track_width_slider_, &QSlider::valueChanged, this, [mapWidget](int value) {
        mapWidget->setTrackWidth(value / static_cast<double>(kTrackStyleSliderScale));
    });
    connect(point_size_slider_, &QSlider::valueChanged, this, [mapWidget](int value) {
        mapWidget->setPointRadius(value / static_cast<double>(kTrackStyleSliderScale));
    });
    connect(show_route_button_, &QPushButton::toggled, this, [this, mapWidget](bool checked) {
        mapWidget->setShowRoute(checked);
        updateVisibilityButtonIcons();
    });
    connect(show_points_button_, &QPushButton::toggled, this, [this, mapWidget](bool checked) {
        mapWidget->setShowTrackPoints(checked);
        if (!checked)
        {
            point_detail_visible_ = false;
            updateSelectedPointDetails();
        }
        updateVisibilityButtonIcons();
    });
    connect(tianditu_key_edit_, &QLineEdit::editingFinished,
            this, &TrajectoryViewerDialog::applyTiandituKeyEdit);
    connect(tianditu_key_edit_, &QLineEdit::returnPressed, this, [this]() {
        applyTiandituKeyEdit();
        if (tianditu_key_menu_)
        {
            tianditu_key_menu_->hide();
        }
    });
    connect(zoom_in_button_, &QToolButton::clicked, this, [this]() {
        static_cast<TrajectoryMapWidget*>(map_widget_)->zoomIn();
    });
    connect(zoom_out_button_, &QToolButton::clicked, this, [this]() {
        static_cast<TrajectoryMapWidget*>(map_widget_)->zoomOut();
    });
    connect(reset_view_button_, &QToolButton::clicked, this, [this]() {
        static_cast<TrajectoryMapWidget*>(map_widget_)->resetView();
    });
    connect(point_detail_close_button_, &QToolButton::clicked,
            this, &TrajectoryViewerDialog::hidePointDetailCard);
    connect(export_button_, &QPushButton::clicked,
            this, &TrajectoryViewerDialog::exportTrackCsv);
    connect(copy_point_button_, &QPushButton::clicked,
            this, &TrajectoryViewerDialog::copySelectedPoint);

    {
        QSettings settings("VaporView", "TrajectoryViewer");
        const QString tiandituKey = settings.value(tiandituKeySettingKey()).toString().trimmed();
        tianditu_key_edit_->setText(tiandituKey);
        mapWidget->setTianDiTuKey(tiandituKey);
        const QString provider = settings.value(tileProviderSettingKey(), QStringLiteral("osm")).toString().trimmed().toLower();
        const TileProvider providerEnum =
            provider == QStringLiteral("tianditu_img")
                ? TileProvider::TianDiTuSatellite
                : (provider == QStringLiteral("tianditu") || provider == QStringLiteral("tianditu_vec"))
                    ? TileProvider::TianDiTuVector
                    : TileProvider::OpenStreetMap;
        mapWidget->setTileProvider(providerEnum);
    }

    VaporView::installCustomTitleBar(this);
    updateThemeStyles();
    updateTexts();
    updateSelectedPointDetails();
}

TrajectoryViewerDialog::~TrajectoryViewerDialog()
{
    for (QObject *sender : {
             static_cast<QObject*>(heat_palette_menu_),
             static_cast<QObject*>(heat_palette_button_),
             static_cast<QObject*>(track_width_slider_),
             static_cast<QObject*>(point_size_slider_),
             static_cast<QObject*>(show_route_button_),
             static_cast<QObject*>(show_points_button_),
             static_cast<QObject*>(point_detail_close_button_),
             static_cast<QObject*>(zoom_in_button_),
             static_cast<QObject*>(zoom_out_button_),
             static_cast<QObject*>(reset_view_button_)})
    {
        if (sender)
        {
            QObject::disconnect(sender, nullptr, this, nullptr);
        }
    }
    if (map_widget_)
    {
        static_cast<TrajectoryMapWidget*>(map_widget_)->cancelTileActivity();
    }
}

void TrajectoryViewerDialog::updateThemeStyles()
{
    if (updating_theme_styles_)
    {
        return;
    }

    updating_theme_styles_ = true;
    const QString themedStyleSheet = VaporView::applyAppThemeTokens(QStringLiteral(
        "QDialog#trajectoryViewerDialog { background-color: @vv-surface; }"
        "QDialog#trajectoryViewerDialog QWidget#customTitleBarContent, QDialog#trajectoryViewerDialog QWidget#trajectoryViewerContent { background-color: @vv-surface; }"
        "QDialog#trajectoryViewerDialog QFrame#trajectoryViewerSidebarCard { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 8px; }"
        "QDialog#trajectoryViewerDialog QFrame#trajectoryViewerSidebarCard QWidget#sectionTitleBar { background-color: @vv-surface; border: none; border-bottom: 1px solid @vv-border; border-top-left-radius: 7px; border-top-right-radius: 7px; min-height: 40px; max-height: 40px; }"
        "QDialog#trajectoryViewerDialog QFrame#trajectoryViewerSidebarCard QLabel#trajectorySidebarTitleIcon { background-color: transparent; border: none; border-radius: 0px; padding: 3px; }"
        "QDialog#trajectoryViewerDialog QFrame#trajectoryViewerSidebarCard QLabel#sectionTitleLabel { background-color: transparent; border: none; color: @vv-text; font-size: 16px; font-weight: bold; margin: 0px; padding: 0px; }"
        "QDialog#trajectoryViewerDialog QScrollArea#trajectoryViewerSidebar { background-color: @vv-surface; border: none; border-bottom-left-radius: 7px; border-bottom-right-radius: 7px; }"
        "QDialog#trajectoryViewerDialog QWidget#trajectoryViewerSidebarViewport, QDialog#trajectoryViewerDialog QWidget#trajectoryViewerSidebarContent { background-color: @vv-surface; border: none; }"
        "QDialog#trajectoryViewerDialog QLabel#trajectorySidebarSummaryLabel, QDialog#trajectoryViewerDialog QLabel#trajectorySidebarStatusLabel { color: @vv-text; background-color: transparent; border: none; font-size: 14px; font-weight: 500; line-height: 140%; }"
        "QDialog#trajectoryViewerDialog QFrame#trajectoryPointDetailCard { background-color: @vv-surface-raised; border: 1px solid @vv-border; border-radius: 8px; }"
        "QDialog#trajectoryViewerDialog QLabel#trajectoryPointDetailLabel { color: @vv-text; background-color: transparent; border: none; font-size: 13px; font-weight: 500; line-height: 140%; }"
        "QDialog#trajectoryViewerDialog QToolButton#trajectoryPointDetailCloseButton { background-color: transparent; border: none; border-radius: 4px; color: @vv-text-muted; min-width: 24px; max-width: 24px; min-height: 24px; max-height: 24px; padding: 0px; }"
        "QDialog#trajectoryViewerDialog QToolButton#trajectoryPointDetailCloseButton:hover, QDialog#trajectoryViewerDialog QToolButton#trajectoryPointDetailCloseButton:focus { background-color: @vv-primary-subtle; color: @vv-primary; }"
        "QDialog#trajectoryViewerDialog QLabel#trajectoryControlLabel { color: @vv-text; background-color: transparent; border: none; font-size: 13px; font-weight: 600; min-height: 24px; }"
        "QDialog#trajectoryViewerDialog QFrame#trajectoryHeatLegendCard, QDialog#trajectoryViewerDialog QFrame#trajectoryMapToolsCard { background-color: @vv-surface-raised; border: 1px solid @vv-border; border-radius: 8px; }"
        "QDialog#trajectoryViewerDialog QLabel#trajectoryHeatLegendTitle { color: @vv-text-strong; background-color: transparent; border: none; font-size: 13px; font-weight: 700; }"
        "QDialog#trajectoryViewerDialog QLabel#trajectoryHeatLegendCaption { color: @vv-text-muted; background-color: transparent; border: none; font-size: 11px; font-weight: 500; }"
        "QDialog#trajectoryViewerDialog QPushButton#trajectoryVisibilityToggle { background-color: transparent; border: none; border-radius: 0px; color: @vv-text; min-width: 24px; max-width: 24px; min-height: 24px; max-height: 24px; padding: 0px; }"
        "QDialog#trajectoryViewerDialog QPushButton#trajectoryVisibilityToggle:hover { background-color: transparent; border: none; }"
        "QDialog#trajectoryViewerDialog QPushButton#trajectoryVisibilityToggle:checked { background-color: transparent; border: none; color: @vv-primary; }"
        "QDialog#trajectoryViewerDialog QPushButton#trajectoryVisibilityToggle:disabled { color: @vv-text-muted; }"
        "QDialog#trajectoryViewerDialog QFrame#trajectoryViewerMapPanel { background-color: @vv-surface; border: 1px solid @vv-border; border-radius: 8px; }"
        "QDialog#trajectoryViewerDialog QWidget#trajectoryViewerMap { background-color: @vv-surface; border: none; border-radius: 8px; }"
        "QDialog#trajectoryViewerDialog QPushButton#trajectorySidebarActionButton { background-color: transparent; border: 1px solid transparent; border-radius: 6px; color: @vv-text; font-size: 14px; font-weight: 600; min-height: 32px; max-height: 32px; padding: 4px 10px; }"
        "QDialog#trajectoryViewerDialog QPushButton#trajectorySidebarActionButton:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
        "QDialog#trajectoryViewerDialog QPushButton#trajectorySidebarActionButton:pressed { background-color: @vv-primary-subtle-pressed; color: @vv-primary; }"
        "QDialog#trajectoryViewerDialog QPushButton#trajectorySidebarActionButton:disabled { background-color: transparent; color: @vv-text-muted; }"
        "QDialog#trajectoryViewerDialog QProgressBar { background-color: @vv-field-bg; border: 1px solid @vv-border; border-radius: 6px; color: @vv-text; font-size: 12px; font-weight: 600; min-height: 16px; max-height: 16px; text-align: center; }"
        "QDialog#trajectoryViewerDialog QProgressBar::chunk { background-color: @vv-progress-chunk; border-radius: 5px; }"
        "QDialog#trajectoryViewerDialog QSlider::groove:horizontal { background-color: @vv-field-bg; border: 1px solid @vv-border; height: 6px; border-radius: 3px; }"
        "QDialog#trajectoryViewerDialog QSlider::handle:horizontal { background-color: @vv-primary; border: 1px solid @vv-primary; width: 14px; margin: -5px 0px; border-radius: 7px; }"
        "QDialog#trajectoryViewerDialog QComboBox#trajectoryMapSourceCombo { background-color: @vv-field-bg; border: 1px solid @vv-border; border-radius: 6px; color: @vv-text; font-size: 14px; font-weight: 600; min-height: 32px; padding: 4px 28px 4px 10px; }"
        "QDialog#trajectoryViewerDialog QComboBox#trajectoryMapSourceCombo:hover { border-color: @vv-border-strong; }"
        "QDialog#trajectoryViewerDialog QComboBox#trajectoryMapSourceCombo:focus { border-color: @vv-primary; }"
        "QDialog#trajectoryViewerDialog QToolButton#trajectoryHeatPaletteButton { background-color: transparent; border: none; border-radius: 4px; color: @vv-text; min-width: 28px; max-width: 28px; min-height: 24px; max-height: 24px; padding: 0px; }"
        "QDialog#trajectoryViewerDialog QToolButton#trajectoryHeatPaletteButton:hover, QDialog#trajectoryViewerDialog QToolButton#trajectoryHeatPaletteButton:focus { background-color: @vv-primary-subtle; border: none; }"
        "QDialog#trajectoryViewerDialog QToolButton#trajectoryHeatPaletteButton::menu-indicator { image: none; width: 0px; }"
        "QDialog#trajectoryViewerDialog QMenu#trajectoryHeatPaletteMenu { background-color: @vv-surface-raised; border: 1px solid @vv-border; border-radius: 6px; color: @vv-text; padding: 4px; margin: 0px; }"
        "QDialog#trajectoryViewerDialog QMenu#trajectoryHeatPaletteMenu::item { background-color: transparent; border-radius: 4px; padding: 6px 18px; }"
        "QDialog#trajectoryViewerDialog QMenu#trajectoryHeatPaletteMenu::item:selected { background-color: @vv-primary-subtle; color: @vv-primary; }"
        "QDialog#trajectoryViewerDialog QToolButton#titleBarButton { background-color: transparent; border: 1px solid transparent; border-radius: 6px; }"
        "QDialog#trajectoryViewerDialog QToolButton#titleBarButton:hover { background-color: @vv-primary-subtle; border-color: @vv-primary-subtle-pressed; }"),
        isDarkPalette());
    if (styleSheet() != themedStyleSheet)
    {
        setStyleSheet(themedStyleSheet);
    }
    updating_theme_styles_ = false;
}

void TrajectoryViewerDialog::applyMapSourceSelection(int index)
{
    auto *mapWidget = static_cast<TrajectoryMapWidget*>(map_widget_);
    const TileProvider selectedProvider = tileProviderFromComboIndex(index);
    const QString tiandituKey = tianditu_key_edit_ ? tianditu_key_edit_->text().trimmed() : QString();
    const bool missingTiandituKey = isTianDiTuProvider(selectedProvider) && tiandituKey.isEmpty();

    QSettings settings("VaporView", "TrajectoryViewer");
    if (tiandituKey.isEmpty())
    {
        settings.remove(tiandituKeySettingKey());
    }
    else
    {
        settings.setValue(tiandituKeySettingKey(), tiandituKey);
    }

    mapWidget->setTianDiTuKey(tiandituKey);
    mapWidget->setTileProvider(selectedProvider);
    settings.setValue(tileProviderSettingKey(), tileProviderKey(selectedProvider));

    if (missingTiandituKey)
    {
        QTimer::singleShot(0, this, [this]() {
            showTiandituKeyMenu();
        });
    }
    updateTexts();
}

void TrajectoryViewerDialog::applyTiandituKeyEdit()
{
    if (!tianditu_key_edit_)
    {
        return;
    }

    auto *mapWidget = static_cast<TrajectoryMapWidget*>(map_widget_);
    const QString tiandituKey = tianditu_key_edit_->text().trimmed();
    if (tianditu_key_edit_->text() != tiandituKey)
    {
        QSignalBlocker blocker(tianditu_key_edit_);
        tianditu_key_edit_->setText(tiandituKey);
    }
    QSettings settings("VaporView", "TrajectoryViewer");
    if (tiandituKey.isEmpty())
    {
        settings.remove(tiandituKeySettingKey());
        mapWidget->setTianDiTuKey(QString());
    }
    else
    {
        settings.setValue(tiandituKeySettingKey(), tiandituKey);
        mapWidget->setTianDiTuKey(tiandituKey);
    }
    updateTexts();
}

void TrajectoryViewerDialog::showTiandituKeyMenu()
{
    if (!tianditu_key_button_ || !tianditu_key_menu_)
    {
        return;
    }

    tianditu_key_menu_->popup(tianditu_key_button_->mapToGlobal(QPoint(0, tianditu_key_button_->height())));
}

void TrajectoryViewerDialog::setEnglish(bool english)
{
    is_english_ = english;
    static_cast<TrajectoryMapWidget*>(map_widget_)->setEnglish(english);
    updateTexts();
    updateSummary();
    updateHeatLegend();
}

void TrajectoryViewerDialog::setTrackLabel(const QString& englishLabel, const QString& chineseLabel)
{
    english_track_label_ = englishLabel;
    chinese_track_label_ = chineseLabel;
    static_cast<TrajectoryMapWidget*>(map_widget_)->setTrackLabel(englishLabel, chineseLabel);
    updateTexts();
    updateSummary();
}

void TrajectoryViewerDialog::setTrackPoints(const QVector<RtkTrackPoint>& points)
{
    track_points_ = points;
    point_detail_visible_ = false;
    static_cast<TrajectoryMapWidget*>(map_widget_)->setTrackPoints(points);
    selected_track_index_ = track_points_.isEmpty()
        ? -1
        : std::clamp(selected_track_index_, 0, static_cast<int>(track_points_.size()) - 1);
    if (!track_points_.isEmpty() && selected_track_index_ < 0)
    {
        selected_track_index_ = 0;
    }
    static_cast<TrajectoryMapWidget*>(map_widget_)->setSelectedTrackIndex(selected_track_index_);
    export_button_->setEnabled(!track_points_.isEmpty());
    copy_point_button_->setEnabled(!track_points_.isEmpty());
    updateSummary();
    updateHeatLegend();
    updateSelectedPointDetails();
}

void TrajectoryViewerDialog::setTrackStats(const RtkTrackStats& stats)
{
    track_stats_ = stats;
    updateSummary();
}

void TrajectoryViewerDialog::setSelectedTrackIndex(int index, bool notifySession)
{
    const int clamped = track_points_.isEmpty()
        ? -1
        : std::clamp(index, 0, static_cast<int>(track_points_.size()) - 1);
    if (selected_track_index_ == clamped)
    {
        updateSelectedPointDetails();
        if (notifySession && selected_track_index_ >= 0)
        {
            emit trackPointActivated(selected_track_index_);
        }
        return;
    }

    selected_track_index_ = clamped;
    static_cast<TrajectoryMapWidget*>(map_widget_)->setSelectedTrackIndex(selected_track_index_);
    updateSelectedPointDetails();
    if (notifySession && selected_track_index_ >= 0)
    {
        emit trackPointActivated(selected_track_index_);
    }
}

void TrajectoryViewerDialog::hidePointDetailCard()
{
    point_detail_visible_ = false;
    if (point_detail_card_)
    {
        point_detail_card_->hide();
    }
}

void TrajectoryViewerDialog::copySelectedPoint()
{
    if (selected_track_index_ < 0 || selected_track_index_ >= track_points_.size() || !qApp)
    {
        return;
    }
    const RtkTrackPoint& point = track_points_.at(selected_track_index_);
    const QString text = QStringLiteral("#%1, %2, %3, %4, csv_row=%5, waveform_frame=%6")
        .arg(selected_track_index_ + 1)
        .arg(QString::number(point.latitude, 'f', 8))
        .arg(QString::number(point.longitude, 'f', 8))
        .arg(formatTimestampUs(point.timestamp_us))
        .arg(point.csv_row >= 0 ? point.csv_row + 1 : 0)
        .arg(point.waveform_frame_index >= 0 ? point.waveform_frame_index + 1 : 0);
    qApp->clipboard()->setText(text);
    map_status_label_->setText(is_english_
        ? QStringLiteral("Selected trajectory point copied to clipboard.")
        : QStringLiteral("已复制当前轨迹点到剪贴板。"));
}

void TrajectoryViewerDialog::exportTrackCsv()
{
    if (track_points_.isEmpty())
    {
        return;
    }

    const QString filename = QFileDialog::getSaveFileName(
        this,
        is_english_ ? QStringLiteral("Export Trajectory CSV") : QStringLiteral("导出轨迹 CSV"),
        QDir::home().filePath(QStringLiteral("vaporview_trajectory.csv")),
        QStringLiteral("CSV (*.csv)"));
    if (filename.isEmpty())
    {
        return;
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        map_status_label_->setText(QString(is_english_
            ? "Failed to export trajectory CSV: %1"
            : "导出轨迹 CSV 失败：%1").arg(file.errorString()));
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "index,csv_row,timestamp_utc,timestamp_us,latitude,longitude,height_m,cumulative_distance_m,segment_distance_m,speed_mps,gnss_fix,peak_value,waveform_frame,waveform_timestamp_us,waveform_delta_ms\n";
    for (int index = 0; index < track_points_.size(); ++index)
    {
        const RtkTrackPoint& point = track_points_.at(index);
        stream << index + 1 << ','
               << (point.csv_row >= 0 ? point.csv_row + 1 : 0) << ','
               << csvCell(formatTimestampUs(point.timestamp_us)) << ','
               << point.timestamp_us << ','
               << QString::number(point.latitude, 'f', 8) << ','
               << QString::number(point.longitude, 'f', 8) << ','
               << (point.has_height ? QString::number(point.height_m, 'f', 3) : QString()) << ','
               << QString::number(point.cumulative_distance_m, 'f', 3) << ','
               << QString::number(point.segment_distance_m, 'f', 3) << ','
               << (point.has_speed ? QString::number(point.speed_mps, 'f', 4) : QString()) << ','
               << csvCell(point.gnss_fix) << ','
               << (point.has_peak_value ? QString::number(point.peak_value, 'f', 6) : QString()) << ','
               << (point.waveform_frame_index >= 0 ? QString::number(point.waveform_frame_index + 1) : QString()) << ','
               << (point.has_waveform_match ? QString::number(point.waveform_timestamp_us) : QString()) << ','
               << (point.has_waveform_match ? QString::number(static_cast<double>(point.waveform_delta_us) / 1000.0, 'f', 3) : QString())
               << '\n';
    }
    map_status_label_->setText(QString(is_english_
        ? "Trajectory CSV exported: %1"
        : "轨迹 CSV 已导出：%1").arg(QDir::toNativeSeparators(filename)));
}

void TrajectoryViewerDialog::updateSelectedPointDetails()
{
    if (track_points_.isEmpty() || selected_track_index_ < 0 || selected_track_index_ >= track_points_.size())
    {
        detail_label_->setText(is_english_
            ? QStringLiteral("Select a trajectory point on the map or scrub the timeline to inspect CSV and waveform linkage.")
            : QStringLiteral("在地图上点击轨迹点，或拖动时间轴，即可查看 CSV 与波形联动信息。"));
        if (point_detail_card_)
        {
            point_detail_card_->hide();
        }
        return;
    }

    const RtkTrackPoint& point = track_points_.at(selected_track_index_);
    const QString heightText = point.has_height ? QStringLiteral("%1 m").arg(QString::number(point.height_m, 'f', 3)) : QStringLiteral("--");
    const QString speedText = point.has_speed ? formatSpeed(point.speed_mps) : QStringLiteral("--");
    const QString peakText = point.has_peak_value ? formatPeakValue(point.peak_value) : QStringLiteral("--");
    const QString waveformText = point.has_waveform_match
        ? QString(is_english_ ? "frame %1, Δ %2" : "第 %1 帧，Δ %2")
              .arg(point.waveform_frame_index + 1)
              .arg(formatSignedDeltaMs(point.waveform_delta_us))
        : QStringLiteral("--");

    const QString title = is_english_
        ? QStringLiteral("Selected #%1 / %2").arg(selected_track_index_ + 1).arg(track_points_.size())
        : QStringLiteral("当前 #%1 / %2").arg(selected_track_index_ + 1).arg(track_points_.size());
    const QVector<QPair<QString, QString>> rows = {
        {is_english_ ? QStringLiteral("CSV row") : QStringLiteral("CSV 行"), QString::number(point.csv_row >= 0 ? point.csv_row + 1 : 0)},
        {is_english_ ? QStringLiteral("Time") : QStringLiteral("时间"), formatTimestampUs(point.timestamp_us)},
        {is_english_ ? QStringLiteral("Latitude") : QStringLiteral("纬度"), QString::number(point.latitude, 'f', 8)},
        {is_english_ ? QStringLiteral("Longitude") : QStringLiteral("经度"), QString::number(point.longitude, 'f', 8)},
        {is_english_ ? QStringLiteral("Height") : QStringLiteral("高度"), heightText},
        {is_english_ ? QStringLiteral("Distance") : QStringLiteral("里程"), formatDistanceMeters(point.cumulative_distance_m)},
        {is_english_ ? QStringLiteral("Speed") : QStringLiteral("速度"), speedText},
        {is_english_ ? QStringLiteral("Peak") : QStringLiteral("峰值"), peakText},
        {is_english_ ? QStringLiteral("Waveform") : QStringLiteral("波形"), waveformText}
    };
    detail_label_->setText(trajectoryInfoTable(title, rows, isDarkPalette()));
    if (point_detail_card_)
    {
        point_detail_card_->setVisible(point_detail_visible_);
    }
}

void TrajectoryViewerDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (!event)
    {
        return;
    }

    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
    {
        updateThemeStyles();
        updateSummary();
        updateHeatLegend();
        updateTitleBarIcons();
    }
}

void TrajectoryViewerDialog::closeEvent(QCloseEvent *event)
{
    if (map_widget_)
    {
        static_cast<TrajectoryMapWidget*>(map_widget_)->cancelTileActivity();
    }
    QDialog::closeEvent(event);
}

void TrajectoryViewerDialog::updateTitleBarIcons()
{
    const bool dark = isDarkPalette();
    if (sidebar_icon_label_)
    {
        sidebar_icon_label_->setPixmap(createTitleBarIcon(QStringLiteral("route"), dark).pixmap(QSize(20, 20)));
    }
    if (heat_palette_button_)
    {
        heat_palette_button_->setIcon(createLucideIcon(QStringLiteral("chevron-down"),
            dark ? appThemeColor(AppThemeColor::TextTitle, true) : appThemeColor(AppThemeColor::TextStrong, false)));
    }
    if (tianditu_key_button_)
    {
        tianditu_key_button_->setIcon(createTitleBarIcon(QStringLiteral("key"), dark));
    }
    if (zoom_in_button_)
    {
        zoom_in_button_->setIcon(createTitleBarIcon(QStringLiteral("zoom-in"), dark));
    }
    if (zoom_out_button_)
    {
        zoom_out_button_->setIcon(createTitleBarIcon(QStringLiteral("zoom-out"), dark));
    }
    if (reset_view_button_)
    {
        reset_view_button_->setIcon(createTitleBarIcon(QStringLiteral("maximize"), dark));
    }
    if (point_detail_close_button_)
    {
        point_detail_close_button_->setIcon(createLucideIcon(QStringLiteral("x"),
            dark ? appThemeColor(AppThemeColor::TextMuted, true) : appThemeColor(AppThemeColor::TextMuted, false)));
    }
    updateVisibilityButtonIcons();
}

void TrajectoryViewerDialog::updateVisibilityButtonIcons()
{
    const bool dark = isDarkPalette();
    auto applyIcon = [dark](QPushButton *button) {
        if (!button)
        {
            return;
        }
        const bool checked = button->isChecked();
        const QColor iconColor = checked
            ? appThemeColor(AppThemeColor::Primary, dark)
            : appThemeColor(AppThemeColor::TextStrong, dark);
        button->setIcon(createLucideIcon(
            checked ? QStringLiteral("square-check-big") : QStringLiteral("square"),
            iconColor));
    };
    applyIcon(show_route_button_);
    applyIcon(show_points_button_);
}

void TrajectoryViewerDialog::updateHeatLegend()
{
    if (!heat_palette_card_ || !heat_gradient_bar_ || !heat_min_label_ || !heat_mid_label_ || !heat_max_label_)
    {
        return;
    }

    float minPeak = 0.0f;
    float maxPeak = 0.0f;
    int peakCount = 0;
    bool hasPeak = false;
    for (const RtkTrackPoint& point : track_points_)
    {
        if (!point.has_peak_value || !std::isfinite(point.peak_value))
        {
            continue;
        }
        if (!hasPeak)
        {
            minPeak = point.peak_value;
            maxPeak = point.peak_value;
            hasPeak = true;
        }
        else
        {
            minPeak = std::min(minPeak, point.peak_value);
            maxPeak = std::max(maxPeak, point.peak_value);
        }
        ++peakCount;
    }

    heat_palette_card_->setVisible(hasPeak);
    if (!hasPeak)
    {
        return;
    }

    static_cast<HeatGradientBarWidget*>(heat_gradient_bar_)->setHeatPalette(
        static_cast<TrajectoryMapWidget*>(map_widget_)->heatPalette());
    heat_min_label_->setText(formatPeakValue(minPeak));
    heat_mid_label_->setText(formatPeakValue((static_cast<double>(minPeak) + static_cast<double>(maxPeak)) * 0.5));
    const QString countText = is_english_
        ? QStringLiteral("%1 samples").arg(peakCount)
        : QStringLiteral("%1 个样本").arg(peakCount);
    heat_palette_card_->setToolTip(countText);
    heat_gradient_bar_->setToolTip(countText);
    heat_max_label_->setText(formatPeakValue(maxPeak));
    heat_gradient_bar_->update();
}

void TrajectoryViewerDialog::updateSummary()
{
    if (track_points_.isEmpty())
    {
        summary_label_->setText(is_english_
            ? QStringLiteral("No valid latitude/longitude samples were found for %1 in this session.").arg(english_track_label_)
            : QStringLiteral("当前会话中没有找到%1的有效经纬度轨迹点。").arg(chinese_track_label_));
        updateSelectedPointDetails();
        return;
    }

    double minLat = std::numeric_limits<double>::infinity();
    double maxLat = -std::numeric_limits<double>::infinity();
    double minLon = std::numeric_limits<double>::infinity();
    double maxLon = -std::numeric_limits<double>::infinity();
    double minHeight = std::numeric_limits<double>::infinity();
    double maxHeight = -std::numeric_limits<double>::infinity();
    bool hasHeightRange = false;
    double totalDistance = 0.0;
    double maxSpeed = 0.0;
    int speedCount = 0;
    for (const RtkTrackPoint& point : track_points_)
    {
        minLat = std::min(minLat, point.latitude);
        maxLat = std::max(maxLat, point.latitude);
        minLon = std::min(minLon, point.longitude);
        maxLon = std::max(maxLon, point.longitude);
        if (point.has_height && std::isfinite(point.height_m))
        {
            hasHeightRange = true;
            minHeight = std::min(minHeight, point.height_m);
            maxHeight = std::max(maxHeight, point.height_m);
        }
        if (std::isfinite(point.cumulative_distance_m))
        {
            totalDistance = std::max(totalDistance, point.cumulative_distance_m);
        }
        if (point.has_speed && std::isfinite(point.speed_mps))
        {
            ++speedCount;
            maxSpeed = std::max(maxSpeed, point.speed_mps);
        }
    }

    const int rejectedTotal = track_stats_.rejected_invalid_nav +
        track_stats_.rejected_bad_fix +
        track_stats_.rejected_zero_coordinate +
        track_stats_.rejected_out_of_range +
        track_stats_.rejected_jump;
    const QString rejectedDetailText = QString(is_english_
        ? "invalid %1, fix %2, zero %3, range %4, jumps>%5m %6"
        : "无效 %1、定位 %2、零点 %3、越界 %4、跳点>%5m %6")
        .arg(track_stats_.rejected_invalid_nav)
        .arg(track_stats_.rejected_bad_fix)
        .arg(track_stats_.rejected_zero_coordinate)
        .arg(track_stats_.rejected_out_of_range)
        .arg(QString::number(track_stats_.jump_threshold_m, 'f', 1))
        .arg(track_stats_.rejected_jump);

    QVector<QPair<QString, QString>> rows = {
        {is_english_ ? QStringLiteral("Points") : QStringLiteral("点数"), QString::number(track_points_.size())},
        {is_english_ ? QStringLiteral("Distance") : QStringLiteral("里程"), formatDistanceMeters(totalDistance)},
        {is_english_ ? QStringLiteral("Max speed") : QStringLiteral("最大速度"), speedCount > 0 ? formatSpeed(maxSpeed) : QStringLiteral("--")},
        {is_english_ ? QStringLiteral("Speed samples") : QStringLiteral("速度样本"), QString::number(speedCount)},
        {is_english_ ? QStringLiteral("Latitude") : QStringLiteral("纬度范围"),
            QStringLiteral("%1 - %2").arg(QString::number(minLat, 'f', 7), QString::number(maxLat, 'f', 7))},
        {is_english_ ? QStringLiteral("Longitude") : QStringLiteral("经度范围"),
            QStringLiteral("%1 - %2").arg(QString::number(minLon, 'f', 7), QString::number(maxLon, 'f', 7))},
        {is_english_ ? QStringLiteral("Height") : QStringLiteral("高度范围"),
            hasHeightRange
                ? QStringLiteral("%1 - %2 m").arg(QString::number(minHeight, 'f', 3), QString::number(maxHeight, 'f', 3))
                : QStringLiteral("--")},
        {is_english_ ? QStringLiteral("Height change") : QStringLiteral("高度变化"),
            hasHeightRange ? QStringLiteral("%1 m").arg(QString::number(maxHeight - minHeight, 'f', 3)) : QStringLiteral("--")},
        {is_english_ ? QStringLiteral("Source rows") : QStringLiteral("来源行"), QString::number(track_stats_.scanned_rows)},
        {is_english_ ? QStringLiteral("Accepted") : QStringLiteral("保留"), QString::number(track_stats_.accepted_points)},
        {is_english_ ? QStringLiteral("Rejected") : QStringLiteral("剔除"), QString::number(rejectedTotal)},
        {is_english_ ? QStringLiteral("Reject detail") : QStringLiteral("剔除明细"), rejectedDetailText}
    };
    const QString title = is_english_
        ? QStringLiteral("%1 %2 points").arg(track_points_.size()).arg(english_track_label_)
        : QStringLiteral("%1 个%2点").arg(track_points_.size()).arg(chinese_track_label_);
    summary_label_->setText(trajectoryInfoTable(title, rows, isDarkPalette()));
    updateSelectedPointDetails();
}

void TrajectoryViewerDialog::updateTexts()
{
    setWindowTitle(is_english_
        ? QStringLiteral("%1 Viewer").arg(english_track_label_)
        : QStringLiteral("%1查看").arg(chinese_track_label_));
    if (sidebar_title_label_)
    {
        sidebar_title_label_->setText(is_english_ ? QStringLiteral("Trajectory Controls") : QStringLiteral("轨迹控制"));
    }
    {
        QSignalBlocker blocker(map_source_combo_);
        map_source_combo_->clear();
        map_source_combo_->addItem(is_english_ ? QStringLiteral("OpenStreetMap") : QStringLiteral("OpenStreetMap"));
        map_source_combo_->addItem(is_english_ ? QStringLiteral("Tianditu Vector") : QStringLiteral("天地图矢量"));
        map_source_combo_->addItem(is_english_ ? QStringLiteral("Tianditu Satellite") : QStringLiteral("天地图卫星"));
        const TileProvider provider = static_cast<TrajectoryMapWidget*>(map_widget_)->tileProvider();
        map_source_combo_->setCurrentIndex(tileProviderComboIndex(provider));
    }
    map_source_combo_->setToolTip(is_english_ ? QStringLiteral("Map source") : QStringLiteral("底图来源"));
    if (heat_palette_menu_)
    {
        QSignalBlocker blocker(heat_palette_menu_);
        heat_palette_menu_->clear();
        const HeatPalette palette = static_cast<TrajectoryMapWidget*>(map_widget_)->heatPalette();
        const int currentIndex = heatPaletteComboIndex(palette);
        for (int index = 0; index < 5; ++index)
        {
            QAction *action = heat_palette_menu_->addAction(heatPaletteName(heatPaletteFromComboIndex(index), is_english_));
            action->setData(index);
            action->setCheckable(true);
            action->setChecked(index == currentIndex);
        }
    }
    heat_palette_button_->setToolTip(is_english_
        ? QStringLiteral("Choose the peak heatmap color ramp.")
        : QStringLiteral("选择峰值热力图色带。"));
    heat_palette_button_->setAccessibleName(is_english_ ? QStringLiteral("Heat palette") : QStringLiteral("热力图色带"));
    if (heat_palette_title_label_)
    {
        heat_palette_title_label_->setText(is_english_ ? QStringLiteral("Peak") : QStringLiteral("峰值"));
    }
    track_width_label_->setText(is_english_ ? QStringLiteral("Route width") : QStringLiteral("路线粗细"));
    point_size_label_->setText(is_english_ ? QStringLiteral("Point size") : QStringLiteral("点大小"));
    show_route_button_->setText(QString());
    show_points_button_->setText(QString());
    show_route_button_->setAccessibleName(is_english_ ? QStringLiteral("Show route") : QStringLiteral("显示路线"));
    show_points_button_->setAccessibleName(is_english_ ? QStringLiteral("Show points") : QStringLiteral("显示路径点"));
    show_route_button_->setToolTip(is_english_
        ? QStringLiteral("Show or hide the trajectory route line.")
        : QStringLiteral("显示或隐藏轨迹路线。"));
    show_points_button_->setToolTip(is_english_
        ? QStringLiteral("Show or hide visible trajectory points. Hidden points are not hover-selected.")
        : QStringLiteral("显示或隐藏路径点。隐藏后鼠标不会吸附到路径点。"));
    track_width_slider_->setToolTip(is_english_
        ? QStringLiteral("Adjust trajectory route line width.")
        : QStringLiteral("调整轨迹路线线宽。"));
    point_size_slider_->setToolTip(is_english_
        ? QStringLiteral("Adjust trajectory point size.")
        : QStringLiteral("调整轨迹点大小。"));
    if (tianditu_key_menu_label_)
    {
        tianditu_key_menu_label_->setText(is_english_ ? QStringLiteral("Tianditu Key") : QStringLiteral("天地图 Key"));
    }
    if (tianditu_key_button_)
    {
        tianditu_key_button_->setToolTip(is_english_ ? QStringLiteral("Tianditu key") : QStringLiteral("天地图 Key"));
        tianditu_key_button_->setStatusTip(tianditu_key_button_->toolTip());
    }
    tianditu_key_edit_->setPlaceholderText(is_english_ ? QStringLiteral("Tianditu Key") : QStringLiteral("天地图 Key"));
    tianditu_key_edit_->setToolTip(
        is_english_
            ? QStringLiteral("Saved Tianditu tile key. Clear the field to remove it.")
            : QStringLiteral("已保存的天地图瓦片 Key。清空输入框即可删除。"));
    map_progress_bar_->setToolTip(
        is_english_
            ? QStringLiteral("Shows the loading progress of the currently visible base map tiles.")
            : QStringLiteral("显示当前可见区域底图瓦片的加载进度。"));
    copy_point_button_->setText(is_english_ ? QStringLiteral("Copy Point") : QStringLiteral("复制点"));
    export_button_->setText(is_english_ ? QStringLiteral("Export CSV") : QStringLiteral("导出 CSV"));
    point_detail_close_button_->setToolTip(is_english_
        ? QStringLiteral("Close selected point details")
        : QStringLiteral("关闭当前点详情"));
    point_detail_close_button_->setAccessibleName(point_detail_close_button_->toolTip());
    copy_point_button_->setToolTip(is_english_
        ? QStringLiteral("Copy selected trajectory point coordinates and linkage info.")
        : QStringLiteral("复制当前轨迹点坐标与联动信息。"));
    export_button_->setToolTip(is_english_
        ? QStringLiteral("Export accepted trajectory points with CSV row, waveform, distance, speed, and peak fields.")
        : QStringLiteral("导出保留轨迹点及 CSV 行、波形、里程、速度和峰值字段。"));
    zoom_in_button_->setText(QString());
    zoom_out_button_->setText(QString());
    reset_view_button_->setText(QString());
    zoom_in_button_->setToolTip(is_english_ ? QStringLiteral("Zoom in") : QStringLiteral("放大"));
    zoom_out_button_->setToolTip(is_english_ ? QStringLiteral("Zoom out") : QStringLiteral("缩小"));
    reset_view_button_->setToolTip(is_english_ ? QStringLiteral("Fit track") : QStringLiteral("适应轨迹"));
    zoom_in_button_->setStatusTip(zoom_in_button_->toolTip());
    zoom_out_button_->setStatusTip(zoom_out_button_->toolTip());
    reset_view_button_->setStatusTip(reset_view_button_->toolTip());
    updateHeatLegend();
    updateTitleBarIcons();
}
