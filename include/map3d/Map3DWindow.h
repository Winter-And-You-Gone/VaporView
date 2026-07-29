#pragma once

#include "geo/GeoTypes.h"
#include "geo/TrajectoryHeatmap.h"
#include "geo/TrajectoryReplay.h"
#include "map3d/MapDataManager.h"
#include "map3d/OsgEarthViewWidget.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QString>
#include <array>
#include <memory>
#include <vector>

class QAction;
class QComboBox;
class QDialog;
class QLabel;
class QPlainTextEdit;
class QSlider;
class QSpinBox;
class QTimer;
class QCloseEvent;
class QEvent;
class QHideEvent;
class QShowEvent;
class QToolBar;

namespace VaporView {
class SingleLevelPopupMenu;
class SingleLevelPopupMenuRow;
}

namespace VaporView::Map3D {

class OsgEarthViewWidget;
class MapResourceDialog;
class MapResourceManager;

class Map3DWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit Map3DWindow(QWidget* parent = nullptr);
    ~Map3DWindow() override;
    void setUiTestMode(bool enabled);

public slots:
    void appendSample(const VaporView::Geo::NavSample& sample);
    void appendSamples(const std::vector<VaporView::Geo::NavSample>& samples);
    void clearTrack();
    void loadSessionDirectory(const QString& sessionDir);
    void noteLiveSampleDrop(const QString& source, const QString& reason, qint64 recordTimestampUs = 0);
    void showMapDiagnostics();
    void showMapResources();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void loadInitialEarthFile();
    void openSessionDirectory();
    void openEarthFile();
    void loadLocalImageryTemplate(const VaporView::Map3D::LocalImageryOption& option);
    void loadLocal3DTilesPreview();
    bool loadConfiguredLocal3DTiles(bool showStatusMessage);
    void clearLocal3DTilesPreview();
    bool applyConfiguredTiandituSatelliteImagery(bool showStatusMessage);
    void openAircraftModel();
    void resetAircraftModel();
    void reloadBestLocalMap();
    void maybeLoadSentinel2ImageryForRange(double rangeM);
    void resetAutomaticSentinel2Imagery();
    bool isSentinel2ImageryActive() const;
    void createLayerMenu(QToolBar* toolbar);
    void setLayerVisible(Map3DLayer layer, bool visible, bool announce);
    bool layerVisible(Map3DLayer layer) const;
    void refreshLayerMenuTheme();
    void refreshLayerMenuAvailability();
    void applyHeatControlsToView();
    void updateHeatLegend();
    void flyToAircraft();
    void flyToTrack();
    void resetView();
    void toggleReplay();
    void stopReplay();
    void onReplayTick();
    void onReplaySliderMoved(int value);
    void onReplaySpeedChanged(int index);
    void rebuildReplayAt(int index, bool forceStatus = true);
    void rebuildReplayAtElapsed(VaporView::Geo::TrajectoryReplay::Duration elapsed);
    void renderReplayAtCurrentPosition(bool forceStatus = true);
    void setReplayEnabled(bool enabled);
    void updateReplayUi();
    int replaySliderMaximum() const;
    int replaySliderValue() const;
    VaporView::Geo::TrajectoryReplay::Duration replaySliderValueToElapsed(int value) const;
    QString replayTimeLabel() const;
    void setMapSelection(const MapDataSelection& selection);
    QString diagnosticsText() const;
    void refreshDiagnosticsText(bool force = false);
    int currentTrackSampleCount() const;
    bool autoFocusTrack(const QString& note);
    void setCameraNote(const QString& note);
    void recordTrackSource(const QString& source,
                           const VaporView::Geo::NavSample* latest,
                           const QString& note = {});
    void showSelectedTrajectorySample(int sampleIndex, const VaporView::Geo::NavSample& sample);
    void clearSelectedTrajectorySample();
    void updateStatus(const VaporView::Geo::NavSample* latest = nullptr, bool force = true);

