#pragma once

#include "data_types.h"
#include "geo/TrajectoryHeatmap.h"
#include "map3d/Map3DLiveSnapshot.h"

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

    static void shutdownBackgroundJobs();

    void open();
    void showDiagnostics();
    void close();
    void setUiTestMode(bool enabled);

    void forwardEpsilonSample(const VaporView::EpsilonData& data, quint64 recordTimestampUs);
    void forwardLiveRenderSample(const VaporView::EpsilonData& data, quint64 recordTimestampUs,
                                 const Geo::TrajectoryHeatValues& heatValues);
    void setLiveTelemetryProvider(std::function<Map3D::Map3DLiveTelemetrySnapshot()> provider);
    void setWaterProfileProviders(Map3D::Map3DProfileRevisionProvider revisionProvider,
                                  Map3D::Map3DWaterProfileProvider snapshotProvider);
    void noteDrop(const QString& source, const QString& reason, quint64 recordTimestampUs = 0);
    void flush();

    VaporView::Map3D::Map3DWindow *window() const;
    int pendingSampleCount() const;
    qint64 latestPendingRecordTimestampUs() const;
    bool flushTimerActive() const;
    QString lastDropReason() const;

signals:
    void openWaveformPageRequested();

private:
    friend struct Map3DLivePipelineTest;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void refreshLiveTelemetry();
    VaporView::Geo::NavSample sampleFromEpsilon(
        const VaporView::EpsilonData& data,
        quint64 recordTimestampUs) const;
    void stopPendingSamples();

    QPointer<VaporView::Map3D::Map3DWindow> window_;
    QTimer flush_timer_;
    QTimer telemetry_timer_;
    std::function<Map3D::Map3DLiveTelemetrySnapshot()> telemetry_provider_;
    Map3D::Map3DProfileRevisionProvider revision_provider_;
    Map3D::Map3DWaterProfileProvider profile_provider_;
    std::vector<Geo::TrajectoryRenderSample> pending_samples_;
    QString last_drop_reason_;
    bool ui_test_mode_ = false;
};

}  // namespace VaporView::Ground
