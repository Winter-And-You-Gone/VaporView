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
class QSpinBox;
class QToolButton;
class QWidget;
class QSlider;
class QPushButton;
class QVBoxLayout;

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
    void setPeakSettings(int searchStartIndex, int searchEndIndex, int filterMode, double filterMin, double filterMax);

signals:
    void trackPointActivated(int index);
    void peakSettingsChangeRequested(int searchStartIndex, int searchEndIndex, int filterMode, double filterMin, double filterMax);

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
    void updateHeatMetricCombo();
    void updateTexts();
    void updateSummary();
    void updateSelectedPointDetails();
    void updateFilterSummary();
    void refreshPeakFilterModeCombo();
    void applyPeakControlEdits();
    void setMapFooterStatus(const QString& text);
    void setSelectedTrackIndex(int index, bool notifySession);
    void finishPendingFilterRange(int endIndex);
    void hidePointDetailCard();
    void exportTrackCsv();
    void copySelectedPoint();
    void filterSelectedPoint();
    void markSelectedPointAsFilterStart();
    void markSelectedPointAsFilterEnd();
    void removeTrajectoryFilterAt(int index);

    struct TrajectoryFilter
    {
        enum class Kind
        {
            Point,
            Range
        };

        Kind kind;
        int start_index;
        int end_index;
    };

    QLabel *summary_label_;
    QLabel *sidebar_title_label_;
    QLabel *sidebar_icon_label_;
    QLabel *detail_label_;
    QFrame *filter_card_;
    QLabel *filter_title_label_;
    QLabel *filter_empty_label_;
    QWidget *filter_list_widget_;
    QVBoxLayout *filter_list_layout_;
    QVector<QLabel*> filter_row_labels_;
    QFrame *peak_card_;
    QLabel *peak_title_label_;
    QLabel *peak_search_start_label_;
    QLabel *peak_search_end_label_;
    QLabel *peak_filter_mode_label_;
    QLabel *peak_filter_min_label_;
    QLabel *peak_filter_max_label_;
    QSpinBox *peak_search_start_spin_;
    QSpinBox *peak_search_end_spin_;
    QComboBox *peak_filter_mode_combo_;
    QLineEdit *peak_filter_min_edit_;
    QLineEdit *peak_filter_max_edit_;
    QPushButton *peak_apply_button_;
    QWidget *map_widget_;
    QLabel *track_width_label_;
    QSlider *track_width_slider_;
    QLabel *point_size_label_;
    QSlider *point_size_slider_;
    QFrame *heat_palette_card_;
    QToolButton *heat_metric_button_;
    QMenu *heat_metric_menu_;
    QWidget *heat_gradient_bar_;
    QLabel *heat_min_label_;
    QLabel *heat_mid_label_;
    QLabel *heat_max_label_;
    QToolButton *heat_palette_button_;
    QMenu *heat_palette_menu_;
    QFrame *map_tools_card_;
    QFrame *point_detail_card_;
    QToolButton *filter_current_point_button_;
    QToolButton *filter_start_point_button_;
    QToolButton *filter_end_point_button_;
    QToolButton *point_detail_close_button_;
    QPushButton *show_route_button_;
    QPushButton *show_points_button_;
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
    QVector<TrajectoryFilter> trajectory_filters_;
    RtkTrackStats track_stats_;
    int pending_filter_range_index_;
    int selected_track_index_;
    bool point_detail_visible_;
};

#endif
