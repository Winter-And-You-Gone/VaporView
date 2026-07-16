#include "ground/session/SessionMapCoordinator.h"

#include "ground/trajectory/TrajectoryViewerDialog.h"
#include "ground/widgets/WindowSizing.h"

#include <QWidget>

namespace VaporView::Ground
{

SessionMapCoordinator::SessionMapCoordinator(QObject *parent)
    : QObject(parent)
{
}

SessionMapCoordinator::~SessionMapCoordinator()
{
    if (dialog_)
    {
        dialog_->close();
        delete dialog_.data();
    }
}

void SessionMapCoordinator::setEnglish(bool english)
{
    is_english_ = english;
    if (dialog_)
    {
        dialog_->setEnglish(english);
    }
}

void SessionMapCoordinator::setPeakSettings(
    int searchStartIndex,
    int searchEndIndex,
    int filterMode,
    double filterMin,
    double filterMax)
{
    peak_search_start_index_ = searchStartIndex;
    peak_search_end_index_ = searchEndIndex;
    peak_filter_mode_ = filterMode;
    peak_filter_min_ = filterMin;
    peak_filter_max_ = filterMax;
    if (dialog_)
    {
        dialog_->setPeakSettings(
            searchStartIndex,
            searchEndIndex,
            filterMode,
            filterMin,
            filterMax);
    }
}

void SessionMapCoordinator::updateTrack(
    const QVector<SessionTrackPoint>& points,
    const SessionTrackStats& stats)
{
    if (!dialog_)
    {
        return;
    }
    dialog_->setTrackStats(stats);
    dialog_->setTrackPoints(points);
}

bool SessionMapCoordinator::showTrajectory(
    QWidget *owner,
    const QVector<SessionTrackPoint>& points,
    const SessionTrackStats& stats)
{
    if (points.isEmpty())
    {
        return false;
    }

    ensureDialog(owner);
    dialog_->setEnglish(is_english_);
    dialog_->setTrackLabel(QStringLiteral("RTK trajectory"), QStringLiteral("RTK轨迹"));
    dialog_->setPeakSettings(
        peak_search_start_index_,
        peak_search_end_index_,
        peak_filter_mode_,
        peak_filter_min_,
        peak_filter_max_);
    dialog_->setTrackStats(stats);
    dialog_->setTrackPoints(points);
    VaporView::centerWindowOnScreen(dialog_, owner);
    dialog_->show();
    dialog_->raise();
    dialog_->activateWindow();
    return true;
}

void SessionMapCoordinator::closeTrajectory()
{
    if (dialog_)
    {
        dialog_->close();
    }
}

bool SessionMapCoordinator::isCreated() const
{
    return !dialog_.isNull();
}

bool SessionMapCoordinator::isVisible() const
{
    return dialog_ && dialog_->isVisible();
}

void SessionMapCoordinator::ensureDialog(QWidget *owner)
{
    if (dialog_)
    {
        return;
    }

    dialog_ = new TrajectoryViewerDialog(owner);
    dialog_->setAttribute(Qt::WA_QuitOnClose, false);
    dialog_->setAttribute(Qt::WA_DeleteOnClose, false);
    connect(dialog_, &TrajectoryViewerDialog::trackPointActivated,
            this, &SessionMapCoordinator::trackPointActivated);
    connect(dialog_, &TrajectoryViewerDialog::peakSettingsChangeRequested,
            this, &SessionMapCoordinator::peakSettingsChangeRequested);
}

}  // namespace VaporView::Ground
