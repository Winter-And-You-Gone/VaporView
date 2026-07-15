#pragma once

#include "data_types.h"
#include "geo/GeoTypes.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QString>

#include <vector>

namespace VaporView::Map3D
{
class Map3DWindow;
}

namespace VaporView::Ground
{

class Map3DController final : public QObject
{
    Q_OBJECT

public:
    explicit Map3DController(QObject *parent = nullptr);
    ~Map3DController() override;

    void open();
    void showDiagnostics();
    void close();

    void forwardEpsilonSample(const VaporView::EpsilonData& data, quint64 recordTimestampUs);
    void noteDrop(const QString& source, const QString& reason, quint64 recordTimestampUs = 0);
    void flush();

    VaporView::Map3D::Map3DWindow *window() const;
    int pendingSampleCount() const;
    qint64 latestPendingRecordTimestampUs() const;
    bool flushTimerActive() const;
    QString lastDropReason() const;

private:
    VaporView::Geo::NavSample sampleFromEpsilon(
        const VaporView::EpsilonData& data,
        quint64 recordTimestampUs) const;
    void stopPendingSamples();

    QPointer<VaporView::Map3D::Map3DWindow> window_;
    QTimer flush_timer_;
    std::vector<VaporView::Geo::NavSample> pending_samples_;
    QString last_drop_reason_;
};

}  // namespace VaporView::Ground
