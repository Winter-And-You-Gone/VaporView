#pragma once

#include "geo/GeoTypes.h"
#include "geo/TrajectoryReplay.h"
#include "map3d/MapDataManager.h"
#include "map3d/OsgEarthViewWidget.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QString>
#include <vector>

class QAction;
class QComboBox;
class QDialog;
class QLabel;
class QPlainTextEdit;
class QSlider;
class QSpinBox;
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
    void noteLiveSampleDrop(const QString& source, const QString& reason, qint64 recordTimestampUs = 0);
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
    void rebuildReplayAt(int index, bool forceStatus = true);
    void rebuildReplayAtElapsedUs(qint64 elapsedUs);
    void setReplayEnabled(bool enabled);
    void updateReplayUi();
    int replaySliderMaximum() const;
    int replaySliderValue() const;
    qint64 replaySliderValueToElapsedUs(int value) const;
    QString replayTimeLabel() const;
    void setMapSelection(const MapDataSelection& selection);
    QString diagnosticsText() const;
    int currentTrackSampleCount() const;
    bool autoFocusTrack(const QString& note);
    void setCameraNote(const QString& note);
    void recordTrackSource(const QString& source,
                           const VaporView::Geo::NavSample* latest,
                           const QString& note = {});
    void updateStatus(const VaporView::Geo::NavSample* latest = nullptr, bool force = true);

    OsgEarthViewWidget* view_ = nullptr;
    QWidget* headless_view_ = nullptr;
    int headless_sample_count_ = 0;
    std::vector<VaporView::Geo::NavSample> headless_samples_;
    int max_visible_samples_ = 200000;
    QAction* follow_action_ = nullptr;
    QLabel* status_label_ = nullptr;
    QAction* diagnostics_action_ = nullptr;
    QAction* replay_action_ = nullptr;
    QAction* replay_stop_action_ = nullptr;
    QSlider* replay_slider_ = nullptr;
    QComboBox* replay_speed_combo_ = nullptr;
    QSpinBox* max_visible_samples_spin_ = nullptr;
    QTimer* replay_timer_ = nullptr;
    QElapsedTimer replay_tick_clock_;
    QElapsedTimer status_update_clock_;
    QDialog* diagnostics_dialog_ = nullptr;
    QPlainTextEdit* diagnostics_text_ = nullptr;
    MapDataManager map_data_manager_;
    MapDataSelection map_selection_;
    VaporView::Geo::TrajectoryReplay replay_;
    QString latest_track_source_;
    QString latest_track_note_;
    QString latest_camera_note_;
    EarthLoadDiagnostics latest_earth_load_;
    QString latest_drop_source_;
    QString latest_drop_reason_;
    VaporView::Geo::NavSample latest_status_sample_;
    bool has_latest_status_sample_ = false;
    qint64 latest_drop_record_timestamp_us_ = 0;
    qint64 latest_track_record_timestamp_us_ = 0;
    qint64 latest_track_device_timestamp_us_ = 0;
};

} // namespace VaporView::Map3D
