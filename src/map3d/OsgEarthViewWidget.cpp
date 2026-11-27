#include "MapLanguage.h"

#include "map3d/OsgEarthViewWidget.h"

#include "map3d/Aircraft3DLayer.h"
#include "map3d/AircraftHeading.h"
#include "Map3DAssetLoader.h"
#include "MapLayerRemoval.h"
#include "EarthProjectionProfile.h"
#include "GroundDetailEllipse.h"
#include "OpenAerialMapImageLayer.h"
#include "OnlineOsmLayer.h"
#include "OnlineTerrainLayer.h"
#include "OpenFreeMapBuildings.h"
#include "CesiumTerrain.h"
#include "TiandituImageLayer.h"
#include <osgEarth/TerrainEngineNode>
#include "Map3DRuntime.h"
#include "map3d/Map3DLoadTracker.h"
#include "map3d/TrackSampling.h"
#include "map3d/Trajectory3DLayer.h"
#include "shared/theme/AppTheme.h"

#include <osg/Camera>
#include <osgDB/DatabasePager>
#include <osgEarth/Threading>
#include <osgEarth/Progress>
#include <osg/BoundingSphere>
#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Point>
#include <osg/Group>
#include <osg/Texture2D>
#include <osg/Viewport>
#include <osgGA/EventQueue>
#include <osgGA/TrackballManipulator>
#include <osgViewer/GraphicsWindow>
#include <osgViewer/Viewer>
#include <osgEarth/EarthManipulator>
#include <osgEarth/DecalLayer>
#include <osgEarth/ElevationLayer>
#include <osgEarth/ElevationPool>
#include <osgEarth/GDAL>
#include <osgEarth/GeoData>
#include <osgEarth/GLUtils>
#include <osgEarth/ImageLayer>
#include <osgEarth/Layer>
#include <osgEarth/MapNode>
#include <osgEarth/Map>
#include <osgEarth/Profile>
#include <osgEarth/Registry>
#include <osgEarth/SpatialReference>
#include <osgEarth/TerrainEngineNode>
#include <osgEarth/TerrainOptions>
#include <osgEarth/TerrainTileNode>
#include <osgEarth/VisibleLayer>
#include <osgEarth/Viewpoint>
#include <osgEarth/XYZ>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEnterEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QShowEvent>
#include <QScrollArea>
#include <QThread>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>

namespace VaporView::Map3D {
namespace {

constexpr double kEarthRadiusM = 6378137.0;
constexpr double kWgs84FirstEccentricitySquared = 6.6943799901413165e-3;
constexpr double kDegreesToRadians = 0.017453292519943295;
constexpr unsigned kTiandituMaxZoom = 18;

osg::ref_ptr<osg::Node> createStarfieldNode()
{
    auto* geode = new osg::Geode;
    auto* geometry = new osg::Geometry;
    auto* vertices = new osg::Vec3Array;
    auto* colors = new osg::Vec4Array;
    constexpr double radius = 18'000'000.0;
    constexpr int starCount = 420;
    for (int index = 0; index < starCount; ++index)
    {
        const double a = std::fmod(index * 2.399963229728653, 6.283185307179586);
        const double z = -0.96 + std::fmod(index * 0.618033988749895, 1.92);
        const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
        vertices->push_back(osg::Vec3(radius * r * std::cos(a), radius * r * std::sin(a), radius * z));
        const float brightness = 0.55f + static_cast<float>((index * 37) % 45) / 100.0f;
        colors->push_back(osg::Vec4(brightness, brightness * 0.96f, brightness * 0.86f, 1.0f));
    }
    geometry->setVertexArray(vertices);
    geometry->setColorArray(colors, osg::Array::BIND_PER_VERTEX);
    geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::POINTS, 0, starCount));
    auto* state = geometry->getOrCreateStateSet();
    state->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    state->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    state->setRenderBinDetails(-10, "RenderBin");
    state->setAttribute(new osg::Point(1.7f));
    geode->addDrawable(geometry);
    return geode;
}

constexpr const char* kTiandituSatelliteLayerName = "Tianditu Satellite imagery";
constexpr const char* kOpenAerialMapLayerName = "OpenAerialMap Satellite imagery";
constexpr const char* kEsriLayerName = "Esri World Imagery Satellite imagery";
constexpr const char* kSelectedLocalSatelliteLayerName = "VaporView selected local satellite imagery";
// Match the native imagery tile width so each zoom step requests only the
// detail the viewport can display.
constexpr int kTerrainTilePixelSize = 256;
constexpr int kTrajectoryInfoCardWidth = 380;
constexpr int kTrajectoryInfoCardScreenMargin = 8;

VaporView::Geo::TrajectoryRenderSample renderSampleFromNavSample(
    const VaporView::Geo::NavSample& sample)
{
    VaporView::Geo::TrajectoryRenderSample renderSample;
    renderSample.navigation = sample;
    return renderSample;
}

QString trajectoryFixQualityLabel(VaporView::Geo::FixQuality quality)
{
    switch (quality)
    {
    case VaporView::Geo::FixQuality::Fixed:
        return mapText(QStringLiteral("RTK 固定解"));
    case VaporView::Geo::FixQuality::Float:
        return mapText(QStringLiteral("RTK 浮点解"));
    case VaporView::Geo::FixQuality::Dgps:
        return mapText(QStringLiteral("差分定位"));
    case VaporView::Geo::FixQuality::Single:
        return mapText(QStringLiteral("单点定位"));
    case VaporView::Geo::FixQuality::Invalid:
        return mapText(QStringLiteral("无效定位"));
    case VaporView::Geo::FixQuality::Unknown:
        break;
    }
    return mapText(QStringLiteral("定位质量未知"));
}

VaporView::AppThemeColor trajectoryFixQualityColor(VaporView::Geo::FixQuality quality)
{
    switch (quality)
    {
    case VaporView::Geo::FixQuality::Fixed:
        return VaporView::AppThemeColor::Success;
    case VaporView::Geo::FixQuality::Float:
        return VaporView::AppThemeColor::Warning;
    case VaporView::Geo::FixQuality::Dgps:
        return VaporView::AppThemeColor::Primary;
    case VaporView::Geo::FixQuality::Single:
        return VaporView::AppThemeColor::ToolbarAmber;
    case VaporView::Geo::FixQuality::Invalid:
        return VaporView::AppThemeColor::Danger;
    case VaporView::Geo::FixQuality::Unknown:
        break;
    }
    return VaporView::AppThemeColor::TextMuted;
}

QString trajectoryHeightReferenceLabel(VaporView::Geo::HeightReference reference)
{
    switch (reference)
    {
    case VaporView::Geo::HeightReference::Wgs84Ellipsoid:
        return mapText(QStringLiteral("WGS84 椭球高"));
    case VaporView::Geo::HeightReference::MeanSeaLevel:
        return mapText(QStringLiteral("平均海平面"));
    case VaporView::Geo::HeightReference::Egm2008:
        return QStringLiteral("EGM2008");
    case VaporView::Geo::HeightReference::LocalNed:
        return mapText(QStringLiteral("局部 NED"));
    case VaporView::Geo::HeightReference::Unknown:
        break;
    }
    return mapText(QStringLiteral("高程基准未知"));
}

class TrajectorySampleInfoCard final : public QFrame {
public:
    static constexpr int kOuterMargin = 4;
    static constexpr int kArrowHeight = 10;
    static constexpr int kArrowHalfWidth = 9;
    static constexpr qreal kCornerRadius = 10.0;

    explicit TrajectorySampleInfoCard(QWidget* parent)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("map3DTrajectoryInfoCard"));
        setAccessibleName(mapText(QStringLiteral("轨迹点详细信息")));
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_Hover, true);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
        setProperty("billboardMode", QStringLiteral("screen-space"));
        setProperty("pointerDirection", QStringLiteral("down"));
        setProperty("cardHovered", false);
        setFrameShape(QFrame::NoFrame);
        setMinimumWidth(kTrajectoryInfoCardWidth + kOuterMargin * 2);
        setMaximumWidth(kTrajectoryInfoCardWidth + kOuterMargin * 2);

        layout_ = new QVBoxLayout(this);
        layout_->setSpacing(10);
        updateLayoutMargins();

        auto* headerLayout = new QHBoxLayout;
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(10);
        title_label_ = new QLabel(this);
        title_label_->setObjectName(QStringLiteral("map3DTrajectoryInfoTitle"));
        QFont titleFont = title_label_->font();
        titleFont.setWeight(QFont::DemiBold);
        if (titleFont.pointSizeF() > 0.0)
        {
            titleFont.setPointSizeF(titleFont.pointSizeF() + 1.0);
        }
        title_label_->setFont(titleFont);
        quality_label_ = new QLabel(this);
        quality_label_->setObjectName(QStringLiteral("map3DTrajectoryInfoQuality"));
        quality_label_->setAlignment(Qt::AlignCenter);
        headerLayout->addWidget(title_label_);
        headerLayout->addStretch(1);
        headerLayout->addWidget(quality_label_);
        layout_->addLayout(headerLayout);

        details_label_ = new QLabel(this);
        details_label_->setObjectName(QStringLiteral("map3DTrajectoryInfoDetails"));
        details_label_->setTextFormat(Qt::RichText);
        details_label_->setWordWrap(true);
        details_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        details_scroll_ = new QScrollArea(this);
        details_scroll_->setFrameShape(QFrame::NoFrame);
        details_scroll_->setWidgetResizable(true);
        details_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        details_scroll_->viewport()->setAutoFillBackground(false);
        details_scroll_->setWidget(details_label_);
        details_label_->setAutoFillBackground(false);
        layout_->addWidget(details_scroll_);
        applyTheme(VaporView::Geo::FixQuality::Unknown);
        hide();
    }

    void setHoverChangedCallback(std::function<void(bool)> callback)
    {
        on_hover_changed_ = std::move(callback);
    }

    void setSample(int sampleIndex, const VaporView::Geo::TrajectoryRenderSample& renderSample, const QString& waterUnit)
    {
        sample_index_ = sampleIndex;
        sample_ = renderSample;
        water_unit_ = waterUnit;
        const auto& sample = renderSample.navigation;
        const QLocale locale;
        title_label_->setText((qApp->property("vaporViewEnglish").toBool()
            ? QStringLiteral("Sample #%1 (Frozen)") : QStringLiteral("采样点 #%1（冻结快照）")).arg(sampleIndex + 1));
        quality_label_->setText(trajectoryFixQualityLabel(sample.fixQuality));

        QString recordTime = QStringLiteral("--");
        if (sample.recordTimestampUs > 0)
        {
            const qint64 timestampMs = sample.recordTimestampUs / 1000;
            const QDateTime localTime = QDateTime::fromMSecsSinceEpoch(timestampMs).toLocalTime();
            recordTime =
                QStringLiteral("%1.%2")
                    .arg(locale.toString(localTime, QLocale::ShortFormat))
                    .arg(timestampMs % 1000, 3, 10, QChar('0'));
        }

        const QString position = sample.hasLlh()
            ? QStringLiteral("%1°, %2°")
                  .arg(locale.toString(sample.latDeg, 'f', 7),
                       locale.toString(sample.lonDeg, 'f', 7))
            : QStringLiteral("--");
        const QString height = sample.hasLlh()
            ? QStringLiteral("%1 m · %2")
                  .arg(locale.toString(sample.heightM, 'f', 2),
                       trajectoryHeightReferenceLabel(sample.heightReference))
            : QStringLiteral("--");

        const QString satellites = sample.satellites > 0
            ? locale.toString(sample.satellites)
            : QStringLiteral("--");
        const QString hdop = std::isfinite(sample.hdop)
            ? locale.toString(sample.hdop, 'f', 2)
            : QStringLiteral("--");
        const QString vdop = std::isfinite(sample.vdop)
            ? locale.toString(sample.vdop, 'f', 2)
            : QStringLiteral("--");
        const QString precision =
            mapText(QStringLiteral("卫星 %1 · HDOP %2 · VDOP %3")).arg(satellites, hdop, vdop);

        QString velocity = QStringLiteral("--");
        if (std::isfinite(sample.velNMps)
            && std::isfinite(sample.velEMps)
            && std::isfinite(sample.velDMps))
        {
            const double speedMps = std::hypot(sample.velNMps, sample.velEMps, sample.velDMps);
            velocity = QStringLiteral("%1 m/s").arg(locale.toString(speedMps, 'f', 2));
        }

        QString attitude = QStringLiteral("--");
        if (std::isfinite(sample.rollDeg)
            && std::isfinite(sample.pitchDeg)
            && std::isfinite(sample.yawDeg))
        {
            attitude =
                mapText(QStringLiteral("横滚 %1° · 俯仰 %2° · 航向 %3°"))
                    .arg(locale.toString(sample.rollDeg, 'f', 2),
                         locale.toString(sample.pitchDeg, 'f', 2),
                         locale.toString(sample.yawDeg, 'f', 2));
        }

        const QString ecef = sample.hasEcef()
            ? QStringLiteral("%1, %2, %3 m")
                  .arg(locale.toString(sample.ecefXM, 'f', 2),
                       locale.toString(sample.ecefYM, 'f', 2),
                       locale.toString(sample.ecefZM, 'f', 2))
            : QStringLiteral("--");

        applyTheme(sample.fixQuality);
        const QColor secondary = VaporView::appThemeColor(
            VaporView::AppThemeColor::TextSecondary,
            VaporView::isDarkThemeEnabled());
        const auto row = [&](const QString& label, const QString& value) {
            return QStringLiteral(
                "<tr><td style=\"color:%1;padding-right:14px;vertical-align:top;\">%2</td>"
                "<td style=\"vertical-align:top;\">%3</td></tr>")
                .arg(secondary.name(QColor::HexRgb), label.toHtmlEscaped(), value.toHtmlEscaped());
        };
        const auto& heat = renderSample.heat;
        const auto measurement = [](double value, const QString& unit) {
            if (!std::isfinite(value)) return QStringLiteral("—");
            return (VaporView::Geo::formatHeatValue(value) + QLatin1Char(' ') + unit).trimmed();
        };
        const bool english = qApp->property("vaporViewEnglish").toBool();
        QString measurements;
        if (std::isfinite(heat.columnWaterConcentration) || std::isfinite(heat.inSituWaterConcentration)
            || std::isfinite(heat.temperatureC) || std::isfinite(heat.humidityRh)
            || std::isfinite(heat.pressureHpa) || std::isfinite(heat.lidarDistanceM))
        {
            measurements = row(english ? QStringLiteral("Column · 816 nm") : QStringLiteral("水汽柱浓度 · 816 nm"),
                               measurement(heat.columnWaterConcentration, waterUnit))
                + row(english ? QStringLiteral("In-situ · 1368 nm") : QStringLiteral("原位水汽浓度 · 1368 nm"),
                      measurement(heat.inSituWaterConcentration, waterUnit))
                + row(english ? QStringLiteral("Temp / RH / pressure") : QStringLiteral("温度 / 湿度 / 气压"),
                      QStringLiteral("%1 / %2 / %3").arg(measurement(heat.temperatureC, QStringLiteral("°C")),
                          measurement(heat.humidityRh, QStringLiteral("%RH")), measurement(heat.pressureHpa, QStringLiteral("hPa"))))
                + row(Geo::heatMetricName(Geo::HeatMetric::LidarDistance, english),
                      measurement(heat.lidarDistanceM, QStringLiteral("m")));
        }
        details_label_->setText(
            QStringLiteral("<table cellspacing=\"0\" cellpadding=\"2\">%1</table>")
                .arg(row(mapText(QStringLiteral("记录时间")), recordTime)
                     + row(mapText(QStringLiteral("经纬度")), position)
                     + row(mapText(QStringLiteral("高程")), height)
                     + row(mapText(QStringLiteral("定位精度")), precision)
                     + row(mapText(QStringLiteral("三维速度")), velocity)
                     + row(mapText(QStringLiteral("姿态")), attitude)
                     + row(QStringLiteral("ECEF"), ecef) + measurements));
        updateLayoutMargins();
        last_viewport_height_ = -1;
        fitDetailsHeight(parentWidget()->height());
        setAccessibleDescription(
            QStringLiteral("%1，%2，%3")
                .arg(title_label_->text(), position, quality_label_->text()));
        update();
    }

    void refreshLanguage() { if (sample_index_ >= 0) setSample(sample_index_, sample_, water_unit_); }
    void setWaterUnit(const QString& unit) { if (sample_index_ >= 0) setSample(sample_index_, sample_, unit); }

    void placeAt(const QPointF& anchor, const QSize& viewportSize)
    {
        fitDetailsHeight(viewportSize.height());
        const int maxX = (std::max)(kTrajectoryInfoCardScreenMargin,
                                    viewportSize.width() - width() - kTrajectoryInfoCardScreenMargin);
        const int x = std::clamp(qRound(anchor.x() - static_cast<qreal>(width()) / 2.0),
                                 kTrajectoryInfoCardScreenMargin,
                                 maxX);
        constexpr int kPointClearance = 10;
        const bool shouldPlaceBelow =
            (qRound(anchor.y()) - height() - kPointClearance) < kTrajectoryInfoCardScreenMargin;
        if (arrow_on_top_ != shouldPlaceBelow)
        {
            arrow_on_top_ = shouldPlaceBelow;
            setProperty("pointerDirection",
                        arrow_on_top_ ? QStringLiteral("up") : QStringLiteral("down"));
            updateLayoutMargins();
            layout_->activate();
            resize(width(), layout_->sizeHint().height());
        }
        int y = arrow_on_top_
            ? (qRound(anchor.y()) + kPointClearance)
            : (qRound(anchor.y()) - height() - kPointClearance);
        y = std::clamp(y,
                       kTrajectoryInfoCardScreenMargin,
                       (std::max)(kTrajectoryInfoCardScreenMargin,
                                  viewportSize.height() - height() - kTrajectoryInfoCardScreenMargin));
        const qreal minArrowX = kOuterMargin + kCornerRadius + kArrowHalfWidth + 2.0;
        const qreal maxArrowX =
            (std::max)(minArrowX,
                       static_cast<qreal>(width()) - kOuterMargin - kCornerRadius - kArrowHalfWidth - 2.0);
        arrow_x_ = std::clamp(anchor.x() - static_cast<qreal>(x), minArrowX, maxArrowX);
        setProperty("pointerTipOffsetX", arrow_x_);
        move(x, y);
        raise();
        update();
    }

