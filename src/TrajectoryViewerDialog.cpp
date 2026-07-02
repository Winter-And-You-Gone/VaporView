#include "AppTheme.h"
#include "TrajectoryViewerDialog.h"
#include "CustomTitleBar.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QComboBox>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QQueue>
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

using VaporView::AppThemeColor;
using VaporView::appThemeColor;
using VaporView::isDarkThemePalette;

namespace
{
constexpr int kTileSize = 256;
constexpr int kDefaultZoom = 16;
constexpr int kMaxZoom = 19;
constexpr int kTianDiTuMaxZoom = 18;
constexpr int kMinZoom = 1;
constexpr int kMapMargin = 12;
constexpr int kTitleBarButtonSize = 34;
constexpr int kTitleBarIconSize = 24;
constexpr int kMaxConcurrentTileRequests = 8;
constexpr auto kTileRequestTimeout = std::chrono::seconds(15);

enum class TileProvider
{
    OpenStreetMap,
    TianDiTuVector,
    TianDiTuSatellite
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
    const double worldSize = static_cast<double>(kTileSize) * static_cast<double>(1 << zoom);
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

QColor heatmapColorAt(double normalized)
{
    static const std::array<std::pair<double, QColor>, 8> stops = {{
        {0.00, appThemeColor(AppThemeColor::Heatmap0, false)},
        {0.14, appThemeColor(AppThemeColor::Heatmap1, false)},
        {0.28, appThemeColor(AppThemeColor::Heatmap2, false)},
        {0.42, appThemeColor(AppThemeColor::Heatmap3, false)},
        {0.56, appThemeColor(AppThemeColor::Heatmap4, false)},
        {0.70, appThemeColor(AppThemeColor::Heatmap5, false)},
        {0.84, appThemeColor(AppThemeColor::Heatmap6, false)},
        {1.00, appThemeColor(AppThemeColor::Heatmap7, false)}
    }};

    const double clamped = std::clamp(normalized, 0.0, 1.0);
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
        , selected_track_index_(-1)
        , active_tile_request_count_(0)
        , tile_request_generation_(0)
        , feedback_update_scheduled_(false)
        , repaint_update_requested_(false)
        , english_track_label_(QStringLiteral("RTK trajectory"))
        , chinese_track_label_(QStringLiteral("RTK轨迹"))
    {
        setMinimumSize(720, 420);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);

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
        selected_track_index_ = track_points_.isEmpty()
            ? -1
            : std::clamp(selected_track_index_, 0, static_cast<int>(track_points_.size()) - 1);
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
        zoom_ = std::min(zoom_, providerMaxZoom(tile_provider_));
        fit_zoom_ = std::min(fit_zoom_, providerMaxZoom(tile_provider_));
        resetTileLoadingState(true);
        requestVisibleTiles();
        updateLoadFeedback();
        update();
    }

    TileProvider tileProvider() const
    {
        return tile_provider_;
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
        requestVisibleTiles();
        update();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
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
            dragging_ = true;
            drag_moved_ = false;
            drag_start_pos_ = event->position();
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
            requestVisibleTiles();
            update();
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (dragging_ && event->button() == Qt::LeftButton)
        {
            const bool wasClick = !drag_moved_;
            dragging_ = false;
            unsetCursor();
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

    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), appThemeColor(AppThemeColor::MapCanvas, false));

        const QRectF mapRect = rect().adjusted(kMapMargin, kMapMargin, -kMapMargin, -kMapMargin - 18);
        painter.fillRect(mapRect, appThemeColor(AppThemeColor::MapViewport, false));

        if (track_points_.isEmpty())
        {
            painter.setPen(appThemeColor(AppThemeColor::MapMutedText, false));
            painter.drawRoundedRect(mapRect, 8.0, 8.0);
            painter.drawText(mapRect, Qt::AlignCenter,
                is_english_
                    ? QStringLiteral("No %1 available in this session.").arg(english_track_label_)
                    : QStringLiteral("当前会话中没有可用的%1。").arg(chinese_track_label_));
            return;
        }

        drawTiles(painter, mapRect);
        drawFallbackGrid(painter, mapRect);
        drawTrack(painter, mapRect);

