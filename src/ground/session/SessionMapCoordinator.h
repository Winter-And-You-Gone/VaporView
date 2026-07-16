#pragma once

#include "ground/SessionData.h"

#include <QObject>
#include <QPointer>

class TrajectoryViewerDialog;
class QWidget;

namespace VaporView::Ground
{

class SessionMapCoordinator final : public QObject
{
    Q_OBJECT

public:
    explicit SessionMapCoordinator(QObject *parent = nullptr);
    ~SessionMapCoordinator() override;

    void setEnglish(bool english);
    void setPeakSettings(int searchStartIndex,
                         int searchEndIndex,
                         int filterMode,
                         double filterMin,
                         double filterMax);
    void updateTrack(const QVector<SessionTrackPoint>& points, const SessionTrackStats& stats);
    bool showTrajectory(QWidget *owner,
                        const QVector<SessionTrackPoint>& points,
                        const SessionTrackStats& stats);
    void closeTrajectory();
    bool isCreated() const;
    bool isVisible() const;

signals:
    void trackPointActivated(int index);
    void peakSettingsChangeRequested(int searchStartIndex,
                                     int searchEndIndex,
                                     int filterMode,
                                     double filterMin,
                                     double filterMax);

private:
    void ensureDialog(QWidget *owner);

    QPointer<TrajectoryViewerDialog> dialog_;
    bool is_english_ = false;
    int peak_search_start_index_ = 0;
    int peak_search_end_index_ = 0;
    int peak_filter_mode_ = 0;
    double peak_filter_min_ = 0.0;
    double peak_filter_max_ = 0.0;
};

}  // namespace VaporView::Ground