protected:
    void changeEvent(QEvent* event) override
    {
        QFrame::changeEvent(event);
        if (!event) return;
        if (event->type() == QEvent::LanguageChange) refreshLanguage();
        else if (event->type() == QEvent::PaletteChange
                 || event->type() == QEvent::ApplicationPaletteChange)
        {
            applyTheme(sample_.navigation.fixQuality);
            update();
        }
    }

    void enterEvent(QEnterEvent* event) override
    {
        QFrame::enterEvent(event);
        setHovered(true);
    }

    void leaveEvent(QEvent* event) override
    {
        QFrame::leaveEvent(event);
        setHovered(false);
    }

    void hideEvent(QHideEvent* event) override
    {
        setHovered(false);
        QFrame::hideEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        const bool dark = VaporView::isDarkThemeEnabled();
        const QColor surface = VaporView::appThemeColor(VaporView::AppThemeColor::SurfaceRaised, dark);
        const QColor border = VaporView::appThemeColor(VaporView::AppThemeColor::BorderStrong, dark);
        const QColor accent = VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark);

        const qreal bodyTop = kOuterMargin + (arrow_on_top_ ? kArrowHeight : 0);
        const qreal bodyHeight = (std::max)(2.0 * kCornerRadius + 2.0,
                                            static_cast<qreal>(height() - 2 * kOuterMargin - kArrowHeight));
        const QRectF body(kOuterMargin, bodyTop, width() - 2 * kOuterMargin, bodyHeight);
        const qreal r = kCornerRadius;
        const qreal ax = std::clamp(arrow_x_,
                                    body.left() + r + kArrowHalfWidth + 1.0,
                                    body.right() - r - kArrowHalfWidth - 1.0);

        QPainterPath path;
        path.moveTo(body.left() + r, body.top());
        if (arrow_on_top_)
        {
            path.lineTo(ax - kArrowHalfWidth, body.top());
            path.lineTo(ax, body.top() - kArrowHeight);
            path.lineTo(ax + kArrowHalfWidth, body.top());
        }
        path.lineTo(body.right() - r, body.top());
        path.arcTo(QRectF(body.right() - 2.0 * r, body.top(), 2.0 * r, 2.0 * r), 90.0, -90.0);
        path.lineTo(body.right(), body.bottom() - r);
        path.arcTo(QRectF(body.right() - 2.0 * r, body.bottom() - 2.0 * r, 2.0 * r, 2.0 * r), 0.0, -90.0);
        if (!arrow_on_top_)
        {
            path.lineTo(ax + kArrowHalfWidth, body.bottom());
            path.lineTo(ax, body.bottom() + kArrowHeight);
            path.lineTo(ax - kArrowHalfWidth, body.bottom());
        }
        path.lineTo(body.left() + r, body.bottom());
        path.arcTo(QRectF(body.left(), body.bottom() - 2.0 * r, 2.0 * r, 2.0 * r), -90.0, -90.0);
        path.lineTo(body.left(), body.top() + r);
        path.arcTo(QRectF(body.left(), body.top(), 2.0 * r, 2.0 * r), 180.0, -90.0);
        path.closeSubpath();

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (hovered_)
        {
            QColor glow = accent;
            glow.setAlpha(dark ? 95 : 65);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(glow, 4.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
        }
        painter.setBrush(surface);
        painter.setPen(QPen(hovered_ ? accent : border,
                            hovered_ ? 2.0 : 1.25,
                            Qt::SolidLine,
                            Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawPath(path);
    }

private:
    void fitDetailsHeight(int viewportHeight)
    {
        if (last_viewport_height_ == viewportHeight) return;
        last_viewport_height_ = viewportHeight;
        const auto margins = layout_->contentsMargins();
        const int textWidth = width() - margins.left() - margins.right();
        const int natural = details_label_->heightForWidth(textWidth);
        const int available = viewportHeight - 2 * kTrajectoryInfoCardScreenMargin - margins.top()
            - margins.bottom() - layout_->spacing() - std::max(title_label_->sizeHint().height(), quality_label_->sizeHint().height());
        const int height = std::clamp(natural, 1, std::max(1, available));
        if (details_scroll_->minimumHeight() != height) details_scroll_->setFixedHeight(height);
        layout_->activate();
        resize(width(), layout_->sizeHint().height());
    }

    void updateLayoutMargins()
    {
        if (!layout_) return;
        const int top = 14 + kOuterMargin + (arrow_on_top_ ? kArrowHeight : 0);
        const int bottom = 14 + kOuterMargin + (arrow_on_top_ ? 0 : kArrowHeight);
        layout_->setContentsMargins(16 + kOuterMargin, top, 16 + kOuterMargin, bottom);
    }

    void setHovered(bool hovered)
    {
        if (hovered_ == hovered) return;
        hovered_ = hovered;
        setProperty("cardHovered", hovered_);
        update();
        if (on_hover_changed_) on_hover_changed_(hovered_);
    }

    int last_viewport_height_ = -1;
    int sample_index_ = -1;
    VaporView::Geo::TrajectoryRenderSample sample_;
    QString water_unit_;
    bool arrow_on_top_ = false;
    bool hovered_ = false;
    qreal arrow_x_ = kTrajectoryInfoCardWidth * 0.5 + kOuterMargin;
    std::function<void(bool)> on_hover_changed_;

    void applyTheme(VaporView::Geo::FixQuality quality)
    {
        if (applying_theme_) return;
        applying_theme_ = true;
        const bool dark = VaporView::isDarkThemeEnabled();
        const QColor text = VaporView::appThemeColor(VaporView::AppThemeColor::Text, dark);
        const QColor accent = VaporView::appThemeColor(trajectoryFixQualityColor(quality), dark);
        const QString cardStyle = QStringLiteral(
                "QFrame#map3DTrajectoryInfoCard { background: transparent; border: none; }"
                "QFrame#map3DTrajectoryInfoCard QLabel { color: %1; background: transparent; border: none; }")
                .arg(text.name(QColor::HexRgb));
        if (styleSheet() != cardStyle)
        {
            setStyleSheet(cardStyle);
        }
        const QString badgeStyle = QStringLiteral(
            "QLabel { color: %1; background: transparent; border: 1px solid %1; "
            "border-radius: 9px; padding: 2px 8px; }")
            .arg(accent.name(QColor::HexRgb));
        if (quality_label_->styleSheet() != badgeStyle)
        {
            quality_label_->setStyleSheet(badgeStyle);
        }
        applying_theme_ = false;
    }

    bool applying_theme_ = false;
    QVBoxLayout* layout_ = nullptr;
    QLabel* title_label_ = nullptr;
    QLabel* quality_label_ = nullptr;
    QScrollArea* details_scroll_ = nullptr;
    QLabel* details_label_ = nullptr;
};

void configureEarthManipulator(osgEarth::EarthManipulator* manipulator)
{
    if (!manipulator)
    {
        return;
    }
    osgEarth::EarthManipulator::Settings* settings = manipulator->getSettings();
    if (!settings)
    {
        return;
    }

    settings->setMinMaxPitch(-90.0, kEarthMaxInteractivePitchDeg);
    settings->setMinMaxDistance(50.0, 20000000.0);
    settings->setTerrainAvoidanceEnabled(true);
    settings->setTerrainAvoidanceMinimumDistance(25.0);
}

double currentEarthManipulatorDistance(const osgViewer::Viewer* viewer)
{
    if (!viewer)
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }
    const auto* manipulator =
        dynamic_cast<const osgEarth::EarthManipulator*>(viewer->getCameraManipulator());
    if (!manipulator)
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }
    const double distanceM = manipulator->getDistance();
    return std::isfinite(distanceM) ? distanceM : (std::numeric_limits<double>::quiet_NaN)();
}

double currentEarthManipulatorPitchDeg(const osgViewer::Viewer* viewer)
{
    if (!viewer)
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }
    const auto* manipulator =
        dynamic_cast<const osgEarth::EarthManipulator*>(viewer->getCameraManipulator());
    if (!manipulator)
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }
    const osgEarth::Viewpoint viewpoint = manipulator->getViewpoint();
    if (!viewpoint.isValid() || !viewpoint.pitch().isSet())
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }
    const double pitchDeg = viewpoint.pitch().get().as(osgEarth::Units::DEGREES);
    return std::isfinite(pitchDeg) ? pitchDeg : (std::numeric_limits<double>::quiet_NaN)();
}

void configureHighResolutionTerrain(osgEarth::MapNode* mapNode)
{
    if (!mapNode) return;

    osgEarth::TerrainOptionsAPI terrainOptions = mapNode->getTerrainOptions();
    terrainOptions.setLODMethod(osgEarth::LODMethod::SCREEN_SPACE);
    terrainOptions.setTileSize(33);
    terrainOptions.setTilePixelSize(kTerrainTilePixelSize);
    terrainOptions.setScreenSpaceError(0.0f);
    terrainOptions.setHeightFieldSkirtRatio(0.02f);
    terrainOptions.setProgressive(true);
    terrainOptions.setMergesPerFrame(2u);
    terrainOptions.setMaxTilesToUnloadPerFrame(4u);
    const unsigned hwThreads = (std::max)(2u, std::thread::hardware_concurrency());
    terrainOptions.setConcurrency(std::clamp(hwThreads / 2u, 2u, 4u));
    terrainOptions.setMorphImagery(false);
    mapNode->setScreenSpaceError(0.0f);

    if (osgEarth::TerrainEngine* terrainEngine = mapNode->getTerrainEngine())
    {
        terrainEngine->dirtyTerrainOptions();
    }
}

void assertGuiThread(const QObject* object, const char* function)
{
    Q_ASSERT_X(object && QThread::currentThread() == object->thread(),
               "OsgEarthViewWidget",
               function);
}

unsigned int toOsgMouseButton(Qt::MouseButton button)
{
    switch (button)
    {
    case Qt::LeftButton:
        return 1;
    case Qt::MiddleButton:
        return 2;
    case Qt::RightButton:
        return 3;
    default:
        return 0;
    }
}

int toOsgKey(const QKeyEvent* event)
{
    if (!event)
    {
        return 0;
    }

    switch (event->key())
    {
    case Qt::Key_Left:
        return osgGA::GUIEventAdapter::KEY_Left;
    case Qt::Key_Right:
        return osgGA::GUIEventAdapter::KEY_Right;
    case Qt::Key_Up:
        return osgGA::GUIEventAdapter::KEY_Up;
    case Qt::Key_Down:
        return osgGA::GUIEventAdapter::KEY_Down;
    case Qt::Key_PageUp:
        return osgGA::GUIEventAdapter::KEY_Page_Up;
    case Qt::Key_PageDown:
        return osgGA::GUIEventAdapter::KEY_Page_Down;
    case Qt::Key_Home:
        return osgGA::GUIEventAdapter::KEY_Home;
    case Qt::Key_End:
        return osgGA::GUIEventAdapter::KEY_End;
    case Qt::Key_Escape:
        return osgGA::GUIEventAdapter::KEY_Escape;
    case Qt::Key_Space:
        return ' ';
    default:
        break;
    }

    const QString text = event->text();
    if (text.size() == 1)
    {
        const char16_t character = text.at(0).toLower().unicode();
        if (QChar(character).isPrint())
        {
            return character;
        }
    }
    return 0;
}
osg::Node* createLocalGridNode()
{
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;

    constexpr int kExtentM = 1000;
    constexpr int kSpacingM = 50;
    for (int value = -kExtentM; value <= kExtentM; value += kSpacingM)
    {
        vertices->push_back(osg::Vec3(value, -kExtentM, 0.0f));
        vertices->push_back(osg::Vec3(value, kExtentM, 0.0f));
        vertices->push_back(osg::Vec3(-kExtentM, value, 0.0f));
        vertices->push_back(osg::Vec3(kExtentM, value, 0.0f));
        const osg::Vec4 color = value == 0
            ? osg::Vec4(0.62f, 0.72f, 0.82f, 1.0f)
            : osg::Vec4(0.28f, 0.34f, 0.40f, 1.0f);
        colors->push_back(color);
        colors->push_back(color);
        colors->push_back(color);
        colors->push_back(color);
    }

    geometry->setVertexArray(vertices.get());
    geometry->setColorArray(colors.get(), osg::Array::BIND_PER_VERTEX);
    geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, static_cast<GLsizei>(vertices->size())));
    geode->addDrawable(geometry.get());
    return geode.release();
}

osg::Vec3d samplePosition(const VaporView::Geo::NavSample& sample)
{
    if (sample.hasEcef())
    {
        return osg::Vec3d(sample.ecefXM, sample.ecefYM, sample.ecefZM);
    }
    if (sample.hasNed())
    {
        return osg::Vec3d(sample.nedEM, sample.nedNM, -sample.nedDM);
    }
    return osg::Vec3d(sample.lonDeg * 100000.0, sample.latDeg * 100000.0, sample.heightM);
}

bool llhToWgs84Ecef(const VaporView::Geo::NavSample& sample, osg::Vec3d& world)
{
    if (!std::isfinite(sample.latDeg) ||
        !std::isfinite(sample.lonDeg) ||
        !std::isfinite(sample.heightM) ||
        std::abs(sample.latDeg) > 90.0 ||
        std::abs(sample.lonDeg) > 180.0)
    {
        return false;
    }

    const double latitude = sample.latDeg * kDegreesToRadians;
    const double longitude = sample.lonDeg * kDegreesToRadians;
    const double sinLatitude = std::sin(latitude);
    const double cosLatitude = std::cos(latitude);
    const double radiusOfCurvature =
        kEarthRadiusM /
        std::sqrt(1.0 - kWgs84FirstEccentricitySquared * sinLatitude * sinLatitude);
    world.set((radiusOfCurvature + sample.heightM) * cosLatitude * std::cos(longitude),
              (radiusOfCurvature + sample.heightM) * cosLatitude * std::sin(longitude),
              (radiusOfCurvature * (1.0 - kWgs84FirstEccentricitySquared) +
               sample.heightM) *
                  sinLatitude);
    return std::isfinite(world.x()) &&
           std::isfinite(world.y()) &&
           std::isfinite(world.z());
}

bool sampleWorldFocusPosition(const VaporView::Geo::NavSample& sample, osg::Vec3d& world)
{
    if (sample.hasEcef())
    {
        world.set(sample.ecefXM, sample.ecefYM, sample.ecefZM);
        return true;
    }
    if (!sample.hasLlh())
    {
        return false;
    }

    if (const osgEarth::SpatialReference* wgs84 =
            osgEarth::SpatialReference::get("wgs84"))
    {
        osgEarth::GeoPoint geo(
            wgs84, sample.lonDeg, sample.latDeg, sample.heightM, osgEarth::ALTMODE_ABSOLUTE);
        if (geo.toWorld(world) &&
            std::isfinite(world.x()) &&
            std::isfinite(world.y()) &&
            std::isfinite(world.z()))
        {
            return true;
        }
    }
    return llhToWgs84Ecef(sample, world);
}

std::optional<osgEarth::Viewpoint> currentEarthViewpoint(osgViewer::Viewer* viewer)
{
    if (!viewer)
    {
        return std::nullopt;
    }

    const auto* manipulator =
        dynamic_cast<const osgEarth::EarthManipulator*>(viewer->getCameraManipulator());
    if (!manipulator)
    {
        return std::nullopt;
    }

    osgEarth::Viewpoint viewpoint = manipulator->getViewpoint();
    if (!viewpoint.isValid() || viewpoint.nodeIsSet())
    {
        return std::nullopt;
    }
    return viewpoint;
}

bool restoreEarthViewpoint(osgViewer::Viewer* viewer, const osgEarth::Viewpoint& viewpoint)
{
    if (!viewer || !viewer->getCamera() || !viewpoint.isValid())
    {
        return false;
    }

    auto* manipulator =
        dynamic_cast<osgEarth::EarthManipulator*>(viewer->getCameraManipulator());
    if (!manipulator)
    {
        return false;
    }
    manipulator->setViewpoint(viewpoint, 0.0);
    manipulator->updateCamera(*viewer->getCamera());
    return true;
}

using Detail::removeLayerByName;

unsigned tiandituSatelliteInsertIndex(osgEarth::Map* map)
{
    if (!map)
    {
        return 0;
    }

    osgEarth::LayerVector layers;
    map->getLayers(layers);
    for (unsigned index = 0; index < layers.size(); ++index)
    {
        if (dynamic_cast<osgEarth::ElevationLayer*>(layers[index].get())
            || dynamic_cast<Detail::OnlineOsmLayer*>(layers[index].get()))
        {
            return index;
        }
    }
    return static_cast<unsigned>(layers.size());
}

std::size_t layerIndex(Map3DLayer layer)
{
    return static_cast<std::size_t>(layer);
}

