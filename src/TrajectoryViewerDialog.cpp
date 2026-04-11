#include "TrajectoryViewerDialog.h"

#include <QHBoxLayout>
#include <QComboBox>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QProgressBar>
#include <QSet>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWidget>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
constexpr int kTileSize = 256;
constexpr int kDefaultZoom = 16;
constexpr int kMaxZoom = 19;
constexpr int kMinZoom = 1;
constexpr int kMapMargin = 12;

enum class TileProvider
{
    OpenStreetMap,
    TianDiTu
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
    case TileProvider::TianDiTu:
        return QStringLiteral("tianditu");
    case TileProvider::OpenStreetMap:
    default:
        return QStringLiteral("osm");
    }
}

QString mapAttributionText(TileProvider provider, bool english)
{
    switch (provider)
    {
    case TileProvider::TianDiTu:
        return english ? QStringLiteral("Map data © Tianditu") : QStringLiteral("底图数据 © 天地图");
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
        , drag_start_pos_()
        , drag_start_center_world_pixel_()
        , english_track_label_(QStringLiteral("RTK trajectory"))
        , chinese_track_label_(QStringLiteral("RTK轨迹"))
    {
        setMinimumSize(720, 420);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
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

    void setEnglish(bool english)
    {
        is_english_ = english;
        updateLoadFeedback();
        update();
    }

    void setTrackPoints(const QVector<RtkTrackPoint>& points)
    {
        track_points_ = points;
        manual_view_active_ = false;
        tile_cache_.clear();
        pending_tiles_.clear();
        failed_tiles_.clear();
        last_tile_error_.clear();
        refreshViewport();
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
        tile_cache_.clear();
        pending_tiles_.clear();
        failed_tiles_.clear();
        last_tile_error_.clear();
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
        if (tile_provider_ == TileProvider::TianDiTu)
        {
            tile_cache_.clear();
            pending_tiles_.clear();
            failed_tiles_.clear();
            last_tile_error_.clear();
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
            dragging_ = false;
            unsetCursor();
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
        painter.fillRect(rect(), QColor("#f7fafc"));

        const QRectF mapRect = rect().adjusted(kMapMargin, kMapMargin, -kMapMargin, -kMapMargin - 18);
        painter.fillRect(mapRect, QColor("#eef4fb"));

        if (track_points_.isEmpty())
        {
            painter.setPen(QColor("#718096"));
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

        painter.setPen(QColor("#4a5568"));
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
    void refreshViewport()
    {
        if (track_points_.isEmpty())
        {
            zoom_ = kDefaultZoom;
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
            fit_zoom_ = kDefaultZoom;
            fit_center_world_pixel_ = latLonToPixel(track_points_.first().latitude, track_points_.first().longitude, fit_zoom_);
            if (!manual_view_active_)
            {
                zoom_ = fit_zoom_;
                center_world_pixel_ = fit_center_world_pixel_;
            }
            requestVisibleTiles();
            return;
        }

        for (int candidateZoom = kMaxZoom; candidateZoom >= kMinZoom; --candidateZoom)
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
        if (tile_provider_ == TileProvider::TianDiTu && tianditu_key_.trimmed().isEmpty())
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
                const QString key = QStringLiteral("%1:%2").arg(tileProviderKey(tile_provider_), tileKey(zoom_, tileX, tileY));
                visibleKeys.insert(key);
                if (tile_cache_.contains(key) || pending_tiles_.contains(key))
                {
                    continue;
                }

                pending_tiles_.insert(key);
                QUrl tileUrl;
                if (tile_provider_ == TileProvider::TianDiTu)
                {
                    tileUrl = QUrl(QStringLiteral("https://t0.tianditu.gov.cn/vec_w/wmts"));
                    QUrlQuery query;
                    query.addQueryItem(QStringLiteral("SERVICE"), QStringLiteral("WMTS"));
                    query.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("GetTile"));
                    query.addQueryItem(QStringLiteral("VERSION"), QStringLiteral("1.0.0"));
                    query.addQueryItem(QStringLiteral("LAYER"), QStringLiteral("vec"));
                    query.addQueryItem(QStringLiteral("STYLE"), QStringLiteral("default"));
                    query.addQueryItem(QStringLiteral("TILEMATRIXSET"), QStringLiteral("w"));
                    query.addQueryItem(QStringLiteral("FORMAT"), QStringLiteral("tiles"));
                    query.addQueryItem(QStringLiteral("TILEMATRIX"), QString::number(zoom_));
                    query.addQueryItem(QStringLiteral("TILEROW"), QString::number(tileY));
                    query.addQueryItem(QStringLiteral("TILECOL"), QString::number(tileX));
                    query.addQueryItem(QStringLiteral("tk"), tianditu_key_.trimmed());
                    tileUrl.setQuery(query);
                }
                else
                {
                    tileUrl = QUrl(QStringLiteral("https://tile.openstreetmap.org/%1/%2/%3.png").arg(zoom_).arg(tileX).arg(tileY));
                }
                QNetworkRequest request(tileUrl);
                if (tile_provider_ == TileProvider::TianDiTu)
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
                QNetworkReply *reply = manager_->get(request);
                QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
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
                    reply->deleteLater();
                    updateLoadFeedback();
                    update();
                });
            }
        }
        current_visible_tile_keys_ = visibleKeys;
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
                const QString key = QStringLiteral("%1:%2").arg(tileProviderKey(tile_provider_), tileKey(zoom_, tileX, tileY));
                const QRectF tileRect(
                    mapRect.left() + tileX * kTileSize - topLeft.x(),
                    mapRect.top() + tileY * kTileSize - topLeft.y(),
                    kTileSize,
                    kTileSize);
                if (tile_cache_.contains(key))
                {
                    painter.drawPixmap(tileRect.toRect(), tile_cache_.value(key));
                }
                else
                {
                    painter.fillRect(tileRect, QColor("#edf2f7"));
                    painter.setPen(QPen(QColor("#d7dee7"), 1));
                    painter.drawRect(tileRect);
                }
            }
        }
    }

    void drawFallbackGrid(QPainter& painter, const QRectF& mapRect)
    {
        painter.save();
        painter.setPen(QPen(QColor(255, 255, 255, 120), 1));
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
        QPolygonF polyline;
        polyline.reserve(track_points_.size());
        for (const RtkTrackPoint& point : track_points_)
        {
            polyline.push_back(worldToScreen(latLonToPixel(point.latitude, point.longitude, zoom_), mapRect));
        }

        painter.save();
        painter.setClipRect(mapRect);
        painter.setPen(QPen(QColor("#2563eb"), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (polyline.size() >= 2)
        {
            painter.drawPolyline(polyline);
        }

        if (!polyline.isEmpty())
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#16a34a"));
            painter.drawEllipse(polyline.first(), 5.0, 5.0);
            painter.setBrush(QColor("#dc2626"));
            painter.drawEllipse(polyline.last(), 5.0, 5.0);
        }
        painter.restore();

        painter.setPen(QPen(QColor("#cbd5e1"), 1));
        painter.drawRoundedRect(mapRect, 8.0, 8.0);
    }

    void adjustZoom(int delta)
    {
        if (track_points_.isEmpty() || delta == 0)
        {
            return;
        }

        const int newZoom = std::clamp(zoom_ + delta, kMinZoom, kMaxZoom);
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
        else if (tile_provider_ == TileProvider::TianDiTu && tianditu_key_.trimmed().isEmpty())
        {
            statusText = is_english_
                ? QStringLiteral("Tianditu is selected but no key is configured.")
                : QStringLiteral("当前已选择天地图，但尚未配置 Key。");
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
                .arg(tile_provider_ == TileProvider::TianDiTu
                    ? (is_english_ ? QStringLiteral("Tianditu") : QStringLiteral("天地图"))
                    : QStringLiteral("OSM"))
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
    QPointF drag_start_pos_;
    QPointF drag_start_center_world_pixel_;
    QString english_track_label_;
    QString chinese_track_label_;
    std::function<void(const QString&)> status_callback_;
    std::function<void(int, int, int)> progress_callback_;
};
}

TrajectoryViewerDialog::TrajectoryViewerDialog(QWidget *parent)
    : QDialog(parent)
    , summary_label_(new QLabel(this))
    , map_status_label_(new QLabel(this))
    , map_progress_bar_(new QProgressBar(this))
    , map_widget_(new TrajectoryMapWidget(this))
    , map_source_combo_(new QComboBox(this))
    , zoom_in_button_(new QPushButton(this))
    , zoom_out_button_(new QPushButton(this))
    , reset_view_button_(new QPushButton(this))
    , delete_tianditu_key_button_(new QPushButton(this))
    , close_button_(new QPushButton(this))
    , is_english_(false)
    , english_track_label_(QStringLiteral("RTK trajectory"))
    , chinese_track_label_(QStringLiteral("RTK轨迹"))
    , track_points_()
{
    setModal(false);
    resize(920, 640);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    summary_label_->setWordWrap(true);
    summary_label_->setObjectName(QStringLiteral("fieldLabel"));
    mainLayout->addWidget(summary_label_);
    map_status_label_->setWordWrap(true);
    map_status_label_->setObjectName(QStringLiteral("fieldLabel"));
    mainLayout->addWidget(map_status_label_);
    map_progress_bar_->setTextVisible(true);
    map_progress_bar_->setMinimum(0);
    map_progress_bar_->setMaximum(1);
    map_progress_bar_->setValue(0);
    mainLayout->addWidget(map_progress_bar_);
    mainLayout->addWidget(map_widget_, 1);

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

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(map_source_combo_);
    connect(map_source_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const TileProvider selectedProvider = index == 1 ? TileProvider::TianDiTu : TileProvider::OpenStreetMap;
        QSettings settings("VaporView", "TrajectoryViewer");
        auto *mapWidget = static_cast<TrajectoryMapWidget*>(map_widget_);
        if (selectedProvider == TileProvider::TianDiTu)
        {
            QString tiandituKey = settings.value(tiandituKeySettingKey()).toString().trimmed();
            if (tiandituKey.isEmpty())
            {
                bool ok = false;
                tiandituKey = QInputDialog::getText(
                    this,
                    is_english_ ? QStringLiteral("Tianditu Key") : QStringLiteral("天地图 Key"),
                    is_english_
                        ? QStringLiteral("Enter a Tianditu tile key before switching map source:")
                        : QStringLiteral("切换到天地图前，请先输入天地图瓦片 Key："),
                    QLineEdit::Normal,
                    QString(),
                    &ok).trimmed();
                if (!ok || tiandituKey.isEmpty())
                {
                    QSignalBlocker blocker(map_source_combo_);
                    map_source_combo_->setCurrentIndex(0);
                    return;
                }
                settings.setValue(tiandituKeySettingKey(), tiandituKey);
            }
            mapWidget->setTianDiTuKey(tiandituKey);
            mapWidget->setTileProvider(TileProvider::TianDiTu);
            settings.setValue(tileProviderSettingKey(), QStringLiteral("tianditu"));
        }
        else
        {
            mapWidget->setTileProvider(TileProvider::OpenStreetMap);
            settings.setValue(tileProviderSettingKey(), QStringLiteral("osm"));
        }
        updateTexts();
    });
    connect(zoom_in_button_, &QPushButton::clicked, this, [this]() {
        static_cast<TrajectoryMapWidget*>(map_widget_)->zoomIn();
    });
    connect(zoom_out_button_, &QPushButton::clicked, this, [this]() {
        static_cast<TrajectoryMapWidget*>(map_widget_)->zoomOut();
    });
    connect(reset_view_button_, &QPushButton::clicked, this, [this]() {
        static_cast<TrajectoryMapWidget*>(map_widget_)->resetView();
    });
    connect(delete_tianditu_key_button_, &QPushButton::clicked, this, [this]() {
        QSettings settings("VaporView", "TrajectoryViewer");
        settings.remove(tiandituKeySettingKey());
        auto *mapWidget = static_cast<TrajectoryMapWidget*>(map_widget_);
        mapWidget->setTianDiTuKey(QString());
        if (mapWidget->tileProvider() == TileProvider::TianDiTu)
        {
            mapWidget->setTileProvider(TileProvider::OpenStreetMap);
            settings.setValue(tileProviderSettingKey(), QStringLiteral("osm"));
            QSignalBlocker blocker(map_source_combo_);
            map_source_combo_->setCurrentIndex(0);
        }
        QMessageBox::information(
            this,
            is_english_ ? QStringLiteral("Map Source") : QStringLiteral("图源设置"),
            is_english_ ? QStringLiteral("The saved Tianditu key has been deleted.")
                        : QStringLiteral("已删除保存的天地图 Key。"));
        updateTexts();
    });
    buttonLayout->addWidget(zoom_in_button_);
    buttonLayout->addWidget(zoom_out_button_);
    buttonLayout->addWidget(reset_view_button_);
    buttonLayout->addWidget(delete_tianditu_key_button_);
    buttonLayout->addStretch(1);
    connect(close_button_, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(close_button_);
    mainLayout->addLayout(buttonLayout);

    {
        QSettings settings("VaporView", "TrajectoryViewer");
        const QString tiandituKey = settings.value(tiandituKeySettingKey()).toString().trimmed();
        mapWidget->setTianDiTuKey(tiandituKey);
        const QString provider = settings.value(tileProviderSettingKey(), QStringLiteral("osm")).toString().trimmed().toLower();
        QSignalBlocker blocker(map_source_combo_);
        map_source_combo_->setCurrentIndex(provider == QStringLiteral("tianditu") ? 1 : 0);
        mapWidget->setTileProvider(
            provider == QStringLiteral("tianditu") && !tiandituKey.isEmpty()
                ? TileProvider::TianDiTu
                : TileProvider::OpenStreetMap);
    }

    updateTexts();
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
    updateSummary();
}

void TrajectoryViewerDialog::updateSummary()
{
    if (track_points_.isEmpty())
    {
        summary_label_->setText(is_english_
            ? QStringLiteral("No valid latitude/longitude samples were found for %1 in this session.").arg(english_track_label_)
            : QStringLiteral("当前会话中没有找到%1的有效经纬度轨迹点。").arg(chinese_track_label_));
        return;
    }

    double minLat = std::numeric_limits<double>::infinity();
    double maxLat = -std::numeric_limits<double>::infinity();
    double minLon = std::numeric_limits<double>::infinity();
    double maxLon = -std::numeric_limits<double>::infinity();
    for (const RtkTrackPoint& point : track_points_)
    {
        minLat = std::min(minLat, point.latitude);
        maxLat = std::max(maxLat, point.latitude);
        minLon = std::min(minLon, point.longitude);
        maxLon = std::max(maxLon, point.longitude);
    }

    summary_label_->setText(QString(is_english_
        ? "Showing %1 %2 points. Latitude %3 to %4, longitude %5 to %6."
        : "正在显示 %1 个%2点。纬度范围 %3 到 %4，经度范围 %5 到 %6。")
        .arg(track_points_.size())
        .arg(is_english_ ? english_track_label_ : chinese_track_label_)
        .arg(QString::number(minLat, 'f', 7))
        .arg(QString::number(maxLat, 'f', 7))
        .arg(QString::number(minLon, 'f', 7))
        .arg(QString::number(maxLon, 'f', 7)));
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
        map_source_combo_->addItem(is_english_ ? QStringLiteral("Tianditu") : QStringLiteral("天地图"));
        map_source_combo_->setCurrentIndex(static_cast<TrajectoryMapWidget*>(map_widget_)->tileProvider() == TileProvider::TianDiTu ? 1 : 0);
    }
    map_progress_bar_->setToolTip(
        is_english_
            ? QStringLiteral("Shows the loading progress of the currently visible base map tiles.")
            : QStringLiteral("显示当前可见区域底图瓦片的加载进度。"));
    zoom_in_button_->setText(is_english_ ? QStringLiteral("Zoom In") : QStringLiteral("放大"));
    zoom_out_button_->setText(is_english_ ? QStringLiteral("Zoom Out") : QStringLiteral("缩小"));
    reset_view_button_->setText(is_english_ ? QStringLiteral("Fit Track") : QStringLiteral("适应轨迹"));
    delete_tianditu_key_button_->setText(is_english_ ? QStringLiteral("Delete Tianditu Key") : QStringLiteral("删除天地图Key"));
    delete_tianditu_key_button_->setToolTip(
        is_english_
            ? QStringLiteral("Delete the saved Tianditu key. If Tianditu is active, the map source falls back to OpenStreetMap.")
            : QStringLiteral("删除已保存的天地图 Key。如果当前正在使用天地图，会自动切回 OpenStreetMap。"));
    close_button_->setText(is_english_ ? QStringLiteral("Close") : QStringLiteral("关闭"));
}
