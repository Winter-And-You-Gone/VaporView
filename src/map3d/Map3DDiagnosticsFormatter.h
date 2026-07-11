#pragma once

#include "map3d/MapDataManager.h"
#include "map3d/OsgEarthViewWidget.h"

namespace VaporView::Map3D {

struct Map3DDiagnosticsContext {
    MapDataSelection mapSelection;
    EarthLoadDiagnostics earthLoad;
    Local3DTilesLoadDiagnostics tilesLoad;
    AircraftModelDiagnostics aircraftModel;
    Map3DPerformanceStats performance;
    TrajectoryQualityStats qualityStats;
    int totalSamples = 0;
    int visibleSamples = 0;
    int maxVisibleSamples = 0;
    QString trackSource;
    QString replayState;
    bool hasReplay = false;
    int replayIndex = -1;
    int replaySampleCount = 0;
    double replaySpeed = 1.0;
    QString replayTime;
    qint64 latestRecordTimestampUs = 0;
    qint64 latestDeviceTimestampUs = 0;
    QString attitudeSource;
    bool followAircraft = false;
    bool hasLatestLlh = false;
    QString heightReference;
    QString fixQuality;
    QString heightSafetyNote;
    QString trackNote;
    QString dropSource;
    QString dropReason;
    qint64 dropRecordTimestampUs = 0;
    QString cameraNote;
};

QString formatMap3DDiagnostics(const Map3DDiagnosticsContext& context);

} // namespace VaporView::Map3D