Map3DLayer mapLayerCategory(const osgEarth::Layer* layer)
{
    if (!layer)
    {
        return Map3DLayer::Count;
    }

    const QString name = QString::fromStdString(layer->getName()).toLower();
    if (dynamic_cast<const osgEarth::ElevationLayer*>(layer))
    {
        return Map3DLayer::DigitalElevation;
    }
    if (name.contains(QStringLiteral("natural earth")))
    {
        return Map3DLayer::BaseMap;
    }
    if (name.contains(QStringLiteral("water"))
        || name.contains(QStringLiteral("hydro")))
    {
        return Map3DLayer::Hydrography;
    }
    if (name.contains(QStringLiteral("road"))
        || name.contains(QStringLiteral("transport")))
    {
        return Map3DLayer::RoadNetwork;
    }
    if (dynamic_cast<const osgEarth::ImageLayer*>(layer))
    {
        if (name.contains(QStringLiteral("satellite"))
            || name.contains(QStringLiteral("sentinel"))
            || name.contains(QStringLiteral("landsat"))
            || name.contains(QStringLiteral("aerial")))
        {
            return Map3DLayer::SatelliteImagery;
        }
        return Map3DLayer::BaseMap;
    }
    return Map3DLayer::Count;
}

} // namespace

OsgEarthViewWidget::OsgEarthViewWidget(QWidget* parent, bool deferRendering)
    : QOpenGLWidget(parent)
    , ground_detail_ellipse_(std::make_shared<Detail::GroundDetailEllipse>())
    , trajectory_layer_(std::make_unique<Trajectory3DLayer>())
    , aircraft_layer_(std::make_unique<Aircraft3DLayer>())
{
    layer_visibility_.fill(true);
    layer_visibility_[layerIndex(Map3DLayer::Hydrography)] = false;
    layer_visibility_[layerIndex(Map3DLayer::RoadNetwork)] = false;
    rendering_started_ = !deferRendering;
    if (rendering_started_)
    {
        initializeMap3DRuntime();
    }
    setMinimumSize(640, 420);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    auto* infoCard = new TrajectorySampleInfoCard(this);
    infoCard->setHoverChangedCallback([this](bool hovered) {
        if (trajectory_layer_)
        {
            trajectory_layer_->setSelectedMarkerHovered(hovered);
            update();
        }
    });
    trajectory_info_card_ = infoCard;
    connect(this, &OsgEarthViewWidget::trajectoryRenderSampleSelected, this, &OsgEarthViewWidget::showTrajectoryInfoCard);
    online_terrain_timer_.setSingleShot(true);
    online_terrain_timer_.setInterval(500);
    connect(&online_terrain_timer_, &QTimer::timeout, this, [this] {
        if (!map_node_ || shutdown_) return;
        auto* map = map_node_->getMap();
        std::vector<const osgEarth::Layer*> refresh;
        osgEarth::GeoExtent missedExtent = osgEarth::GeoExtent::INVALID;
        bool hasUnboundedMiss = false;
        osgEarth::ElevationLayerVector layers;
        map->getLayers(layers);
        for (const auto& layer : layers)
        {
            osgEarth::GeoExtent layerExtent = osgEarth::GeoExtent::INVALID;
            if (auto* online = dynamic_cast<Detail::OnlineTerrainLayer*>(layer.get());
                online && online->resumeDownloads(&layerExtent) && online->isOpen())
            {
                online->dirty();
                refresh.push_back(online);
                if (!layerExtent.isValid())
                {
                    hasUnboundedMiss = true;
                }
                else if (!missedExtent.isValid())
                {
                    missedExtent = layerExtent;
                }
                else
                {
                    missedExtent.expandToInclude(layerExtent);
                }
            }
        }
        if (!refresh.empty())
        {
            // Flush ElevationPool's L2/LUT and recompute its layer-revision hash
            // so tiles built while online DEM was deferred re-query 30 m data.
            if (auto* pool = map->getElevationPool())
                pool->setMap(map);
            // Reload only the deferred region's elevation so resident tiles outside
            // the newly visited area do not trigger another full-globe rebuild.
            if (auto* engine = map_node_->getTerrainEngine())
                engine->invalidateRegion(
                    refresh,
                    hasUnboundedMiss ? osgEarth::GeoExtent::INVALID : missedExtent,
                    kTerrainMinLevel,
                    INT_MAX);
        }
    });
    frameTimer_.setSingleShot(true);
    frameTimer_.setTimerType(Qt::PreciseTimer);
    frameTimer_.setInterval(0);
    connect(this, &QOpenGLWidget::frameSwapped, this, [this]() {
        if (shutdown_ || !rendering_started_ || !isVisible() || !frame_pending_presentation_)
            return;
        frame_pending_presentation_ = false;
        present_ms_ = present_clock_.isValid() ? present_clock_.nsecsElapsed() / 1000000.0 : 0.0;
        if (frame_interval_clock_.isValid())
        {
            const double interval = frame_interval_clock_.nsecsElapsed() / 1000000.0;
            if (interval > 0.0)
            {
                frame_intervals_.push_back(interval);
                if (frame_intervals_.size() > 120) frame_intervals_.pop_front();
                smoothed_frame_interval_ms_ = smoothed_frame_interval_ms_ <= 0.0 ? interval
                    : smoothed_frame_interval_ms_ * 0.9 + interval * 0.1;
                frames_per_second_ = 1000.0 / smoothed_frame_interval_ms_;
            }
        }
        frame_interval_clock_.start();
        auto* jobs = jobs::get_metrics();
        const bool loading = jobs->total_pending() > 0 || jobs->total_running() > 0
            || jobs->total_postprocessing() > 0
            || (viewer_ && viewer_->getDatabasePager()->getRequestsInProgress());
        const bool active = loading || (viewer_ && viewer_->getRequestContinousUpdate())
            || !camera_activity_clock_.isValid()
            || camera_activity_clock_.elapsed() < 1000;
        // Include drawing/composition time in the frame budget and leave a small
        // event-loop slice so mouse/wheel/UI events stay responsive under DEM bursts.
        const int delay = active ? (std::max)(4, 16 - static_cast<int>(last_frame_ms_ + present_ms_)) : 100;
        idle_rendering_ = !active;
        frameTimer_.start(delay);
        if (!performance_publish_clock_.isValid() || performance_publish_clock_.elapsed() >= 500)
        {
            performance_publish_clock_.start();
            if (map_node_ && map_node_->getMap())
            {
                auto* layer = dynamic_cast<Detail::TiandituImageLayer*>(
                    map_node_->getMap()->getLayerByName(kTiandituSatelliteLayerName));
                if (layer)
                {
                    const auto changed = layer->refreshFallbackRegions();
                    if (auto* engine = map_node_->getTerrainEngine())
                    {
                        for (const auto& extent : changed)
                            engine->invalidateRegion({layer}, extent, 0u, INT_MAX);
                    }
                }
                if (layer && layerVisible(Map3DLayer::SatelliteImagery) && layer->fallbackPending.exchange(false))
                    emit imageryFallbackNotice();
            }
            emit performanceUpdated();
        }
    });
    connect(&frameTimer_, &QTimer::timeout, this, [this]() {
        if (!shutdown_)
        {
            update();
        }
    });
}

OsgEarthViewWidget::~OsgEarthViewWidget()
{
    shutdown();
}

void OsgEarthViewWidget::shutdown()
{
    assertGuiThread(this, Q_FUNC_INFO);
    frameTimer_.stop();
    if (shutdown_)
    {
        return;
    }
    shutdown_ = true;
    if (trajectory_info_card_)
    {
        trajectory_info_card_->hide();
    }
    ++earth_load_generation_;
    ++local_3d_tiles_load_generation_;
    ++aircraft_load_generation_;
    Map3DLoadTracker::instance().interruptAll();
    cancelAsyncWatchers();
    QObject::disconnect(gl_context_destruction_connection_);
    setUpdatesEnabled(false);
    clearFocus();

    bool madeCurrent = false;
    QOpenGLContext* openGlContext = context();
    if (initialized_ && openGlContext && openGlContext->isValid())
    {
        makeCurrent();
        madeCurrent = QOpenGLContext::currentContext() == openGlContext;
    }
    if (viewer_)
    {
        viewer_->setDone(true);
        viewer_->stopThreading();
        viewer_->setCameraManipulator(nullptr);
        viewer_->setDatabasePager(nullptr);
        viewer_->setImagePager(nullptr);
        if (viewer_->getCamera())
        {
            viewer_->getCamera()->setGraphicsContext(nullptr);
        }
        viewer_->setSceneData(nullptr);
    }
    if (root_)
    {
        if (madeCurrent)
        {
            root_->releaseGLObjects(nullptr);
        }
        root_->removeChildren(0, root_->getNumChildren());
    }
    earth_node_ = nullptr;
    local_3d_tiles_node_ = nullptr;
    overlay_transform_ = nullptr;

    map_node_ = nullptr;
    trajectory_layer_.reset();
    aircraft_layer_.reset();
    root_ = nullptr;
    graphics_window_ = nullptr;
    viewer_.reset();
    initialized_ = false;
    if (madeCurrent)
    {
        doneCurrent();
    }
}

void OsgEarthViewWidget::registerAsyncWatcher(QFutureWatcherBase* watcher)
{
    if (watcher)
    {
        async_watchers_.insert(watcher);
    }
}

void OsgEarthViewWidget::unregisterAsyncWatcher(QFutureWatcherBase* watcher)
{
    async_watchers_.remove(watcher);
}

void OsgEarthViewWidget::cancelAsyncWatchers()
{
    const QSet<QFutureWatcherBase*> watchers = async_watchers_;
    async_watchers_.clear();
    for (QFutureWatcherBase* watcher : watchers)
    {
        if (!watcher)
        {
            continue;
        }
        QObject::disconnect(watcher, nullptr, this, nullptr);
        watcher->cancel();
        // The worker may be inside an uncancellable osgDB/osgEarth call.  Do
        // not block the GUI thread; finished callbacks are detached and late
        // results are discarded by their generation checks.
        watcher->setParent(nullptr);
        watcher->deleteLater();
    }
}

void OsgEarthViewWidget::appendNavigationSample(const VaporView::Geo::NavSample& sample)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !trajectory_layer_ || !aircraft_layer_)
    {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    if (shared_samples_)
    {
        // Live navigation starts a new mutable track instead of copying a
        // potentially large immutable Session/Replay trajectory.
        clearTrack();
    }
    else
    {
        detachSharedSamplesForLiveAppend();
    }
    track_data_mode_ = TrackDataMode::LiveNavigation;
    raw_samples_.push_back(renderSampleFromNavSample(sample));
    while (!preserve_full_track_extent_
           && static_cast<int>(raw_samples_.size()) > maxVisibleSamples())
    {
        raw_samples_.pop_front();
    }
    const VaporView::Geo::NavSample displaySample = toDisplaySample(sample);
    trajectory_layer_->appendNavigationSample(displaySample);
    aircraft_layer_->updateSample(displaySample);
    updateFollowCamera(displaySample);
    last_track_update_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    update();
}

void OsgEarthViewWidget::appendRenderSample(const VaporView::Geo::TrajectoryRenderSample& sample)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !trajectory_layer_ || !aircraft_layer_)
    {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    detachSharedSamplesForLiveAppend();
    track_data_mode_ = TrackDataMode::RenderSamples;
    raw_samples_.push_back(sample);
    while (!preserve_full_track_extent_
           && static_cast<int>(raw_samples_.size()) > maxVisibleSamples())
    {
        raw_samples_.pop_front();
    }
    const VaporView::Geo::TrajectoryRenderSample displaySample = toDisplaySample(sample);
    trajectory_layer_->appendRenderSample(displaySample);
    aircraft_layer_->updateSample(displaySample.navigation);
    updateFollowCamera(displaySample.navigation);
    last_track_update_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    update();
}

void OsgEarthViewWidget::appendNavigationSamples(const std::vector<VaporView::Geo::NavSample>& samples)
{
    for (const auto& sample : samples)
    {
        appendNavigationSample(sample);
    }
}

void OsgEarthViewWidget::appendRenderSamples(
    const std::vector<VaporView::Geo::TrajectoryRenderSample>& samples)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !trajectory_layer_ || !aircraft_layer_)
    {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    if (!samples.empty())
    {
        detachSharedSamplesForLiveAppend();
    }
    track_data_mode_ = TrackDataMode::RenderSamples;
    raw_samples_.insert(raw_samples_.end(), samples.cbegin(), samples.cend());
    while (!preserve_full_track_extent_
           && static_cast<int>(raw_samples_.size()) > maxVisibleSamples())
    {
        raw_samples_.pop_front();
    }
    std::vector<VaporView::Geo::TrajectoryRenderSample> displaySamples;
    displaySamples.reserve(samples.size());
    for (const VaporView::Geo::TrajectoryRenderSample& sample : samples)
    {
        displaySamples.push_back(toDisplaySample(sample));
    }
    trajectory_layer_->appendRenderSamples(displaySamples);
    if (!displaySamples.empty())
    {
        aircraft_layer_->updateSample(displaySamples.back().navigation);
        updateFollowCamera(displaySamples.back().navigation);
    }
    last_track_update_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    update();
}

void OsgEarthViewWidget::setSamples(const std::vector<VaporView::Geo::NavSample>& samples)
{
    setSamples(std::make_shared<const std::vector<VaporView::Geo::NavSample>>(samples));
}

void OsgEarthViewWidget::setSamples(
    const std::vector<VaporView::Geo::TrajectoryRenderSample>& samples)
{
    setSamples(std::make_shared<const std::vector<VaporView::Geo::TrajectoryRenderSample>>(samples));
}

void OsgEarthViewWidget::setSamples(
    std::shared_ptr<const std::vector<VaporView::Geo::NavSample>> samples,
    int sampleCount)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !trajectory_layer_ || !aircraft_layer_)
    {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    raw_samples_.clear();
    shared_nav_samples_ = std::move(samples);
    shared_samples_.reset();
    const int available = shared_nav_samples_ ? static_cast<int>(shared_nav_samples_->size()) : 0;
    shared_sample_count_ = sampleCount < 0 ? available : std::clamp(sampleCount, 0, available);
    preserve_full_track_extent_ = true;
    track_data_mode_ = TrackDataMode::LiveNavigation;
    local_frame_ = VaporView::Geo::LocalTangentPlane();
    resetWorldOverlayOrigin();
    rebuildDisplayTrack();
    last_track_update_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    update();
}

void OsgEarthViewWidget::setSamples(
    std::shared_ptr<const std::vector<VaporView::Geo::TrajectoryRenderSample>> samples,
    int sampleCount)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !trajectory_layer_ || !aircraft_layer_)
    {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    raw_samples_.clear();
    shared_samples_ = std::move(samples);
    shared_nav_samples_.reset();
    const int available = shared_samples_ ? static_cast<int>(shared_samples_->size()) : 0;
    shared_sample_count_ = sampleCount < 0 ? available : std::clamp(sampleCount, 0, available);
    preserve_full_track_extent_ = true;
    track_data_mode_ = TrackDataMode::RenderSamples;
    local_frame_ = VaporView::Geo::LocalTangentPlane();
    resetWorldOverlayOrigin();
    rebuildDisplayTrack();
    last_track_update_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    update();
}

void OsgEarthViewWidget::appendSampleFromStorage(
    const std::shared_ptr<const std::vector<VaporView::Geo::NavSample>>& samples,
    int sampleIndex)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !samples || sampleIndex < 0
        || sampleIndex >= static_cast<int>(samples->size()))
    {
        return;
    }
    if (shared_nav_samples_ != samples || sampleIndex != shared_sample_count_)
    {
        setSamples(samples, sampleIndex + 1);
        return;
    }

    QElapsedTimer timer;
    timer.start();
    ++shared_sample_count_;
    const VaporView::Geo::NavSample displaySample =
        toDisplaySample((*samples)[static_cast<std::size_t>(sampleIndex)]);
    trajectory_layer_->appendNavigationSample(displaySample);
    aircraft_layer_->updateSample(displaySample);
    updateFollowCamera(displaySample);
    last_track_update_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    update();
}

void OsgEarthViewWidget::appendSampleFromStorage(
    const std::shared_ptr<const std::vector<VaporView::Geo::TrajectoryRenderSample>>& samples,
    int sampleIndex)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !samples || sampleIndex < 0
        || sampleIndex >= static_cast<int>(samples->size()))
    {
        return;
    }
    if (shared_samples_ != samples || sampleIndex != shared_sample_count_)
    {
        setSamples(samples, sampleIndex + 1);
        return;
    }

    QElapsedTimer timer;
    timer.start();
    ++shared_sample_count_;
    const VaporView::Geo::TrajectoryRenderSample displaySample =
        toDisplaySample((*samples)[static_cast<std::size_t>(sampleIndex)]);
    trajectory_layer_->appendRenderSample(displaySample);
    aircraft_layer_->updateSample(displaySample.navigation);
    updateFollowCamera(displaySample.navigation);
    last_track_update_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    update();
}

void OsgEarthViewWidget::clearTrack()
{
    assertGuiThread(this, Q_FUNC_INFO);
    raw_samples_.clear();
    shared_samples_.reset();
    shared_nav_samples_.reset();
    shared_sample_count_ = 0;
    preserve_full_track_extent_ = false;
    track_data_mode_ = TrackDataMode::LiveNavigation;
    height_reference_status_.clear();
    if (shutdown_ || !trajectory_layer_ || !aircraft_layer_)
    {
        return;
    }
    clearTrajectorySampleSelection();
    trajectory_layer_->clear();
    aircraft_layer_->clear();
    local_frame_ = VaporView::Geo::LocalTangentPlane();
    resetWorldOverlayOrigin();
    update();
}

bool OsgEarthViewWidget::loadEarthFile(const QString& earthPath)
{
    assertGuiThread(this, Q_FUNC_INFO);
    ++earth_load_generation_;
    const Detail::EarthAssetLoadResult result = Detail::loadEarthAsset(earthPath);
    return applyEarthLoad(result.diagnostics, result.node, result.mapNode, result.useXihuInitialView, false);
}

void OsgEarthViewWidget::loadEarthFileAsync(const QString& earthPath,
                                            std::function<void(bool)> finished)
{
    loadEarthFileAsync(earthPath, false, std::move(finished));
}

