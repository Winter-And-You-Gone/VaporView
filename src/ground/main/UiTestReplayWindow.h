#pragma once

#include "shared/session/UnifiedRawDat.h"
#include <QElapsedTimer>
#include <QHash>
#include <QTimer>
#include <QWidget>
#include <atomic>
#include <functional>
#include <memory>

class QProgressBar;
class QLabel;
class QPushButton;
class QSlider;
class QComboBox;

class UiTestReplayWindow final : public QWidget
{
public:
    enum class Stream { Sensors, Laser, System, Feature, Peak, NavigationStatus, Log, Raw, LegacyWave, FpgaSensor, FpgaWave };
    struct Sample {
        Stream stream;
        quint64 timestamp;
        QHash<QString, QString> fields;
        VaporView::SessionRawDat::RawRecordHeader header;
        QByteArray bytes;
        QVector<float> legacyWave;
    };
    UiTestReplayWindow(QWidget *parent, bool english, const QString& directory);
    ~UiTestReplayWindow() override;
    void loadSession(const QString& path);
    std::function<void()> exitMode;
    std::function<void(bool replay)> resetSource;
    std::function<void(const Sample&)> present;
    std::function<void()> refresh;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    struct Index;
    QString label(const char *zh, const char *en) const;
    void selectSimulation();
    void seek(quint64 timestamp);
    void tick();
    bool deliver(int stream, int index);
    void updatePosition();
    void setPlaying(bool playing);
    const bool english_;
    QString directory_;
    std::shared_ptr<Index> index_;
    std::shared_ptr<std::atomic_bool> cancel_;
    QPushButton *simulate_, *choose_, *play_;
    QLabel *source_, *time_, *detail_;
    QComboBox *streams_;
    QSlider *slider_;
    QProgressBar *loading_;
    QTimer timer_;
    QElapsedTimer clock_;
    quint64 position_ = 0, anchor_ = 0;
    qsizetype next_ = 0;
    bool resumeAfterDrag_ = false;
};
