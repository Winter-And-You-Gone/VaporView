#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QStandardPaths>
#include <QtCore/QStringList>
#include <QtCore/QTemporaryDir>

#include <cstdlib>
#include <iostream>

#ifndef VAPORVIEW_SOURCE_DIR
#define VAPORVIEW_SOURCE_DIR "."
#endif

namespace {

struct ProcessResult {
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    QString standardOutput;
    QString standardError;
};

void fail(const QString& message)
{
    std::cerr << "FAIL: " << message.toStdString() << '\n';
    std::exit(1);
}

void require(bool condition, const QString& message)
{
    if (!condition)
    {
        fail(message);
    }
}

QString findPython()
{
    for (const QString& candidate : {QStringLiteral("python"), QStringLiteral("python3"), QStringLiteral("py")})
    {
        const QString path = QStandardPaths::findExecutable(candidate);
        if (!path.isEmpty())
        {
            return path;
        }
    }
    return {};
}

ProcessResult runProcess(const QString& program,
                         const QStringList& arguments,
                         const QProcessEnvironment& environment = QProcessEnvironment::systemEnvironment())
{
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();

    ProcessResult result;
    result.started = process.waitForStarted(10000);
    if (!result.started)
    {
        result.standardError = process.errorString();
        return result;
    }

    if (!process.waitForFinished(15000))
    {
        result.timedOut = true;
        process.kill();
        process.waitForFinished(5000);
    }

    result.exitCode = process.exitCode();
    result.standardOutput = QString::fromUtf8(process.readAllStandardOutput());
    result.standardError = QString::fromUtf8(process.readAllStandardError());
    return result;
}

QProcessEnvironment environmentWithoutGdalTools()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), QString());
    environment.insert(QStringLiteral("Path"), QString());
    return environment;
}

void writeDummyFile(const QString& path, const QByteArray& contents)
{
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile file(info.absoluteFilePath());
    require(file.open(QIODevice::WriteOnly), QStringLiteral("open %1").arg(info.absoluteFilePath()));
    file.write(contents);
}

QString readTextFile(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly | QIODevice::Text), QStringLiteral("open %1").arg(path));
    return QString::fromUtf8(file.readAll());
}

} // namespace