void OsgEarthViewWidget::loadEarthFilePreservingViewAsync(const QString& earthPath,
                                                          std::function<void(bool)> finished)
{
    loadEarthFileAsync(earthPath, true, std::move(finished));
}

void OsgEarthViewWidget::prioritizeSatelliteImagery()
{
    if (!map_node_ || !layerVisible(Map3DLayer::DigitalElevation)) return;
    auto* map = map_node_->getMap();
    if (!map) return;
    if (!online_terrain_timer_.isActive())
    {
        osgEarth::ElevationLayerVector layers;
        map->getLayers(layers);
        for (const auto& layer : layers)
        {
            if (auto* online = dynamic_cast<Detail::OnlineTerrainLayer*>(layer.get()))
                online->deferDownloads();
        }
    }
    online_terrain_timer_.start();
}

bool OsgEarthViewWidget::applyTiandituSatelliteImagery(const QString& requestUrl, const QString& key)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !map_node_ || !map_node_->getMap())
    {
        return false;
    }

    osgEarth::Map* map = map_node_->getMap();
    const Detail::OnlineMapResource resource{requestUrl, key};
    auto* existing = dynamic_cast<Detail::TiandituImageLayer*>(map->getLayerByName(kTiandituSatelliteLayerName));
    if (existing && existing->isOpen() && existing->usesResource(resource))
    {
        removeLayerByName(map, kOpenAerialMapLayerName);
        removeLayerByName(map, kEsriLayerName);
        applyLayerVisibility(Map3DLayer::SatelliteImagery);
        return true;
    }
    if (!Detail::isOnlineMapResourceConfigured(Detail::OnlineMapProvider::Tianditu, resource))
    {
        removeLayerByName(map, kTiandituSatelliteLayerName);
        earth_load_diagnostics_.layerSummaries.push_back(
            mapText(QStringLiteral("未应用天地图卫星影像：未配置密钥。")));
        applyLayerVisibility(Map3DLayer::SatelliteImagery);
        update();
        return false;
    }

    osg::ref_ptr<osgEarth::XYZImageLayer> layer = new Detail::TiandituImageLayer(resource);
    layer->setName(kTiandituSatelliteLayerName);
    // The driver requires a template to open, but tile requests use the private resource configuration.
    layer->setURL(osgEarth::URI(Detail::defaultRequestUrl(Detail::OnlineMapProvider::Tianditu).toStdString()));
    layer->setProfile(osgEarth::Profile::create(osgEarth::Profile::SPHERICAL_MERCATOR));
    layer->setFormat("jpg");
    // Level 0 returns a successful HTTP response containing a no-imagery placeholder.
    layer->options().minLevel() = 1u;
    layer->options().maxLevel() = kTiandituMaxZoom;
    layer->options().maxDataLevel() = kTiandituMaxZoom;

    earth_load_diagnostics_.layerSummaries.push_back(
        mapText(QStringLiteral("天地图源级别：%1–%2；不使用第 0 级占位图。"))
            .arg(layer->options().minLevel().get()).arg(layer->options().maxDataLevel().get()));
    removeLayerByName(map, kTiandituSatelliteLayerName);
    const unsigned insertIndex = tiandituSatelliteInsertIndex(map);
    Map3DLoadTracker::instance().interruptCategory(MapLoadCategory::Imagery);
    map->insertLayer(layer.get(), insertIndex);
    removeLayerByName(map, kOpenAerialMapLayerName);
    removeLayerByName(map, kEsriLayerName);
    earth_load_diagnostics_.layerSummaries.push_back(
        mapText(QStringLiteral("天地图卫星影像已添加到图层索引 %1%2。"))
            .arg(insertIndex)
            .arg(layer->isOpen()
                     ? QString()
                     : QStringLiteral("; pending open status=%1")
                           .arg(QString::fromStdString(layer->getStatus().toString()))));
    ++earth_load_diagnostics_.layerCount;
    if (layer->isOpen())
    {
        ++earth_load_diagnostics_.openLayerCount;
    }
    applyLayerVisibility(Map3DLayer::SatelliteImagery);
    prioritizeSatelliteImagery();
    update();
    return true;
}

bool OsgEarthViewWidget::applyOpenAerialMapImagery()
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !map_node_ || !map_node_->getMap()) return false;
    osgEarth::Map* map = map_node_->getMap();
    auto* existing = map->getLayerByName(kOpenAerialMapLayerName);
    if (!existing || !existing->isOpen())
    {
        osg::ref_ptr<Detail::OpenAerialMapImageLayer> layer = new Detail::OpenAerialMapImageLayer;
        if (layer->open().isError()) return false;
        removeLayerByName(map, kOpenAerialMapLayerName);
        map->insertLayer(layer.get(), tiandituSatelliteInsertIndex(map));
    }
    Map3DLoadTracker::instance().interruptCategory(MapLoadCategory::Imagery);
    removeLayerByName(map, kTiandituSatelliteLayerName);
    removeLayerByName(map, kEsriLayerName);
    applyLayerVisibility(Map3DLayer::SatelliteImagery);
    update();
    return true;
}

bool OsgEarthViewWidget::applyEsriWorldImagery(const QString& requestUrl, const QString& key)
{
    assertGuiThread(this, Q_FUNC_INFO);
    const Detail::OnlineMapResource resource{requestUrl, key};
    if (shutdown_ || !map_node_ || !map_node_->getMap()
        || !Detail::isOnlineMapResourceConfigured(Detail::OnlineMapProvider::Esri, resource)) return false;
    osgEarth::Map* map = map_node_->getMap();
    auto* existing = dynamic_cast<osgEarth::XYZImageLayer*>(map->getLayerByName(kEsriLayerName));
    if (!existing || !existing->isOpen() || esri_imagery_key_ != key || esri_imagery_request_url_ != requestUrl)
    {
        class TrackedEsriImageLayer final : public osgEarth::XYZImageLayer
        {
        public:
            explicit TrackedEsriImageLayer(const Detail::OnlineMapResource& resource) : resource_(resource) {}

            osgEarth::GeoImage createImageImplementation(const osgEarth::TileKey& key,
                                                         osgEarth::ProgressCallback* progress) const override
            {
                if (progress && progress->isCanceled()) return osgEarth::GeoImage::INVALID;
                ScopedMapLoadTask loadTask(MapLoadCategory::Imagery);
                auto image = Detail::readCredentialedImageryTile(Detail::OnlineMapProvider::Esri, resource_, key, progress);
                if (progress && progress->isCanceled())
                {
                    loadTask.cancel();
                    return osgEarth::GeoImage::INVALID;
                }
                loadTask.finish();
                return image;
            }

        private:
            const Detail::OnlineMapResource resource_;
        };
        osg::ref_ptr<osgEarth::XYZImageLayer> layer = new TrackedEsriImageLayer(resource);
        layer->setName(kEsriLayerName);
        layer->setURL(osgEarth::URI(Detail::defaultRequestUrl(Detail::OnlineMapProvider::Esri).toStdString()));
        layer->setProfile(osgEarth::Profile::create(osgEarth::Profile::SPHERICAL_MERCATOR));
        layer->setFormat("jpg");
        layer->options().maxLevel() = 23u;
        layer->options().maxDataLevel() = 23u;
        layer->setCachePolicy(osgEarth::CachePolicy::NO_CACHE);
        if (layer->open().isError()) return false;
        removeLayerByName(map, kEsriLayerName);
        map->insertLayer(layer.get(), tiandituSatelliteInsertIndex(map));
        esri_imagery_key_ = key;
        esri_imagery_request_url_ = requestUrl;
    }
    Map3DLoadTracker::instance().interruptCategory(MapLoadCategory::Imagery);
    removeLayerByName(map, kTiandituSatelliteLayerName);
    removeLayerByName(map, kOpenAerialMapLayerName);
    applyLayerVisibility(Map3DLayer::SatelliteImagery);
    update();
    return true;
}

bool OsgEarthViewWidget::applyLocalSatelliteImagery(const QString& vrtPath)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !map_node_ || !map_node_->getMap() || !QFileInfo(vrtPath).isFile())
        return false;

    osgEarth::Map* map = map_node_->getMap();
    const std::string url = QDir::fromNativeSeparators(QFileInfo(vrtPath).absoluteFilePath()).toStdString();
    auto* existing = dynamic_cast<osgEarth::GDALImageLayer*>(
        map->getLayerByName(kSelectedLocalSatelliteLayerName));
    if (!existing || !existing->isOpen() || existing->getURL().full() != url)
    {
        osg::ref_ptr<osgEarth::GDALImageLayer> layer = new osgEarth::GDALImageLayer;
        layer->setName(kSelectedLocalSatelliteLayerName);
        layer->setURL(osgEarth::URI(url));
        if (layer->open().isError()) return false;
        removeLayerByName(map, kSelectedLocalSatelliteLayerName);
        map->insertLayer(layer.get(), tiandituSatelliteInsertIndex(map));
    }
    Map3DLoadTracker::instance().interruptCategory(MapLoadCategory::Imagery);
    removeLayerByName(map, kTiandituSatelliteLayerName);
    removeLayerByName(map, kOpenAerialMapLayerName);
    removeLayerByName(map, kEsriLayerName);
    applyLayerVisibility(Map3DLayer::SatelliteImagery);
    update();
    return true;
}

void OsgEarthViewWidget::clearSelectedSatelliteImagery()
{
    assertGuiThread(this, Q_FUNC_INFO);
    Map3DLoadTracker::instance().interruptCategory(MapLoadCategory::Imagery);
    if (shutdown_ || !map_node_ || !map_node_->getMap())
        return;
    osgEarth::Map* map = map_node_->getMap();
    removeLayerByName(map, kTiandituSatelliteLayerName);
    removeLayerByName(map, kOpenAerialMapLayerName);
    removeLayerByName(map, kEsriLayerName);
    removeLayerByName(map, kSelectedLocalSatelliteLayerName);
    applyLayerVisibility(Map3DLayer::SatelliteImagery);
    update();
}

void OsgEarthViewWidget::setBaseMapSource(const QString& source)
{
    assertGuiThread(this, Q_FUNC_INFO);
    base_map_source_ = source;
    if (shutdown_ || !map_node_ || !map_node_->getMap())
        return;
    applyLayerVisibility(Map3DLayer::BaseMap);
}

void OsgEarthViewWidget::setDemSource(const QString& source)
{
    assertGuiThread(this, Q_FUNC_INFO);
    dem_source_ = source;
    Map3DLoadTracker::instance().interruptCategory(MapLoadCategory::Dem);
    if (shutdown_ || !map_node_ || !map_node_->getMap())
        return;
    auto* map = map_node_->getMap();
    if (dem_source_.isEmpty() || dem_source_ == QStringLiteral("online:cesium")
        || map->getLayerByName("Cesium World Terrain"))
    {
        earth_load_diagnostics_.cesiumTerrainConfigured = Detail::addCesiumTerrainFallback(map);
    }
    applyLayerVisibility(Map3DLayer::DigitalElevation);
    std::vector<const osgEarth::Layer*> refresh;
    osgEarth::ElevationLayerVector layers;
    map->getLayers(layers);
    for (const auto& layer : layers)
    {
        if (layer)
        {
            refresh.push_back(layer.get());
        }
    }
    if (auto* pool = map->getElevationPool())
        pool->setMap(map);
    if (auto* engine = map_node_->getTerrainEngine())
        engine->invalidateRegion(refresh, osgEarth::GeoExtent::INVALID, 0u, INT_MAX);
    update();
}

QString OsgEarthViewWidget::activeElevationSourceLabel() const
{
    if (!map_node_ || !map_node_->getMap() || !layerVisible(Map3DLayer::DigitalElevation))
        return {};
    osgEarth::ElevationLayerVector layers;
    map_node_->getMap()->getLayers(layers);
    for (auto it = layers.rbegin(); it != layers.rend(); ++it)
    {
        const auto& layer = *it;
        if (layer && layer->isOpen() && layer->getVisible())
        {
            return QString::fromStdString(layer->getName());
        }
    }
    return {};
}

void OsgEarthViewWidget::setOsmSource(const QString& source)
{
    assertGuiThread(this, Q_FUNC_INFO);
    osm_source_ = source;
    Map3DLoadTracker::instance().interruptCategory(MapLoadCategory::Osm);
    if (shutdown_ || !map_node_ || !map_node_->getMap())
        return;
    applyLayerVisibility(Map3DLayer::Hydrography);
    applyLayerVisibility(Map3DLayer::RoadNetwork);
}

bool OsgEarthViewWidget::isOnlineOsmActive() const
{
    if (!map_node_ || !map_node_->getMap())
        return false;
    const auto* map = map_node_->getMap();
    const auto* roads = dynamic_cast<const osgEarth::VisibleLayer*>(
        map->getLayerByName(Detail::OnlineOsmLayer::roadsName));
    const auto* water = dynamic_cast<const osgEarth::VisibleLayer*>(
        map->getLayerByName(Detail::OnlineOsmLayer::waterName));
    return (roads && roads->getVisible()) || (water && water->getVisible());
}

void OsgEarthViewWidget::setBuildingsSource(const QString& source)
{
    assertGuiThread(this, Q_FUNC_INFO);
    buildings_source_ = source;
    Map3DLoadTracker::instance().interruptCategory(MapLoadCategory::Buildings);
    if (shutdown_)
        return;
    applyLayerVisibility(Map3DLayer::Buildings3D);
}

bool OsgEarthViewWidget::isOpenFreeMapBuildingsActive() const
{
    if (!map_node_ || !map_node_->getMap() || !layerVisible(Map3DLayer::Buildings3D))
        return false;
    const auto* layer = dynamic_cast<const osgEarth::VisibleLayer*>(
        map_node_->getMap()->getLayerByName(Detail::OpenFreeMapBuildings::layerName));
    return layer && layer->getVisible();
}

bool OsgEarthViewWidget::isCesiumTerrainActive() const
{
    if (!map_node_ || !map_node_->getMap() || !layerVisible(Map3DLayer::DigitalElevation))
        return false;
    const auto* layer = dynamic_cast<const osgEarth::VisibleLayer*>(
        map_node_->getMap()->getLayerByName("Cesium World Terrain"));
    return layer && layer->isOpen() && layer->getVisible();
}

QString OsgEarthViewWidget::activeSatelliteImagerySource() const
{
    if (!map_node_ || !map_node_->getMap() || !layerVisible(Map3DLayer::SatelliteImagery))
        return {};
    const auto* online = dynamic_cast<const osgEarth::VisibleLayer*>(
        map_node_->getMap()->getLayerByName(kTiandituSatelliteLayerName));
    if (online && online->isOpen() && online->getVisible()) return QStringLiteral("online:tianditu");
    const auto* openAerialMap = dynamic_cast<const osgEarth::VisibleLayer*>(
        map_node_->getMap()->getLayerByName(kOpenAerialMapLayerName));
    if (openAerialMap && openAerialMap->isOpen() && openAerialMap->getVisible())
        return QStringLiteral("online:openaerialmap");
    const auto* esri = dynamic_cast<const osgEarth::VisibleLayer*>(
        map_node_->getMap()->getLayerByName(kEsriLayerName));
    if (esri && esri->isOpen() && esri->getVisible()) return QStringLiteral("online:esri");
    const auto* local = dynamic_cast<const osgEarth::GDALImageLayer*>(
        map_node_->getMap()->getLayerByName(kSelectedLocalSatelliteLayerName));
    if (local && local->isOpen() && local->getVisible())
        return QStringLiteral("local:") + QString::fromStdString(local->getURL().full());
    return {};
}

void OsgEarthViewWidget::loadEarthFileAsync(const QString& earthPath,
                                            bool preserveCurrentEarthView,
                                            std::function<void(bool)> finished)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_)
    {
        if (finished) finished(false);
        return;
    }
    const quint64 generation = ++earth_load_generation_;
    Map3DLoadTracker::instance().interruptCategory(MapLoadCategory::Earth);
    auto loadTask = std::make_shared<ScopedMapLoadTask>(MapLoadCategory::Earth);
    earth_load_diagnostics_ = {};
    earth_load_diagnostics_.attempted = true;
    earth_load_diagnostics_.requestedPath = earthPath;
    auto* watcher = new QFutureWatcher<Detail::EarthAssetLoadResult>(this);
    registerAsyncWatcher(watcher);
    connect(watcher, &QFutureWatcher<Detail::EarthAssetLoadResult>::finished,
            this, [this, watcher, generation, preserveCurrentEarthView, loadTask, finished = std::move(finished)]() mutable {
        unregisterAsyncWatcher(watcher);
        watcher->deleteLater();
        if (shutdown_ || generation != earth_load_generation_)
        {
            loadTask->cancel();
            return;
        }
        if (watcher->isCanceled() || watcher->future().resultCount()==0) {
            loadTask->cancel();
            if (finished) finished(false);
            return;
        }
        const Detail::EarthAssetLoadResult result = watcher->result();
        const bool loaded = applyEarthLoad(result.diagnostics,
                                           result.node,
                                           result.mapNode,
                                           result.useXihuInitialView,
                                           preserveCurrentEarthView);
        loadTask->finish();
        if (finished) finished(loaded);
    });
    watcher->setFuture(QtConcurrent::run([earthPath]() {
        return Detail::loadEarthAsset(earthPath);
    }));
}

QString OsgEarthViewWidget::cesiumTerrainCredit(bool collapsible) const
{
    return Detail::cesiumTerrainCredit(map_node_ ? map_node_->getMap() : nullptr, collapsible);
}

