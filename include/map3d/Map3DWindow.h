#pragma once

#include "geo/GeoTypes.h"
#include "geo/TrajectoryReplay.h"
#include "map3d/MapDataManager.h"

#include <QMainWindow>
#include <vector>

class QAction;
class QComboBox;
class QDialog;
class QLabel;
class QPlainTextEdit;
class QSlider;
class QTimer;

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
    void showMapDiagnostics();

private:
    void loadInitialEarthFile();
    void openSessionDirectory();
    void openEarthFile();
    void reloadBestLocalMap();
    void flyToAircraft();
    void flyToTrack();
    void resetView();
    void toggleReplay();
    void stopReplay();
    void onReplayTick();
    void onReplaySliderMoved(int value);
    void onReplaySpeedChanged(int index);
    void rebuildReplayAt(int index);
    void setReplayEnabled(bool enabled);
    void updateReplayUi();
    void setMapSelection(const MapDataSelection& selection);
    QString diagnosticsText() const;
    void updateStatus(const VaporView::Geo::NavSample* latest = nullptr);

    OsgEarthViewWidget* view_ = nullptr;
    QWidget* headless_view_ = nullptr;
    int headless_sample_count_ = 0;
    int max_visible_samples_ = 200000;
    QAction* follow_action_ = nullptr;
    QLabel* status_label_ = nullptr;
    QAction* diagnostics_action_ = nullptr;
    QAction* replay_action_ = nullptr;
    QAction* replay_stop_action_ = nullptr;
    QSlider* replay_slider_ = nullptr;
    QComboBox* replay_speed_combo_ = nullptr;
    QTimer* replay_timer_ = nullptr;
    QDialog* diagnostics_dialog_ = nullptr;
    QPlainTextEdit* diagnostics_text_ = nullptr;
    MapDataManager map_data_manager_;
    MapDataSelection map_selection_;
    VaporView::Geo::TrajectoryReplay replay_;
};

} // namespace VaporView::Map3D
