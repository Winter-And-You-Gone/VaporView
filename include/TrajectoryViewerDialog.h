#ifndef VaporView_TRAJECTORY_VIEWER_DIALOG_H_
#define VaporView_TRAJECTORY_VIEWER_DIALOG_H_

#include "SessionViewerWindow.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QComboBox;
class QCloseEvent;
class QEvent;
class QFrame;
class QLineEdit;
class QMenu;
class QProgressBar;
class QToolButton;
class QWidget;
class QSlider;
class QTimer;
class QPushButton;

class TrajectoryViewerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TrajectoryViewerDialog(QWidget *parent = nullptr);
    ~TrajectoryViewerDialog() override;

    void setEnglish(bool english);
    void setTrackLabel(const QString& englishLabel, const QString& chineseLabel);
    void setTrackPoints(const QVector<RtkTrackPoint>& points);
    void setTrackStats(const RtkTrackStats& stats);

signals:
    void trackPointActivated(int index);

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void applyMapSourceSelection(int index);
    void applyTiandituKeyEdit();
    void showTiandituKeyMenu();
    void updateThemeStyles();
    void updateTitleBarIcons();
    void updateVisibilityButtonIcons();
    void updateHeatLegend();
    void updateTexts();
    void updateSummary();
    void updateSelectedPointDetails();
    void setSelectedTrackIndex(int index, bool notifySession);
    void onTimelineChanged(int value);
    void togglePlayback();
    void advancePlayback();
    void exportTrackCsv();
    void copySelectedPoint();

    QLabel *summary_label_;
    QLabel *sidebar_title_label_;
    QLabel *sidebar_icon_label_;
    QLabel *detail_label_;
    QLabel *map_status_label_;
    QProgressBar *map_progress_bar_;
    QWidget *map_widget_;
    QSlider *timeline_slider_;
    QLabel *track_width_label_;
    QSlider *track_width_slider_;
    QLabel *point_size_label_;
    QSlider *point_size_slider_;
    QFrame *heat_palette_card_;
    QLabel *heat_palette_title_label_;
    QWidget *heat_gradient_bar_;
    QLabel *heat_min_label_;
    QLabel *heat_mid_label_;
    QLabel *heat_max_label_;
    QToolButton *heat_palette_button_;
    QMenu *heat_palette_menu_;
    QFrame *map_tools_card_;
    QFrame *point_detail_card_;
    QPushButton *show_route_button_;
    QPushButton *show_points_button_;
    QPushButton *play_button_;
    QPushButton *export_button_;
    QPushButton *copy_point_button_;
    QComboBox *map_source_combo_;
    QLineEdit *tianditu_key_edit_;
    QToolButton *tianditu_key_button_;
    QMenu *tianditu_key_menu_;
    QLabel *tianditu_key_menu_label_;
    QToolButton *zoom_in_button_;
    QToolButton *zoom_out_button_;
    QToolButton *reset_view_button_;
    bool is_english_;
    bool updating_theme_styles_;
    QString english_track_label_;
    QString chinese_track_label_;
    QVector<RtkTrackPoint> track_points_;
    RtkTrackStats track_stats_;
    int selected_track_index_;
    bool point_detail_visible_;
    QTimer *playback_timer_;
};

#endif