bool OsgEarthViewWidget::loadOnlineGlobe()
{
    osg::ref_ptr<osgEarth::Map> map = new osgEarth::Map;
    // Give transparent vectors a continuous surface even without any imagery files.
    osg::ref_ptr<osgEarth::Contrib::DecalImageLayer> background = new osgEarth::Contrib::DecalImageLayer;
    background->setName("Globe background");
    background->options().maxDataLevel() = 0u;
    osg::ref_ptr<osg::Image> pixel = osgEarth::ImageUtils::createOnePixelImage(osg::Vec4(0.22f, 0.25f, 0.28f, 1.0f));
    background->addDecal("background", background->getProfile()->getExtent(), pixel.get());
    map->addLayer(background.get());
    osg::ref_ptr<osgEarth::MapNode> globe = new osgEarth::MapNode(map.get());
    EarthLoadDiagnostics diagnostics;
    diagnostics.attempted = diagnostics.loaded = diagnostics.foundMapNode = true;
    diagnostics.onlineOsmAvailable = true;
    Detail::configureBuiltInTerrain(map.get(), diagnostics);
    return applyEarthLoad(diagnostics, globe, globe.get(), false, false);
}

bool OsgEarthViewWidget::applyEarthLoad(EarthLoadDiagnostics diagnostics,
                                        osg::ref_ptr<osg::Node> node,
                                        osgEarth::MapNode* mapNode,
                                        bool useXihuInitialView,
                                        bool preserveCurrentEarthView)
{
    assertGuiThread(this, Q_FUNC_INFO);
    earth_load_diagnostics_ = std::move(diagnostics);
    if (shutdown_ || !earth_load_diagnostics_.loaded || !node) return false;
    initializeSceneIfNeeded();
    if (!root_)
    {
        earth_load_diagnostics_.loaded = false;
        earth_load_diagnostics_.failureReason = mapText(QStringLiteral("场景根节点尚未初始化。"));
        return false;
    }
    const std::optional<osgEarth::Viewpoint> previousViewpoint =
        preserveCurrentEarthView ? currentEarthViewpoint(viewer_.get()) : std::nullopt;
    const bool replacedPreviousNode = earth_node_.valid();
    if (earth_node_) root_->removeChild(earth_node_.get());
    earth_node_ = std::move(node);
    if (map_node_ != mapNode) online_terrain_timer_.stop();
    map_node_ = mapNode;
    configureHighResolutionTerrain(map_node_);
    terrain_detail_callback_pending_ = true;
    earth_load_diagnostics_.layerSummaries.push_back(
        mapText(QStringLiteral("地形细节：可见区域优先的屏幕空间 LOD，%1 px 瓦片阈值，低视角自适应裁剪和细节级别。")).arg(kTerrainTilePixelSize));
    use_xihu_initial_view_ = useXihuInitialView;
    if (replacedPreviousNode)
    {
        earth_load_diagnostics_.layerSummaries.push_back(mapText(QStringLiteral("已替换原地图场景。")));
    }
    trajectory_layer_->setUseWorldCoordinates(true);
    aircraft_layer_->setUseWorldCoordinates(true);
    resetWorldOverlayOrigin();
    root_->insertChild(0, earth_node_.get());
    applyAllLayerVisibility();
    setInitialEarthView();
    if (previousViewpoint && restoreEarthViewpoint(viewer_.get(), *previousViewpoint))
    {
        earth_load_diagnostics_.layerSummaries.push_back(
            QStringLiteral("Preserved previous Earth camera viewpoint."));
    }
    rebuildDisplayTrack();
    // A session may finish loading before the initial asynchronous Earth load.
    if (!previousViewpoint && sampleCount() > 0)
    {
        flyToTrack();
    }
    update();
    return true;
}

bool OsgEarthViewWidget::loadLocal3DTilesPreview(const QString& tilesetPath)
{
    assertGuiThread(this, Q_FUNC_INFO);
    ++local_3d_tiles_load_generation_;
    pending_local_tiles_path_.clear();
    const Detail::Local3DTilesAssetLoadResult result = Detail::loadLocal3DTilesAsset(tilesetPath);
    return applyLocal3DTilesLoad(result.diagnostics, result.node);
}

void OsgEarthViewWidget::loadLocal3DTilesPreviewAsync(
    const QString& tilesetPath,
    std::function<void(bool)> finished)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_)
    {
        if (finished) finished(false);
        return;
    }
    const quint64 generation = ++local_3d_tiles_load_generation_;
    auto loadTask = std::make_shared<ScopedMapLoadTask>(MapLoadCategory::Buildings);
    pending_local_tiles_path_ = tilesetPath;
    applyLayerVisibility(Map3DLayer::Buildings3D);
    local_3d_tiles_load_diagnostics_ = {};
    local_3d_tiles_load_diagnostics_.attempted = true;
    local_3d_tiles_load_diagnostics_.requestedPath = tilesetPath;
    auto* watcher = new QFutureWatcher<Detail::Local3DTilesAssetLoadResult>(this);
    registerAsyncWatcher(watcher);
    connect(watcher, &QFutureWatcher<Detail::Local3DTilesAssetLoadResult>::finished,
            this, [this, watcher, generation, loadTask, finished = std::move(finished)]() mutable {
        unregisterAsyncWatcher(watcher);
        watcher->deleteLater();
        if (shutdown_ || generation != local_3d_tiles_load_generation_)
        {
            loadTask->cancel();
            return;
        }
        pending_local_tiles_path_.clear();
        if (watcher->isCanceled() || watcher->future().resultCount()==0) {
            loadTask->cancel();
            applyLayerVisibility(Map3DLayer::Buildings3D);
            if (finished) finished(false);
            return;
        }
        const Detail::Local3DTilesAssetLoadResult result = watcher->result();
        const bool loaded = applyLocal3DTilesLoad(result.diagnostics, result.node);
        loadTask->finish();
        if (finished) finished(loaded);
    });
    watcher->setFuture(QtConcurrent::run([tilesetPath]() {
        return Detail::loadLocal3DTilesAsset(tilesetPath);
    }));
}

bool OsgEarthViewWidget::applyLocal3DTilesLoad(Local3DTilesLoadDiagnostics diagnostics,
                                               osg::ref_ptr<osg::Group> node)
{
    assertGuiThread(this, Q_FUNC_INFO);
    local_3d_tiles_load_diagnostics_ = std::move(diagnostics);
    if (shutdown_) return false;
    if (!local_3d_tiles_load_diagnostics_.loaded || !node)
    {
        applyLayerVisibility(Map3DLayer::Buildings3D);
        return false;
    }
    initializeSceneIfNeeded();
    if (!root_)
    {
        local_3d_tiles_load_diagnostics_.loaded = false;
        local_3d_tiles_load_diagnostics_.failureReason = mapText(QStringLiteral("场景根节点尚未初始化。"));
        return false;
    }
    local_3d_tiles_load_diagnostics_.clearedPreviousPreview = local_3d_tiles_node_.valid();
    if (local_3d_tiles_node_) root_->removeChild(local_3d_tiles_node_.get());
    for (unsigned i = 0; i < node->getNumChildren(); ++i)
        node->getChild(i)->addCullCallback(new Detail::BuildingEllipseCull(ground_detail_ellipse_));
    local_3d_tiles_node_ = std::move(node);
    root_->addChild(local_3d_tiles_node_.get());
    if (map_node_) removeLayerByName(map_node_->getMap(), Detail::OpenFreeMapBuildings::layerName);
    applyLayerVisibility(Map3DLayer::Buildings3D);
    update();
    return true;
}

void OsgEarthViewWidget::clearLocal3DTilesPreview()
{
    assertGuiThread(this, Q_FUNC_INFO);
    ++local_3d_tiles_load_generation_;
    pending_local_tiles_path_.clear();
    if (shutdown_)
    {
        local_3d_tiles_node_ = nullptr;
        return;
    }
    if (root_ && local_3d_tiles_node_)
    {
        root_->removeChild(local_3d_tiles_node_.get());
        local_3d_tiles_node_ = nullptr;
        if (map_node_) removeLayerByName(map_node_->getMap(), Detail::OpenFreeMapBuildings::layerName);
    }
    applyLayerVisibility(Map3DLayer::Buildings3D);
}

EarthLoadDiagnostics OsgEarthViewWidget::earthLoadDiagnostics() const
{
    return earth_load_diagnostics_;
}

Local3DTilesLoadDiagnostics OsgEarthViewWidget::local3DTilesLoadDiagnostics() const
{
    return local_3d_tiles_load_diagnostics_;
}

AircraftModelDiagnostics OsgEarthViewWidget::aircraftModelDiagnostics() const
{
    return aircraft_model_diagnostics_;
}

bool OsgEarthViewWidget::loadAircraftModel(const QString& modelPath)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_)
    {
        aircraft_model_diagnostics_.failureReason = QStringLiteral("3D view is shutting down.");
        return false;
    }
    ++aircraft_load_generation_;
    initializeSceneIfNeeded();
    return loadAircraftModelFile(modelPath, QStringLiteral("User-selected aircraft model"));
}

void OsgEarthViewWidget::loadAircraftModelAsync(const QString& modelPath,
                                                std::function<void(bool)> finished)
{
    loadAircraftModelFileAsync(modelPath,
                               QStringLiteral("User-selected aircraft model"),
                               std::move(finished));
}

void OsgEarthViewWidget::resetAircraftModelToBuiltIn()
{
    assertGuiThread(this, Q_FUNC_INFO);
    ++aircraft_load_generation_;
    if (shutdown_)
    {

        return;
    }
    initializeSceneIfNeeded();
    aircraft_model_diagnostics_ = {};
    aircraft_model_diagnostics_.usingBuiltInMarker = true;
    aircraft_model_diagnostics_.failureReason =
        QStringLiteral("User reset aircraft model; using built-in marker.");
    if (aircraft_layer_)
    {
        aircraft_layer_->clearCustomModel();
    }
    update();
}

void OsgEarthViewWidget::setLayerVisible(Map3DLayer layer, bool visible)
{
    const std::size_t index = layerIndex(layer);
    if (index >= layer_visibility_.size())
    {
        return;
    }
    layer_visibility_[index] = visible;
    if (!visible)
    {
        switch (layer)
        {
        case Map3DLayer::SatelliteImagery:
            Map3DLoadTracker::instance().interruptCategory(MapLoadCategory::Imagery);
            break;
        case Map3DLayer::DigitalElevation:
            Map3DLoadTracker::instance().interruptCategory(MapLoadCategory::Dem);
            break;
        case Map3DLayer::Hydrography:
        case Map3DLayer::RoadNetwork:
            if (!layerVisible(Map3DLayer::Hydrography) && !layerVisible(Map3DLayer::RoadNetwork))
                Map3DLoadTracker::instance().interruptCategory(MapLoadCategory::Osm);
            break;
        case Map3DLayer::Buildings3D:
            Map3DLoadTracker::instance().interruptCategory(MapLoadCategory::Buildings);
            break;
        case Map3DLayer::FlightElements:
            clearTrajectorySampleSelection();
            break;
        default:
            break;
        }
    }
    applyLayerVisibility(layer);
}

bool OsgEarthViewWidget::layerVisible(Map3DLayer layer) const
{
    const std::size_t index = layerIndex(layer);
    return index < layer_visibility_.size() && layer_visibility_[index];
}

bool OsgEarthViewWidget::layerAvailable(Map3DLayer layer) const
{
    if ((layer == Map3DLayer::Hydrography || layer == Map3DLayer::RoadNetwork)
        && map_node_ && earth_load_diagnostics_.onlineOsmAvailable)
        return true;
    if (layer == Map3DLayer::Buildings3D)
    {
        return local_3d_tiles_node_.valid() || (map_node_ && earth_load_diagnostics_.onlineOsmAvailable);
    }
    if (layer == Map3DLayer::FlightElements)
    {
        return trajectory_layer_ && aircraft_layer_;
    }
    if (!map_node_ || !map_node_->getMap())
    {
        return false;
    }

    osgEarth::LayerVector layers;
    map_node_->getMap()->getLayers(layers);
    return std::any_of(layers.cbegin(), layers.cend(), [layer](const osg::ref_ptr<osgEarth::Layer>& candidate) {
        return dynamic_cast<osgEarth::VisibleLayer*>(candidate.get())
            && mapLayerCategory(candidate.get()) == layer;
    });
}

void OsgEarthViewWidget::applyLayerVisibility(Map3DLayer layer)
{
    const auto* tianditu = map_node_ && map_node_->getMap()
        ? map_node_->getMap()->getLayerByName(kTiandituSatelliteLayerName) : nullptr;
    const auto* openAerialMap = map_node_ && map_node_->getMap()
        ? map_node_->getMap()->getLayerByName(kOpenAerialMapLayerName) : nullptr;
    const auto* esri = map_node_ && map_node_->getMap()
        ? map_node_->getMap()->getLayerByName(kEsriLayerName) : nullptr;
    const auto* online = tianditu && tianditu->isOpen() ? tianditu
        : openAerialMap && openAerialMap->isOpen() ? openAerialMap : esri;
    const bool onlineSatellite = online && online->isOpen() && layerVisible(Map3DLayer::SatelliteImagery);
    const bool showBaseForOpenAerialMap = onlineSatellite && online == openAerialMap;
    const auto* selectedLocal = map_node_ && map_node_->getMap()
        ? map_node_->getMap()->getLayerByName(kSelectedLocalSatelliteLayerName) : nullptr;
    const bool localSatellite = selectedLocal && selectedLocal->isOpen()
        && layerVisible(Map3DLayer::SatelliteImagery) && !onlineSatellite;
    const bool visible = layerVisible(layer)
        || (layer == Map3DLayer::BaseMap && layerVisible(Map3DLayer::SatelliteImagery)
            && (!onlineSatellite || showBaseForOpenAerialMap));
    if (layer == Map3DLayer::SatelliteImagery) applyLayerVisibility(Map3DLayer::BaseMap);
    if (layer == Map3DLayer::Buildings3D)
    {
        const bool manualLocalOnly = buildings_source_ == QStringLiteral("local:osg_tiles");
        const bool manualOnlineOnly = buildings_source_ == QStringLiteral("online:openfreemap");
        if (local_3d_tiles_node_)
        {
            local_3d_tiles_node_->setNodeMask(visible && !manualOnlineOnly ? ~0u : 0u);
        }
        if (map_node_ && (earth_load_diagnostics_.onlineOsmAvailable || manualOnlineOnly))
        {
            auto* map = map_node_->getMap();
            const bool wantOnline = visible && !manualLocalOnly
                && (manualOnlineOnly || local_3d_tiles_node_ || pending_local_tiles_path_.isEmpty());
            if (!wantOnline)
            {
                removeLayerByName(map, Detail::OpenFreeMapBuildings::layerName);
            }
            else
            {
                auto* existing = dynamic_cast<Detail::OpenFreeMapBuildings*>(
                    map->getLayerByName(Detail::OpenFreeMapBuildings::layerName));
                const auto* local = dynamic_cast<const Detail::LocalBuildingTiles*>(local_3d_tiles_node_.get());
                const bool wantCoverageMask = !manualOnlineOnly && local && !local->coverage.isEmpty();
                if (existing && existing->hasLocalCoverage() != wantCoverageMask)
                {
                    removeLayerByName(map, Detail::OpenFreeMapBuildings::layerName);
                    existing = nullptr;
                }
                if (!existing)
                {
                    map->addLayer(new Detail::OpenFreeMapBuildings(
                        wantCoverageMask ? local->coverage : QPainterPath{},
                        ground_detail_ellipse_));
                }
            }
        }
    }
    else if (layer == Map3DLayer::FlightElements)
    {
        const osg::Node::NodeMask mask = visible ? ~0u : 0u;
        if (trajectory_layer_ && trajectory_layer_->node())
        {
            trajectory_layer_->node()->setNodeMask(mask);
        }
        if (aircraft_layer_ && aircraft_layer_->node())
        {
            aircraft_layer_->node()->setNodeMask(mask);
        }
    }
    else if (map_node_ && map_node_->getMap())
    {
        auto* map = map_node_->getMap();
        if ((layer == Map3DLayer::Hydrography || layer == Map3DLayer::RoadNetwork)
            && (earth_load_diagnostics_.onlineOsmAvailable || !osm_source_.isEmpty()))
        {
            const bool roads = layer == Map3DLayer::RoadNetwork;
            const char* name = roads ? Detail::OnlineOsmLayer::roadsName : Detail::OnlineOsmLayer::waterName;
            auto* offline = dynamic_cast<osgEarth::ImageLayer*>(
                map->getLayerByName(roads ? "OSM roads" : "OSM water fill"));
            const bool manualLocal = osm_source_ == QStringLiteral("local:osm");
            const bool manualOnline = osm_source_ == QStringLiteral("online:osm");
            if (manualLocal)
            {
                removeLayerByName(map, name);
                if (offline) offline->setVisible(visible);
                update();
                return;
            }
            if (offline) offline->setVisible(false);
            if (!visible)
            {
                removeLayerByName(map, name);
            }
            else
            {
                const bool allowFallback = !manualOnline;
                auto* existing = dynamic_cast<Detail::OnlineOsmLayer*>(map->getLayerByName(name));
                if (existing && existing->allowsOfflineFallback() != allowFallback)
                {
                    removeLayerByName(map, name);
                    existing = nullptr;
                }
                if (!existing)
                {
                    osg::ref_ptr<Detail::OnlineOsmLayer> overlay =
                        new Detail::OnlineOsmLayer(roads, offline, allowFallback);
                    auto* roadLayer = map->getLayerByName(Detail::OnlineOsmLayer::roadsName);
                    if (!roads && roadLayer)
                        map->insertLayer(overlay.get(), map->getIndexOfLayer(roadLayer));
                    else
                        map->addLayer(overlay.get());
                }
            }
            update();
            return;
        }
        if (layer == Map3DLayer::BaseMap && base_map_source_ == QStringLiteral("builtin:globe")
            && !map->getLayerByName("Globe background"))
        {
            osg::ref_ptr<osgEarth::Contrib::DecalImageLayer> background = new osgEarth::Contrib::DecalImageLayer;
            background->setName("Globe background");
            background->options().maxDataLevel() = 0u;
            osg::ref_ptr<osg::Image> pixel = osgEarth::ImageUtils::createOnePixelImage(osg::Vec4(0.22f, 0.25f, 0.28f, 1.0f));
            background->addDecal("background", background->getProfile()->getExtent(), pixel.get());
            background->open();
            map->insertLayer(background.get(), 0);
        }
        osgEarth::LayerVector layers;
        map_node_->getMap()->getLayers(layers);
        bool hasOpenNaturalEarth = false;
        if (layer == Map3DLayer::BaseMap)
        {
            for (const osg::ref_ptr<osgEarth::Layer>& candidate : layers)
            {
                if (candidate && candidate->isOpen()
                    && QString::fromStdString(candidate->getName()).contains(QStringLiteral("natural earth"), Qt::CaseInsensitive))
                {
                    hasOpenNaturalEarth = true;
                    break;
                }
            }
        }
        for (const osg::ref_ptr<osgEarth::Layer>& candidate : layers)
        {
            auto* visibleLayer = dynamic_cast<osgEarth::VisibleLayer*>(candidate.get());
            if (visibleLayer && mapLayerCategory(candidate.get()) == layer)
            {
                if (layer == Map3DLayer::DigitalElevation && !dem_source_.isEmpty())
                {
                    const QString layerName = QString::fromStdString(candidate->getName());
                    bool matches = false;
                    if (dem_source_ == QStringLiteral("local:copernicus"))
                        matches = layerName.contains(QStringLiteral("Copernicus"), Qt::CaseInsensitive)
                            && layerName != QStringLiteral("Online Copernicus GLO-30");
                    else if (dem_source_ == QStringLiteral("local:srtm"))
                        matches = layerName.contains(QStringLiteral("SRTM"), Qt::CaseInsensitive);
                    else if (dem_source_ == QStringLiteral("online:copernicus"))
                        matches = layerName == QStringLiteral("Online Copernicus GLO-30");
                    else if (dem_source_ == QStringLiteral("online:cesium"))
                        matches = layerName == QStringLiteral("Cesium World Terrain");
                    else if (dem_source_ == QStringLiteral("builtin:etopo2022"))
                        matches = layerName == QLatin1String(kGlobalBaseDemLayerName);
                    visibleLayer->setVisible(visible && matches);
                    continue;
                }
                if (layer == Map3DLayer::BaseMap)
                {
                    const QString layerName = QString::fromStdString(candidate->getName());
                    const bool isGlobeBg = layerName == QStringLiteral("Globe background");
                    const bool isNaturalEarth = layerName.contains(QStringLiteral("natural earth"), Qt::CaseInsensitive);
                    const bool competingBase = onlineSatellite && candidate.get() != online
                        && dynamic_cast<osgEarth::ImageLayer*>(candidate.get()) && !showBaseForOpenAerialMap;
                    if (base_map_source_ == QStringLiteral("builtin:globe"))
                    {
                        visibleLayer->setVisible(visible && !competingBase && isGlobeBg);
                        continue;
                    }
                    if (base_map_source_ == QStringLiteral("local:natural_earth"))
                    {
                        visibleLayer->setVisible(visible && !competingBase && isNaturalEarth);
                        continue;
                    }
                    if (isGlobeBg && hasOpenNaturalEarth)
                    {
                        visibleLayer->setVisible(false);
                        continue;
                    }
                }
                const bool competingImagery = onlineSatellite && candidate.get() != online
                    && dynamic_cast<osgEarth::ImageLayer*>(candidate.get())
                    && (layer == Map3DLayer::SatelliteImagery
                        || (layer == Map3DLayer::BaseMap && !showBaseForOpenAerialMap));
                const bool otherLocalImagery = localSatellite && layer == Map3DLayer::SatelliteImagery
                    && candidate.get() != selectedLocal && dynamic_cast<osgEarth::ImageLayer*>(candidate.get());
                visibleLayer->setVisible(visible && !competingImagery && !otherLocalImagery);
            }
        }
    }
    update();
}

