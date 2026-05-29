#ifndef VaporView_TRAJECTORY_VIEWER_DIALOG_H_
#define VaporView_TRAJECTORY_VIEWER_DIALOG_H_

#include "SessionViewerWindow.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QComboBox;
class QLineEdit;
class QProgressBar;
class QToolButton;
class QWidget;

class TrajectoryViewerDialog : public QDialog
{
public:
    explicit TrajectoryViewerDialog(QWidget *parent = nullptr);

    void setEnglish(bool english);
    void setTrackLabel(const QString& englishLabel, const QString& chineseLabel);
    void setTrackPoints(const QVector<RtkTrackPoint>& points);

private:
    void installTitleBarControls();
    void applyMapSourceSelection(int index);
    void applyTiandituKeyEdit();
    void updateTexts();
    void updateSummary();

    QLabel *summary_label_;
    QLabel *legend_label_;
    QLabel *map_status_label_;
    QProgressBar *map_progress_bar_;
    QWidget *map_widget_;
    QComboBox *map_source_combo_;
    QLineEdit *tianditu_key_edit_;
    QToolButton *zoom_in_button_;
    QToolButton *zoom_out_button_;
    QToolButton *reset_view_button_;
    QWidget *title_bar_controls_;
    bool is_english_;
    QString english_track_label_;
    QString chinese_track_label_;
    QVector<RtkTrackPoint> track_points_;
};

#endif
