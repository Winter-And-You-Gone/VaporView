#pragma once

#include "geo/CoordinateTransform.h"
#include "geo/GeoTypes.h"
#include "geo/TrajectoryHeatmap.h"
#include "map3d/Trajectory3DLayer.h"

#include <QOpenGLWidget>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/Node>
#include <osg/ref_ptr>
#include <osgViewer/GraphicsWindow>
#include <deque>
#include <array>
#include <functional>
#include <memory>
#include <vector>

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QHideEvent;
class QShowEvent;
class QFutureWatcherBase;

namespace osgViewer {
class Viewer;
}

namespace osgEarth {
class MapNode;
}

namespace VaporView::Map3D {

enum class Map3DLayer
{
    BaseMap,
    SatelliteImagery,
    DigitalElevation,
    Hydrography,
    RoadNetwork,
    Buildings3D,
    FlightElements,
    Count
};

inline constexpr std::size_t kMap3DLayerCount =
    static_cast<std::size_t>(Map3DLayer::Count);

class Aircraft3DLayer;
class Trajectory3DLayer;

struct Map3DPerformanceStats {
    int totalSamples = 0;
    int visibleSamples = 0;
    int maxVisibleSamples = 0;
    int segmentCount = 0;
    int segmentSize = 0;
    TrajectoryQualityStats qualityStats;
    double frameMs = 0.0;
    double framesPerSecond = 0.0;
    double trackUpdateMs = 0.0;
    QString heightReferenceStatus;
};

struct EarthLoadDiagnostics {
    QString requestedPath;
    bool attempted = false;
    bool loaded = false;
    bool usedTexturedFallback = false;
    bool foundMapNode = false;
    int layerCount = 0;
    int openLayerCount = 0;
    QString failureReason;
    QStringList layerSummaries;
};

struct Local3DTilesLoadDiagnostics {
    QString requestedPath;
    bool attempted = false;
    bool loaded = false;
    bool clearedPreviousPreview = false;
    QString failureReason;
    QString nodeDescription;
    int tileCount = 0;
    int payloadCount = 0;
    int loadedPayloadCount = 0;
    int failedPayloadCount = 0;
    QStringList warnings;
};

struct AircraftModelDiagnostics {
    QString requestedPath;
    bool attempted = false;
    bool loaded = false;
    bool usingBuiltInMarker = true;
    QString failureReason;
    QString nodeDescription;
};

class OsgEarthViewWidget final : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit OsgEarthViewWidget(QWidget* parent = nullptr, bool deferRendering = false);
    ~OsgEarthViewWidget() override;

