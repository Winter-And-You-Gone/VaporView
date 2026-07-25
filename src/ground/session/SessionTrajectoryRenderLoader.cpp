#include "ground/session/SessionTrajectoryRenderLoader.h"

#include "geo/SessionTrackReader.h"
#include "ground/session/SessionLoader.h"
#include "ground/session/SessionTimelineModel.h"
#include "ground/session/SessionWaveformRepository.h"

#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <cmath>
#include <utility>

namespace VaporView::Ground::Session
{
namespace
{

void appendWarning(QString& destination, const QString& warning)
{
    const QString trimmed = warning.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }
    if (!destination.isEmpty())
    {
        destination += QLatin1Char(' ');
    }
    destination += trimmed;
}

void attachDefaultWaveformPeaks(const VaporView::Ground::SessionMetadata& metadata,
                                QVector<VaporView::Ground::SessionTrackPoint>& trackPoints,
                                QString& warning)
{
    VaporView::Ground::SessionWaveformCatalogResult catalogResult =
        VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata);
    if (!catalogResult.success)
    {
        appendWarning(warning, catalogResult.error);
        return;
    }
    appendWarning(warning, catalogResult.warning);

    const bool hasCachedPeakCsv =
        !catalogResult.catalog.waveformPeaksCsvFilename.isEmpty()
        && QFileInfo::exists(catalogResult.catalog.waveformPeaksCsvFilename);
    if (catalogResult.catalog.isEmpty() && !hasCachedPeakCsv)
    {
        return;
    }

    VaporView::Ground::SessionWaveformPeakSeriesResult peakSeries;
    if (hasCachedPeakCsv)
    {
        peakSeries =
            VaporView::Ground::SessionWaveformRepository::loadCachedPeakSeries(catalogResult.catalog);
    }
    if (!peakSeries.success && !catalogResult.catalog.isEmpty())
    {
        peakSeries = VaporView::Ground::SessionWaveformRepository::calculatePeakSeries(
            catalogResult.catalog,
            0,
            0);
    }
    if (!peakSeries.success)
    {
        appendWarning(warning, peakSeries.error);
        return;
    }

    const QVector<float> filteredPeaks =
        VaporView::Ground::SessionWaveformRepository::applyPeakFilter(
            peakSeries.peakValues,
            VaporView::Ground::SessionPeakFilterSettings{});
    VaporView::Ground::Session::SessionTimelineModel::attachWaveformPeaks(
        trackPoints,
        peakSeries.timestampsUs,
        filteredPeaks);
}

VaporView::Geo::TrajectoryHeatValues heatValuesFromTrackPoint(
    const VaporView::Ground::SessionTrackPoint& point)
{
    VaporView::Geo::TrajectoryHeatValues values;
    if (point.has_peak_value && std::isfinite(point.peak_value))
    {
        values.peak = static_cast<double>(point.peak_value);
    }
    if (point.has_humidity && std::isfinite(point.humidity_rh))
    {
        values.humidityRh = point.humidity_rh;
    }
    if (point.has_temperature && std::isfinite(point.temperature_c))
    {
        values.temperatureC = point.temperature_c;
    }
    if (point.has_pressure && std::isfinite(point.pressure_hpa))
    {
        values.pressureHpa = point.pressure_hpa;
    }
    return values;
}

}  // namespace

SessionTrajectoryRenderLoadResult SessionTrajectoryRenderLoader::loadSessionDirectory(
    const QString& sessionDir)
{
    SessionTrajectoryRenderLoadResult result;
    VaporView::Geo::SessionTrackReadResult track = VaporView::Geo::readSessionTrack(sessionDir);
    if (!track.ok)
    {
        result.error = track.error;
        result.sourceCsvPath = track.sourceCsvPath;
        return result;
    }

    result.sourceCsvPath = track.sourceCsvPath;
    result.totalRows = track.totalRows;
    result.rejectedRows = track.rejectedRows;
    appendWarning(result.warning, track.warning);

    VaporView::Ground::SessionMetadataLoadResult metadataResult =
        VaporView::Ground::SessionLoader::loadMetadata(sessionDir);
    if (!metadataResult.success)
    {
        appendWarning(result.warning, metadataResult.error);
        result.sourceCsvRows = track.sourceCsvRows;
        result.samples.reserve(track.samples.size());
        for (VaporView::Geo::NavSample& sample : track.samples)
        {
            VaporView::Geo::TrajectoryRenderSample renderSample;
            renderSample.navigation = std::move(sample);
            result.samples.push_back(std::move(renderSample));
        }
        result.success = true;
        return result;
    }
    appendWarning(result.warning, metadataResult.warning);

    VaporView::Ground::SessionSensorLoadResult sensorResult =
        VaporView::Ground::SessionLoader::loadSensors(metadataResult.metadata);
    if (!sensorResult.success || !sensorResult.fileAvailable)
    {
        appendWarning(result.warning, sensorResult.warning);
        result.sourceCsvRows = track.sourceCsvRows;
        result.samples.reserve(track.samples.size());
        for (VaporView::Geo::NavSample& sample : track.samples)
        {
            VaporView::Geo::TrajectoryRenderSample renderSample;
            renderSample.navigation = std::move(sample);
            result.samples.push_back(std::move(renderSample));
        }
        result.success = true;
        return result;
    }

    QVector<VaporView::Ground::SessionTrackPoint> trackPoints =
        std::move(sensorResult.data.track_points);
    attachDefaultWaveformPeaks(metadataResult.metadata, trackPoints, result.warning);

    QHash<int, VaporView::Geo::NavSample> navSampleByCsvRow;
    navSampleByCsvRow.reserve(static_cast<int>(
        std::min(track.samples.size(), track.sourceCsvRows.size())));
    const std::size_t mappedNavCount = std::min(track.samples.size(), track.sourceCsvRows.size());
    for (std::size_t index = 0; index < mappedNavCount; ++index)
    {
        const int csvRow = static_cast<int>(track.sourceCsvRows[index]);
        if (csvRow >= 0 && !navSampleByCsvRow.contains(csvRow))
        {
            navSampleByCsvRow.insert(csvRow, track.samples[index]);
        }
    }

    result.samples.reserve(trackPoints.size());
    result.sourceCsvRows.reserve(trackPoints.size());
    for (const VaporView::Ground::SessionTrackPoint& point : std::as_const(trackPoints))
    {
        const auto navIt = navSampleByCsvRow.constFind(point.csv_row);
        if (navIt == navSampleByCsvRow.cend())
        {
            continue;
        }
        VaporView::Geo::TrajectoryRenderSample renderSample;
        renderSample.navigation = navIt.value();
        renderSample.heat = heatValuesFromTrackPoint(point);
        result.samples.push_back(std::move(renderSample));
        result.sourceCsvRows.push_back(point.csv_row);
    }

    result.success = true;
    return result;
}

}  // namespace VaporView::Ground::Session
