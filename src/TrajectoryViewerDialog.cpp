#include "TrajectoryViewerDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int kTileSize = 256;
constexpr int kDefaultZoom = 16;
constexpr int kMaxZoom = 19;
constexpr int kMinZoom = 1;
constexpr int kMapMargin = 12;

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

class TrajectoryMapWidget : public QWidget
{
public:
    explicit TrajectoryMapWidget(QWidget *parent = nullptr)
        : QWidget(parent)
        , manager_(new QNetworkAccessManager(this))
        , is_english_(false)
        , zoom_(kDefaultZoom)
        , center_world_pixel_(0.0, 0.0)
    {
        setMinimumSize(720, 420);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setEnglish(bool english)
    {
        is_english_ = english;
        update();
    }

    void setTrackPoints(const QVector<RtkTrackPoint>& points)
    {
        track_points_ = points;
        refreshViewport();
        update();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        refreshViewport();
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
                is_english_ ? QStringLiteral("No RTK trajectory available in this session.")
                            : QStringLiteral("当前会话中没有可用的 RTK 轨迹。"));
            return;
        }

        drawTiles(painter, mapRect);
        drawFallbackGrid(painter, mapRect);
        drawTrack(painter, mapRect);

        painter.setPen(QColor("#4a5568"));
        painter.drawText(QRectF(mapRect.left(), mapRect.bottom() + 2, mapRect.width(), 16),
            Qt::AlignLeft | Qt::AlignVCenter,
            is_english_
                ? QStringLiteral("Map data © OpenStreetMap contributors")
                : QStringLiteral("底图数据 © OpenStreetMap contributors"));
        painter.drawText(QRectF(mapRect.left(), mapRect.bottom() + 2, mapRect.width(), 16),
            Qt::AlignRight | Qt::AlignVCenter,
            QString(is_english_ ? "RTK points: %1" : "RTK 点数: %1").arg(track_points_.size()));
    }

private:
    void refreshViewport()
    {
        if (track_points_.isEmpty())
        {
            zoom_ = kDefaultZoom;
            center_world_pixel_ = QPointF();
            return;
        }

        const QSizeF mapSize = size() - QSize(2 * kMapMargin, 2 * kMapMargin + 18);
        const double availableWidth = std::max(200.0, mapSize.width());
        const double availableHeight = std::max(160.0, mapSize.height());

        if (track_points_.size() == 1)
        {
            zoom_ = kDefaultZoom;
            center_world_pixel_ = latLonToPixel(track_points_.first().latitude, track_points_.first().longitude, zoom_);
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
                zoom_ = candidateZoom;
                center_world_pixel_ = QPointF((minX + maxX) * 0.5, (minY + maxY) * 0.5);
                requestVisibleTiles();
                return;
            }
        }

        zoom_ = kMinZoom;
        double minX = std::numeric_limits<double>::infinity();
        double maxX = -std::numeric_limits<double>::infinity();
        double minY = std::numeric_limits<double>::infinity();
        double maxY = -std::numeric_limits<double>::infinity();
        for (const RtkTrackPoint& point : track_points_)
        {
            const QPointF pixel = latLonToPixel(point.latitude, point.longitude, zoom_);
            minX = std::min(minX, pixel.x());
            maxX = std::max(maxX, pixel.x());
            minY = std::min(minY, pixel.y());
            maxY = std::max(maxY, pixel.y());
        }
        center_world_pixel_ = QPointF((minX + maxX) * 0.5, (minY + maxY) * 0.5);
        requestVisibleTiles();
    }

    void requestVisibleTiles()
    {
        if (track_points_.isEmpty())
        {
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

        for (int tileX = minTileX; tileX <= maxTileX; ++tileX)
        {
            for (int tileY = minTileY; tileY <= maxTileY; ++tileY)
            {
                const QString key = tileKey(zoom_, tileX, tileY);
                if (tile_cache_.contains(key) || pending_tiles_.contains(key))
                {
                    continue;
                }

                pending_tiles_.insert(key);
                QNetworkRequest request(QUrl(QStringLiteral("https://tile.openstreetmap.org/%1/%2/%3.png").arg(zoom_).arg(tileX).arg(tileY)));
                request.setRawHeader("User-Agent", "VaporView/1.0");
                QNetworkReply *reply = manager_->get(request);
                QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
                    pending_tiles_.remove(key);
                    if (!reply->error())
                    {
                        QPixmap tile;
                        tile.loadFromData(reply->readAll());
                        if (!tile.isNull())
                        {
                            tile_cache_.insert(key, tile);
                        }
                    }
                    reply->deleteLater();
                    update();
                });
            }
        }
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
                const QString key = tileKey(zoom_, tileX, tileY);
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

    QNetworkAccessManager *manager_;
    QVector<RtkTrackPoint> track_points_;
    QHash<QString, QPixmap> tile_cache_;
    QSet<QString> pending_tiles_;
    bool is_english_;
    int zoom_;
    QPointF center_world_pixel_;
};
}