        painter.setPen(appThemeColor(AppThemeColor::MapText, false));
        painter.drawText(QRectF(mapRect.left(), mapRect.bottom() + 2, mapRect.width(), 16),
            Qt::AlignLeft | Qt::AlignVCenter,
            mapAttributionText(tile_provider_, is_english_));
        painter.drawText(QRectF(mapRect.left(), mapRect.bottom() + 2, mapRect.width(), 16),
            Qt::AlignRight | Qt::AlignVCenter,
            QString(is_english_ ? "%1 points: %2" : "%1点数: %2")
                .arg(is_english_ ? english_track_label_ : chinese_track_label_)
                .arg(track_points_.size()));
    }

private:
    void resetTileLoadingState(bool clearCache)
    {
        ++tile_request_generation_;
        if (clearCache)
        {
            tile_cache_.clear();
        }
        pending_tiles_.clear();
        queued_tile_requests_.clear();
        failed_tiles_.clear();
        last_tile_error_.clear();
        active_tile_request_count_ = 0;
        abortActiveTileReplies();
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

    void enqueueTileRequest(const QString& key, const QNetworkRequest& request)
    {
        if (tile_cache_.contains(key) || pending_tiles_.contains(key))
        {
            return;
        }

        pending_tiles_.insert(key);
        queued_tile_requests_.enqueue({key, request, tile_request_generation_});
    }

    void pruneQueuedTileRequests()
    {
        QQueue<TileFetchRequest> retainedRequests;
        while (!queued_tile_requests_.isEmpty())
        {
            const TileFetchRequest request = queued_tile_requests_.dequeue();
            if (request.generation == tile_request_generation_ &&
                current_visible_tile_keys_.contains(request.key) &&
                pending_tiles_.contains(request.key))
            {
                retainedRequests.enqueue(request);
                continue;
            }
            pending_tiles_.remove(request.key);
        }
        queued_tile_requests_ = retainedRequests;
    }

    void processTileRequestQueue()
    {
        while (active_tile_request_count_ < kMaxConcurrentTileRequests && !queued_tile_requests_.isEmpty())
        {
            const TileFetchRequest tileRequest = queued_tile_requests_.dequeue();
            if (tileRequest.generation != tile_request_generation_ ||
                !current_visible_tile_keys_.contains(tileRequest.key) ||
                !pending_tiles_.contains(tileRequest.key))
            {
                pending_tiles_.remove(tileRequest.key);
                continue;
            }

            ++active_tile_request_count_;
            QNetworkReply *reply = manager_->get(tileRequest.request);
            active_tile_replies_.insert(reply);
            QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, key = tileRequest.key, generation = tileRequest.generation]() {
                active_tile_replies_.remove(reply);
                reply->deleteLater();
                if (generation != tile_request_generation_)
                {
                    return;
                }

                active_tile_request_count_ = std::max(0, active_tile_request_count_ - 1);
                pending_tiles_.remove(key);
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
                        failed_tiles_.insert(key);
                        last_tile_error_ = is_english_
                            ? QStringLiteral("The tile response could not be decoded as an image.")
                            : QStringLiteral("收到的瓦片响应无法解码为图像。");
                    }
                }
                else
                {
                    failed_tiles_.insert(key);
                    last_tile_error_ = reply->errorString();
                }

                scheduleLoadFeedbackUpdate(true);
                processTileRequestQueue();
            });
        }
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
            zoom_ = std::min(kDefaultZoom, providerMaxZoom(tile_provider_));
            center_world_pixel_ = QPointF();
            fit_zoom_ = zoom_;
            fit_center_world_pixel_ = center_world_pixel_;
            current_visible_tile_keys_.clear();
            updateLoadFeedback();
            return;
        }

        const QSizeF mapSize = size() - QSize(2 * kMapMargin, 2 * kMapMargin + 18);
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

    void requestVisibleTiles()
    {
        if (track_points_.isEmpty())
        {
            current_visible_tile_keys_.clear();
            updateLoadFeedback();
            return;
        }
        if (isTianDiTuProvider(tile_provider_) && tianditu_key_.trimmed().isEmpty())
        {
            current_visible_tile_keys_.clear();
            updateLoadFeedback();
            return;
        }

        const QRectF mapRect = rect().adjusted(kMapMargin, kMapMargin, -kMapMargin, -kMapMargin - 18);
        const QPointF topLeft = center_world_pixel_ - QPointF(mapRect.width() * 0.5, mapRect.height() * 0.5);
        const QPointF bottomRight = center_world_pixel_ + QPointF(mapRect.width() * 0.5, mapRect.height() * 0.5);
        const int tileCount = 1 << zoom_;

        const int minTileX = std::max(0, static_cast<int>(std::floor(topLeft.x() / kTileSize)));
        const int maxTileX = std::min(tileCount - 1, static_cast<int>(std::floor(bottomRight.x() / kTileSize)));
        const int minTileY = std::max(0, static_cast<int>(std::floor(topLeft.y() / kTileSize)));
        const int maxTileY = std::min(tileCount - 1, static_cast<int>(std::floor(bottomRight.y() / kTileSize)));

        QSet<QString> visibleKeys;
        for (int tileX = minTileX; tileX <= maxTileX; ++tileX)
        {
            for (int tileY = minTileY; tileY <= maxTileY; ++tileY)
            {
                const QVector<TileLayerSpec> layers = tileLayerSpecs(tile_provider_);
                for (const TileLayerSpec& layer : layers)
                {
                    const QString key = QStringLiteral("%1:%2:%3")
                        .arg(tileProviderKey(tile_provider_), layer.cache_suffix, tileKey(zoom_, tileX, tileY));
                    visibleKeys.insert(key);
                    if (tile_cache_.contains(key) || pending_tiles_.contains(key))
                    {
                        continue;
                    }

                    QUrl tileUrl;
                    if (tile_provider_ == TileProvider::OpenStreetMap)
                    {
                        tileUrl = QUrl(QStringLiteral("https://tile.openstreetmap.org/%1/%2/%3.png").arg(zoom_).arg(tileX).arg(tileY));
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
                        query.addQueryItem(QStringLiteral("TILEMATRIX"), QString::number(zoom_));
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
                    enqueueTileRequest(key, request);
                }
            }
        }
        current_visible_tile_keys_ = visibleKeys;
        pruneQueuedTileRequests();
        processTileRequestQueue();
        failed_tiles_ = failed_tiles_.intersect(current_visible_tile_keys_);
        updateLoadFeedback();
    }

    void drawTiles(QPainter& painter, const QRectF& mapRect)
    {
        const QPointF topLeft = center_world_pixel_ - QPointF(mapRect.width() * 0.5, mapRect.height() * 0.5);
        const QPointF bottomRight = center_world_pixel_ + QPointF(mapRect.width() * 0.5, mapRect.height() * 0.5);
        const int tileCount = 1 << zoom_;

        const int minTileX = std::max(0, static_cast<int>(std::floor(topLeft.x() / kTileSize)));
        const int maxTileX = std::min(tileCount - 1, static_cast<int>(std::floor(bottomRight.x() / kTileSize)));
        const int minTileY = std::max(0, static_cast<int>(std::floor(topLeft.y() / kTileSize)));
        const int maxTileY = std::min(tileCount - 1, static_cast<int>(std::floor(bottomRight.y() / kTileSize)));

        for (int tileX = minTileX; tileX <= maxTileX; ++tileX)
        {
            for (int tileY = minTileY; tileY <= maxTileY; ++tileY)
            {
                const QRectF tileRect(
                    mapRect.left() + tileX * kTileSize - topLeft.x(),
                    mapRect.top() + tileY * kTileSize - topLeft.y(),
                    kTileSize,
                    kTileSize);
                bool drewTile = false;
                const QVector<TileLayerSpec> layers = tileLayerSpecs(tile_provider_);
                for (const TileLayerSpec& layer : layers)
                {
                    const QString key = QStringLiteral("%1:%2:%3")
                        .arg(tileProviderKey(tile_provider_), layer.cache_suffix, tileKey(zoom_, tileX, tileY));
                    if (!tile_cache_.contains(key))
                    {
                        continue;
                    }
                    painter.drawPixmap(tileRect.toRect(), tile_cache_.value(key));
                    drewTile = true;
                }
                if (!drewTile)
                {
                    painter.fillRect(tileRect, appThemeColor(AppThemeColor::MapTileBackground, false));
                    painter.setPen(QPen(appThemeColor(AppThemeColor::MapTileBorder, false), 1));
                    painter.drawRect(tileRect);
                }
            }
        }
    }

    void drawFallbackGrid(QPainter& painter, const QRectF& mapRect)
    {
        painter.save();
        painter.setPen(QPen(appThemeColor(AppThemeColor::MapGrid, false), 1));
        for (int i = 1; i < 6; ++i)
        {
            const qreal x = mapRect.left() + mapRect.width() * i / 6.0;
            const qreal y = mapRect.top() + mapRect.height() * i / 6.0;
            painter.drawLine(QPointF(x, mapRect.top()), QPointF(x, mapRect.bottom()));
            painter.drawLine(QPointF(mapRect.left(), y), QPointF(mapRect.right(), y));
        }
        painter.restore();
    }

    QPointF worldToScreen(const QPointF& worldPixel, const QRectF& mapRect) const
    {
        const QPointF topLeft = center_world_pixel_ - QPointF(mapRect.width() * 0.5, mapRect.height() * 0.5);
        return QPointF(
            mapRect.left() + (worldPixel.x() - topLeft.x()),
            mapRect.top() + (worldPixel.y() - topLeft.y()));
    }

    void drawTrack(QPainter& painter, const QRectF& mapRect)
    {
        QVector<QPointF> polyline;
        polyline.reserve(track_points_.size());
        bool hasPeakRange = false;
        float minPeak = std::numeric_limits<float>::max();
        float maxPeak = std::numeric_limits<float>::lowest();
        for (const RtkTrackPoint& point : track_points_)
        {
            polyline.push_back(worldToScreen(latLonToPixel(point.latitude, point.longitude, zoom_), mapRect));
            if (point.has_peak_value)
            {
                hasPeakRange = true;
                minPeak = std::min(minPeak, point.peak_value);
                maxPeak = std::max(maxPeak, point.peak_value);
            }
        }

        painter.save();
        painter.setClipRect(mapRect);
        if (polyline.size() >= 2)
        {
            for (int index = 0; index + 1 < polyline.size(); ++index)
            {
                QColor segmentColor = defaultTrackColor();
                const RtkTrackPoint& firstPoint = track_points_.at(index);
                const RtkTrackPoint& secondPoint = track_points_.at(index + 1);
                if (hasPeakRange)
                {
                    if (firstPoint.has_peak_value && secondPoint.has_peak_value)
                    {
                        segmentColor = trackHeatColor((firstPoint.peak_value + secondPoint.peak_value) * 0.5f, minPeak, maxPeak);
                    }
                    else if (firstPoint.has_peak_value)
                    {
                        segmentColor = trackHeatColor(firstPoint.peak_value, minPeak, maxPeak);
                    }
                    else if (secondPoint.has_peak_value)
                    {
                        segmentColor = trackHeatColor(secondPoint.peak_value, minPeak, maxPeak);
                    }
                }

                painter.setPen(QPen(segmentColor, 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawLine(polyline.at(index), polyline.at(index + 1));
            }
        }

        if (!polyline.isEmpty())
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(appThemeColor(AppThemeColor::TrackStart, false));
            painter.drawEllipse(polyline.first(), 5.0, 5.0);
            painter.setBrush(appThemeColor(AppThemeColor::TrackEnd, false));
            painter.drawEllipse(polyline.last(), 5.0, 5.0);
        }
        if (selected_track_index_ >= 0 && selected_track_index_ < polyline.size())
        {
            const QPointF selectedPoint = polyline.at(selected_track_index_);
            painter.setPen(QPen(appThemeColor(AppThemeColor::TrackEnd, false), 3.0));
            painter.setBrush(appThemeColor(AppThemeColor::MapViewport, false));
            painter.drawEllipse(selectedPoint, 9.0, 9.0);
            painter.setPen(Qt::NoPen);
            painter.setBrush(appThemeColor(AppThemeColor::TrackEnd, false));
            painter.drawEllipse(selectedPoint, 4.0, 4.0);
        }
        painter.restore();

        painter.setPen(QPen(appThemeColor(AppThemeColor::MapBoundary, false), 1));
        painter.drawRoundedRect(mapRect, 8.0, 8.0);
    }

    int closestTrackPointIndex(const QPointF& pos) const
    {
        if (track_points_.isEmpty())
        {
            return -1;
        }

        const QRectF mapRect = rect().adjusted(kMapMargin, kMapMargin, -kMapMargin, -kMapMargin - 18);
        if (!mapRect.adjusted(-8.0, -8.0, 8.0, 8.0).contains(pos))
        {
            return -1;
        }

        int bestIndex = -1;
        double bestDistanceSquared = std::numeric_limits<double>::infinity();
        for (int index = 0; index < track_points_.size(); ++index)
        {
            const RtkTrackPoint& point = track_points_.at(index);
            const QPointF screenPoint = worldToScreen(latLonToPixel(point.latitude, point.longitude, zoom_), mapRect);
            const QPointF delta = screenPoint - pos;
            const double distanceSquared = delta.x() * delta.x() + delta.y() * delta.y();
            if (distanceSquared < bestDistanceSquared)
            {
                bestDistanceSquared = distanceSquared;
                bestIndex = index;
            }
        }

        return bestDistanceSquared <= 18.0 * 18.0 ? bestIndex : -1;
    }

    void adjustZoom(int delta)
    {
        if (track_points_.isEmpty() || delta == 0)
        {
            return;
        }

        const int newZoom = std::clamp(zoom_ + delta, kMinZoom, providerMaxZoom(tile_provider_));
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
        requestVisibleTiles();
        update();
    }

    double pixelToLongitude(const QPointF& pixel, int zoom) const
    {
        const double worldSize = static_cast<double>(kTileSize) * static_cast<double>(1 << zoom);
        return pixel.x() / worldSize * 360.0 - 180.0;
    }

    double pixelToLatitude(const QPointF& pixel, int zoom) const
    {
        const double worldSize = static_cast<double>(kTileSize) * static_cast<double>(1 << zoom);
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
    QHash<QString, QPixmap> tile_cache_;
    QSet<QString> pending_tiles_;
    QQueue<TileFetchRequest> queued_tile_requests_;
    QSet<QNetworkReply*> active_tile_replies_;
    QSet<QString> current_visible_tile_keys_;
    QSet<QString> failed_tiles_;
    bool is_english_;
    TileProvider tile_provider_;
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
    int selected_track_index_;
    int active_tile_request_count_;
    int tile_request_generation_;
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
    , legend_label_(new QLabel(this))
    , detail_label_(new QLabel(this))
    , map_status_label_(new QLabel(this))
    , map_progress_bar_(new QProgressBar(this))
    , map_widget_(new TrajectoryMapWidget(this))
    , timeline_slider_(new QSlider(Qt::Horizontal, this))
    , play_button_(new QPushButton(this))
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
    , english_track_label_(QStringLiteral("RTK trajectory"))
    , chinese_track_label_(QStringLiteral("RTK轨迹"))
    , track_points_()
    , track_stats_()
    , selected_track_index_(-1)
    , playback_timer_(new QTimer(this))
{
    setWindowFlag(Qt::Window, true);
    setModal(false);
    resize(1080, 680);

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(12);

    auto *sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("trajectoryViewerSidebar"));
    sidebar->setMinimumWidth(280);
    sidebar->setMaximumWidth(340);
    sidebar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(8);

    auto *mapPanel = new QWidget(this);
    mapPanel->setObjectName(QStringLiteral("trajectoryViewerMapPanel"));
    mapPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *mapPanelLayout = new QVBoxLayout(mapPanel);
    mapPanelLayout->setContentsMargins(0, 0, 0, 0);
    mapPanelLayout->setSpacing(8);

    map_widget_->setObjectName(QStringLiteral("trajectoryViewerMap"));
    summary_label_->setWordWrap(true);
    summary_label_->setObjectName(QStringLiteral("fieldLabel"));
    sidebarLayout->addWidget(summary_label_);
    legend_label_->setWordWrap(true);
    legend_label_->setTextFormat(Qt::RichText);
    legend_label_->setObjectName(QStringLiteral("fieldLabel"));
    detail_label_->setWordWrap(true);
    detail_label_->setObjectName(QStringLiteral("fieldLabel"));
    detail_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sidebarLayout->addWidget(detail_label_);
    map_status_label_->setWordWrap(true);
    map_status_label_->setObjectName(QStringLiteral("fieldLabel"));
    sidebarLayout->addWidget(map_status_label_);
    map_progress_bar_->setTextVisible(true);
    map_progress_bar_->setMinimum(0);
    map_progress_bar_->setMaximum(1);
    map_progress_bar_->setValue(0);
    sidebarLayout->addWidget(map_progress_bar_);

    auto *timelineLayout = new QHBoxLayout();
    timelineLayout->setContentsMargins(0, 0, 0, 0);
    timelineLayout->setSpacing(8);
    timeline_slider_->setEnabled(false);
    timeline_slider_->setRange(0, 0);
    timeline_slider_->setTracking(true);
    play_button_->setEnabled(false);
    export_button_->setEnabled(false);
    copy_point_button_->setEnabled(false);
    timelineLayout->addWidget(play_button_);
    timelineLayout->addWidget(timeline_slider_, 1);
    timelineLayout->addWidget(copy_point_button_);
    timelineLayout->addWidget(export_button_);
    sidebarLayout->addLayout(timelineLayout);

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
        setSelectedTrackIndex(index, true);
    });

    map_source_combo_->setObjectName(QStringLiteral("trajectoryMapSourceCombo"));
    map_source_combo_->setFixedWidth(160);
    map_source_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    map_source_combo_->setToolTip(is_english_ ? QStringLiteral("Map source") : QStringLiteral("底图来源"));

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

    mapPanelLayout->addWidget(legend_label_);
    mapPanelLayout->addWidget(map_widget_, 1);
    mainLayout->addWidget(sidebar);
    mainLayout->addWidget(mapPanel, 1);

    connect(map_source_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TrajectoryViewerDialog::applyMapSourceSelection);
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
    connect(timeline_slider_, &QSlider::valueChanged,
            this, &TrajectoryViewerDialog::onTimelineChanged);
    connect(play_button_, &QPushButton::clicked,
            this, &TrajectoryViewerDialog::togglePlayback);
    connect(export_button_, &QPushButton::clicked,
            this, &TrajectoryViewerDialog::exportTrackCsv);
    connect(copy_point_button_, &QPushButton::clicked,
            this, &TrajectoryViewerDialog::copySelectedPoint);
    connect(playback_timer_, &QTimer::timeout,
            this, &TrajectoryViewerDialog::advancePlayback);
    playback_timer_->setInterval(350);

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
    updateTexts();
    updateSelectedPointDetails();
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
    static_cast<TrajectoryMapWidget*>(map_widget_)->setTrackPoints(points);
    selected_track_index_ = track_points_.isEmpty()
        ? -1
        : std::clamp(selected_track_index_, 0, static_cast<int>(track_points_.size()) - 1);
    {
        QSignalBlocker blocker(timeline_slider_);
        timeline_slider_->setEnabled(!track_points_.isEmpty());
        timeline_slider_->setRange(track_points_.isEmpty() ? 0 : 1,
                                   track_points_.isEmpty() ? 0 : static_cast<int>(track_points_.size()));
        timeline_slider_->setValue(selected_track_index_ >= 0 ? selected_track_index_ + 1 : (track_points_.isEmpty() ? 0 : 1));
    }
    if (!track_points_.isEmpty() && selected_track_index_ < 0)
    {
        selected_track_index_ = 0;
    }
    static_cast<TrajectoryMapWidget*>(map_widget_)->setSelectedTrackIndex(selected_track_index_);
    play_button_->setEnabled(!track_points_.isEmpty());
    export_button_->setEnabled(!track_points_.isEmpty());
    copy_point_button_->setEnabled(!track_points_.isEmpty());
    updateSummary();
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
    if (timeline_slider_)
    {
        QSignalBlocker blocker(timeline_slider_);
        timeline_slider_->setValue(selected_track_index_ >= 0 ? selected_track_index_ + 1 : 0);
    }
    updateSelectedPointDetails();
    if (notifySession && selected_track_index_ >= 0)
    {
        emit trackPointActivated(selected_track_index_);
    }
}

