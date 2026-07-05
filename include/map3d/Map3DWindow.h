#pragma once

#include "geo/GeoTypes.h"
#include "map3d/MapDataManager.h"

#include <QMainWindow>
#include <vector>

class QAction;
class QDialog;
class QLabel;
class QPlainTextEdit;

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
    void showMapDiagnostics();
    void setMapSelection(const MapDataSelection& selection);
    QString diagnosticsText() const;
    void updateStatus(const VaporView::Geo::NavSample* latest = nullptr);

    OsgEarthViewWidget* view_ = nullptr;
    QWidget* headless_view_ = nullptr;
    int headless_sample_count_ = 0;
    QAction* follow_action_ = nullptr;
    QLabel* status_label_ = nullptr;
    QAction* diagnostics_action_ = nullptr;
    QDialog* diagnostics_dialog_ = nullptr;
    QPlainTextEdit* diagnostics_text_ = nullptr;
    MapDataManager map_data_manager_;
    MapDataSelection map_selection_;
};

} // namespace VaporView::Map3D