    OsgEarthViewWidget* view_ = nullptr;
    QWidget* headless_view_ = nullptr;
    int headless_sample_count_ = 0;
    std::vector<VaporView::Geo::NavSample> headless_samples_;
    std::vector<VaporView::Geo::TrajectoryRenderSample> headless_render_samples_;
    int max_visible_samples_ = 200000;
    VaporView::Geo::HeatMetric heat_metric_ = VaporView::Geo::HeatMetric::Peak;
    VaporView::Geo::HeatPalette heat_palette_ = VaporView::Geo::HeatPalette::Candy;
    QAction* follow_action_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* heat_legend_label_ = nullptr;
    QAction* diagnostics_action_ = nullptr;
    QAction* map_resources_action_ = nullptr;
    QAction* track_line_visible_action_ = nullptr;
    QAction* track_points_visible_action_ = nullptr;
    QAction* layers_action_ = nullptr;
    QAction* local_imagery_action_ = nullptr;
    QAction* local_3d_tiles_action_ = nullptr;
    QAction* clear_local_3d_tiles_action_ = nullptr;
    QAction* load_aircraft_model_action_ = nullptr;
    QAction* reset_aircraft_model_action_ = nullptr;
    VaporView::SingleLevelPopupMenu* local_imagery_menu_ = nullptr;
    VaporView::SingleLevelPopupMenu* layers_menu_ = nullptr;
    std::array<QAction*, kMap3DLayerCount> layer_actions_{};
    std::array<VaporView::SingleLevelPopupMenuRow*, kMap3DLayerCount> layer_rows_{};
    std::array<bool, kMap3DLayerCount> layer_visibility_{};
    QAction* replay_action_ = nullptr;
    QAction* replay_stop_action_ = nullptr;
    QSlider* replay_slider_ = nullptr;
    QComboBox* replay_speed_combo_ = nullptr;
    QComboBox* heat_metric_combo_ = nullptr;
    QComboBox* heat_palette_combo_ = nullptr;
    QSpinBox* max_visible_samples_spin_ = nullptr;
    QSpinBox* track_line_width_spin_ = nullptr;
    QSpinBox* track_point_size_spin_ = nullptr;
    QTimer* replay_timer_ = nullptr;
    QTimer* sentinel2_auto_load_timer_ = nullptr;
    QElapsedTimer replay_tick_clock_;
    QElapsedTimer status_update_clock_;
    QDialog* diagnostics_dialog_ = nullptr;
    QPlainTextEdit* diagnostics_text_ = nullptr;
    MapResourceManager* map_resource_manager_ = nullptr;
    MapResourceDialog* map_resource_dialog_ = nullptr;
    MapDataManager map_data_manager_;
    MapDataSelection map_selection_;
    VaporView::Geo::TrajectoryReplay replay_;
    std::shared_ptr<const std::vector<VaporView::Geo::TrajectoryRenderSample>> replay_render_storage_;
    int rendered_replay_index_ = -1;
    QString latest_track_source_;
    QString latest_track_note_;
    QString latest_camera_note_;
    EarthLoadDiagnostics latest_earth_load_;
    Local3DTilesLoadDiagnostics latest_local_3d_tiles_load_;
    QString latest_drop_source_;
    QString latest_drop_reason_;
    VaporView::Geo::NavSample latest_status_sample_;
    VaporView::Geo::NavSample selected_track_sample_;
    bool has_latest_status_sample_ = false;
    bool has_selected_track_sample_ = false;
    bool automatic_sentinel2_imagery_loaded_ = false;
    bool automatic_sentinel2_imagery_loading_ = false;
    bool tianditu_satellite_imagery_loaded_ = false;
    qint64 latest_drop_record_timestamp_us_ = 0;
    qint64 latest_track_record_timestamp_us_ = 0;
    qint64 latest_track_device_timestamp_us_ = 0;
    int selected_track_sample_index_ = -1;
    bool ui_test_mode_ = false;
    int ui_test_saved_max_visible_samples_ = 200000;
    int ui_test_saved_heat_metric_index_ = 0;
    int ui_test_saved_heat_palette_index_ = 0;
    bool ui_test_saved_follow_aircraft_ = false;
    bool ui_test_saved_track_line_visible_ = true;
    bool ui_test_saved_track_points_visible_ = true;
    int ui_test_saved_track_line_width_ = 5;
    int ui_test_saved_track_point_size_ = 7;
    int ui_test_saved_replay_speed_index_ = 1;
    std::array<bool, kMap3DLayerCount> ui_test_saved_layer_visibility_{};
};

} // namespace VaporView::Map3D