int main()
{
    const QString python = findPython();
    if (python.isEmpty())
    {
        std::cout << "SKIP: python interpreter was not found on PATH\n";
        return 0;
    }

    const QDir sourceRoot(QString::fromLocal8Bit(VAPORVIEW_SOURCE_DIR));
    const QString osmScript = sourceRoot.filePath(QStringLiteral("scripts/prepare-osm-local-data.py"));
    const QString demScript = sourceRoot.filePath(QStringLiteral("scripts/prepare-demo-dem.py"));
    const QString mapsReadme = sourceRoot.filePath(QStringLiteral("data/maps/README.md"));
    const QString fullLocalEarth = sourceRoot.filePath(QStringLiteral("data/maps/vaporview_full_local.earth"));
    const QString fullLocalSrtmEarth = sourceRoot.filePath(QStringLiteral("data/maps/vaporview_full_local_srtm.earth"));
    const QString sentinel2ImageryEarth = sourceRoot.filePath(QStringLiteral("data/maps/vaporview_with_sentinel2_imagery.earth"));
    const QString landsatImageryEarth = sourceRoot.filePath(QStringLiteral("data/maps/vaporview_with_landsat_imagery.earth"));
    const QString openAerialMapImageryEarth = sourceRoot.filePath(QStringLiteral("data/maps/vaporview_with_openaerialmap_imagery.earth"));
    require(QFileInfo(osmScript).isFile(), QStringLiteral("OSM script exists"));
    require(QFileInfo(demScript).isFile(), QStringLiteral("DEM script exists"));
    require(QFileInfo(mapsReadme).isFile(), QStringLiteral("data/maps README exists"));
    require(QFileInfo(fullLocalEarth).isFile(), QStringLiteral("full local earth template exists"));
    require(QFileInfo(fullLocalSrtmEarth).isFile(), QStringLiteral("SRTM full local earth template exists"));
    require(QFileInfo(sentinel2ImageryEarth).isFile(), QStringLiteral("Sentinel-2 imagery earth template exists"));
    require(QFileInfo(landsatImageryEarth).isFile(), QStringLiteral("Landsat imagery earth template exists"));
    require(QFileInfo(openAerialMapImageryEarth).isFile(), QStringLiteral("OpenAerialMap imagery earth template exists"));

    const QStringList forbiddenOnlineSources = {QStringLiteral("cesium"),
                                                QStringLiteral("google"),
                                                QStringLiteral("mapbox"),
                                                QStringLiteral("arcgisonline"),
                                                QStringLiteral("tianditu"),
                                                QStringLiteral("http://"),
                                                QStringLiteral("https://")};
    const QString mapsReadmeText = readTextFile(mapsReadme);
    const QString osmScriptText = readTextFile(osmScript);
    require(mapsReadmeText.contains(QStringLiteral("Natural Earth"))
                && mapsReadmeText.contains(QStringLiteral("Copernicus DEM"))
                && mapsReadmeText.contains(QStringLiteral("SRTM"))
                && mapsReadmeText.contains(QStringLiteral("OpenStreetMap"))
                && mapsReadmeText.contains(QStringLiteral("Sentinel-2"))
                && mapsReadmeText.contains(QStringLiteral("3D Tiles"))
                && mapsReadmeText.contains(QStringLiteral("Local grid fallback")),
            QStringLiteral("data/maps README describes the local offline data stack"));
    for (const QString& forbidden : forbiddenOnlineSources)
    {
        require(!mapsReadmeText.toLower().contains(forbidden),
                QStringLiteral("data/maps README does not reference forbidden online source %1").arg(forbidden));
    }

    const QString fullLocalEarthText = readTextFile(fullLocalEarth);
    require(osmScriptText.contains(QStringLiteral("BUILDING_HEIGHT_SQL"))
                && osmScriptText.contains(QStringLiteral("hstore_get_value(other_tags, 'height')"))
                && osmScriptText.contains(QStringLiteral("hstore_get_value(other_tags, 'building:height')"))
                && osmScriptText.contains(QStringLiteral("hstore_get_value(other_tags, 'building:levels')"))
                && osmScriptText.contains(QStringLiteral("hstore_get_value(other_tags, 'levels')"))
                && osmScriptText.contains(QStringLiteral("AS extrusion_height_m"))
                && osmScriptText.contains(QStringLiteral("\"-dialect\", \"SQLITE\", \"-sql\"")),
            QStringLiteral("prepare-osm-local-data.py standardizes building extrusion height from local OSM tags"));
    require(fullLocalEarthText.contains(QStringLiteral("<FeatureImage name=\"OSM water fill\"")),
            QStringLiteral("full local earth renders OSM water with FeatureImage"));
    require(fullLocalEarthText.contains(QStringLiteral("<FeatureImage name=\"OSM roads\"")),
            QStringLiteral("full local earth renders OSM roads with FeatureImage"));
    require(fullLocalEarthText.contains(QStringLiteral("<FeatureImage name=\"OSM building footprints\"")),
            QStringLiteral("full local earth renders OSM building footprints with FeatureImage"));
    require(fullLocalEarthText.contains(QStringLiteral("<TiledFeatureModel name=\"OSM building extrusion\"")),
            QStringLiteral("full local earth renders OSM buildings with TiledFeatureModel extrusion"));
    require(fullLocalEarthText.contains(QStringLiteral("extrusion-height:        Math.max(feature.properties.extrusion_height_m, 10.0)"))
                && fullLocalEarthText.contains(QStringLiteral("extrusion-flatten:       true"))
                && fullLocalEarthText.contains(QStringLiteral("altitude-clamping:       terrain")),
            QStringLiteral("full local earth uses standardized building extrusion height with fallback"));
    require(fullLocalEarthText.contains(QStringLiteral("<TiledFeatureModel name=\"OSM place labels\"")),
            QStringLiteral("full local earth renders OSM place labels with TiledFeatureModel"));
    for (const QString& forbidden : forbiddenOnlineSources)
    {
        require(!fullLocalEarthText.toLower().contains(forbidden),
                QStringLiteral("full local earth does not reference forbidden online source %1").arg(forbidden));
    }

    const QString fullLocalSrtmEarthText = readTextFile(fullLocalSrtmEarth);
    require(fullLocalSrtmEarthText.contains(QStringLiteral("<TiledFeatureModel name=\"OSM building extrusion\"")),
            QStringLiteral("SRTM full local earth also renders OSM buildings with extrusion"));
    require(fullLocalSrtmEarthText.contains(QStringLiteral("extrusion-height:        Math.max(feature.properties.extrusion_height_m, 10.0)"))
                && fullLocalSrtmEarthText.contains(QStringLiteral("extrusion-flatten:       true"))
                && fullLocalSrtmEarthText.contains(QStringLiteral("altitude-clamping:       terrain")),
            QStringLiteral("SRTM full local earth uses standardized building extrusion height with fallback"));
    for (const QString& forbidden : forbiddenOnlineSources)
    {
        require(!fullLocalSrtmEarthText.toLower().contains(forbidden),
                QStringLiteral("SRTM full local earth does not reference forbidden online source %1").arg(forbidden));
    }

    const QStringList imageryEarthFiles = {sentinel2ImageryEarth, landsatImageryEarth, openAerialMapImageryEarth};
    const QStringList imageryVrtPaths = {QStringLiteral("imagery/sentinel2/sentinel2.vrt"),
                                         QStringLiteral("imagery/landsat/landsat.vrt"),
                                         QStringLiteral("imagery/openaerialmap/openaerialmap.vrt")};
    for (int index = 0; index < imageryEarthFiles.size(); ++index)
    {
        const QString imageryEarthText = readTextFile(imageryEarthFiles[index]);
        require(imageryEarthText.contains(QStringLiteral("natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt")),
                QStringLiteral("imagery earth template keeps Natural Earth offline background"));
        require(imageryEarthText.contains(imageryVrtPaths[index]),
                QStringLiteral("imagery earth template references its local VRT"));
        require(imageryEarthText.contains(QStringLiteral("<GDALImage")),
                QStringLiteral("imagery earth template uses GDALImage layers"));
        for (const QString& forbidden : forbiddenOnlineSources)
        {
            require(!imageryEarthText.toLower().contains(forbidden),
                    QStringLiteral("imagery earth template does not reference forbidden online source %1").arg(forbidden));
        }
    }

    const ProcessResult osmHelp = runProcess(python, {osmScript, QStringLiteral("--help")});
    require(osmHelp.started, QStringLiteral("prepare-osm-local-data.py --help starts: %1").arg(osmHelp.standardError));
    require(!osmHelp.timedOut, QStringLiteral("prepare-osm-local-data.py --help does not time out"));
    require(osmHelp.exitCode == 0,
            QStringLiteral("prepare-osm-local-data.py --help exits 0, stderr=%1").arg(osmHelp.standardError));
    require(osmHelp.standardOutput.contains(QStringLiteral("GeoPackage"))
                && osmHelp.standardOutput.contains(QStringLiteral("download data"))
                && osmHelp.standardOutput.contains(QStringLiteral("--check"))
                && osmHelp.standardOutput.contains(QStringLiteral("extrusion_height_m"))
                && osmHelp.standardOutput.contains(QStringLiteral("roads"))
                && osmHelp.standardOutput.contains(QStringLiteral("water"))
                && osmHelp.standardOutput.contains(QStringLiteral("buildings"))
                && osmHelp.standardOutput.contains(QStringLiteral("places")),
            QStringLiteral("prepare-osm-local-data.py --help describes local GeoPackage conversion"));

    const ProcessResult demHelp = runProcess(python, {demScript, QStringLiteral("--help")});
    require(demHelp.started, QStringLiteral("prepare-demo-dem.py --help starts: %1").arg(demHelp.standardError));
    require(!demHelp.timedOut, QStringLiteral("prepare-demo-dem.py --help does not time out"));
    require(demHelp.exitCode == 0,
            QStringLiteral("prepare-demo-dem.py --help exits 0, stderr=%1").arg(demHelp.standardError));
    require(demHelp.standardOutput.contains(QStringLiteral("--dem-dir"))
                && demHelp.standardOutput.contains(QStringLiteral("canonical"))
                && demHelp.standardOutput.contains(QStringLiteral("MapDataManager")),
            QStringLiteral("prepare-demo-dem.py --help explains external tile directory and canonical VRT output"));

    QTemporaryDir fakeProject;
    require(fakeProject.isValid(), QStringLiteral("temporary fake project root is valid"));
    const QDir fakeRoot(fakeProject.path());
    writeDummyFile(fakeRoot.filePath(QStringLiteral("data/maps/terrain/copernicus_dem_glo30/dummy.tif")),
                   QByteArrayLiteral("not a real geotiff"));

    const ProcessResult missingGdal =
        runProcess(python,
                   {demScript, QStringLiteral("--project-root"), fakeRoot.absolutePath(), QStringLiteral("--check")},
                   environmentWithoutGdalTools());
    require(missingGdal.started, QStringLiteral("prepare-demo-dem.py --check starts: %1").arg(missingGdal.standardError));
    require(!missingGdal.timedOut, QStringLiteral("prepare-demo-dem.py --check does not time out"));
    require(missingGdal.exitCode == 2,
            QStringLiteral("prepare-demo-dem.py --check exits 2 when GDAL is missing, stdout=%1 stderr=%2")
                .arg(missingGdal.standardOutput, missingGdal.standardError));
    require(missingGdal.standardError.contains(QStringLiteral("gdalbuildvrt was not found")),
            QStringLiteral("prepare-demo-dem.py reports missing gdalbuildvrt clearly"));

    QTemporaryDir customDemProject;
    require(customDemProject.isValid(), QStringLiteral("temporary custom DEM project root is valid"));
    const QDir customDemRoot(customDemProject.path());
    const QString customTileDir = customDemRoot.filePath(QStringLiteral("external_dem_tiles"));
    writeDummyFile(QDir(customTileDir).filePath(QStringLiteral("custom_tile.tif")),
                   QByteArrayLiteral("not a real geotiff"));
    writeDummyFile(customDemRoot.filePath(QStringLiteral("data/maps/vaporview_with_dem.earth")),
                   QByteArrayLiteral("terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt"));
    const ProcessResult customDemMissingGdal =
        runProcess(python,
                   {demScript,
                    QStringLiteral("--project-root"),
                    customDemRoot.absolutePath(),
                    QStringLiteral("--dem-dir"),
                    customTileDir,
                    QStringLiteral("--check")},
                   environmentWithoutGdalTools());
    require(customDemMissingGdal.started,
            QStringLiteral("prepare-demo-dem.py --dem-dir --check starts: %1").arg(customDemMissingGdal.standardError));
    require(!customDemMissingGdal.timedOut,
            QStringLiteral("prepare-demo-dem.py --dem-dir --check does not time out"));
    require(customDemMissingGdal.exitCode == 2,
            QStringLiteral("prepare-demo-dem.py --dem-dir --check exits 2 without GDAL, stdout=%1 stderr=%2")
                .arg(customDemMissingGdal.standardOutput, customDemMissingGdal.standardError));
    require(customDemMissingGdal.standardError.contains(QStringLiteral("gdalbuildvrt was not found"))
                && !customDemMissingGdal.standardError.contains(QStringLiteral("no GeoTIFF DEM tiles found")),
            QStringLiteral("prepare-demo-dem.py --dem-dir uses the external tile directory before checking GDAL"));

    QTemporaryDir fakeOsmProject;
    require(fakeOsmProject.isValid(), QStringLiteral("temporary fake OSM project root is valid"));
    const QDir fakeOsmRoot(fakeOsmProject.path());
    const ProcessResult missingOsmOutputs =
        runProcess(python,
                   {osmScript,
                    fakeOsmRoot.filePath(QStringLiteral("data/maps/osm/local_extract.osm.pbf")),
                    QStringLiteral("--project-root"),
                    fakeOsmRoot.absolutePath(),
                    QStringLiteral("--check")},
                   environmentWithoutGdalTools());
    require(missingOsmOutputs.started,
            QStringLiteral("prepare-osm-local-data.py --check starts: %1").arg(missingOsmOutputs.standardError));
    require(!missingOsmOutputs.timedOut,
            QStringLiteral("prepare-osm-local-data.py --check does not time out"));
    require(missingOsmOutputs.exitCode == 2,
            QStringLiteral("prepare-osm-local-data.py --check exits 2 when GeoPackages are missing, stdout=%1 stderr=%2")
                .arg(missingOsmOutputs.standardOutput, missingOsmOutputs.standardError));
    require(missingOsmOutputs.standardError.contains(QStringLiteral("ogrinfo was not found"))
                && missingOsmOutputs.standardError.contains(QStringLiteral("missing generated GeoPackage")),
            QStringLiteral("prepare-osm-local-data.py --check reports missing OSM outputs clearly"));

    return 0;
}
