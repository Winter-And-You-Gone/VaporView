#include "Map3DDiagnosticsFormatter.h"

#include <QFileInfo>

#include <algorithm>

namespace VaporView::Map3D {
namespace {

QString availabilityLabel(bool available)
{
    return available ? QStringLiteral("可用") : QStringLiteral("缺失");
}

QString selectedDemLabel(const MapDataDiagnostics& diagnostics)
{
    if (!diagnostics.selectedDemLayerAvailable)
    {
        return QStringLiteral("无");
    }
    return diagnostics.selectedElevationSource.isEmpty()
        ? QStringLiteral("可用")
        : diagnostics.selectedElevationSource;
}

QString selectedOsmLabel(const MapDataDiagnostics& diagnostics)
{
    if (!diagnostics.selectedOsmLayersAvailable)
    {
        return QStringLiteral("未选择（%1/4 个文件）").arg(diagnostics.osmLayerCount);
    }
    return QStringLiteral("%1 个安全图层（水系/道路，%2/4 个文件）")
        .arg(diagnostics.selectedOsmLayerCount)
        .arg(diagnostics.osmLayerCount);
}

QString fileAvailabilityLabel(bool available, const QString& path)
{
    return QStringLiteral("%1 - %2")
        .arg(available ? QStringLiteral("可用") : QStringLiteral("缺失"), path);
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
    lines << QStringLiteral("模式： %1 (%2)")
                 .arg(MapDataManager::modeLabel(context.mapSelection.mode),
                      MapDataManager::modeKey(context.mapSelection.mode));
    lines << QStringLiteral("底图优先级： %1")
                 .arg(diagnostics.baseMapPriority.isEmpty()
                          ? QStringLiteral("Copernicus DEM > SRTM > Natural Earth > 本地网格")
                          : diagnostics.baseMapPriority);
    lines << QStringLiteral("已选底图模式： %1 (%2)")
                 .arg(diagnostics.selectedBaseModeLabel.isEmpty()
                          ? QStringLiteral("<未评估>")
                          : diagnostics.selectedBaseModeLabel,
                      diagnostics.selectedBaseModeKey.isEmpty()
                          ? QStringLiteral("<未评估>")
                          : diagnostics.selectedBaseModeKey);
    lines << QStringLiteral("已选底图场景文件： %1")
                 .arg(diagnostics.selectedBaseEarthFilePath.isEmpty()
                          ? QStringLiteral("<无>")
                          : diagnostics.selectedBaseEarthFilePath);
    if (!context.mapSelection.description.isEmpty())
    {
        lines << QStringLiteral("说明： %1").arg(context.mapSelection.description);
    }
    const QString earthFile = context.mapSelection.earthFile.isEmpty() ? context.mapSelection.earthFilePath : context.mapSelection.earthFile;
    lines << QStringLiteral("地图场景文件： %1").arg(earthFile.isEmpty() ? QStringLiteral("<无>") : earthFile);
    lines << QStringLiteral("地图场景加载：");
    lines << QStringLiteral("  请求路径： %1")
                 .arg(context.earthLoad.requestedPath.isEmpty() ? QStringLiteral("<无>") : context.earthLoad.requestedPath);
    lines << QStringLiteral("  已尝试加载： %1").arg(context.earthLoad.attempted ? QStringLiteral("是") : QStringLiteral("否"));
    lines << QStringLiteral("  已加载： %1").arg(context.earthLoad.loaded ? QStringLiteral("是") : QStringLiteral("否"));
    lines << QStringLiteral("  纹理回退： %1").arg(context.earthLoad.usedTexturedFallback ? QStringLiteral("是") : QStringLiteral("否"));
    lines << QStringLiteral("  MapNode: %1").arg(context.earthLoad.foundMapNode ? QStringLiteral("是") : QStringLiteral("否"));
    lines << QStringLiteral("  图层： %1/%2 已打开").arg(context.earthLoad.openLayerCount).arg(context.earthLoad.layerCount);
    if (!context.earthLoad.failureReason.isEmpty())
    {
        lines << QStringLiteral("  失败原因或说明： %1").arg(context.earthLoad.failureReason);
    }
    if (!context.earthLoad.layerSummaries.isEmpty())
    {
        lines << QStringLiteral("  图层详情：");
        for (const QString& layerSummary : context.earthLoad.layerSummaries)
        {
            lines << QStringLiteral("    - %1").arg(layerSummary);
        }
    }
    lines << QStringLiteral("本地原生 OSG 建筑加载：");
    lines << QStringLiteral("  请求路径： %1")
                 .arg(context.tilesLoad.requestedPath.isEmpty()
                          ? QStringLiteral("<无>")
                          : context.tilesLoad.requestedPath);
    lines << QStringLiteral("  已尝试加载： %1").arg(context.tilesLoad.attempted ? QStringLiteral("是") : QStringLiteral("否"));
    lines << QStringLiteral("  已加载： %1").arg(context.tilesLoad.loaded ? QStringLiteral("是") : QStringLiteral("否"));
    lines << QStringLiteral("  已清除原有预览： %1")
                 .arg(context.tilesLoad.clearedPreviousPreview ? QStringLiteral("是") : QStringLiteral("否"));
    lines << QStringLiteral("  数据块： %1/%2 已加载，%3 失败")
                 .arg(context.tilesLoad.loadedPayloadCount)
                 .arg(context.tilesLoad.payloadCount)
                 .arg(context.tilesLoad.failedPayloadCount);
    if (!context.tilesLoad.nodeDescription.isEmpty())
    {
        lines << QStringLiteral("  节点： %1").arg(context.tilesLoad.nodeDescription);
    }
    if (!context.tilesLoad.failureReason.isEmpty())
    {
        lines << QStringLiteral("  失败原因或说明： %1").arg(context.tilesLoad.failureReason);
    }
    const AircraftModelDiagnostics& aircraftModel = context.aircraftModel;
    lines << QStringLiteral("飞行器模型：");
    lines << QStringLiteral("  请求路径： %1")
                 .arg(aircraftModel.requestedPath.isEmpty()
                          ? QStringLiteral("<无>")
                          : aircraftModel.requestedPath);
    lines << QStringLiteral("  已尝试加载： %1").arg(aircraftModel.attempted ? QStringLiteral("是") : QStringLiteral("否"));
    lines << QStringLiteral("  已加载： %1").arg(aircraftModel.loaded ? QStringLiteral("是") : QStringLiteral("否"));
    lines << QStringLiteral("  内置标记： %1").arg(aircraftModel.usingBuiltInMarker ? QStringLiteral("是") : QStringLiteral("否"));
    if (!aircraftModel.nodeDescription.isEmpty())
    {
        lines << QStringLiteral("  节点： %1").arg(aircraftModel.nodeDescription);
    }
    if (!aircraftModel.failureReason.isEmpty())
    {
        lines << QStringLiteral("  失败原因或说明： %1").arg(aircraftModel.failureReason);
    }
    lines << QStringLiteral("渲染性能：");
    lines << QStringLiteral("  采样点： %1 可见 / %2 总计 / %3 隐藏")
                 .arg(visibleSamples)
                 .arg(totalSamples)
                 .arg(hiddenSamples);
    lines << QStringLiteral("  最大可见采样点数： %1").arg(maxVisibleSamples);
    lines << QStringLiteral("  轨迹分段： %1 段 × %2 点")
                 .arg(stats.segmentCount)
                 .arg(stats.segmentSize);
    lines << QStringLiteral("  FPS: %1").arg(stats.framesPerSecond, 0, 'f', 1);
    lines << QStringLiteral("  CPU 单帧耗时 ms（非 GPU 耗时）： %1").arg(stats.frameMs, 0, 'f', 1);
    lines << QStringLiteral("  显示帧间隔 P95 ms（含空闲等待）： %1").arg(stats.frameIntervalP95Ms, 0, 'f', 1);
    lines << QStringLiteral("  绘制结束到帧交换耗时 ms： %1").arg(stats.presentMs, 0, 'f', 1);
    lines << QStringLiteral("  osgEarth 等待/运行任务数（整个进程）： %1/%2").arg(stats.pendingJobs).arg(stats.runningJobs);
    lines << QStringLiteral("  卫星影像请求/失败次数（不含缓存命中）： %1/%2").arg(stats.imageryRequests).arg(stats.imageryFailures);
    lines << QStringLiteral("  轨迹更新耗时 ms： %1").arg(stats.trackUpdateMs, 0, 'f', 1);
    lines << QStringLiteral("轨迹质量：");
    lines << QStringLiteral("  可见轨迹线采样点： %1").arg(qualityStats.lineSamples);
    lines << QStringLiteral("  可见标记采样点： %1").arg(qualityStats.markerSamples);
    lines << QStringLiteral("  固定解： %1").arg(qualityStats.fixedSamples);
    lines << QStringLiteral("  浮点解： %1").arg(qualityStats.floatSamples);
    lines << QStringLiteral("  差分定位： %1").arg(qualityStats.dgpsSamples);
    lines << QStringLiteral("  单点定位： %1").arg(qualityStats.singleSamples);
    lines << QStringLiteral("  未知： %1").arg(qualityStats.unknownSamples);
    lines << QStringLiteral("  无效或不可用： %1").arg(qualityStats.invalidSamples);
    lines << QStringLiteral("  跳变标记： %1").arg(qualityStats.jumpSamples);
    lines << QStringLiteral("轨迹数据：");
    lines << QStringLiteral("  来源： %1").arg(context.trackSource.isEmpty() ? QStringLiteral("无") : context.trackSource);
    lines << QStringLiteral("  回放状态： %1").arg(context.replayState);
    if (context.hasReplay)
    {
        lines << QStringLiteral("  回放位置： %1/%2")
                     .arg((std::max)(0, context.replayIndex + 1))
                     .arg(context.replaySampleCount);
        lines << QStringLiteral("  回放速度： %1x").arg(context.replaySpeed, 0, 'g', 3);
        lines << QStringLiteral("  回放时间： %1").arg(context.replayTime);
    }
    lines << QStringLiteral("  最新记录时间戳 us： %1")
                 .arg(context.latestRecordTimestampUs > 0 ? QString::number(context.latestRecordTimestampUs) : QStringLiteral("<无>"));
    lines << QStringLiteral("  最新设备时间戳 us： %1")
                 .arg(context.latestDeviceTimestampUs > 0 ? QString::number(context.latestDeviceTimestampUs) : QStringLiteral("<无>"));
    lines << QStringLiteral("  姿态来源： %1")
                 .arg(context.attitudeSource);
    lines << QStringLiteral("  跟随飞行器： %1")
                 .arg(context.followAircraft ? QStringLiteral("开启") : QStringLiteral("关闭"));
    if (context.hasLatestLlh)
    {
        lines << QStringLiteral("  高度基准： %1")
                     .arg(context.heightReference);
        lines << QStringLiteral("  定位质量： %1")
                     .arg(context.fixQuality);
        lines << QStringLiteral("  高度使用说明： %1").arg(context.heightSafetyNote);
        if (!stats.heightReferenceStatus.isEmpty())
        {
            lines << QStringLiteral("  高度转换： %1").arg(stats.heightReferenceStatus);
        }
    }
    if (!context.trackNote.isEmpty())
    {
        lines << QStringLiteral("  备注： %1").arg(context.trackNote);
    }
    lines << QStringLiteral("  最近丢弃数据来源： %1").arg(context.dropSource.isEmpty() ? QStringLiteral("<无>") : context.dropSource);
    lines << QStringLiteral("  最近丢弃原因： %1").arg(context.dropReason.isEmpty() ? QStringLiteral("<无>") : context.dropReason);
    lines << QStringLiteral("  最近丢弃记录时间戳 us： %1")
                 .arg(context.dropRecordTimestampUs > 0 ? QString::number(context.dropRecordTimestampUs) : QStringLiteral("<无>"));
    lines << QStringLiteral("视角： %1").arg(context.cameraNote.isEmpty() ? QStringLiteral("<无>") : context.cameraNote);
    lines << QStringLiteral("图层摘要：");
    lines << QStringLiteral("  就绪状态： %1")
                 .arg(diagnostics.readinessSummary.isEmpty() ? QStringLiteral("<未评估>") : diagnostics.readinessSummary);
    if (!diagnostics.readinessChecks.isEmpty())
    {
        lines << QStringLiteral("  就绪检查：");
        for (const QString& check : diagnostics.readinessChecks)
        {
            lines << QStringLiteral("    - %1").arg(check);
        }
    }
    if (!diagnostics.readinessNextSteps.isEmpty())
    {
        lines << QStringLiteral("  后续操作：");
        for (const QString& step : diagnostics.readinessNextSteps)
        {
            lines << QStringLiteral("    - %1").arg(step);
        }
    }
    lines << QStringLiteral("  Natural Earth: %1").arg(availabilityLabel(diagnostics.naturalEarthAvailable));
    lines << QStringLiteral("  本地网格回退： %1%2")
                 .arg(diagnostics.localGridFallbackAvailable ? QStringLiteral("可用") : QStringLiteral("不可用"),
                      diagnostics.localGridFallbackActive ? QStringLiteral("（使用中）") : QStringLiteral("（备用）"));
    lines << QStringLiteral("  已选 DEM： %1").arg(selectedDemLabel(diagnostics));
    lines << QStringLiteral("  Copernicus DEM VRT: %1").arg(availabilityLabel(diagnostics.copernicusDemAvailable));
    lines << QStringLiteral("  SRTM VRT: %1").arg(availabilityLabel(diagnostics.srtmDemAvailable));
    lines << QStringLiteral("  OSM 矢量数据： %1 (已找到 %2/4 个文件)")
                 .arg(diagnostics.osmVectorAvailable ? QStringLiteral("可用") : QStringLiteral("缺失"))
                 .arg(diagnostics.osmLayerCount);
    lines << QStringLiteral("  已选 OSM： %1").arg(selectedOsmLabel(diagnostics));
    lines << QStringLiteral("  已选完整本地地图场景： %1")
                 .arg(diagnostics.selectedFullLocalEarthPath.isEmpty() ? QStringLiteral("<未选择>") : diagnostics.selectedFullLocalEarthPath);
    lines << QStringLiteral("  可选本地影像 VRT： 已找到 %1/3").arg(diagnostics.localImageryLayerCount);
    lines << QStringLiteral("  可从菜单加载的本地影像： %1/3")
                 .arg(diagnostics.localImageryMenuEntryCount);
    if (!diagnostics.localImageryOptions.empty())
    {
        lines << QStringLiteral("  本地影像菜单：");
        for (const LocalImageryOption& option : diagnostics.localImageryOptions)
        {
            lines << QStringLiteral("    - %1: %2 (VRT: %3, earth: %4)")
                         .arg(option.label,
                              option.available ? QStringLiteral("可从菜单加载") : QStringLiteral("缺少 VRT 或场景模板"),
                              QFileInfo(option.vrtPath).isFile() ? QStringLiteral("已找到") : QStringLiteral("缺失"),
                              QFileInfo(option.earthFilePath).isFile() ? QStringLiteral("已找到") : QStringLiteral("缺失"));
        }
    }
    lines << QStringLiteral("  可选原生 OSG 建筑瓦片： %1")
                 .arg(diagnostics.local3DTilesAvailable ? QStringLiteral("可用") : QStringLiteral("未配置"));
    lines << QStringLiteral("  原生 OSG 建筑瓦片格式约定： %1")
                 .arg(diagnostics.local3DTilesAvailable
                          ? (diagnostics.local3DTilesTilesetValid ? QStringLiteral("有效") : QStringLiteral("需要检查"))
                           : QStringLiteral("未检查"));
    lines << QStringLiteral("  实景三维本地地图： %1")
                 .arg(diagnostics.real3DLocalReady ? QStringLiteral("就绪") : QStringLiteral("未就绪"));
    lines << QStringLiteral("  实景三维场景： %1").arg(diagnostics.real3DLocalEarthPath);
    lines << QStringLiteral("当前工作目录： %1").arg(diagnostics.currentWorkingDirectory.isEmpty() ? QStringLiteral("<未知>") : diagnostics.currentWorkingDirectory);
    lines << QStringLiteral("项目根目录： %1").arg(diagnostics.projectRoot.isEmpty() ? QStringLiteral("<未知>") : diagnostics.projectRoot);
    lines << QStringLiteral("地图根目录： %1").arg(diagnostics.mapsRoot.isEmpty() ? QStringLiteral("<未知>") : diagnostics.mapsRoot);
    lines << QStringLiteral("完整本地 Copernicus 场景： %1").arg(diagnostics.fullLocalEarthPath);
    lines << QStringLiteral("完整本地 SRTM 场景： %1").arg(diagnostics.fullLocalSrtmEarthPath);
    lines << QStringLiteral("Natural Earth 纹理： %1").arg(diagnostics.naturalEarthTexturePath);
    lines << QStringLiteral("Natural Earth VRT: %1").arg(diagnostics.naturalEarthVrtPath);
    lines << QStringLiteral("Natural Earth 栅格： %1").arg(diagnostics.naturalEarthRasterPath);
    lines << QStringLiteral("Copernicus DEM VRT: %1").arg(diagnostics.copernicusDemVrtPath);
    lines << QStringLiteral("SRTM VRT: %1").arg(diagnostics.srtmDemVrtPath);
    lines << QStringLiteral("OSM 道路： %1").arg(fileAvailabilityLabel(diagnostics.osmRoadsAvailable, diagnostics.osmRoadsPath));
    lines << QStringLiteral("OSM 水系： %1").arg(fileAvailabilityLabel(diagnostics.osmWaterAvailable, diagnostics.osmWaterPath));
    lines << QStringLiteral("OSM 建筑： %1").arg(fileAvailabilityLabel(diagnostics.osmBuildingsAvailable, diagnostics.osmBuildingsPath));
    lines << QStringLiteral("OSM 地名： %1").arg(fileAvailabilityLabel(diagnostics.osmPlacesAvailable, diagnostics.osmPlacesPath));
    if (!diagnostics.osmLayerContracts.isEmpty())
    {
        lines << QStringLiteral("OSM 图层约定：");
        for (const QString& contract : diagnostics.osmLayerContracts)
        {
            lines << QStringLiteral("  - %1").arg(contract);
        }
    }
    lines << QStringLiteral("Sentinel-2 影像 VRT： %1").arg(diagnostics.sentinel2ImageryVrtPath);
    lines << QStringLiteral("Landsat 影像 VRT： %1").arg(diagnostics.landsatImageryVrtPath);
    lines << QStringLiteral("OpenAerialMap 影像 VRT： %1").arg(diagnostics.openAerialMapImageryVrtPath);
    lines << QStringLiteral("原生 OSG 建筑瓦片集： %1").arg(diagnostics.local3DTilesTilesetPath);
    lines << QStringLiteral("原生 OSG 建筑瓦片有效： %1").arg(diagnostics.local3DTilesTilesetValid ? QStringLiteral("是") : QStringLiteral("否"));
    lines << QStringLiteral("原生 OSG 建筑瓦片引用资源数： %1").arg(diagnostics.local3DTilesResourceCount);
    if (!diagnostics.local3DTilesResourceUris.isEmpty())
    {
        lines << QStringLiteral("原生 OSG 建筑瓦片资源 URI：");
        for (const QString& uri : diagnostics.local3DTilesResourceUris)
        {
            lines << QStringLiteral("  - %1").arg(uri);
        }
    }
    if (!diagnostics.local3DTilesExternalUris.isEmpty())
    {
        lines << QStringLiteral("原生 OSG 建筑瓦片非本地或不支持的 URI：");
        for (const QString& uri : diagnostics.local3DTilesExternalUris)
        {
            lines << QStringLiteral("  - %1").arg(uri);
        }
    }
    if (!diagnostics.local3DTilesMissingResources.isEmpty())
    {
        lines << QStringLiteral("原生 OSG 建筑瓦片缺失资源：");
        for (const QString& path : diagnostics.local3DTilesMissingResources)
        {
            lines << QStringLiteral("  - %1").arg(path);
        }
    }
    if (!diagnostics.local3DTilesDiagnostics.isEmpty())
    {
        lines << QStringLiteral("原生 OSG 建筑瓦片诊断：");
        for (const QString& message : diagnostics.local3DTilesDiagnostics)
        {
            lines << QStringLiteral("  - %1").arg(message);
        }
    }
    lines << QStringLiteral("OSG 插件路径： %1").arg(diagnostics.osgPluginPath.isEmpty() ? QStringLiteral("<未找到>") : diagnostics.osgPluginPath);
    lines << QStringLiteral("OSG_LIBRARY_PATH: %1").arg(diagnostics.osgLibraryPath.isEmpty() ? QStringLiteral("<未设置>") : diagnostics.osgLibraryPath);
    lines << QStringLiteral("OSGEARTH_NOTIFY_LEVEL: %1").arg(diagnostics.osgEarthNotifyLevel.isEmpty() ? QStringLiteral("<未设置>") : diagnostics.osgEarthNotifyLevel);
    lines << QStringLiteral("osgEarth 环境变量：");
    if (diagnostics.osgEarthEnvironment.isEmpty())
    {
        lines << QStringLiteral("  - <均未设置>");
    }
    else
    {
        for (const QString& entry : diagnostics.osgEarthEnvironment)
        {
            lines << QStringLiteral("  - %1").arg(entry);
        }
    }
    lines << QStringLiteral("GDAL_DATA: %1").arg(diagnostics.gdalDataPath.isEmpty() ? QStringLiteral("<未找到>") : diagnostics.gdalDataPath);
    lines << QStringLiteral("PROJ_DATA: %1").arg(diagnostics.projDataPath.isEmpty() ? QStringLiteral("<未找到>") : diagnostics.projDataPath);
    lines << QStringLiteral("PROJ_LIB: %1").arg(diagnostics.projLibPath.isEmpty() ? QStringLiteral("<未找到>") : diagnostics.projLibPath);

    if (!diagnostics.foundFiles.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("已找到文件：");
        for (const QString& path : diagnostics.foundFiles)
        {
            lines << QStringLiteral("  - %1").arg(path);
        }
    }

    if (!diagnostics.missingFiles.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("缺失文件：");
        for (const QString& path : diagnostics.missingFiles)
        {
            lines << QStringLiteral("  - %1").arg(path);
        }
    }

    if (!diagnostics.fullLocalBlockers.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("完整本地地图阻塞项：");
        for (const QString& blocker : diagnostics.fullLocalBlockers)
        {
            lines << QStringLiteral("  - %1").arg(blocker);
        }
    }

    if (!diagnostics.warnings.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("警告：");
        for (const QString& warning : diagnostics.warnings)
        {
            lines << QStringLiteral("  - %1").arg(warning);
        }
    }

    if (!diagnostics.messages.isEmpty())
    {
        lines << QString();
        lines << QStringLiteral("诊断信息：");
        for (const QString& message : diagnostics.messages)
        {
            lines << QStringLiteral("  - %1").arg(message);
        }
    }
    return lines.join(QLatin1Char('\n'));

}

} // namespace VaporView::Map3D
