#include "Map3DDiagnosticsFormatter.h"

#include <QFileInfo>

#include <algorithm>

namespace VaporView::Map3D {
namespace {

QString availabilityLabel(bool available)
{
    return available ? QStringLiteral("available") : QStringLiteral("missing");
}

QString selectedDemLabel(const MapDataDiagnostics& diagnostics)
{
    if (!diagnostics.selectedDemLayerAvailable)
    {
        return QStringLiteral("none");
    }
    return diagnostics.selectedElevationSource.isEmpty()
        ? QStringLiteral("available")
        : diagnostics.selectedElevationSource;
}

QString selectedOsmLabel(const MapDataDiagnostics& diagnostics)
{
    if (!diagnostics.selectedOsmLayersAvailable)
    {
        return QStringLiteral("not selected (%1/4 files)").arg(diagnostics.osmLayerCount);
    }
    return QStringLiteral("%1 safe layers (water/roads, %2/4 files)")
        .arg(diagnostics.selectedOsmLayerCount)
        .arg(diagnostics.osmLayerCount);
}

QString fileAvailabilityLabel(bool available, const QString& path)
{
    return QStringLiteral("%1 - %2")
        .arg(available ? QStringLiteral("available") : QStringLiteral("missing"), path);
}

} // namespace

QString formatMap3DDiagnostics(const Map3DDiagnosticsContext& context)
{
    const MapDataDiagnostics& diagnostics = context.mapSelection.diagnostics;
    const Map3DPerformanceStats& stats = context.performance;
    const int totalSamples = context.totalSamples;
    const int visibleSamples = context.visibleSamples;
    const int maxVisibleSamples = context.maxVisibleSamples;
    const int hiddenSamples = (std::max)(0, totalSamples - visibleSamples);
    const TrajectoryQualityStats& qualityStats = context.qualityStats;
    QStringList lines;
    lines << QStringLiteral("Mode: %1 (%2)")
                 .arg(MapDataManager::modeLabel(context.mapSelection.mode),
                      MapDataManager::modeKey(context.mapSelection.mode));
    lines << QStringLiteral("Base map priority: %1")
                 .arg(diagnostics.baseMapPriority.isEmpty()
                          ? QStringLiteral("Copernicus DEM > SRTM > Natural Earth > Local grid")
                          : diagnostics.baseMapPriority);
    lines << QStringLiteral("Selected base mode: %1 (%2)")
                 .arg(diagnostics.selectedBaseModeLabel.isEmpty()
                          ? QStringLiteral("<not evaluated>")
                          : diagnostics.selectedBaseModeLabel,
                      diagnostics.selectedBaseModeKey.isEmpty()
                          ? QStringLiteral("<not evaluated>")
                          : diagnostics.selectedBaseModeKey);
    lines << QStringLiteral("Selected base earth file: %1")
                 .arg(diagnostics.selectedBaseEarthFilePath.isEmpty()
                          ? QStringLiteral("<none>")
                          : diagnostics.selectedBaseEarthFilePath);
    if (!context.mapSelection.description.isEmpty())
    {
        lines << QStringLiteral("Description: %1").arg(context.mapSelection.description);
    }
    const QString earthFile = context.mapSelection.earthFile.isEmpty() ? context.mapSelection.earthFilePath : context.mapSelection.earthFile;
    lines << QStringLiteral("Earth file: %1").arg(earthFile.isEmpty() ? QStringLiteral("<none>") : earthFile);
    lines << QStringLiteral("Earth load:");
    lines << QStringLiteral("  Requested path: %1")
                 .arg(context.earthLoad.requestedPath.isEmpty() ? QStringLiteral("<none>") : context.earthLoad.requestedPath);
    lines << QStringLiteral("  Attempted: %1").arg(context.earthLoad.attempted ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Loaded: %1").arg(context.earthLoad.loaded ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Textured fallback: %1").arg(context.earthLoad.usedTexturedFallback ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  MapNode: %1").arg(context.earthLoad.foundMapNode ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Layers: %1/%2 open").arg(context.earthLoad.openLayerCount).arg(context.earthLoad.layerCount);
    if (!context.earthLoad.failureReason.isEmpty())
    {
        lines << QStringLiteral("  Failure/note: %1").arg(context.earthLoad.failureReason);
    }
    if (!context.earthLoad.layerSummaries.isEmpty())
    {
        lines << QStringLiteral("  Layer details:");
        for (const QString& layerSummary : context.earthLoad.layerSummaries)
        {
            lines << QStringLiteral("    - %1").arg(layerSummary);
        }
    }
    lines << QStringLiteral("Local native OSG building load:");
    lines << QStringLiteral("  Requested path: %1")
                 .arg(context.tilesLoad.requestedPath.isEmpty()
                          ? QStringLiteral("<none>")
                          : context.tilesLoad.requestedPath);
    lines << QStringLiteral("  Attempted: %1").arg(context.tilesLoad.attempted ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Loaded: %1").arg(context.tilesLoad.loaded ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Cleared previous preview: %1")
                 .arg(context.tilesLoad.clearedPreviousPreview ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Payloads: %1/%2 loaded, %3 failed")
                 .arg(context.tilesLoad.loadedPayloadCount)
                 .arg(context.tilesLoad.payloadCount)
                 .arg(context.tilesLoad.failedPayloadCount);
    if (!context.tilesLoad.nodeDescription.isEmpty())
    {
        lines << QStringLiteral("  Node: %1").arg(context.tilesLoad.nodeDescription);
    }
    if (!context.tilesLoad.failureReason.isEmpty())
    {
        lines << QStringLiteral("  Failure/note: %1").arg(context.tilesLoad.failureReason);
    }
    const AircraftModelDiagnostics& aircraftModel = context.aircraftModel;
    lines << QStringLiteral("Aircraft model:");
    lines << QStringLiteral("  Requested path: %1")
                 .arg(aircraftModel.requestedPath.isEmpty()
                          ? QStringLiteral("<none>")
                          : aircraftModel.requestedPath);
    lines << QStringLiteral("  Attempted: %1").arg(aircraftModel.attempted ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Loaded: %1").arg(aircraftModel.loaded ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("  Built-in marker: %1").arg(aircraftModel.usingBuiltInMarker ? QStringLiteral("yes") : QStringLiteral("no"));
    if (!aircraftModel.nodeDescription.isEmpty())
    {
        lines << QStringLiteral("  Node: %1").arg(aircraftModel.nodeDescription);
    }
    if (!aircraftModel.failureReason.isEmpty())
    {
        lines << QStringLiteral("  Failure/note: %1").arg(aircraftModel.failureReason);
    }
    lines << QStringLiteral("Render performance:");
    lines << QStringLiteral("  Samples: %1 visible / %2 total / %3 hidden")
                 .arg(visibleSamples)
                 .arg(totalSamples)
                 .arg(hiddenSamples);
    lines << QStringLiteral("  Max visible samples: %1").arg(maxVisibleSamples);
    lines << QStringLiteral("  Trajectory segments: %1 x %2 samples")
                 .arg(stats.segmentCount)
                 .arg(stats.segmentSize);
    lines << QStringLiteral("  FPS: %1").arg(stats.framesPerSecond, 0, 'f', 1);
    lines << QStringLiteral("  Frame ms: %1").arg(stats.frameMs, 0, 'f', 1);
    lines << QStringLiteral("  Track update ms: %1").arg(stats.trackUpdateMs, 0, 'f', 1);
    lines << QStringLiteral("Trajectory quality:");
    lines << QStringLiteral("  Visible line samples: %1").arg(qualityStats.lineSamples);
    lines << QStringLiteral("  Visible marker samples: %1").arg(qualityStats.markerSamples);
    lines << QStringLiteral("  Fixed: %1").arg(qualityStats.fixedSamples);
    lines << QStringLiteral("  Float: %1").arg(qualityStats.floatSamples);
    lines << QStringLiteral("  DGPS: %1").arg(qualityStats.dgpsSamples);
    lines << QStringLiteral("  Single: %1").arg(qualityStats.singleSamples);
    lines << QStringLiteral("  Unknown: %1").arg(qualityStats.unknownSamples);
    lines << QStringLiteral("  Invalid/unusable: %1").arg(qualityStats.invalidSamples);
    lines << QStringLiteral("  Jump markers: %1").arg(qualityStats.jumpSamples);
    lines << QStringLiteral("Track data:");
    lines << QStringLiteral("  Source: %1").arg(context.trackSource.isEmpty() ? QStringLiteral("none") : context.trackSource);
    lines << QStringLiteral("  Replay state: %1").arg(context.replayState);
    if (context.hasReplay)
    {
        lines << QStringLiteral("  Replay position: %1/%2")
                     .arg((std::max)(0, context.replayIndex + 1))
                     .arg(context.replaySampleCount);
        lines << QStringLiteral("  Replay speed: %1x").arg(context.replaySpeed, 0, 'g', 3);
        lines << QStringLiteral("  Replay time: %1").arg(context.replayTime);
    }
    lines << QStringLiteral("  Latest record timestamp us: %1")
                 .arg(context.latestRecordTimestampUs > 0 ? QString::number(context.latestRecordTimestampUs) : QStringLiteral("<none>"));
    lines << QStringLiteral("  Latest device timestamp us: %1")
                 .arg(context.latestDeviceTimestampUs > 0 ? QString::number(context.latestDeviceTimestampUs) : QStringLiteral("<none>"));
    lines << QStringLiteral("  Attitude source: %1")
                 .arg(context.attitudeSource);
    lines << QStringLiteral("  Follow aircraft: %1")
                 .arg(context.followAircraft ? QStringLiteral("on") : QStringLiteral("off"));
    if (context.hasLatestLlh)
    {
        lines << QStringLiteral("  Height reference: %1")
                     .arg(context.heightReference);
        lines << QStringLiteral("  Fix quality: %1")
                     .arg(context.fixQuality);
        lines << QStringLiteral("  Height safety note: %1").arg(context.heightSafetyNote);
        if (!stats.heightReferenceStatus.isEmpty())
        {
            lines << QStringLiteral("  Height conversion: %1").arg(stats.heightReferenceStatus);
        }
    }
    if (!context.trackNote.isEmpty())
    {
        lines << QStringLiteral("  Note: %1").arg(context.trackNote);
    }
    lines << QStringLiteral("  Last drop source: %1").arg(context.dropSource.isEmpty() ? QStringLiteral("<none>") : context.dropSource);
    lines << QStringLiteral("  Last drop reason: %1").arg(context.dropReason.isEmpty() ? QStringLiteral("<none>") : context.dropReason);
    lines << QStringLiteral("  Last drop record timestamp us: %1")
                 .arg(context.dropRecordTimestampUs > 0 ? QString::number(context.dropRecordTimestampUs) : QStringLiteral("<none>"));
    lines << QStringLiteral("Camera: %1").arg(context.cameraNote.isEmpty() ? QStringLiteral("<none>") : context.cameraNote);
    lines << QStringLiteral("Layer summary:");
    lines << QStringLiteral("  Readiness: %1")
                 .arg(diagnostics.readinessSummary.isEmpty() ? QStringLiteral("<not evaluated>") : diagnostics.readinessSummary);
    if (!diagnostics.readinessChecks.isEmpty())
    {
        lines << QStringLiteral("  Readiness checks:");
        for (const QString& check : diagnostics.readinessChecks)
        {
            lines << QStringLiteral("    - %1").arg(check);
        }
    }
    if (!diagnostics.readinessNextSteps.isEmpty())
    {
        lines << QStringLiteral("  Next steps:");
        for (const QString& step : diagnostics.readinessNextSteps)
        {
            lines << QStringLiteral("    - %1").arg(step);
        }
    }
    lines << QStringLiteral("  Natural Earth: %1").arg(availabilityLabel(diagnostics.naturalEarthAvailable));
    lines << QStringLiteral("  Local grid fallback: %1%2")
                 .arg(diagnostics.localGridFallbackAvailable ? QStringLiteral("available") : QStringLiteral("unavailable"),
                      diagnostics.localGridFallbackActive ? QStringLiteral(" (active)") : QStringLiteral(" (standby)"));
    lines << QStringLiteral("  Selected DEM: %1").arg(selectedDemLabel(diagnostics));
    lines << QStringLiteral("  Copernicus DEM VRT: %1").arg(availabilityLabel(diagnostics.copernicusDemAvailable));
    lines << QStringLiteral("  SRTM VRT: %1").arg(availabilityLabel(diagnostics.srtmDemAvailable));
    lines << QStringLiteral("  OSM vectors: %1 (%2/4 files found)")
                 .arg(diagnostics.osmVectorAvailable ? QStringLiteral("available") : QStringLiteral("missing"))
                 .arg(diagnostics.osmLayerCount);
    lines << QStringLiteral("  Selected OSM: %1").arg(selectedOsmLabel(diagnostics));
    lines << QStringLiteral("  Selected full-local earth: %1")
                 .arg(diagnostics.selectedFullLocalEarthPath.isEmpty() ? QStringLiteral("<not selected>") : diagnostics.selectedFullLocalEarthPath);
    lines << QStringLiteral("  Optional local imagery VRTs: %1/3 found").arg(diagnostics.localImageryLayerCount);
    lines << QStringLiteral("  Optional local imagery menu-ready overlays: %1/3")
                 .arg(diagnostics.localImageryMenuEntryCount);
    if (!diagnostics.localImageryOptions.empty())
    {
        lines << QStringLiteral("  Local imagery menu:");
        for (const LocalImageryOption& option : diagnostics.localImageryOptions)
        {
            lines << QStringLiteral("    - %1: %2 (VRT: %3, earth: %4)")
                         .arg(option.label,
                              option.available ? QStringLiteral("menu ready") : QStringLiteral("missing VRT or earth template"),
                              QFileInfo(option.vrtPath).isFile() ? QStringLiteral("found") : QStringLiteral("missing"),
                              QFileInfo(option.earthFilePath).isFile() ? QStringLiteral("found") : QStringLiteral("missing"));
        }
    }
    lines << QStringLiteral("  Optional local 3D Tiles: %1")
                 .arg(diagnostics.local3DTilesAvailable ? QStringLiteral("available") : QStringLiteral("not configured"));
    lines << QStringLiteral("  Local 3D Tiles contract: %1")
                 .arg(diagnostics.local3DTilesAvailable
                          ? (diagnostics.local3DTilesTilesetValid ? QStringLiteral("valid") : QStringLiteral("needs attention"))
                           : QStringLiteral("not checked"));
    lines << QStringLiteral("  Real 3D local map: %1")
                 .arg(diagnostics.real3DLocalReady ? QStringLiteral("ready") : QStringLiteral("not ready"));
    lines << QStringLiteral("  Real 3D earth: %1").arg(diagnostics.real3DLocalEarthPath);
    lines << QStringLiteral("Current working directory: %1").arg(diagnostics.currentWorkingDirectory.isEmpty() ? QStringLiteral("<unknown>") : diagnostics.currentWorkingDirectory);
    lines << QStringLiteral("Project root: %1").arg(diagnostics.projectRoot.isEmpty() ? QStringLiteral("<unknown>") : diagnostics.projectRoot);
    lines << QStringLiteral("Maps root: %1").arg(diagnostics.mapsRoot.isEmpty() ? QStringLiteral("<unknown>") : diagnostics.mapsRoot);
    lines << QStringLiteral("Full local Copernicus earth: %1").arg(diagnostics.fullLocalEarthPath);
    lines << QStringLiteral("Full local SRTM earth: %1").arg(diagnostics.fullLocalSrtmEarthPath);
    lines << QStringLiteral("Natural Earth texture: %1").arg(diagnostics.naturalEarthTexturePath);
    lines << QStringLiteral("Natural Earth VRT: %1").arg(diagnostics.naturalEarthVrtPath);
    lines << QStringLiteral("Natural Earth raster: %1").arg(diagnostics.naturalEarthRasterPath);
    lines << QStringLiteral("Copernicus DEM VRT: %1").arg(diagnostics.copernicusDemVrtPath);
    lines << QStringLiteral("SRTM VRT: %1").arg(diagnostics.srtmDemVrtPath);
    lines << QStringLiteral("OSM roads: %1").arg(fileAvailabilityLabel(diagnostics.osmRoadsAvailable, diagnostics.osmRoadsPath));
    lines << QStringLiteral("OSM water: %1").arg(fileAvailabilityLabel(diagnostics.osmWaterAvailable, diagnostics.osmWaterPath));
    lines << QStringLiteral("OSM buildings: %1").arg(fileAvailabilityLabel(diagnostics.osmBuildingsAvailable, diagnostics.osmBuildingsPath));
    lines << QStringLiteral("OSM places: %1").arg(fileAvailabilityLabel(diagnostics.osmPlacesAvailable, diagnostics.osmPlacesPath));
    if (!diagnostics.osmLayerContracts.isEmpty())
    {
        lines << QStringLiteral("OSM layer contract:");
        for (const QString& contract : diagnostics.osmLayerContracts)
        {
            lines << QStringLiteral("  - %1").arg(contract);
        }
    }
    lines << QStringLiteral("Sentinel-2 imagery VRT: %1").arg(diagnostics.sentinel2ImageryVrtPath);
    lines << QStringLiteral("Landsat imagery VRT: %1").arg(diagnostics.landsatImageryVrtPath);
    lines << QStringLiteral("OpenAerialMap imagery VRT: %1").arg(diagnostics.openAerialMapImageryVrtPath);
    lines << QStringLiteral("Local 3D Tiles tileset: %1").arg(diagnostics.local3DTilesTilesetPath);
    lines << QStringLiteral("Local 3D Tiles valid: %1").arg(diagnostics.local3DTilesTilesetValid ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("Local 3D Tiles referenced resources: %1").arg(diagnostics.local3DTilesResourceCount);
    if (!diagnostics.local3DTilesResourceUris.isEmpty())
    {
        lines << QStringLiteral("Local 3D Tiles resource URIs:");
        for (const QString& uri : diagnostics.local3DTilesResourceUris)
        {
            lines << QStringLiteral("  - %1").arg(uri);
        }
    }
    if (!diagnostics.local3DTilesExternalUris.isEmpty())
    {
        lines << QStringLiteral("Local 3D Tiles non-local/unsupported URIs:");
        for (const QString& uri : diagnostics.local3DTilesExternalUris)
        {
            lines << QStringLiteral("  - %1").arg(uri);
        }
    }
    if (!diagnostics.local3DTilesMissingResources.isEmpty())
    {
        lines << QStringLiteral("Local 3D Tiles missing resources:");
        for (const QString& path : diagnostics.local3DTilesMissingResources)
        {
            lines << QStringLiteral("  - %1").arg(path);
        }
    }
    if (!diagnostics.local3DTilesDiagnostics.isEmpty())
    {
        lines << QStringLiteral("Local 3D Tiles diagnostics:");
        for (const QString& message : diagnostics.local3DTilesDiagnostics)
        {
            lines << QStringLiteral("  - %1").arg(message);
        }
    }
    lines << QStringLiteral("OSG plugin path: %1").arg(diagnostics.osgPluginPath.isEmpty() ? QStringLiteral("<not found>") : diagnostics.osgPluginPath);
    lines << QStringLiteral("OSG_LIBRARY_PATH: %1").arg(diagnostics.osgLibraryPath.isEmpty() ? QStringLiteral("<not set>") : diagnostics.osgLibraryPath);
    lines << QStringLiteral("OSGEARTH_NOTIFY_LEVEL: %1").arg(diagnostics.osgEarthNotifyLevel.isEmpty() ? QStringLiteral("<not set>") : diagnostics.osgEarthNotifyLevel);
    lines << QStringLiteral("osgEarth environment:");
    if (diagnostics.osgEarthEnvironment.isEmpty())
    {
        lines << QStringLiteral("  - <none set>");
    }
    else
    {
        for (const QString& entry : diagnostics.osgEarthEnvironment)
        {
            lines << QStringLiteral("  - %1").arg(entry);
        }
    }
    lines << QStringLiteral("GDAL_DATA: %1").arg(diagnostics.gdalDataPath.isEmpty() ? QStringLiteral("<not found>") : diagnostics.gdalDataPath);
    lines << QStringLiteral("PROJ_DATA: %1").arg(diagnostics.projDataPath.isEmpty() ? QStringLiteral("<not found>") : diagnostics.projDataPath);
    lines << QStringLiteral("PROJ_LIB: %1").arg(diagnostics.projLibPath.isEmpty() ? QStringLiteral("<not found>") : diagnostics.projLibPath);

    if (!diagnostics.foundFiles.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("Found files:");
        for (const QString& path : diagnostics.foundFiles)
        {
            lines << QStringLiteral("  - %1").arg(path);
        }
    }

    if (!diagnostics.missingFiles.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("Missing files:");
        for (const QString& path : diagnostics.missingFiles)
        {
            lines << QStringLiteral("  - %1").arg(path);
        }
    }

    if (!diagnostics.fullLocalBlockers.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("Full local map blockers:");
        for (const QString& blocker : diagnostics.fullLocalBlockers)
        {
            lines << QStringLiteral("  - %1").arg(blocker);
        }
    }

    if (!diagnostics.warnings.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("Warnings:");
        for (const QString& warning : diagnostics.warnings)
        {
            lines << QStringLiteral("  - %1").arg(warning);
        }
    }

    if (!diagnostics.messages.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("Diagnostics:");
        for (const QString& message : diagnostics.messages)
        {
            lines << QStringLiteral("  - %1").arg(message);
        }
    }
    return lines.join(QLatin1Char('\n'));

}

} // namespace VaporView::Map3D