void OsgEarthViewWidget::applyAllLayerVisibility()
{
    for (std::size_t index = 0; index < layer_visibility_.size(); ++index)
    {
        applyLayerVisibility(static_cast<Map3DLayer>(index));
    }
}

void OsgEarthViewWidget::setFollowAircraft(bool enabled)
{
    follow_aircraft_ = enabled;
    if (shutdown_ || !viewer_)
    {
        return;
    }

    if (earth_node_ && map_node_)
    {
        auto* manipulator =
            dynamic_cast<osgEarth::EarthManipulator*>(viewer_->getCameraManipulator());
        if (!manipulator)
        {
            osg::ref_ptr<osgEarth::EarthManipulator> replacement = new osgEarth::EarthManipulator;
            viewer_->setCameraManipulator(replacement.get());
            manipulator = replacement.get();
        }
        configureEarthManipulator(manipulator);
    }
    else if (!dynamic_cast<osgGA::TrackballManipulator*>(viewer_->getCameraManipulator()))
    {
        viewer_->setCameraManipulator(new osgGA::TrackballManipulator);
    }
    if (enabled)
    {
        if (const VaporView::Geo::NavSample* latest = latestRawSample())
        {
            updateFollowCamera(toDisplaySample(*latest));
        }
    }
}

bool OsgEarthViewWidget::hasLocal3DTilesPreview() const
{
    return local_3d_tiles_node_.valid();
}

double OsgEarthViewWidget::earthCameraRangeM() const
{
    if (!viewer_ || !earth_node_ || !map_node_)
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }

    const auto* manipulator =
        dynamic_cast<const osgEarth::EarthManipulator*>(viewer_->getCameraManipulator());
    if (!manipulator)
    {
        return (std::numeric_limits<double>::quiet_NaN)();
    }

    const double rangeM = manipulator->getDistance();
    return std::isfinite(rangeM) ? rangeM : (std::numeric_limits<double>::quiet_NaN)();
}

namespace {

osgEarth::EarthManipulator* ensureEarthManipulator(osgViewer::Viewer* viewer)
{
    if (!viewer)
    {
        return nullptr;
    }
    auto* manipulator = dynamic_cast<osgEarth::EarthManipulator*>(viewer->getCameraManipulator());
    if (!manipulator)
    {
        osg::ref_ptr<osgEarth::EarthManipulator> replacement = new osgEarth::EarthManipulator;
        viewer->setCameraManipulator(replacement.get());
        manipulator = replacement.get();
    }
    configureEarthManipulator(manipulator);
    return manipulator;
}

osgGA::TrackballManipulator* ensureTrackballManipulator(osgViewer::Viewer* viewer)
{
    if (!viewer)
    {
        return nullptr;
    }
    auto* manipulator = dynamic_cast<osgGA::TrackballManipulator*>(viewer->getCameraManipulator());
    if (!manipulator)
    {
        osg::ref_ptr<osgGA::TrackballManipulator> replacement = new osgGA::TrackballManipulator;
        viewer->setCameraManipulator(replacement.get());
        manipulator = replacement.get();
    }
    return manipulator;
}

} // namespace

void OsgEarthViewWidget::setMaxVisibleSamples(int maxVisibleSamples)
{
    if (shutdown_ || !trajectory_layer_)
    {
        return;
    }
    trajectory_layer_->setMaxVisibleSamples(maxVisibleSamples);
    if (!preserve_full_track_extent_)
    {
        while (static_cast<int>(raw_samples_.size()) > trajectory_layer_->maxVisibleSamples())
        {
            raw_samples_.pop_front();
        }
    }
    if (preserve_full_track_extent_)
    {
        rebuildDisplayTrack();
    }
    update();
}

void OsgEarthViewWidget::setHeatCalibrationEpochFilter(std::optional<std::uint32_t> epoch)
{
    if (!trajectory_layer_ || trajectory_layer_->heatCalibrationEpochFilter() == epoch) return;
    trajectory_layer_->setHeatCalibrationEpochFilter(epoch);
    update();
}

std::optional<std::uint32_t> OsgEarthViewWidget::heatCalibrationEpochFilter() const
{
    return trajectory_layer_ ? trajectory_layer_->heatCalibrationEpochFilter() : std::nullopt;
}

void OsgEarthViewWidget::setWaterUnitLabel(const QString& unit)
{
    if (water_unit_ == unit) return;
    water_unit_ = unit;
    auto* card = static_cast<TrajectorySampleInfoCard*>(trajectory_info_card_);
    if (card) {
        card->setWaterUnit(Geo::heatMetricUnit(Geo::HeatMetric::ColumnWaterConcentration, unit));
        updateTrajectoryInfoCardPosition();
    }
}

void OsgEarthViewWidget::setHeatMetric(VaporView::Geo::HeatMetric metric)
{
    if (!trajectory_layer_)
    {
        return;
    }
    trajectory_layer_->setHeatMetric(metric);
    update();
}

VaporView::Geo::HeatMetric OsgEarthViewWidget::heatMetric() const
{
    return trajectory_layer_ ? trajectory_layer_->heatMetric() : VaporView::Geo::HeatMetric::Peak;
}

void OsgEarthViewWidget::setHeatPalette(VaporView::Geo::HeatPalette palette)
{
    if (!trajectory_layer_)
    {
        return;
    }
    trajectory_layer_->setHeatPalette(palette);
    update();
}

VaporView::Geo::HeatPalette OsgEarthViewWidget::heatPalette() const
{
    return trajectory_layer_ ? trajectory_layer_->heatPalette() : VaporView::Geo::HeatPalette::Candy;
}

VaporView::Geo::HeatRange OsgEarthViewWidget::heatRange() const
{
    return trajectory_layer_ ? trajectory_layer_->heatRange() : VaporView::Geo::HeatRange{};
}

void OsgEarthViewWidget::setTrackLineVisible(bool visible)
{
    if (!trajectory_layer_)
    {
        return;
    }
    trajectory_layer_->setTrackLineVisible(visible);
    update();
}

bool OsgEarthViewWidget::trackLineVisible() const
{
    return trajectory_layer_ ? trajectory_layer_->trackLineVisible() : true;
}

void OsgEarthViewWidget::setTrackPointsVisible(bool visible)
{
    if (!trajectory_layer_)
    {
        return;
    }
    trajectory_layer_->setTrackPointsVisible(visible);
    update();
}

bool OsgEarthViewWidget::trackPointsVisible() const
{
    return trajectory_layer_ ? trajectory_layer_->trackPointsVisible() : true;
}

void OsgEarthViewWidget::setTrackLineWidth(float width)
{
    if (!trajectory_layer_)
    {
        return;
    }
    trajectory_layer_->setTrackLineWidth(width);
    update();
}

float OsgEarthViewWidget::trackLineWidth() const
{
    return trajectory_layer_ ? trajectory_layer_->trackLineWidth() : 5.0f;
}

void OsgEarthViewWidget::setTrackPointSize(float size)
{
    if (!trajectory_layer_)
    {
        return;
    }
    trajectory_layer_->setTrackPointSize(size);
    update();
}

float OsgEarthViewWidget::trackPointSize() const
{
    return trajectory_layer_ ? trajectory_layer_->trackPointSize() : 7.0f;
}

void OsgEarthViewWidget::startRendering()
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || rendering_failed_ || rendering_started_)
    {
        return;
    }

    rendering_started_ = true;
    QTimer::singleShot(1000, this, [this]() {
        if (!shutdown_ && rendering_started_ && isVisible() && !isValid())
            failRendering(mapText(QStringLiteral("无法创建 OpenGL 渲染上下文，请检查显卡驱动")));
    });
    // osgEarth creates a temporary native WGL context while it initializes.
    // Do not ask OSG to restore this widget through its temporary device context.
    if (QOpenGLContext::currentContext() == context())
    {
        doneCurrent();
    }
    try { initializeMap3DRuntime(); }
    catch (const std::exception& error) { failRendering(QString::fromUtf8(error.what())); return; }
    catch (...) { failRendering(mapText(QStringLiteral("渲染运行库初始化失败"))); return; }
    if (isVisible())
    {
        frame_interval_clock_.invalidate();
        frame_intervals_.clear();
        camera_activity_clock_.invalidate();
        frameTimer_.start(0);
    }
    update();
}

bool OsgEarthViewWidget::isRenderingStarted() const
{
    return rendering_started_ && !shutdown_;
}

bool OsgEarthViewWidget::flyToAircraft()
{
    const VaporView::Geo::NavSample* latest = latestRawSample();
    if (shutdown_ || !latest)
    {
        return false;
    }
    initializeSceneIfNeeded();
    osg::Vec3d center;
    if (earth_node_ && map_node_)
    {
        if (!sampleWorldFocusPosition(*latest, center))
        {
            return false;
        }
        setLookAt(center, 1200.0);
    }
    else
    {
        setLookAt(samplePosition(toDisplaySample(*latest)), 600.0);
    }
    update();
    return true;
}

bool OsgEarthViewWidget::flyToTrack()
{
    if (shutdown_ || sampleCount() == 0)
    {
        return false;
    }
    initializeSceneIfNeeded();

    osg::BoundingSphere bounds;
    std::vector<VaporView::Geo::NavSample> focusNavigationSamples;
    std::vector<VaporView::Geo::TrajectoryRenderSample> focusSamples;
    if (shared_nav_samples_)
    {
        focusNavigationSamples = uniformlySampleTrack(
            *shared_nav_samples_,
            static_cast<std::size_t>(shared_sample_count_),
            maxVisibleSamples());
    }
    else if (shared_samples_)
    {
        focusSamples = uniformlySampleTrack(*shared_samples_,
                                            static_cast<std::size_t>(shared_sample_count_),
                                            maxVisibleSamples());
    }
    else
    {
        focusSamples.assign(raw_samples_.cbegin(), raw_samples_.cend());
    }
    if (earth_node_ && map_node_)
    {
        for (const VaporView::Geo::NavSample& sample : focusNavigationSamples)
        {
            osg::Vec3d world;
            if (sampleWorldFocusPosition(sample, world))
            {
                bounds.expandBy(world);
            }
        }
        for (const VaporView::Geo::TrajectoryRenderSample& sample : focusSamples)
        {
            osg::Vec3d world;
            if (sampleWorldFocusPosition(sample.navigation, world))
            {
                bounds.expandBy(world);
            }
        }
    }
    else
    {
        for (const VaporView::Geo::NavSample& sample : focusNavigationSamples)
        {
            bounds.expandBy(samplePosition(toDisplaySample(sample)));
        }
        for (const VaporView::Geo::TrajectoryRenderSample& sample : focusSamples)
        {
            bounds.expandBy(samplePosition(toDisplaySample(sample.navigation)));
        }
    }
    if (!bounds.valid())
    {
        return false;
    }

    setLookAt(bounds.center(),
              trackFocusRangeM(static_cast<double>(bounds.radius()), earth_node_ && map_node_));
    update();
    return true;
}

void OsgEarthViewWidget::resetView()
{
    if (shutdown_)
    {
        return;
    }
    initializeSceneIfNeeded();
    setInitialEarthView();
    update();
}

int OsgEarthViewWidget::sampleCount() const
{
    return (shared_samples_ || shared_nav_samples_)
        ? shared_sample_count_
        : static_cast<int>(raw_samples_.size());
}

int OsgEarthViewWidget::visibleSampleCount() const
{
    return trajectory_layer_ ? trajectory_layer_->visibleSampleCount() : 0;
}

int OsgEarthViewWidget::maxVisibleSamples() const
{
    return trajectory_layer_ ? trajectory_layer_->maxVisibleSamples() : 0;
}

Map3DPerformanceStats OsgEarthViewWidget::performanceStats() const
{
    Map3DPerformanceStats stats;
    stats.totalSamples = sampleCount();
    stats.visibleSamples = visibleSampleCount();
    stats.maxVisibleSamples = maxVisibleSamples();
    stats.segmentCount = trajectory_layer_ ? trajectory_layer_->segmentCount() : 0;
    stats.segmentSize = trajectory_layer_ ? trajectory_layer_->segmentSize() : 0;
    stats.qualityStats = trajectory_layer_ ? trajectory_layer_->qualityStats() : TrajectoryQualityStats{};
    stats.frameMs = smoothed_frame_ms_ > 0.0 ? smoothed_frame_ms_ : last_frame_ms_;
    stats.framesPerSecond = frames_per_second_;
    stats.idleRendering = idle_rendering_;
    if (!frame_intervals_.empty())
    {
        std::vector<double> sorted(frame_intervals_.begin(), frame_intervals_.end());
        std::sort(sorted.begin(), sorted.end());
        stats.frameIntervalP95Ms = sorted[static_cast<std::size_t>(std::ceil(sorted.size() * 0.95)) - 1];
    }
    stats.presentMs = present_ms_;
    stats.pendingJobs = jobs::get_metrics()->total_pending();
    stats.runningJobs = jobs::get_metrics()->total_running();
    if (map_node_ && map_node_->getMap())
    {
        if (const auto* imagery = dynamic_cast<const Detail::TiandituImageLayer*>(
                map_node_->getMap()->getLayerByName(kTiandituSatelliteLayerName)))
        {
            stats.imageryRequests = imagery->requestCount();
            stats.imageryFailures = imagery->failureCount();
        }
    }
    stats.trackUpdateMs = last_track_update_ms_;
    stats.heightReferenceStatus = height_reference_status_;
    return stats;
}

QSize OsgEarthViewWidget::framebufferSize() const
{
    return framebuffer_size_;
}

bool OsgEarthViewWidget::hasEarthMap() const
{
    return earth_node_.valid();
}

