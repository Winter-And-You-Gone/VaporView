#ifndef VaporView_TRAJECTORY_VIEWER_DIALOG_H_
#define VaporView_TRAJECTORY_VIEWER_DIALOG_H_

#include "SessionViewerWindow.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QPushButton;
class QWidget;

class TrajectoryViewerDialog : public QDialog
{
public:
    explicit TrajectoryViewerDialog(QWidget *parent = nullptr);

    void setEnglish(bool english);
    void setTrackLabel(const QString& englishLabel, const QString& chineseLabel);
    void setTrackPoints(const QVector<RtkTrackPoint>& points);

private:
    void updateTexts();
    void updateSummary();

    QLabel *summary_label_;
    QWidget *map_widget_;
    QPushButton *zoom_in_button_;
    QPushButton *zoom_out_button_;
    QPushButton *reset_view_button_;
    QPushButton *close_button_;
    bool is_english_;
    QString english_track_label_;
    QString chinese_track_label_;
    QVector<RtkTrackPoint> track_points_;
};

#endif
