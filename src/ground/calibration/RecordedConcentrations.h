#pragma once
#include "geo/TrajectoryHeatmap.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <cstdint>
#include <vector>

namespace VaporView::Ground::Calibration
{
struct RecordedConcentrations
{
    bool success = false;
    QString error;
    QJsonObject snapshot;
    QJsonArray rows;
};
// Reads only the parameters embedded in the historical Session/export. Never
// consults the current selection or the source .vcal file.
RecordedConcentrations readRecordedConcentrations(const QString& filename);
bool exportRecordedConcentrations(const QString& filename,const RecordedConcentrations&,QString *error);
bool attachRecordedConcentrationsToSamples(
    const QString& sessionDir,
    const std::vector<quint64>& sampleRecordTimestampsUs,
    std::vector<VaporView::Geo::TrajectoryRenderSample>& samples,
    QString& waterUnit,
    QString& warning,
    quint64 maxDeltaUs = 2'000'000ULL,
    std::uint32_t calibrationEpoch = 1);
}