void TrajectoryViewerDialog::onTimelineChanged(int value)
{
    if (track_points_.isEmpty() || value <= 0)
    {
        setSelectedTrackIndex(-1, false);
        return;
    }
    setSelectedTrackIndex(value - 1, true);
}

void TrajectoryViewerDialog::togglePlayback()
{
    if (track_points_.isEmpty())
    {
        return;
    }
    if (playback_timer_->isActive())
    {
        playback_timer_->stop();
    }
    else
    {
        if (selected_track_index_ < 0 || selected_track_index_ >= track_points_.size() - 1)
        {
            setSelectedTrackIndex(0, true);
        }
        playback_timer_->start();
    }
    updateTexts();
}

void TrajectoryViewerDialog::advancePlayback()
{
    if (track_points_.isEmpty())
    {
        playback_timer_->stop();
        updateTexts();
        return;
    }

    const int nextIndex = selected_track_index_ < 0 ? 0 : selected_track_index_ + 1;
    if (nextIndex >= track_points_.size())
    {
        playback_timer_->stop();
        updateTexts();
        return;
    }
    setSelectedTrackIndex(nextIndex, true);
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

    detail_label_->setText(QString(is_english_
        ? "Selected #%1/%2 | CSV row %3 | %4 | lat %5 lon %6 | height %7 | distance %8 | speed %9 | peak %10 | waveform %11"
        : "当前 #%1/%2 | CSV 第 %3 行 | %4 | 纬度 %5 经度 %6 | 高度 %7 | 里程 %8 | 速度 %9 | 峰值 %10 | 波形 %11")
        .arg(selected_track_index_ + 1)
        .arg(track_points_.size())
        .arg(point.csv_row >= 0 ? point.csv_row + 1 : 0)
        .arg(formatTimestampUs(point.timestamp_us))
        .arg(QString::number(point.latitude, 'f', 8))
        .arg(QString::number(point.longitude, 'f', 8))
        .arg(heightText)
        .arg(formatDistanceMeters(point.cumulative_distance_m))
        .arg(speedText)
        .arg(peakText)
        .arg(waveformText));
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
        updateTitleBarIcons();
    }
}

