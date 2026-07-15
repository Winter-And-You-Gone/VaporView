#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>

namespace VaporView::Ground
{

class SessionPlaybackController final : public QObject
{
    Q_OBJECT

public:
    explicit SessionPlaybackController(QObject *parent = nullptr);

    void setTimeline(int frameCount, QVector<quint64> timestampsUs = {});
    void clear();

    int frameCount() const;
    int currentFrame() const;
    double speed() const;
    bool isPlaying() const;

public slots:
    void seek(int frameIndex);
    void play();
    void pause();
    void togglePlayback();
    void setSpeed(double speed);
    void advanceOneFrame();

signals:
    void currentFrameChanged(int frameIndex);
    void playingChanged(bool playing);
    void speedChanged(double speed);

private:
    int nextIntervalMs() const;
    void scheduleNextFrame();
    void setPlaying(bool playing);

    QTimer timer_;
    QVector<quint64> timestamps_us_;
    int frame_count_ = 0;
    int current_frame_ = -1;
    double speed_ = 1.0;
    bool playing_ = false;
};

}  // namespace VaporView::Ground
