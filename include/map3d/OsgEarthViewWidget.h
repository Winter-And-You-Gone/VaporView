#pragma once

#include "geo/GeoTypes.h"
#include "map3d/Trajectory3DLayer.h"

#include <QOpenGLWidget>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <osg/Group>
#include <osg/Node>
#include <osg/ref_ptr>
#include <osgViewer/GraphicsWindow>
#include <memory>
#include <vector>

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

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
    void clearTrack();
    bool loadEarthFile(const QString& earthPath);
    bool loadLocal3DTilesPreview(const QString& tilesetPath);
    void clearLocal3DTilesPreview();
    bool loadAircraftModel(const QString& modelPath);
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

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
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
    bool loadAircraftModelFile(const QString& modelPath, const QString& fallbackReasonPrefix);
    void updateFollowCamera(const VaporView::Geo::NavSample& sample);
    void setInitialEarthView();
    void rebuildDisplayTrack();
    VaporView::Geo::NavSample toDisplaySample(const VaporView::Geo::NavSample& sample);
    VaporView::Geo::NavSample toLocalSample(const VaporView::Geo::NavSample& sample);
    VaporView::Geo::NavSample toWorldSample(const VaporView::Geo::NavSample& sample) const;
    void setLookAt(const osg::Vec3d& center, double distanceM);

    QTimer frameTimer_;
    bool initialized_ = false;
    bool shutdown_ = false;
    bool follow_aircraft_ = false;
    QSize framebuffer_size_;
    double last_frame_ms_ = 0.0;
    double smoothed_frame_ms_ = 0.0;
    double frames_per_second_ = 0.0;
    double last_track_update_ms_ = 0.0;
    bool has_local_origin_ = false;
    double origin_lat_deg_ = 0.0;
    double origin_lon_deg_ = 0.0;
    double origin_height_m_ = 0.0;
    std::unique_ptr<osgViewer::Viewer> viewer_;
    osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> graphics_window_;
    osg::ref_ptr<osg::Group> root_;
    osg::ref_ptr<osg::Node> earth_node_;
    osg::ref_ptr<osg::Node> local_3d_tiles_node_;
    osgEarth::MapNode* map_node_ = nullptr;
    EarthLoadDiagnostics earth_load_diagnostics_;
    Local3DTilesLoadDiagnostics local_3d_tiles_load_diagnostics_;
    AircraftModelDiagnostics aircraft_model_diagnostics_;
    std::vector<VaporView::Geo::NavSample> raw_samples_;
    std::unique_ptr<Trajectory3DLayer> trajectory_layer_;
    std::unique_ptr<Aircraft3DLayer> aircraft_layer_;
};

} // namespace VaporView::Map3D
