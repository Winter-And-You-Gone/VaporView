#pragma once

#include "geo/GeoTypes.h"

#include <QMainWindow>
#include <vector>

class QAction;
class QLabel;

namespace VaporView::Map3D {

class OsgEarthViewWidget;

class Map3DWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit Map3DWindow(QWidget* parent = nullptr);
    ~Map3DWindow() override;

public slots:
    void appendSample(const VaporView::Geo::NavSample& sample);
    void appendSamples(const std::vector<VaporView::Geo::NavSample>& samples);
    void clearTrack();
    void loadSessionDirectory(const QString& sessionDir);

private:
    void loadInitialEarthFile();
    void openSessionDirectory();
    void openEarthFile();
    void updateStatus(const VaporView::Geo::NavSample* latest = nullptr);

    OsgEarthViewWidget* view_ = nullptr;
    QAction* follow_action_ = nullptr;
    QLabel* status_label_ = nullptr;
};

} // namespace VaporView::Map3D