void TrajectoryViewerDialog::updateTitleBarIcons()
{
    const bool dark = isDarkPalette();
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
}

void TrajectoryViewerDialog::updateSummary()
{
    if (track_points_.isEmpty())
    {
        summary_label_->setText(is_english_
            ? QStringLiteral("No valid latitude/longitude samples were found for %1 in this session.").arg(english_track_label_)
            : QStringLiteral("当前会话中没有找到%1的有效经纬度轨迹点。").arg(chinese_track_label_));
        legend_label_->setText(is_english_
            ? QStringLiteral("Legend: no valid peak values are available for heatmap coloring.")
            : QStringLiteral("图例：当前没有可用于热力着色的有效峰值。"));
        updateSelectedPointDetails();
        return;
    }

    double minLat = std::numeric_limits<double>::infinity();
    double maxLat = -std::numeric_limits<double>::infinity();
    double minLon = std::numeric_limits<double>::infinity();
    double maxLon = -std::numeric_limits<double>::infinity();
    double minHeight = std::numeric_limits<double>::infinity();
    double maxHeight = -std::numeric_limits<double>::infinity();
    double minPeak = std::numeric_limits<double>::infinity();
    double maxPeak = -std::numeric_limits<double>::infinity();
    bool hasHeightRange = false;
    bool hasPeakRange = false;
    double totalDistance = 0.0;
    double maxSpeed = 0.0;
    int speedCount = 0;
    int peakCount = 0;
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
        if (point.has_peak_value && std::isfinite(point.peak_value))
        {
            hasPeakRange = true;
            ++peakCount;
            minPeak = std::min(minPeak, static_cast<double>(point.peak_value));
            maxPeak = std::max(maxPeak, static_cast<double>(point.peak_value));
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

    const QString heightRangeText = hasHeightRange
        ? (is_english_
            ? QStringLiteral(" Height %1 to %2 m, change %3 m.")
                  .arg(QString::number(minHeight, 'f', 3),
                       QString::number(maxHeight, 'f', 3),
                       QString::number(maxHeight - minHeight, 'f', 3))
            : QStringLiteral("高度范围 %1 到 %2 m，变化区间 %3 m。")
                  .arg(QString::number(minHeight, 'f', 3),
                       QString::number(maxHeight, 'f', 3),
                       QString::number(maxHeight - minHeight, 'f', 3)))
        : QString();

    const int rejectedTotal = track_stats_.rejected_invalid_nav +
        track_stats_.rejected_bad_fix +
        track_stats_.rejected_zero_coordinate +
        track_stats_.rejected_out_of_range +
        track_stats_.rejected_jump;
    const QString qualityText = QString(is_english_
        ? " Source rows %1, accepted %2, rejected %3 (invalid %4, fix %5, zero %6, range %7, jumps>%8m %9)."
        : "来源行 %1，保留 %2，剔除 %3（无效 %4、定位 %5、零点 %6、越界 %7、跳点>%8m %9）。")
        .arg(track_stats_.scanned_rows)
        .arg(track_stats_.accepted_points)
        .arg(rejectedTotal)
        .arg(track_stats_.rejected_invalid_nav)
        .arg(track_stats_.rejected_bad_fix)
        .arg(track_stats_.rejected_zero_coordinate)
        .arg(track_stats_.rejected_out_of_range)
        .arg(QString::number(track_stats_.jump_threshold_m, 'f', 1))
        .arg(track_stats_.rejected_jump);

    summary_label_->setText(QString(is_english_
        ? "Showing %1 %2 points. Distance %3, max speed %4 from %5 speed samples. Latitude %6 to %7, longitude %8 to %9.%10%11"
        : "正在显示 %1 个%2点。里程 %3，最大速度 %4（%5 个速度样本）。纬度范围 %6 到 %7，经度范围 %8 到 %9。%10%11")
        .arg(track_points_.size())
        .arg(is_english_ ? english_track_label_ : chinese_track_label_)
        .arg(formatDistanceMeters(totalDistance))
        .arg(speedCount > 0 ? formatSpeed(maxSpeed) : QStringLiteral("--"))
        .arg(speedCount)
        .arg(QString::number(minLat, 'f', 7))
        .arg(QString::number(maxLat, 'f', 7))
        .arg(QString::number(minLon, 'f', 7))
        .arg(QString::number(maxLon, 'f', 7))
        .arg(heightRangeText)
        .arg(qualityText));

    if (!hasPeakRange)
    {
        legend_label_->setText(is_english_
            ? QStringLiteral("Legend: no valid peak values remain after filtering, so the trajectory falls back to the default blue line.")
            : QStringLiteral("图例：过滤后没有剩余有效峰值，轨迹将回退为默认蓝线。"));
        updateSelectedPointDetails();
        return;
    }

    const double totalRange = maxPeak - minPeak;
    const int bandCount = 7;
    const double section = totalRange / static_cast<double>(bandCount);
    QStringList legendEntries;
    legendEntries.reserve(bandCount);
    for (int index = 0; index < bandCount; ++index)
    {
        const double startValue = minPeak + section * index;
        const double endValue = (index == bandCount - 1) ? maxPeak : (minPeak + section * (index + 1));
        const QString bandText = is_english_
            ? QStringLiteral("Band %1: %2 to %3").arg(index + 1).arg(formatPeakValue(startValue)).arg(formatPeakValue(endValue))
            : QStringLiteral("%1段: %2 到 %3").arg(index + 1).arg(formatPeakValue(startValue)).arg(formatPeakValue(endValue));
        const QString bandColor = heatmapColorAt((index + 0.5) / static_cast<double>(bandCount)).name();
        legendEntries.push_back(QStringLiteral("<span style=\"color:%1; font-weight:600;\">■</span> %2").arg(bandColor, bandText));
    }
    legend_label_->setText(QString(is_english_
        ? "Peak heatmap (%1 matched points): "
        : "峰值热力图（%1 个匹配点）：").arg(peakCount) +
        legendEntries.join(QStringLiteral("&nbsp;&nbsp;&nbsp;")));
    updateSelectedPointDetails();
}

void TrajectoryViewerDialog::updateTexts()
{
    setWindowTitle(is_english_
        ? QStringLiteral("%1 Viewer").arg(english_track_label_)
        : QStringLiteral("%1查看").arg(chinese_track_label_));
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
    play_button_->setText(playback_timer_->isActive()
        ? (is_english_ ? QStringLiteral("Pause") : QStringLiteral("暂停"))
        : (is_english_ ? QStringLiteral("Play") : QStringLiteral("播放")));
    copy_point_button_->setText(is_english_ ? QStringLiteral("Copy Point") : QStringLiteral("复制点"));
    export_button_->setText(is_english_ ? QStringLiteral("Export CSV") : QStringLiteral("导出 CSV"));
    timeline_slider_->setToolTip(is_english_
        ? QStringLiteral("Scrub along accepted trajectory points and sync the selected point back to CSV/waveform views.")
        : QStringLiteral("沿有效轨迹点拖动，并同步定位到 CSV / 波形视图。"));
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
    updateTitleBarIcons();
}