TrajectoryViewerDialog::TrajectoryViewerDialog(QWidget *parent)
    : QDialog(parent)
    , summary_label_(new QLabel(this))
    , map_widget_(new TrajectoryMapWidget(this))
    , close_button_(new QPushButton(this))
    , is_english_(false)
{
    setModal(false);
    resize(920, 640);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    summary_label_->setWordWrap(true);
    summary_label_->setObjectName(QStringLiteral("fieldLabel"));
    mainLayout->addWidget(summary_label_);
    mainLayout->addWidget(map_widget_, 1);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);
    connect(close_button_, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(close_button_);
    mainLayout->addLayout(buttonLayout);

    updateTexts();
}

void TrajectoryViewerDialog::setEnglish(bool english)
{
    is_english_ = english;
    static_cast<TrajectoryMapWidget*>(map_widget_)->setEnglish(english);
    updateTexts();
}

void TrajectoryViewerDialog::setTrackPoints(const QVector<RtkTrackPoint>& points)
{
    static_cast<TrajectoryMapWidget*>(map_widget_)->setTrackPoints(points);
    if (points.isEmpty())
    {
        summary_label_->setText(is_english_
            ? QStringLiteral("No valid RTK latitude/longitude samples were found in this session.")
            : QStringLiteral("当前会话中没有找到有效的 RTK 经纬度轨迹点。"));
        return;
    }

    double minLat = std::numeric_limits<double>::infinity();
    double maxLat = -std::numeric_limits<double>::infinity();
    double minLon = std::numeric_limits<double>::infinity();
    double maxLon = -std::numeric_limits<double>::infinity();
    for (const RtkTrackPoint& point : points)
    {
        minLat = std::min(minLat, point.latitude);
        maxLat = std::max(maxLat, point.latitude);
        minLon = std::min(minLon, point.longitude);
        maxLon = std::max(maxLon, point.longitude);
    }

    summary_label_->setText(QString(is_english_
        ? "Showing %1 RTK trajectory points. Latitude %2 to %3, longitude %4 to %5."
        : "正在显示 %1 个 RTK 轨迹点。纬度范围 %2 到 %3，经度范围 %4 到 %5。")
        .arg(points.size())
        .arg(QString::number(minLat, 'f', 7))
        .arg(QString::number(maxLat, 'f', 7))
        .arg(QString::number(minLon, 'f', 7))
        .arg(QString::number(maxLon, 'f', 7)));
}

void TrajectoryViewerDialog::updateTexts()
{
    setWindowTitle(is_english_ ? QStringLiteral("RTK Trajectory Viewer") : QStringLiteral("RTK轨迹查看"));
    close_button_->setText(is_english_ ? QStringLiteral("Close") : QStringLiteral("关闭"));
}
