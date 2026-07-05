#pragma once

#include "geo/GeoTypes.h"

#include <QOpenGLWidget>
#include <QTimer>
#include <osg/Group>
#include <osg/Node>
#include <osg/ref_ptr>
#include <osgViewer/GraphicsWindow>
#include <memory>
#include <vector>

namespace osgViewer {
class Viewer;
}

namespace VaporView::Map3D {

class Aircraft3DLayer;
class Trajectory3DLayer;

class OsgEarthViewWidget final : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit OsgEarthViewWidget(QWidget* parent = nullptr);
    ~OsgEarthViewWidget() override;

    void appendSample(const VaporView::Geo::NavSample& sample);
    void appendSamples(const std::vector<VaporView::Geo::NavSample>& samples);
    void clearTrack();
    bool loadEarthFile(const QString& earthPath);
    void setFollowAircraft(bool enabled);

    int sampleCount() const;
    QSize framebufferSize() const;

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void initializeSceneIfNeeded();
    void updateCameraViewport(int w, int h);
    void updateFollowCamera(const VaporView::Geo::NavSample& sample);
    VaporView::Geo::NavSample toLocalSample(const VaporView::Geo::NavSample& sample);

    QTimer frameTimer_;
    bool initialized_ = false;
    bool follow_aircraft_ = true;
    QSize framebuffer_size_;
    bool has_local_origin_ = false;
    double origin_lat_deg_ = 0.0;
    double origin_lon_deg_ = 0.0;
    double origin_height_m_ = 0.0;
    std::unique_ptr<osgViewer::Viewer> viewer_;
    osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> graphics_window_;
    osg::ref_ptr<osg::Group> root_;
    osg::ref_ptr<osg::Node> earth_node_;
    std::unique_ptr<Trajectory3DLayer> trajectory_layer_;
    std::unique_ptr<Aircraft3DLayer> aircraft_layer_;
};

} // namespace VaporView::Map3D
