#include "ground/session/SessionPlaybackController.h"

#include <algorithm>
#include <cmath>

namespace VaporView::Ground
{

SessionPlaybackController::SessionPlaybackController(QObject *parent)
    : QObject(parent)
{
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &SessionPlaybackController::advanceOneFrame);
}

void SessionPlaybackController::setTimeline(int frameCount, QVector<quint64> timestampsUs)
{
    pause();
    frame_count_ = std::max(0, frameCount);
    timestamps_us_ = std::move(timestampsUs);
    if (timestamps_us_.size() > frame_count_)
    {
        timestamps_us_.resize(frame_count_);
    }

    const int nextFrame = frame_count_ > 0 ? 0 : -1;
    if (current_frame_ != nextFrame)
    {
        current_frame_ = nextFrame;
        emit currentFrameChanged(current_frame_);
    }
}

void SessionPlaybackController::clear()
{
    setTimeline(0);
}

int SessionPlaybackController::frameCount() const
{
    return frame_count_;
}

int SessionPlaybackController::currentFrame() const
{
    return current_frame_;
}

double SessionPlaybackController::speed() const
{
    return speed_;
}

bool SessionPlaybackController::isPlaying() const
{
    return playing_;
}

void SessionPlaybackController::seek(int frameIndex)
{
    if (frame_count_ <= 0)
    {
        return;
    }

    const int clampedFrame = std::clamp(frameIndex, 0, frame_count_ - 1);
    if (current_frame_ != clampedFrame)
    {
        current_frame_ = clampedFrame;
        emit currentFrameChanged(current_frame_);
    }
    if (playing_)
    {
        scheduleNextFrame();
    }
}

void SessionPlaybackController::play()
{
    if (frame_count_ <= 0)
    {
        return;
    }
    if (current_frame_ >= frame_count_ - 1)
    {
        seek(0);
    }
    setPlaying(true);
    scheduleNextFrame();
}

void SessionPlaybackController::pause()
{
    timer_.stop();
    setPlaying(false);
}

void SessionPlaybackController::togglePlayback()
{
    if (playing_)
    {
        pause();
    }
    else
    {
        play();
    }
}

void SessionPlaybackController::setSpeed(double speed)
{
    if (!std::isfinite(speed) || speed <= 0.0)
    {
        return;
    }

    const double clampedSpeed = std::clamp(speed, 0.1, 16.0);
    if (qFuzzyCompare(speed_, clampedSpeed))
    {
        return;
    }
    speed_ = clampedSpeed;
    emit speedChanged(speed_);
    if (playing_)
    {
        scheduleNextFrame();
    }
}

void SessionPlaybackController::advanceOneFrame()
{
    if (frame_count_ <= 0 || current_frame_ >= frame_count_ - 1)
    {
        pause();
        return;
    }

    ++current_frame_;
    emit currentFrameChanged(current_frame_);
    if (current_frame_ >= frame_count_ - 1)
    {
        pause();
    }
    else if (playing_)
    {
        scheduleNextFrame();
    }
}

int SessionPlaybackController::nextIntervalMs() const
{
    constexpr int kFallbackIntervalMs = 33;
    if (current_frame_ < 0 || current_frame_ + 1 >= timestamps_us_.size())
    {
        return std::max(1, static_cast<int>(std::lround(kFallbackIntervalMs / speed_)));
    }

    const quint64 currentUs = timestamps_us_.at(current_frame_);
    const quint64 nextUs = timestamps_us_.at(current_frame_ + 1);
    if (currentUs == 0 || nextUs <= currentUs)
    {
        return std::max(1, static_cast<int>(std::lround(kFallbackIntervalMs / speed_)));
    }

    const double intervalMs = static_cast<double>(nextUs - currentUs) / 1000.0 / speed_;
    return std::clamp(static_cast<int>(std::lround(intervalMs)), 1, 1000);
}

void SessionPlaybackController::scheduleNextFrame()
{
    if (!playing_ || current_frame_ < 0 || current_frame_ >= frame_count_ - 1)
    {
        pause();
        return;
    }
    timer_.start(nextIntervalMs());
}

void SessionPlaybackController::setPlaying(bool playing)
{
    if (playing_ == playing)
    {
        return;
    }
    playing_ = playing;
    emit playingChanged(playing_);
}

}  // namespace VaporView::Ground
