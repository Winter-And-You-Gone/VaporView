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
    void setTrackPoints(const QVector<RtkTrackPoint>& points);

private:
    void updateTexts();

    QLabel *summary_label_;
    QWidget *map_widget_;
    QPushButton *close_button_;
    bool is_english_;
};

#endif