void OsgEarthViewWidget::initializeGL()
try {
    if (!context() || !context()->isValid()) {
        failRendering(mapText(QStringLiteral("OpenGL 上下文不可用")));
        return;
    }
    if (shutdown_)
    {
        return;
    }
    initializeSceneIfNeeded();
    QObject::disconnect(gl_context_destruction_connection_);
    if (QOpenGLContext* openGlContext = context())
    {
        gl_context_destruction_connection_ =
            connect(openGlContext,
                    &QOpenGLContext::aboutToBeDestroyed,
                    this,
                    &OsgEarthViewWidget::releaseGlObjectsForContextDestruction,
                    Qt::DirectConnection);
    }
}

catch (const std::exception& error) { failRendering(QString::fromUtf8(error.what())); }
catch (...) { failRendering(mapText(QStringLiteral("OpenGL 初始化失败"))); }

void OsgEarthViewWidget::resizeGL(int w, int h)
try {
    if (rendering_failed_) return;
    if (shutdown_)
    {
        return;
    }
    initializeSceneIfNeeded();
    updateCameraViewport(w, h);
}

catch (const std::exception& error) { failRendering(QString::fromUtf8(error.what())); }
catch (...) { failRendering(mapText(QStringLiteral("渲染视口调整失败"))); }

void OsgEarthViewWidget::paintGL()
try {
    if (rendering_failed_) return;
    if (shutdown_)
    {
        return;
    }
    initializeSceneIfNeeded();
    if (viewer_)
    {
        QElapsedTimer timer;
        timer.start();
        updateCameraProjectionForCurrentView();
        viewer_->frame();
        const auto& viewMatrix = viewer_->getCamera()->getViewMatrix();
        if (!camera_activity_clock_.isValid() || last_view_matrix_ != viewMatrix)
        {
            last_view_matrix_ = viewMatrix;
            camera_activity_clock_.start();
        }
        updateTrajectoryInfoCardPosition();
        last_frame_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
        smoothed_frame_ms_ = smoothed_frame_ms_ <= 0.0
            ? last_frame_ms_
            : (smoothed_frame_ms_ * 0.9 + last_frame_ms_ * 0.1);
        present_clock_.start();
        frame_pending_presentation_ = true;
    }
}

catch (const std::exception& error) { failRendering(QString::fromUtf8(error.what())); }
catch (...) { failRendering(mapText(QStringLiteral("渲染帧失败"))); }

void OsgEarthViewWidget::failRendering(const QString& reason)
{
    if (rendering_failed_) return;
    rendering_failed_ = true;
    rendering_started_ = false;
    frameTimer_.stop();
    QTimer::singleShot(0, this, [this, reason]() { emit renderingFailed(reason); });
}

void OsgEarthViewWidget::mousePressEvent(QMouseEvent* event)
{
    if (shutdown_)
    {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }
    initializeSceneIfNeeded();
    setFocus(Qt::MouseFocusReason);
    mouse_press_position_ = event->position();
    mouse_press_tracks_selection_ = event->button() == Qt::LeftButton;
    mouse_dragged_since_press_ = false;
    const unsigned int button = toOsgMouseButton(event->button());
    if (graphics_window_ && button != 0)
    {
        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        graphics_window_->getEventQueue()->mouseButtonPress(
            static_cast<float>(event->position().x() * dpr),
            static_cast<float>(event->position().y() * dpr),
            button);
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(event);
}

void OsgEarthViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (shutdown_)
    {
        QOpenGLWidget::mouseReleaseEvent(event);
        return;
    }
    initializeSceneIfNeeded();
    const bool canSelectTrajectory =
        mouse_press_tracks_selection_
        && event->button() == Qt::LeftButton
        && !mouse_dragged_since_press_;
    const unsigned int button = toOsgMouseButton(event->button());
    if (graphics_window_ && button != 0)
    {
        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        graphics_window_->getEventQueue()->mouseButtonRelease(
            static_cast<float>(event->position().x() * dpr),
            static_cast<float>(event->position().y() * dpr),
            button);
        if (canSelectTrajectory)
        {
            selectTrajectorySampleAt(event->position());
        }
        mouse_press_tracks_selection_ = false;
        mouse_dragged_since_press_ = false;
        update();
        event->accept();
        return;
    }
    mouse_press_tracks_selection_ = false;
    mouse_dragged_since_press_ = false;
    QOpenGLWidget::mouseReleaseEvent(event);
}

void OsgEarthViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (shutdown_)
    {
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }
    initializeSceneIfNeeded();
    if (mouse_press_tracks_selection_)
    {
        const QPointF delta = event->position() - mouse_press_position_;
        const double distanceSq = delta.x() * delta.x() + delta.y() * delta.y();
        if (distanceSq > 25.0)
        {
            mouse_dragged_since_press_ = true;
        }
    }
    if (graphics_window_)
    {
        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        if (event->buttons() != Qt::NoButton) prioritizeSatelliteImagery();
        graphics_window_->getEventQueue()->mouseMotion(
            static_cast<float>(event->position().x() * dpr),
            static_cast<float>(event->position().y() * dpr));
        if (!frame_pending_presentation_ || !present_clock_.isValid() || present_clock_.elapsed() >= 50)
            update();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

void OsgEarthViewWidget::wheelEvent(QWheelEvent* event)
{
    if (shutdown_)
    {
        QOpenGLWidget::wheelEvent(event);
        return;
    }
    initializeSceneIfNeeded();
    if (graphics_window_ && event->angleDelta().y() != 0)
    {
        prioritizeSatelliteImagery();
        const osgGA::GUIEventAdapter::ScrollingMotion motion =
            event->angleDelta().y() > 0
                ? osgGA::GUIEventAdapter::SCROLL_UP
                : osgGA::GUIEventAdapter::SCROLL_DOWN;
        graphics_window_->getEventQueue()->mouseScroll(motion);
        if (!frame_pending_presentation_ || !present_clock_.isValid() || present_clock_.elapsed() >= 50)
            update();
        event->accept();
        return;
    }
    QOpenGLWidget::wheelEvent(event);
}

void OsgEarthViewWidget::keyPressEvent(QKeyEvent* event)
{
    if (shutdown_)
    {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }
    initializeSceneIfNeeded();
    if (event->key() == Qt::Key_Escape
        && trajectory_layer_
        && trajectory_layer_->selectedSampleIndex() >= 0)
    {
        clearTrajectorySampleSelection();
        update();
        event->accept();
        return;
    }
    const int key = toOsgKey(event);
    if (graphics_window_ && key != 0)
    {
        graphics_window_->getEventQueue()->keyPress(key);
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}

void OsgEarthViewWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (shutdown_)
    {
        QOpenGLWidget::keyReleaseEvent(event);
        return;
    }
    initializeSceneIfNeeded();
    const int key = toOsgKey(event);
    if (graphics_window_ && key != 0)
    {
        graphics_window_->getEventQueue()->keyRelease(key);
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::keyReleaseEvent(event);
}

void OsgEarthViewWidget::initializeSceneIfNeeded()
{
    if (shutdown_ || !rendering_started_)
    {
        return;
    }
    if (initialized_)
    {
        return;
    }

    root_ = new osg::Group;
    root_->addChild(createStarfieldNode());
    overlay_transform_ = new osg::MatrixTransform;
    overlay_transform_->addChild(trajectory_layer_->node());
    overlay_transform_->addChild(aircraft_layer_->node());
    root_->addChild(createLocalGridNode());
    root_->addChild(overlay_transform_.get());
    loadDefaultAircraftModelIfAvailable();

    viewer_ = std::make_unique<osgViewer::Viewer>();
    viewer_->setThreadingModel(osgViewer::Viewer::SingleThreaded);
    viewer_->setRealizeOperation(new osgEarth::GL3RealizeOperation);
    viewer_->setSceneData(root_.get());
    if (!follow_aircraft_)
    {
        viewer_->setCameraManipulator(new osgGA::TrackballManipulator);
    }

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const int framebufferWidth = (std::max)(1, static_cast<int>(std::lround(width() * dpr)));
    const int framebufferHeight = (std::max)(1, static_cast<int>(std::lround(height() * dpr)));
    graphics_window_ = new osgViewer::GraphicsWindowEmbedded(0, 0, framebufferWidth, framebufferHeight);
    if (viewer_->getCamera())
    {
        viewer_->getCamera()->setGraphicsContext(graphics_window_.get());
        viewer_->getCamera()->setClearColor(osg::Vec4(0.004f, 0.008f, 0.022f, 1.0f));
        viewer_->getCamera()->setViewMatrixAsLookAt(osg::Vec3d(220.0, -320.0, 240.0),
                                                    osg::Vec3d(0.0, 0.0, 0.0),
                                                    osg::Vec3d(0.0, 0.0, 1.0));
    }
    updateCameraViewport(width(), height());
    osgEarth::GL3RealizeOperation gl3RealizeOperation;
    gl3RealizeOperation(graphics_window_.get());
    initialized_ = true;
}

void OsgEarthViewWidget::loadDefaultAircraftModelIfAvailable()
{
    const QString modelPath = firstExistingMap3DFile(map3DRuntimeRootCandidates(),
                                                {QStringLiteral("resources/maps/models/aircraft/vaporview_aircraft.osgb"),
                                                 QStringLiteral("resources/maps/models/aircraft/vaporview_aircraft.osg"),
                                                 QStringLiteral("resources/maps/models/aircraft/vaporview_aircraft.glb"),
                                                 QStringLiteral("resources/maps/models/aircraft/vaporview_aircraft.gltf")});
    loadAircraftModelFileAsync(modelPath, QStringLiteral("Default aircraft model"));
}

bool OsgEarthViewWidget::loadAircraftModelFile(const QString& modelPath,
                                               const QString& fallbackReasonPrefix)
{
    const Detail::AircraftAssetLoadResult result =
        Detail::loadAircraftAsset(modelPath, fallbackReasonPrefix);
    return applyAircraftModelLoad(result.diagnostics, result.node);
}

void OsgEarthViewWidget::loadAircraftModelFileAsync(
    const QString& modelPath,
    const QString& fallbackReasonPrefix,
    std::function<void(bool)> finished)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_)
    {
        if (finished) finished(false);
        return;
    }
    const quint64 generation = ++aircraft_load_generation_;
    aircraft_model_diagnostics_ = {};
    aircraft_model_diagnostics_.attempted = !modelPath.trimmed().isEmpty();
    aircraft_model_diagnostics_.requestedPath = modelPath;
    auto* watcher = new QFutureWatcher<Detail::AircraftAssetLoadResult>(this);
    registerAsyncWatcher(watcher);
    connect(watcher, &QFutureWatcher<Detail::AircraftAssetLoadResult>::finished,
            this, [this, watcher, generation, finished = std::move(finished)]() mutable {
        unregisterAsyncWatcher(watcher);
        watcher->deleteLater();
        if (shutdown_ || generation != aircraft_load_generation_) return;
        if (watcher->isCanceled() || watcher->future().resultCount()==0) {
            if (finished) finished(false);
            return;
        }
        const Detail::AircraftAssetLoadResult result = watcher->result();
        const bool loaded = applyAircraftModelLoad(result.diagnostics, result.node);
        if (finished) finished(loaded);
    });
    watcher->setFuture(QtConcurrent::run([modelPath, fallbackReasonPrefix]() {
        return Detail::loadAircraftAsset(modelPath, fallbackReasonPrefix);
    }));
}

bool OsgEarthViewWidget::applyAircraftModelLoad(AircraftModelDiagnostics diagnostics,
                                                osg::ref_ptr<osg::Node> node)
{
    assertGuiThread(this, Q_FUNC_INFO);
    if (shutdown_ || !aircraft_layer_) return false;
    if (!diagnostics.loaded || !node)
    {
        diagnostics.usingBuiltInMarker = !aircraft_layer_->hasCustomModel();
        aircraft_model_diagnostics_ = std::move(diagnostics);
        return false;
    }
    aircraft_layer_->setCustomModel(node.get());
    aircraft_model_diagnostics_ = std::move(diagnostics);
    update();
    return true;
}

void OsgEarthViewWidget::updateCameraViewport(int w, int h)
{
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());

    const int safeWidth = (std::max)(1, static_cast<int>(std::lround(w * dpr)));
    const int safeHeight = (std::max)(1, static_cast<int>(std::lround(h * dpr)));
    framebuffer_size_ = QSize(safeWidth, safeHeight);
    if (trajectory_layer_)
    {
        trajectory_layer_->setViewportMetrics(safeWidth, safeHeight, static_cast<float>(dpr));
    }

    if (graphics_window_)
    {
        graphics_window_->resized(0, 0, safeWidth, safeHeight);
        graphics_window_->getEventQueue()->windowResize(0, 0, safeWidth, safeHeight);
    }

    if (viewer_ && viewer_->getCamera())
    {
        viewer_->getCamera()->setViewport(new osg::Viewport(0, 0, safeWidth, safeHeight));
        updateCameraProjectionForCurrentView();
    }
}

void OsgEarthViewWidget::updateCameraProjectionForCurrentView()
{
    if (!viewer_ || !viewer_->getCamera())
    {
        return;
    }

    const int safeWidth = (std::max)(1, framebuffer_size_.width());
    const int safeHeight = (std::max)(1, framebuffer_size_.height());
    const double rangeM = earth_node_ && map_node_
        ? currentEarthManipulatorDistance(viewer_.get())
        : (std::numeric_limits<double>::quiet_NaN)();
    const double pitchDeg = earth_node_ && map_node_
        ? currentEarthManipulatorPitchDeg(viewer_.get())
        : (std::numeric_limits<double>::quiet_NaN)();
    const EarthProjectionProfile projectionProfile =
        earthProjectionProfile(rangeM, pitchDeg);
    // A newly created globe opens its terrain engine during its first traversal.
    if (terrain_detail_callback_pending_ && map_node_ && map_node_->getTerrainEngine())
    {
        map_node_->getTerrainEngine()->setComputeTilePixelSizeCallback(
            [ellipse = ground_detail_ellipse_](osg::Node* node, osg::NodeVisitor&) {
                const auto* tile = dynamic_cast<const osgEarth::TerrainTile*>(node);
                // REX interprets one pixel as too small to subdivide and
                // nonpositive values as a request for native screen-space LOD.
                if (tile && tile->getKey().getLOD() >= ellipse->maxAllowedLod(node->getBound(), kTerrainMinLevel))
                    return 1.0f;
                return -1.0f;
            });
        terrain_detail_callback_pending_ = false;
    }
    auto* manipulator = dynamic_cast<osgEarth::EarthManipulator*>(viewer_->getCameraManipulator());
    if (manipulator && map_node_ && map_node_->isGeocentric())
        ground_detail_ellipse_->update(map_node_->getMapSRS()->getEllipsoid(),
                                       manipulator->getMatrix(), rangeM, pitchDeg);
    else
        *ground_detail_ellipse_ = {};

    osg::Camera* camera = viewer_->getCamera();
    const osg::Matrixd projection = osg::Matrixd::perspective(
        30.0,
        static_cast<double>(safeWidth) / static_cast<double>(safeHeight),
        kEarthProjectionNearPlaneM,
        (std::max)(projectionProfile.farPlaneM, ground_detail_ellipse_->requiredFarPlaneM));
    if (camera->getProjectionMatrix() != projection)
        camera->setProjectionMatrix(projection);
    camera->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
    camera->setCullingMode(osg::CullSettings::ENABLE_ALL_CULLING);
    camera->setSmallFeatureCullingPixelSize(projectionProfile.smallFeatureCullPixels);
    camera->setLODScale(projectionProfile.lodScale);
}

void OsgEarthViewWidget::updateFollowCamera(const VaporView::Geo::NavSample& sample)
{
    if (!follow_aircraft_ || !viewer_ || !viewer_->getCamera())
    {
        return;
    }

    const osg::Vec3d center = samplePosition(sample);
    const double yawRad = aircraftHeadingRad(sample);
    constexpr double kFollowDistanceM = 160.0;
    constexpr double kFollowHeightM = 90.0;

    if (earth_node_ && sample.hasEcef() && sample.hasLlh())
    {
        osgEarth::GeoPoint focalPoint;
        const osgEarth::SpatialReference* wgs84 = osgEarth::SpatialReference::get("wgs84");
        if (wgs84 && focalPoint.fromWorld(wgs84, center))
        {
            osgEarth::EarthManipulator* manipulator = ensureEarthManipulator(viewer_.get());
            if (manipulator)
            {
                const double rangeM = std::hypot(kFollowDistanceM, kFollowHeightM);
                const double pitchDeg = -std::atan2(kFollowHeightM, kFollowDistanceM)
                    * 180.0 / 3.14159265358979323846;
                manipulator->setViewpoint(
                    osgEarth::Viewpoint("Follow aircraft",
                                        focalPoint.x(),
                                        focalPoint.y(),
                                        focalPoint.z(),
                                        aircraftHeadingDeg(sample),
                                        pitchDeg,
                                        rangeM),
                    0.0);
                manipulator->updateCamera(*viewer_->getCamera());
                return;
            }
        }
        return;
    }

    if (!sample.hasNed())
    {
        return;
    }

    const osg::Vec3d eye(center.x() - std::sin(yawRad) * kFollowDistanceM,
                         center.y() - std::cos(yawRad) * kFollowDistanceM,
                         center.z() + kFollowHeightM);
    osgGA::TrackballManipulator* manipulator = ensureTrackballManipulator(viewer_.get());
    if (manipulator)
    {
        manipulator->setHomePosition(eye, center, osg::Vec3d(0.0, 0.0, 1.0), false);
        manipulator->home(0.0);
        manipulator->updateCamera(*viewer_->getCamera());
    }
}

void OsgEarthViewWidget::showEvent(QShowEvent* event)
{
    QOpenGLWidget::showEvent(event);
    if (!shutdown_ && rendering_started_)
    {
        frame_interval_clock_.invalidate();
        frame_intervals_.clear();
        camera_activity_clock_.invalidate();
        frameTimer_.start(0);
    }
}

