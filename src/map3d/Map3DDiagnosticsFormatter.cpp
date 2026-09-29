#include "MapLanguage.h"
#include "Map3DDiagnosticsFormatter.h"

#include <QFileInfo>

#include <algorithm>

namespace VaporView::Map3D {
namespace {

QString availabilityLabel(bool available)
{
    return available ? mapText(QStringLiteral("可用")) : mapText(QStringLiteral("缺失"));
}

QString selectedDemLabel(const MapDataDiagnostics& diagnostics)
{
    if (!diagnostics.selectedDemLayerAvailable)
    {
        return mapText(QStringLiteral("无"));
    }
    return diagnostics.selectedElevationSource.isEmpty()
        ? mapText(QStringLiteral("可用"))
        : diagnostics.selectedElevationSource;
}

QString selectedOsmLabel(const MapDataDiagnostics& diagnostics)
{
    if (!diagnostics.selectedOsmLayersAvailable)
    {
        return mapText(QStringLiteral("未选择（%1/4 个文件）")).arg(diagnostics.osmLayerCount);
    }
    return mapText(QStringLiteral("%1 个安全图层（水系/道路，%2/4 个文件）"))
        .arg(diagnostics.selectedOsmLayerCount)
        .arg(diagnostics.osmLayerCount);
}

QString fileAvailabilityLabel(bool available, const QString& path)
{
    return QStringLiteral("%1 - %2")
        .arg(available ? mapText(QStringLiteral("可用")) : mapText(QStringLiteral("缺失")), path);
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
    lines << mapText(QStringLiteral("模式： %1 (%2)"))
                 .arg(MapDataManager::modeLabel(context.mapSelection.mode),
                      MapDataManager::modeKey(context.mapSelection.mode));
    lines << mapText(QStringLiteral("底图优先级： %1"))
                 .arg(diagnostics.baseMapPriority.isEmpty()
                          ? mapText(QStringLiteral("Copernicus DEM > SRTM > Natural Earth > 本地网格"))
                          : diagnostics.baseMapPriority);
    lines << mapText(QStringLiteral("已选底图模式： %1 (%2)"))
                 .arg(diagnostics.selectedBaseModeLabel.isEmpty()
                          ? mapText(QStringLiteral("<未评估>"))
                          : diagnostics.selectedBaseModeLabel,
                      diagnostics.selectedBaseModeKey.isEmpty()
                          ? mapText(QStringLiteral("<未评估>"))
                          : diagnostics.selectedBaseModeKey);
    lines << mapText(QStringLiteral("已选底图场景文件： %1"))
                 .arg(diagnostics.selectedBaseEarthFilePath.isEmpty()
                          ? mapText(QStringLiteral("<无>"))
                          : diagnostics.selectedBaseEarthFilePath);
    if (!context.mapSelection.description.isEmpty())
    {
        lines << mapText(QStringLiteral("说明： %1")).arg(context.mapSelection.description);
    }
    const QString earthFile = context.mapSelection.earthFile.isEmpty() ? context.mapSelection.earthFilePath : context.mapSelection.earthFile;
    lines << mapText(QStringLiteral("地图场景文件： %1")).arg(earthFile.isEmpty() ? mapText(QStringLiteral("<无>")) : earthFile);
    lines << mapText(QStringLiteral("地图场景加载："));
    lines << mapText(QStringLiteral("  请求路径： %1"))
                 .arg(context.earthLoad.requestedPath.isEmpty() ? mapText(QStringLiteral("<无>")) : context.earthLoad.requestedPath);
    lines << mapText(QStringLiteral("  已尝试加载： %1")).arg(context.earthLoad.attempted ? mapText(QStringLiteral("是")) : mapText(QStringLiteral("否")));
    lines << mapText(QStringLiteral("  已加载： %1")).arg(context.earthLoad.loaded ? mapText(QStringLiteral("是")) : mapText(QStringLiteral("否")));
    lines << mapText(QStringLiteral("  纹理回退： %1")).arg(context.earthLoad.usedTexturedFallback ? mapText(QStringLiteral("是")) : mapText(QStringLiteral("否")));
    lines << QStringLiteral("  MapNode: %1").arg(context.earthLoad.foundMapNode ? mapText(QStringLiteral("是")) : mapText(QStringLiteral("否")));
    lines << mapText(QStringLiteral("  图层： %1/%2 已打开")).arg(context.earthLoad.openLayerCount).arg(context.earthLoad.layerCount);
    if (!context.earthLoad.failureReason.isEmpty())
    {
        lines << mapText(QStringLiteral("  失败原因或说明： %1")).arg(context.earthLoad.failureReason);
    }
    if (!context.earthLoad.layerSummaries.isEmpty())
    {
        lines << mapText(QStringLiteral("  图层详情："));
        for (const QString& layerSummary : context.earthLoad.layerSummaries)
        {
            lines << QStringLiteral("    - %1").arg(layerSummary);
        }
    }
    lines << mapText(QStringLiteral("本地原生 OSG 建筑加载："));
    lines << mapText(QStringLiteral("  请求路径： %1"))
                 .arg(context.tilesLoad.requestedPath.isEmpty()
                          ? mapText(QStringLiteral("<无>"))
                          : context.tilesLoad.requestedPath);
    lines << mapText(QStringLiteral("  已尝试加载： %1")).arg(context.tilesLoad.attempted ? mapText(QStringLiteral("是")) : mapText(QStringLiteral("否")));
    lines << mapText(QStringLiteral("  已加载： %1")).arg(context.tilesLoad.loaded ? mapText(QStringLiteral("是")) : mapText(QStringLiteral("否")));
    lines << mapText(QStringLiteral("  已清除原有预览： %1"))
                 .arg(context.tilesLoad.clearedPreviousPreview ? mapText(QStringLiteral("是")) : mapText(QStringLiteral("否")));
    lines << mapText(QStringLiteral("  数据块： %1/%2 已加载，%3 失败"))
                 .arg(context.tilesLoad.loadedPayloadCount)
                 .arg(context.tilesLoad.payloadCount)
                 .arg(context.tilesLoad.failedPayloadCount);
    if (!context.tilesLoad.nodeDescription.isEmpty())
    {
        lines << mapText(QStringLiteral("  节点： %1")).arg(context.tilesLoad.nodeDescription);
    }
    if (!context.tilesLoad.failureReason.isEmpty())
    {
        lines << mapText(QStringLiteral("  失败原因或说明： %1")).arg(context.tilesLoad.failureReason);
    }
    const AircraftModelDiagnostics& aircraftModel = context.aircraftModel;
    lines << mapText(QStringLiteral("飞行器模型："));
    lines << mapText(QStringLiteral("  请求路径： %1"))
                 .arg(aircraftModel.requestedPath.isEmpty()
                          ? mapText(QStringLiteral("<无>"))
                          : aircraftModel.requestedPath);
    lines << mapText(QStringLiteral("  已尝试加载： %1")).arg(aircraftModel.attempted ? mapText(QStringLiteral("是")) : mapText(QStringLiteral("否")));
    lines << mapText(QStringLiteral("  已加载： %1")).arg(aircraftModel.loaded ? mapText(QStringLiteral("是")) : mapText(QStringLiteral("否")));
    lines << mapText(QStringLiteral("  内置标记： %1")).arg(aircraftModel.usingBuiltInMarker ? mapText(QStringLiteral("是")) : mapText(QStringLiteral("否")));
    if (!aircraftModel.nodeDescription.isEmpty())
    {
        lines << mapText(QStringLiteral("  节点： %1")).arg(aircraftModel.nodeDescription);
    }
    if (!aircraftModel.failureReason.isEmpty())
    {
        lines << mapText(QStringLiteral("  失败原因或说明： %1")).arg(aircraftModel.failureReason);
    }
    lines << mapText(QStringLiteral("渲染性能："));
    lines << mapText(QStringLiteral("  采样点： %1 可见 / %2 总计 / %3 隐藏"))
                 .arg(visibleSamples)
                 .arg(totalSamples)
                 .arg(hiddenSamples);
    lines << mapText(QStringLiteral("  最大可见采样点数： %1")).arg(maxVisibleSamples);
    lines << mapText(QStringLiteral("  轨迹分段： %1 段 × %2 点"))
                 .arg(stats.segmentCount)
                 .arg(stats.segmentSize);
    lines << QStringLiteral("  FPS: %1").arg(stats.framesPerSecond, 0, 'f', 1);
    lines << mapText(QStringLiteral("  CPU 单帧耗时 ms（非 GPU 耗时）： %1")).arg(stats.frameMs, 0, 'f', 1);
    lines << mapText(QStringLiteral("  显示帧间隔 P95 ms（含空闲等待）： %1")).arg(stats.frameIntervalP95Ms, 0, 'f', 1);
    lines << mapText(QStringLiteral("  绘制结束到帧交换耗时 ms： %1")).arg(stats.presentMs, 0, 'f', 1);
    lines << mapText(QStringLiteral("  osgEarth 等待/运行任务数（整个进程）： %1/%2")).arg(stats.pendingJobs).arg(stats.runningJobs);
    lines << mapText(QStringLiteral("  卫星影像请求/失败次数（不含缓存命中）： %1/%2")).arg(stats.imageryRequests).arg(stats.imageryFailures);
    lines << mapText(QStringLiteral("  轨迹更新耗时 ms： %1")).arg(stats.trackUpdateMs, 0, 'f', 1);
    lines << mapText(QStringLiteral("轨迹质量："));
    lines << mapText(QStringLiteral("  可见轨迹线采样点： %1")).arg(qualityStats.lineSamples);
    lines << mapText(QStringLiteral("  可见标记采样点： %1")).arg(qualityStats.markerSamples);
    lines << mapText(QStringLiteral("  固定解： %1")).arg(qualityStats.fixedSamples);
    lines << mapText(QStringLiteral("  浮点解： %1")).arg(qualityStats.floatSamples);
    lines << mapText(QStringLiteral("  差分定位： %1")).arg(qualityStats.dgpsSamples);
    lines << mapText(QStringLiteral("  单点定位： %1")).arg(qualityStats.singleSamples);
    lines << mapText(QStringLiteral("  未知： %1")).arg(qualityStats.unknownSamples);
    lines << mapText(QStringLiteral("  无效或不可用： %1")).arg(qualityStats.invalidSamples);
    lines << mapText(QStringLiteral("  跳变标记： %1")).arg(qualityStats.jumpSamples);
    lines << mapText(QStringLiteral("轨迹数据："));
    lines << mapText(QStringLiteral("  来源： %1")).arg(context.trackSource.isEmpty() ? mapText(QStringLiteral("无")) : context.trackSource);
    lines << mapText(QStringLiteral("  回放状态： %1")).arg(context.replayState);
    if (context.hasReplay)
    {
        lines << mapText(QStringLiteral("  回放位置： %1/%2"))
                     .arg((std::max)(0, context.replayIndex + 1))
                     .arg(context.replaySampleCount);
        lines << mapText(QStringLiteral("  回放速度： %1x")).arg(context.replaySpeed, 0, 'g', 3);
        lines << mapText(QStringLiteral("  回放时间： %1")).arg(context.replayTime);
    }
    lines << mapText(QStringLiteral("  最新记录时间戳 us： %1"))
                 .arg(context.latestRecordTimestampUs > 0 ? QString::number(context.latestRecordTimestampUs) : mapText(QStringLiteral("<无>")));
    lines << mapText(QStringLiteral("  最新设备时间戳 us： %1"))
                 .arg(context.latestDeviceTimestampUs > 0 ? QString::number(context.latestDeviceTimestampUs) : mapText(QStringLiteral("<无>")));
    lines << mapText(QStringLiteral("  姿态来源： %1"))
                 .arg(context.attitudeSource);
    lines << mapText(QStringLiteral("  跟随飞行器： %1"))
                 .arg(context.followAircraft ? mapText(QStringLiteral("开启")) : mapText(QStringLiteral("关闭")));
    if (context.hasLatestLlh)
    {
        lines << mapText(QStringLiteral("  高度基准： %1"))
                     .arg(context.heightReference);
        lines << mapText(QStringLiteral("  定位质量： %1"))
                     .arg(context.fixQuality);
        lines << mapText(QStringLiteral("  高度使用说明： %1")).arg(context.heightSafetyNote);
        if (!stats.heightReferenceStatus.isEmpty())
        {
            lines << mapText(QStringLiteral("  高度转换： %1")).arg(stats.heightReferenceStatus);
        }
    }
    if (!context.trackNote.isEmpty())
    {
        lines << mapText(QStringLiteral("  备注： %1")).arg(context.trackNote);
    }
    lines << mapText(QStringLiteral("  最近丢弃数据来源： %1")).arg(context.dropSource.isEmpty() ? mapText(QStringLiteral("<无>")) : context.dropSource);
    lines << mapText(QStringLiteral("  最近丢弃原因： %1")).arg(context.dropReason.isEmpty() ? mapText(QStringLiteral("<无>")) : context.dropReason);
    lines << mapText(QStringLiteral("  最近丢弃记录时间戳 us： %1"))
                 .arg(context.dropRecordTimestampUs > 0 ? QString::number(context.dropRecordTimestampUs) : mapText(QStringLiteral("<无>")));
    lines << mapText(QStringLiteral("视角： %1")).arg(context.cameraNote.isEmpty() ? mapText(QStringLiteral("<无>")) : context.cameraNote);
    lines << mapText(QStringLiteral("图层摘要："));
    lines << mapText(QStringLiteral("  就绪状态： %1"))
                 .arg(diagnostics.readinessSummary.isEmpty() ? mapText(QStringLiteral("<未评估>")) : diagnostics.readinessSummary);
    if (!diagnostics.readinessChecks.isEmpty())
    {
        lines << mapText(QStringLiteral("  就绪检查："));
        for (const QString& check : diagnostics.readinessChecks)
        {
            lines << QStringLiteral("    - %1").arg(check);
        }
    }
    if (!diagnostics.readinessNextSteps.isEmpty())
    {
        lines << mapText(QStringLiteral("  后续操作："));
        for (const QString& step : diagnostics.readinessNextSteps)
        {
            lines << QStringLiteral("    - %1").arg(step);
        }
    }
    lines << QStringLiteral("  Natural Earth: %1").arg(availabilityLabel(diagnostics.naturalEarthAvailable));
    lines << mapText(QStringLiteral("  本地网格回退： %1%2"))
                 .arg(diagnostics.localGridFallbackAvailable ? mapText(QStringLiteral("可用")) : mapText(QStringLiteral("不可用")),
                      diagnostics.localGridFallbackActive ? mapText(QStringLiteral("（使用中）")) : mapText(QStringLiteral("（备用）")));
    lines << mapText(QStringLiteral("  已选 DEM： %1")).arg(selectedDemLabel(diagnostics));
    lines << QStringLiteral("  Copernicus DEM VRT: %1").arg(availabilityLabel(diagnostics.copernicusDemAvailable));
    lines << QStringLiteral("  SRTM VRT: %1").arg(availabilityLabel(diagnostics.srtmDemAvailable));
    lines << mapText(QStringLiteral("  OSM 矢量数据： %1 (已找到 %2/4 个文件)"))
                 .arg(diagnostics.osmVectorAvailable ? mapText(QStringLiteral("可用")) : mapText(QStringLiteral("缺失")))
                 .arg(diagnostics.osmLayerCount);
    lines << mapText(QStringLiteral("  已选 OSM： %1")).arg(selectedOsmLabel(diagnostics));
    lines << mapText(QStringLiteral("  已选完整本地地图场景： %1"))
                 .arg(diagnostics.selectedFullLocalEarthPath.isEmpty() ? mapText(QStringLiteral("<未选择>")) : diagnostics.selectedFullLocalEarthPath);
    lines << mapText(QStringLiteral("  可选本地影像 VRT： 已找到 %1/3")).arg(diagnostics.localImageryLayerCount);
    lines << mapText(QStringLiteral("  可从菜单加载的本地影像： %1/3"))
                 .arg(diagnostics.localImageryMenuEntryCount);
    if (!diagnostics.localImageryOptions.empty())
    {
        lines << mapText(QStringLiteral("  本地影像菜单："));
        for (const LocalImageryOption& option : diagnostics.localImageryOptions)
        {
            lines << QStringLiteral("    - %1: %2 (VRT: %3, earth: %4)")
                         .arg(option.label,
                              option.available ? mapText(QStringLiteral("可从菜单加载")) : mapText(QStringLiteral("缺少 VRT 或场景模板")),
                              QFileInfo(option.vrtPath).isFile() ? mapText(QStringLiteral("已找到")) : mapText(QStringLiteral("缺失")),
                              QFileInfo(option.earthFilePath).isFile() ? mapText(QStringLiteral("已找到")) : mapText(QStringLiteral("缺失")));
        }
    }
    lines << mapText(QStringLiteral("  可选原生 OSG 建筑瓦片： %1"))
                 .arg(diagnostics.local3DTilesAvailable ? mapText(QStringLiteral("可用")) : mapText(QStringLiteral("未配置")));
    lines << mapText(QStringLiteral("  原生 OSG 建筑瓦片格式约定： %1"))
                 .arg(diagnostics.local3DTilesAvailable
                          ? (diagnostics.local3DTilesTilesetValid ? mapText(QStringLiteral("有效")) : mapText(QStringLiteral("需要检查")))
                           : mapText(QStringLiteral("未检查")));
    lines << mapText(QStringLiteral("  实景三维本地地图： %1"))
                 .arg(diagnostics.real3DLocalReady ? mapText(QStringLiteral("就绪")) : mapText(QStringLiteral("未就绪")));
    lines << mapText(QStringLiteral("  实景三维场景： %1")).arg(diagnostics.real3DLocalEarthPath);
    lines << mapText(QStringLiteral("当前工作目录： %1")).arg(diagnostics.currentWorkingDirectory.isEmpty() ? mapText(QStringLiteral("<未知>")) : diagnostics.currentWorkingDirectory);
    lines << mapText(QStringLiteral("项目根目录： %1")).arg(diagnostics.projectRoot.isEmpty() ? mapText(QStringLiteral("<未知>")) : diagnostics.projectRoot);
    lines << mapText(QStringLiteral("地图根目录： %1")).arg(diagnostics.mapsRoot.isEmpty() ? mapText(QStringLiteral("<未知>")) : diagnostics.mapsRoot);
    lines << mapText(QStringLiteral("完整本地 Copernicus 场景： %1")).arg(diagnostics.fullLocalEarthPath);
    lines << mapText(QStringLiteral("完整本地 SRTM 场景： %1")).arg(diagnostics.fullLocalSrtmEarthPath);
    lines << mapText(QStringLiteral("Natural Earth 纹理： %1")).arg(diagnostics.naturalEarthTexturePath);
    lines << QStringLiteral("Natural Earth VRT: %1").arg(diagnostics.naturalEarthVrtPath);
    lines << mapText(QStringLiteral("Natural Earth 栅格： %1")).arg(diagnostics.naturalEarthRasterPath);
    lines << QStringLiteral("Copernicus DEM VRT: %1").arg(diagnostics.copernicusDemVrtPath);
    lines << QStringLiteral("SRTM VRT: %1").arg(diagnostics.srtmDemVrtPath);
    lines << mapText(QStringLiteral("OSM 道路： %1")).arg(fileAvailabilityLabel(diagnostics.osmRoadsAvailable, diagnostics.osmRoadsPath));
    lines << mapText(QStringLiteral("OSM 水系： %1")).arg(fileAvailabilityLabel(diagnostics.osmWaterAvailable, diagnostics.osmWaterPath));
    lines << mapText(QStringLiteral("OSM 建筑： %1")).arg(fileAvailabilityLabel(diagnostics.osmBuildingsAvailable, diagnostics.osmBuildingsPath));
    lines << mapText(QStringLiteral("OSM 地名： %1")).arg(fileAvailabilityLabel(diagnostics.osmPlacesAvailable, diagnostics.osmPlacesPath));
    if (!diagnostics.osmLayerContracts.isEmpty())
    {
        lines << mapText(QStringLiteral("OSM 图层约定："));
        for (const QString& contract : diagnostics.osmLayerContracts)
        {
            lines << QStringLiteral("  - %1").arg(contract);
        }
    }
    lines << mapText(QStringLiteral("Sentinel-2 影像 VRT： %1")).arg(diagnostics.sentinel2ImageryVrtPath);
    lines << mapText(QStringLiteral("Landsat 影像 VRT： %1")).arg(diagnostics.landsatImageryVrtPath);
    lines << mapText(QStringLiteral("OpenAerialMap 影像 VRT： %1")).arg(diagnostics.openAerialMapImageryVrtPath);
    lines << mapText(QStringLiteral("原生 OSG 建筑瓦片集： %1")).arg(diagnostics.local3DTilesTilesetPath);
    lines << mapText(QStringLiteral("原生 OSG 建筑瓦片有效： %1")).arg(diagnostics.local3DTilesTilesetValid ? mapText(QStringLiteral("是")) : mapText(QStringLiteral("否")));
    lines << mapText(QStringLiteral("原生 OSG 建筑瓦片引用资源数： %1")).arg(diagnostics.local3DTilesResourceCount);
    if (!diagnostics.local3DTilesResourceUris.isEmpty())
    {
        lines << mapText(QStringLiteral("原生 OSG 建筑瓦片资源 URI："));
        for (const QString& uri : diagnostics.local3DTilesResourceUris)
        {
            lines << QStringLiteral("  - %1").arg(uri);
        }
    }
    if (!diagnostics.local3DTilesExternalUris.isEmpty())
    {
        lines << mapText(QStringLiteral("原生 OSG 建筑瓦片非本地或不支持的 URI："));
        for (const QString& uri : diagnostics.local3DTilesExternalUris)
        {
            lines << QStringLiteral("  - %1").arg(uri);
        }
    }
    if (!diagnostics.local3DTilesMissingResources.isEmpty())
    {
        lines << mapText(QStringLiteral("原生 OSG 建筑瓦片缺失资源："));
        for (const QString& path : diagnostics.local3DTilesMissingResources)
        {
            lines << QStringLiteral("  - %1").arg(path);
        }
    }
    if (!diagnostics.local3DTilesDiagnostics.isEmpty())
    {
        lines << mapText(QStringLiteral("原生 OSG 建筑瓦片诊断："));
        for (const QString& message : diagnostics.local3DTilesDiagnostics)
        {
            lines << QStringLiteral("  - %1").arg(message);
        }
    }
    lines << mapText(QStringLiteral("OSG 插件路径： %1")).arg(diagnostics.osgPluginPath.isEmpty() ? mapText(QStringLiteral("<未找到>")) : diagnostics.osgPluginPath);
    lines << QStringLiteral("OSG_LIBRARY_PATH: %1").arg(diagnostics.osgLibraryPath.isEmpty() ? mapText(QStringLiteral("<未设置>")) : diagnostics.osgLibraryPath);
    lines << QStringLiteral("OSGEARTH_NOTIFY_LEVEL: %1").arg(diagnostics.osgEarthNotifyLevel.isEmpty() ? mapText(QStringLiteral("<未设置>")) : diagnostics.osgEarthNotifyLevel);
    lines << mapText(QStringLiteral("osgEarth 环境变量："));
    if (diagnostics.osgEarthEnvironment.isEmpty())
    {
        lines << mapText(QStringLiteral("  - <均未设置>"));
    }
    else
    {
        for (const QString& entry : diagnostics.osgEarthEnvironment)
        {
            lines << QStringLiteral("  - %1").arg(entry);
        }
    }
    lines << QStringLiteral("GDAL_DATA: %1").arg(diagnostics.gdalDataPath.isEmpty() ? mapText(QStringLiteral("<未找到>")) : diagnostics.gdalDataPath);
    lines << QStringLiteral("PROJ_DATA: %1").arg(diagnostics.projDataPath.isEmpty() ? mapText(QStringLiteral("<未找到>")) : diagnostics.projDataPath);
    lines << QStringLiteral("PROJ_LIB: %1").arg(diagnostics.projLibPath.isEmpty() ? mapText(QStringLiteral("<未找到>")) : diagnostics.projLibPath);

    if (!diagnostics.foundFiles.isEmpty())
    {
        lines << QString();
        lines << mapText(QStringLiteral("已找到文件："));
        for (const QString& path : diagnostics.foundFiles)
        {
            lines << QStringLiteral("  - %1").arg(path);
        }
    }

    if (!diagnostics.missingFiles.isEmpty())
    {
        lines << QString();
        lines << mapText(QStringLiteral("缺失文件："));
        for (const QString& path : diagnostics.missingFiles)
        {
            lines << QStringLiteral("  - %1").arg(path);
        }
    }

    if (!diagnostics.fullLocalBlockers.isEmpty())
    {
        lines << QString();
        lines << mapText(QStringLiteral("完整本地地图阻塞项："));
        for (const QString& blocker : diagnostics.fullLocalBlockers)
        {
            lines << QStringLiteral("  - %1").arg(blocker);
        }
    }

    if (!diagnostics.warnings.isEmpty())
    {
        lines << QString();
        lines << mapText(QStringLiteral("警告："));
        for (const QString& warning : diagnostics.warnings)
        {
            lines << QStringLiteral("  - %1").arg(warning);
        }
    }

    if (!diagnostics.messages.isEmpty())
    {
        lines << QString();
        lines << mapText(QStringLiteral("诊断信息："));
        for (const QString& message : diagnostics.messages)
        {
            lines << QStringLiteral("  - %1").arg(message);
        }
    }
    const bool english = QCoreApplication::instance()->property("vaporViewEnglish").toBool();
    for (QString& line : lines) {
        const int bullet = line.indexOf(QStringLiteral("- "));
        if (bullet >= 0 && line.left(bullet).trimmed().isEmpty())
            line = line.left(bullet + 2) + mapRenderedText(line.mid(bullet + 2), english);
        else
            line = mapRenderedText(line, english);
    }
    return lines.join(QLatin1Char('\n'));

}

} // namespace VaporView::Map3D
