#pragma once

#include "geo/CoordinateTransform.h"
#include "geo/GeoTypes.h"
#include "map3d/Trajectory3DLayer.h"

#include <QOpenGLWidget>
#include <QMetaObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/Node>
#include <osg/ref_ptr>
#include <osgViewer/GraphicsWindow>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QHideEvent;
class QShowEvent;

namespace osgViewer {
class Viewer;
}

namespace osgEarth {
class MapNode;
}

namespace VaporView::Map3D {

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
    explicit OsgEarthViewWidget(QWidget* parent = nullptr);
    ~OsgEarthViewWidget() override;

    void appendSample(const VaporView::Geo::NavSample& sample);
    void appendSamples(const std::vector<VaporView::Geo::NavSample>& samples);
    void setSamples(const std::vector<VaporView::Geo::NavSample>& samples);
    void setSamples(std::shared_ptr<const std::vector<VaporView::Geo::NavSample>> samples,
                    int sampleCount = -1);
    void appendSampleFromStorage(
        const std::shared_ptr<const std::vector<VaporView::Geo::NavSample>>& samples,
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
    void setFollowAircraft(bool enabled);
    void setMaxVisibleSamples(int maxVisibleSamples);
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
    double earthCameraRangeM() const;

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
    void initializeSceneIfNeeded();
    void updateCameraViewport(int w, int h);
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
    VaporView::Geo::NavSample toDisplaySample(const VaporView::Geo::NavSample& sample);
    VaporView::Geo::NavSample toLocalSample(const VaporView::Geo::NavSample& sample);
    VaporView::Geo::NavSample toWorldSample(const VaporView::Geo::NavSample& sample);
    void resetWorldOverlayOrigin();
    void updateWorldOverlayOriginFromSample(const VaporView::Geo::NavSample& sample);
    void setLookAt(const osg::Vec3d& center, double distanceM);
    void releaseGlObjectsForContextDestruction();

    QTimer frameTimer_;
    bool initialized_ = false;
    bool shutdown_ = false;
    bool follow_aircraft_ = false;
    bool use_xihu_initial_view_ = false;
    QMetaObject::Connection gl_context_destruction_connection_;
    QSize framebuffer_size_;
    double last_frame_ms_ = 0.0;
    double smoothed_frame_ms_ = 0.0;
    double frames_per_second_ = 0.0;
    double last_track_update_ms_ = 0.0;
    VaporView::Geo::LocalTangentPlane local_frame_;
    bool has_world_overlay_origin_ = false;
    osg::Vec3d world_overlay_origin_;
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
    std::deque<VaporView::Geo::NavSample> raw_samples_;
    std::shared_ptr<const std::vector<VaporView::Geo::NavSample>> shared_samples_;
    int shared_sample_count_ = 0;
    bool preserve_full_track_extent_ = false;
    QString height_reference_status_;
    std::unique_ptr<Trajectory3DLayer> trajectory_layer_;
    std::unique_ptr<Aircraft3DLayer> aircraft_layer_;
    quint64 earth_load_generation_ = 0;
    quint64 local_3d_tiles_load_generation_ = 0;
    quint64 aircraft_load_generation_ = 0;
};

} // namespace VaporView::Map3D