void OsgEarthViewWidget::hideEvent(QHideEvent* event)
{
    frameTimer_.stop();
    QOpenGLWidget::hideEvent(event);
}

void OsgEarthViewWidget::releaseGlObjectsForContextDestruction()
{
    if (shutdown_ || !root_ || QOpenGLContext::currentContext() != context())
    {
        return;
    }
    root_->releaseGLObjects(nullptr);
}

void OsgEarthViewWidget::setInitialEarthView()
{
    if (!viewer_)
    {
        return;
    }

    if (!earth_node_ || !map_node_)
    {
        osg::ref_ptr<osgGA::TrackballManipulator> manipulator = new osgGA::TrackballManipulator;
        manipulator->setHomePosition(osg::Vec3d(0.0, -kEarthRadiusM * 3.0, kEarthRadiusM * 1.2),
                                     osg::Vec3d(0.0, 0.0, 0.0),
                                     osg::Vec3d(0.0, 0.0, 1.0),
                                     false);
        viewer_->setCameraManipulator(manipulator.get());
        manipulator->home(0.0);

        if (viewer_->getCamera())
        {
            viewer_->getCamera()->setViewMatrixAsLookAt(osg::Vec3d(0.0, -kEarthRadiusM * 3.0, kEarthRadiusM * 1.2),
                                                        osg::Vec3d(0.0, 0.0, 0.0),
                                                        osg::Vec3d(0.0, 0.0, 1.0));
        }
        return;
    }

    osg::ref_ptr<osgEarth::EarthManipulator> manipulator = new osgEarth::EarthManipulator;
    configureEarthManipulator(manipulator.get());
    viewer_->setCameraManipulator(manipulator.get());
    const osgEarth::Viewpoint initialView =
        use_xihu_initial_view_
            ? osgEarth::Viewpoint("Hangzhou Xihu", 120.10, 30.25, 0.0, 0.0, -90.0, 18000000.0)
            : osgEarth::Viewpoint("VaporView Earth", 0.0, 20.0, 0.0, 0.0, -90.0, 18000000.0);
    manipulator->setViewpoint(initialView, 0.0);
    if (viewer_->getCamera())
    {
        manipulator->updateCamera(*viewer_->getCamera());
    }
}

void OsgEarthViewWidget::setLookAt(const osg::Vec3d& center, double distanceM)
{
    if (!viewer_ || !viewer_->getCamera())
    {
        return;
    }

    const double safeDistance = (std::max)(50.0, distanceM);
    if (earth_node_ && map_node_)
    {
        if (!(center.length2() > 1.0))
        {
            return;
        }
        osgEarth::GeoPoint focalPoint;
        const osgEarth::SpatialReference* wgs84 = osgEarth::SpatialReference::get("wgs84");
        osgEarth::EarthManipulator* manipulator = ensureEarthManipulator(viewer_.get());
        if (wgs84 && manipulator && focalPoint.fromWorld(wgs84, center))
        {
            prioritizeSatelliteImagery();
            manipulator->setViewpoint(
                osgEarth::Viewpoint("Map target",
                                    focalPoint.x(),
                                    focalPoint.y(),
                                    focalPoint.z(),
                                    0.0,
                                    -35.0,
                                    safeDistance),
                0.0);
            manipulator->updateCamera(*viewer_->getCamera());
            return;
        }
        return;
    }

    const osg::Vec3d eye(center.x() - safeDistance,
                         center.y() - safeDistance,
                         center.z() + safeDistance * 0.65);
    osgGA::TrackballManipulator* manipulator = ensureTrackballManipulator(viewer_.get());
    if (manipulator)
    {
        manipulator->setHomePosition(eye, center, osg::Vec3d(0.0, 0.0, 1.0), false);
        manipulator->home(0.0);
        manipulator->updateCamera(*viewer_->getCamera());
    }
}

bool OsgEarthViewWidget::selectTrajectorySampleAt(const QPointF& widgetPosition)
{
    if (shutdown_
        || !layerVisible(Map3DLayer::FlightElements)
        || !trajectory_layer_
        || !viewer_
        || !viewer_->getCamera())
    {
        return false;
    }

    osg::Camera* camera = viewer_->getCamera();
    osg::Viewport* viewport = camera->getViewport();
    if (!viewport)
    {
        return false;
    }

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const double screenX = widgetPosition.x() * dpr;
    const double screenYTop = widgetPosition.y() * dpr;
    const double screenYBottom =
        static_cast<double>((std::max)(1, framebuffer_size_.height())) - screenYTop;
    const osg::Matrixd localToWorld =
        overlay_transform_.valid() ? overlay_transform_->getMatrix() : osg::Matrixd::identity();
    const osg::Matrixd localToWindow =
        localToWorld
        * camera->getViewMatrix()
        * camera->getProjectionMatrix()
        * viewport->computeWindowMatrix();

    constexpr double kPickRadiusPx = 16.0;
    std::optional<TrajectoryPickResult> pick =
        trajectory_layer_->pickNearestSample(localToWindow,
                                             screenX,
                                             screenYBottom,
                                             kPickRadiusPx * static_cast<double>(dpr));
    if (!pick)
    {
        pick = trajectory_layer_->pickNearestSample(localToWindow,
                                                    screenX,
                                                    screenYTop,
                                                    kPickRadiusPx * static_cast<double>(dpr));
    }
    if (!pick)
    {
        clearTrajectorySampleSelection();
        update();
        return false;
    }

    trajectory_layer_->setSelectedSampleIndex(pick->sampleIndex);
    emit trajectoryRenderSampleSelected(pick->sampleIndex, pick->renderSample);
    emit trajectorySampleSelected(pick->sampleIndex, pick->renderSample.navigation);
    update();
    return true;
}

void OsgEarthViewWidget::clearTrajectorySampleSelection()
{
    const bool hadSelection = trajectory_layer_
        && trajectory_layer_->selectedSampleIndex() >= 0;
    if (trajectory_layer_)
    {
        trajectory_layer_->setSelectedSampleIndex(-1);
    }
    if (trajectory_info_card_)
    {
        trajectory_info_card_->hide();
    }
    if (hadSelection)
    {
        emit trajectorySampleSelectionCleared();
    }
}

void OsgEarthViewWidget::showTrajectoryInfoCard(
    int sampleIndex,
    const VaporView::Geo::TrajectoryRenderSample& sample)
{
    auto* card = static_cast<TrajectorySampleInfoCard*>(trajectory_info_card_);
    if (!card)
    {
        return;
    }
    card->setSample(sampleIndex, sample, Geo::heatMetricUnit(Geo::HeatMetric::ColumnWaterConcentration, water_unit_));
    updateTrajectoryInfoCardPosition();
}

void OsgEarthViewWidget::updateTrajectoryInfoCardPosition()
{
    auto* card = static_cast<TrajectorySampleInfoCard*>(trajectory_info_card_);
    if (!card
        || shutdown_
        || !layerVisible(Map3DLayer::FlightElements)
        || !trajectory_layer_
        || trajectory_layer_->selectedSampleIndex() < 0
        || !viewer_
        || !viewer_->getCamera())
    {
        if (card)
        {
            card->hide();
        }
        return;
    }

    osg::Vec3d localPosition;
    if (!trajectory_layer_->displayPositionForSample(
            trajectory_layer_->selectedSampleIndex(), localPosition))
    {
        card->hide();
        return;
    }

    osg::Camera* camera = viewer_->getCamera();
    osg::Viewport* viewport = camera->getViewport();
    if (!viewport)
    {
        card->hide();
        return;
    }
    const osg::Matrixd localToWorld = overlay_transform_.valid()
        ? overlay_transform_->getMatrix()
        : osg::Matrixd::identity();
    const osg::Matrixd localToWindow =
        localToWorld
        * camera->getViewMatrix()
        * camera->getProjectionMatrix()
        * viewport->computeWindowMatrix();
    const osg::Vec3d projected = localPosition * localToWindow;
    if (!std::isfinite(projected.x())
        || !std::isfinite(projected.y())
        || !std::isfinite(projected.z())
        || projected.z() < 0.0
        || projected.z() > 1.0)
    {
        card->hide();
        return;
    }

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const QPointF anchor(projected.x() / dpr,
                         (static_cast<double>(framebuffer_size_.height()) - projected.y()) / dpr);
    if (anchor.x() < 0.0
        || anchor.x() > width()
        || anchor.y() < 0.0
        || anchor.y() > height())
    {
        card->hide();
        return;
    }
    card->placeAt(anchor, size());
    card->show();
}

void OsgEarthViewWidget::rebuildDisplayTrack()
{
    clearTrajectorySampleSelection();
    trajectory_layer_->clear();
    aircraft_layer_->clear();
    if (shared_nav_samples_)
    {
        const std::vector<VaporView::Geo::NavSample> selectedSamples =
            uniformlySampleTrack(*shared_nav_samples_,
                                 static_cast<std::size_t>(shared_sample_count_),
                                 maxVisibleSamples());
        std::vector<VaporView::Geo::NavSample> displaySamples;
        displaySamples.reserve(selectedSamples.size());
        for (const VaporView::Geo::NavSample& sample : selectedSamples)
        {
            displaySamples.push_back(toDisplaySample(sample));
        }
        trajectory_layer_->appendNavigationSamples(displaySamples);
    }
    else if (track_data_mode_ == TrackDataMode::LiveNavigation)
    {
        std::vector<VaporView::Geo::NavSample> displaySamples;
        displaySamples.reserve(raw_samples_.size());
        for (const VaporView::Geo::TrajectoryRenderSample& sample : raw_samples_)
        {
            displaySamples.push_back(toDisplaySample(sample.navigation));
        }
        trajectory_layer_->appendNavigationSamples(displaySamples);
    }
    else
    {
        std::vector<VaporView::Geo::TrajectoryRenderSample> selectedSamples;
        if (shared_samples_)
        {
            selectedSamples = uniformlySampleTrack(*shared_samples_,
                                                   static_cast<std::size_t>(shared_sample_count_),
                                                   maxVisibleSamples());
        }
        else
        {
            selectedSamples.assign(raw_samples_.cbegin(), raw_samples_.cend());
        }
        std::vector<VaporView::Geo::TrajectoryRenderSample> displaySamples;
        displaySamples.reserve(selectedSamples.size());
        for (const VaporView::Geo::TrajectoryRenderSample& sample : selectedSamples)
        {
            displaySamples.push_back(toDisplaySample(sample));
        }
        trajectory_layer_->appendRenderSamples(displaySamples);
    }
    if (const VaporView::Geo::NavSample* latest = latestRawSample())
    {
        const VaporView::Geo::NavSample latestDisplaySample = toDisplaySample(*latest);
        aircraft_layer_->updateSample(latestDisplaySample);
        updateFollowCamera(latestDisplaySample);
    }
}

void OsgEarthViewWidget::detachSharedSamplesForLiveAppend()
{
    if (!shared_samples_ && !shared_nav_samples_)
    {
        preserve_full_track_extent_ = false;
        return;
    }
    if (shared_samples_)
    {
        const auto end = shared_samples_->cbegin() + shared_sample_count_;
        raw_samples_.assign(shared_samples_->cbegin(), end);
    }
    else if (shared_nav_samples_)
    {
        const auto end = shared_nav_samples_->cbegin() + shared_sample_count_;
        for (auto it = shared_nav_samples_->cbegin(); it != end; ++it)
        {
            raw_samples_.push_back(renderSampleFromNavSample(*it));
        }
    }
    shared_samples_.reset();
    shared_nav_samples_.reset();
    shared_sample_count_ = 0;
    preserve_full_track_extent_ = false;
    while (static_cast<int>(raw_samples_.size()) > maxVisibleSamples())
    {
        raw_samples_.pop_front();
    }
    rebuildDisplayTrack();
}

const VaporView::Geo::NavSample* OsgEarthViewWidget::latestRawSample() const
{
    if (shared_samples_ && shared_sample_count_ > 0)
    {
        return &(*shared_samples_)[static_cast<std::size_t>(shared_sample_count_ - 1)].navigation;
    }
    if (shared_nav_samples_ && shared_sample_count_ > 0)
    {
        return &(*shared_nav_samples_)[static_cast<std::size_t>(shared_sample_count_ - 1)];
    }
    return raw_samples_.empty() ? nullptr : &raw_samples_.back().navigation;
}

VaporView::Geo::TrajectoryRenderSample OsgEarthViewWidget::toDisplaySample(
    const VaporView::Geo::TrajectoryRenderSample& sample)
{
    VaporView::Geo::TrajectoryRenderSample displaySample = sample;
    displaySample.navigation = toDisplaySample(sample.navigation);
    return displaySample;
}

VaporView::Geo::NavSample OsgEarthViewWidget::toDisplaySample(const VaporView::Geo::NavSample& sample)
{
    if (earth_node_)
    {
        return toWorldSample(sample);
    }
    return toLocalSample(sample);
}

void OsgEarthViewWidget::resetWorldOverlayOrigin()
{
    has_world_overlay_origin_ = false;
    world_overlay_origin_.set(0.0, 0.0, 0.0);
    if (overlay_transform_)
    {
        overlay_transform_->setMatrix(osg::Matrix::identity());
    }
    if (trajectory_layer_)
    {
        trajectory_layer_->clearWorldOrigin();
    }
    if (aircraft_layer_)
    {
        aircraft_layer_->clearWorldOrigin();
    }
}

void OsgEarthViewWidget::updateWorldOverlayOriginFromSample(const VaporView::Geo::NavSample& sample)
{
    if (!earth_node_ || !sample.hasEcef())
    {
        return;
    }

    if (has_world_overlay_origin_)
    {
        return;
    }

    world_overlay_origin_.set(sample.ecefXM, sample.ecefYM, sample.ecefZM);
    has_world_overlay_origin_ = true;
    if (overlay_transform_)
    {
        overlay_transform_->setMatrix(osg::Matrix::translate(world_overlay_origin_));
    }
    if (trajectory_layer_)
    {
        trajectory_layer_->setWorldOrigin(world_overlay_origin_);
    }
    if (aircraft_layer_)
    {
        aircraft_layer_->setWorldOrigin(world_overlay_origin_);
    }
}

VaporView::Geo::NavSample OsgEarthViewWidget::toLocalSample(const VaporView::Geo::NavSample& sample)
{
    if (sample.hasNed() || !sample.hasLlh())
    {
        return sample;
    }

    if (!local_frame_.isValid())
    {
        local_frame_ = VaporView::Geo::LocalTangentPlane(sample);
    }

    VaporView::Geo::NavSample local = sample;
    const VaporView::Geo::NedPoint ned = VaporView::Geo::navSampleToNed(sample, local_frame_);
    local.nedNM = ned.northM;
    local.nedEM = ned.eastM;
    local.nedDM = ned.downM;
    return local;
}

VaporView::Geo::NavSample OsgEarthViewWidget::toWorldSample(const VaporView::Geo::NavSample& sample)
{
    const bool hasRecordedEcef = sample.hasEcef();
    if (hasRecordedEcef)
    {
        height_reference_status_ = QStringLiteral("Using recorded ECEF position; no height datum conversion required.");
        VaporView::Geo::NavSample worldSample = sample;
        if (!worldSample.hasLlh())
        {
            const VaporView::Geo::LlhPoint llh = VaporView::Geo::ecefToLlh(
                {sample.ecefXM, sample.ecefYM, sample.ecefZM});
            worldSample.latDeg = llh.latDeg;
            worldSample.lonDeg = llh.lonDeg;
            worldSample.heightM = llh.heightM;
            worldSample.heightReference = VaporView::Geo::HeightReference::Wgs84Ellipsoid;
        }
        updateWorldOverlayOriginFromSample(worldSample);
        return worldSample;
    }

    if (!sample.hasLlh())
    {
        return sample;
    }

    if (sample.heightReference != VaporView::Geo::HeightReference::Wgs84Ellipsoid)
    {
        height_reference_status_ = QStringLiteral(
            "Height datum cannot be converted without recorded ECEF; sample omitted from the Earth overlay.");
        VaporView::Geo::NavSample invalidWorldSample = sample;
        const double invalid = (std::numeric_limits<double>::quiet_NaN)();
        invalidWorldSample.latDeg = invalid;
        invalidWorldSample.lonDeg = invalid;
        invalidWorldSample.heightM = invalid;
        invalidWorldSample.ecefXM = invalid;
        invalidWorldSample.ecefYM = invalid;
        invalidWorldSample.ecefZM = invalid;
        invalidWorldSample.nedNM = invalid;
        invalidWorldSample.nedEM = invalid;
        invalidWorldSample.nedDM = invalid;
        return invalidWorldSample;
    }

    height_reference_status_ = mapText(QStringLiteral("已应用 WGS84 椭球高。"));

    osg::Vec3d world;
    bool converted = false;
    if (const osgEarth::SpatialReference* wgs84 =
            osgEarth::SpatialReference::get("wgs84"))
    {
        osgEarth::GeoPoint geo(
            wgs84, sample.lonDeg, sample.latDeg, sample.heightM, osgEarth::ALTMODE_ABSOLUTE);
        converted = geo.toWorld(world) &&
                    std::isfinite(world.x()) &&
                    std::isfinite(world.y()) &&
                    std::isfinite(world.z());
    }
    if (!converted)
    {
        converted = llhToWgs84Ecef(sample, world);
    }
    if (!converted)
    {
        return sample;
    }

    VaporView::Geo::NavSample worldSample = sample;
    worldSample.ecefXM = world.x();
    worldSample.ecefYM = world.y();
    worldSample.ecefZM = world.z();
    updateWorldOverlayOriginFromSample(worldSample);
    return worldSample;
}

} // namespace VaporView::Map3D