    void appendNavigationSample(const VaporView::Geo::NavSample& sample);
    void appendRenderSample(const VaporView::Geo::TrajectoryRenderSample& sample);
    void appendNavigationSamples(const std::vector<VaporView::Geo::NavSample>& samples);
    void appendRenderSamples(const std::vector<VaporView::Geo::TrajectoryRenderSample>& samples);
    void setSamples(const std::vector<VaporView::Geo::NavSample>& samples);
    void setSamples(const std::vector<VaporView::Geo::TrajectoryRenderSample>& samples);
    void setSamples(std::shared_ptr<const std::vector<VaporView::Geo::NavSample>> samples,
                    int sampleCount = -1);
    void setSamples(std::shared_ptr<const std::vector<VaporView::Geo::TrajectoryRenderSample>> samples,
                    int sampleCount = -1);
    void appendSampleFromStorage(
        const std::shared_ptr<const std::vector<VaporView::Geo::NavSample>>& samples,
        int sampleIndex);
    void appendSampleFromStorage(
        const std::shared_ptr<const std::vector<VaporView::Geo::TrajectoryRenderSample>>& samples,
        int sampleIndex);
    void clearTrack();
    bool loadEarthFile(const QString& earthPath);
    void loadEarthFileAsync(const QString& earthPath, std::function<void(bool)> finished);
    void loadEarthFilePreservingViewAsync(const QString& earthPath, std::function<void(bool)> finished);
    bool applyTiandituSatelliteImagery(const QString& key);
    bool loadLocal3DTilesPreview(const QString& tilesetPath);
    void loadLocal3DTilesPreviewAsync(const QString& tilesetPath,
                                      std::function<void(bool)> finished);
    void clearLocal3DTilesPreview();
    bool loadAircraftModel(const QString& modelPath);
    void loadAircraftModelAsync(const QString& modelPath, std::function<void(bool)> finished);
    void resetAircraftModelToBuiltIn();
    void setLayerVisible(Map3DLayer layer, bool visible);
    void setFollowAircraft(bool enabled);
    void setMaxVisibleSamples(int maxVisibleSamples);
    void setHeatMetric(VaporView::Geo::HeatMetric metric);
    VaporView::Geo::HeatMetric heatMetric() const;
    void setHeatPalette(VaporView::Geo::HeatPalette palette);
    VaporView::Geo::HeatPalette heatPalette() const;
    VaporView::Geo::HeatRange heatRange() const;
    void setTrackLineVisible(bool visible);
    bool trackLineVisible() const;
    void setTrackPointsVisible(bool visible);
    bool trackPointsVisible() const;
    void setTrackLineWidth(float width);
    float trackLineWidth() const;
    void setTrackPointSize(float size);
    float trackPointSize() const;
    void startRendering();
    bool isRenderingStarted() const;
    bool flyToAircraft();
    bool flyToTrack();
    void resetView();
    void shutdown();

    int sampleCount() const;
    int visibleSampleCount() const;
    int maxVisibleSamples() const;
    Map3DPerformanceStats performanceStats() const;
    EarthLoadDiagnostics earthLoadDiagnostics() const;
    Local3DTilesLoadDiagnostics local3DTilesLoadDiagnostics() const;
    AircraftModelDiagnostics aircraftModelDiagnostics() const;
    QSize framebufferSize() const;
    bool hasEarthMap() const;
    bool hasLocal3DTilesPreview() const;
    bool layerVisible(Map3DLayer layer) const;
    bool layerAvailable(Map3DLayer layer) const;
    double earthCameraRangeM() const;

signals:
    void trajectorySampleSelected(int sampleIndex, VaporView::Geo::NavSample sample);
    void trajectorySampleSelectionCleared();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    enum class TrackDataMode {
        LiveNavigation,
        RenderSamples,
    };
    void registerAsyncWatcher(QFutureWatcherBase* watcher);
    void unregisterAsyncWatcher(QFutureWatcherBase* watcher);
    void cancelAsyncWatchers();
    void initializeSceneIfNeeded();
    void updateCameraViewport(int w, int h);
    void updateCameraProjectionForCurrentView();
    void loadDefaultAircraftModelIfAvailable();
    void loadEarthFileAsync(const QString& earthPath,
                            bool preserveCurrentEarthView,
                            std::function<void(bool)> finished);
    bool loadAircraftModelFile(const QString& modelPath, const QString& fallbackReasonPrefix);
    void loadAircraftModelFileAsync(const QString& modelPath,
                                    const QString& fallbackReasonPrefix,
                                    std::function<void(bool)> finished = {});
    bool applyEarthLoad(EarthLoadDiagnostics diagnostics,
                        osg::ref_ptr<osg::Node> node,
                        osgEarth::MapNode* mapNode,
                        bool useXihuInitialView,
                        bool preserveCurrentEarthView);
    bool applyLocal3DTilesLoad(Local3DTilesLoadDiagnostics diagnostics,
                               osg::ref_ptr<osg::Group> node);
    bool applyAircraftModelLoad(AircraftModelDiagnostics diagnostics,
                                osg::ref_ptr<osg::Node> node);
    void updateFollowCamera(const VaporView::Geo::NavSample& sample);
    void setInitialEarthView();
    void rebuildDisplayTrack();
    void detachSharedSamplesForLiveAppend();
    const VaporView::Geo::NavSample* latestRawSample() const;
    VaporView::Geo::TrajectoryRenderSample toDisplaySample(
        const VaporView::Geo::TrajectoryRenderSample& sample);
    VaporView::Geo::NavSample toDisplaySample(const VaporView::Geo::NavSample& sample);
    VaporView::Geo::NavSample toLocalSample(const VaporView::Geo::NavSample& sample);
    VaporView::Geo::NavSample toWorldSample(const VaporView::Geo::NavSample& sample);
    void resetWorldOverlayOrigin();
    void updateWorldOverlayOriginFromSample(const VaporView::Geo::NavSample& sample);
    void applyLayerVisibility(Map3DLayer layer);
    void applyAllLayerVisibility();
    void setLookAt(const osg::Vec3d& center, double distanceM);
    bool selectTrajectorySampleAt(const QPointF& widgetPosition);
    void clearTrajectorySampleSelection();
    void showTrajectoryInfoCard(int sampleIndex, const VaporView::Geo::NavSample& sample);
    void updateTrajectoryInfoCardPosition();
    void releaseGlObjectsForContextDestruction();

    QTimer frameTimer_;
    bool initialized_ = false;
    bool rendering_started_ = false;
    bool shutdown_ = false;
    bool follow_aircraft_ = false;
    bool use_xihu_initial_view_ = false;
    QMetaObject::Connection gl_context_destruction_connection_;
    QSize framebuffer_size_;
    double last_frame_ms_ = 0.0;
    double smoothed_frame_ms_ = 0.0;
    double frames_per_second_ = 0.0;
    QElapsedTimer frame_interval_clock_;
    double smoothed_frame_interval_ms_ = 0.0;
    double last_track_update_ms_ = 0.0;
    VaporView::Geo::LocalTangentPlane local_frame_;
    bool has_world_overlay_origin_ = false;
    osg::Vec3d world_overlay_origin_;
    QPointF mouse_press_position_;
    bool mouse_press_tracks_selection_ = false;
    bool mouse_dragged_since_press_ = false;
    QWidget* trajectory_info_card_ = nullptr;
    std::unique_ptr<osgViewer::Viewer> viewer_;
    osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> graphics_window_;
    osg::ref_ptr<osg::Group> root_;
    osg::ref_ptr<osg::MatrixTransform> overlay_transform_;
    osg::ref_ptr<osg::Node> earth_node_;
    osg::ref_ptr<osg::Node> local_3d_tiles_node_;
    osgEarth::MapNode* map_node_ = nullptr;
    EarthLoadDiagnostics earth_load_diagnostics_;
    Local3DTilesLoadDiagnostics local_3d_tiles_load_diagnostics_;
    AircraftModelDiagnostics aircraft_model_diagnostics_;
    std::deque<VaporView::Geo::TrajectoryRenderSample> raw_samples_;
    std::shared_ptr<const std::vector<VaporView::Geo::TrajectoryRenderSample>> shared_samples_;
    std::shared_ptr<const std::vector<VaporView::Geo::NavSample>> shared_nav_samples_;
    int shared_sample_count_ = 0;
    bool preserve_full_track_extent_ = false;
    TrackDataMode track_data_mode_ = TrackDataMode::LiveNavigation;
    QString height_reference_status_;
    std::unique_ptr<Trajectory3DLayer> trajectory_layer_;
    std::unique_ptr<Aircraft3DLayer> aircraft_layer_;
    std::array<bool, kMap3DLayerCount> layer_visibility_{};
    quint64 earth_load_generation_ = 0;
    quint64 local_3d_tiles_load_generation_ = 0;
    quint64 aircraft_load_generation_ = 0;
    QSet<QFutureWatcherBase*> async_watchers_;
};

} // namespace VaporView::Map3D
