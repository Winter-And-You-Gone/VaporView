#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QStandardPaths>
#include <QtCore/QStringList>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>

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
    environment.remove(QStringLiteral("GDAL_BIN"));
    environment.insert(QStringLiteral("VAPORVIEW_GDAL_TOOL_SEARCH"), QStringLiteral("PATH_ONLY"));
    return environment;
}

QProcessEnvironment environmentWithOnlyPath(const QString& path)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), path);
    environment.insert(QStringLiteral("Path"), path);
    environment.remove(QStringLiteral("GDAL_BIN"));
    environment.insert(QStringLiteral("VAPORVIEW_GDAL_TOOL_SEARCH"), QStringLiteral("PATH_ONLY"));
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

void writeTextFile(const QString& path, const QString& contents)
{
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile file(info.absoluteFilePath());
    require(file.open(QIODevice::WriteOnly | QIODevice::Text), QStringLiteral("open %1").arg(info.absoluteFilePath()));
    QTextStream stream(&file);
    stream << contents;
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
    const QString imageryScript = sourceRoot.filePath(QStringLiteral("scripts/prepare-local-imagery.py"));
    const QString mapsReadme = sourceRoot.filePath(QStringLiteral("data/maps/README.md"));
    const QString fullLocalEarth = sourceRoot.filePath(QStringLiteral("data/maps/vaporview_full_local.earth"));
    const QString fullLocalSrtmEarth = sourceRoot.filePath(QStringLiteral("data/maps/vaporview_full_local_srtm.earth"));
    const QString sentinel2ImageryEarth = sourceRoot.filePath(QStringLiteral("data/maps/vaporview_with_sentinel2_imagery.earth"));
    const QString landsatImageryEarth = sourceRoot.filePath(QStringLiteral("data/maps/vaporview_with_landsat_imagery.earth"));
    const QString openAerialMapImageryEarth = sourceRoot.filePath(QStringLiteral("data/maps/vaporview_with_openaerialmap_imagery.earth"));
    require(QFileInfo(osmScript).isFile(), QStringLiteral("OSM script exists"));
    require(QFileInfo(demScript).isFile(), QStringLiteral("DEM script exists"));
    require(QFileInfo(imageryScript).isFile(), QStringLiteral("local imagery script exists"));
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
                && demHelp.standardOutput.contains(QStringLiteral("--gdal-bin"))
                && demHelp.standardOutput.contains(QStringLiteral("canonical"))
                && demHelp.standardOutput.contains(QStringLiteral("MapDataManager"))
                && demHelp.standardOutput.contains(QStringLiteral("OSGeo4W")),
            QStringLiteral("prepare-demo-dem.py --help explains external tile directory and canonical VRT output"));

    const ProcessResult imageryHelp = runProcess(python, {imageryScript, QStringLiteral("--help")});
    require(imageryHelp.started,
            QStringLiteral("prepare-local-imagery.py --help starts: %1").arg(imageryHelp.standardError));
    require(!imageryHelp.timedOut, QStringLiteral("prepare-local-imagery.py --help does not time out"));
    require(imageryHelp.exitCode == 0,
            QStringLiteral("prepare-local-imagery.py --help exits 0, stderr=%1").arg(imageryHelp.standardError));
    require(imageryHelp.standardOutput.contains(QStringLiteral("sentinel2"))
                && imageryHelp.standardOutput.contains(QStringLiteral("landsat"))
                && imageryHelp.standardOutput.contains(QStringLiteral("openaerialmap"))
                && imageryHelp.standardOutput.contains(QStringLiteral("--imagery-dir"))
                && imageryHelp.standardOutput.contains(QStringLiteral("--gdal-bin"))
                && imageryHelp.standardOutput.contains(QStringLiteral("MapDataManager"))
                && imageryHelp.standardOutput.contains(QStringLiteral("toolbar menu"))
                && imageryHelp.standardOutput.contains(QStringLiteral("does not download")),
            QStringLiteral("prepare-local-imagery.py --help describes local imagery VRT preparation"));

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
    require(missingGdal.standardError.contains(QStringLiteral("set GDAL_BIN"))
                && missingGdal.standardError.contains(QStringLiteral("--gdal-bin"))
                && missingGdal.standardError.contains(QStringLiteral("Searched:")),
            QStringLiteral("prepare-demo-dem.py reports GDAL tool search hints"));

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
    require(customDemMissingGdal.standardError.contains(QStringLiteral("--gdal-bin")),
            QStringLiteral("prepare-demo-dem.py --dem-dir missing GDAL hint mentions explicit tool directory"));

    QTemporaryDir fakeImageryProject;
    require(fakeImageryProject.isValid(), QStringLiteral("temporary fake imagery project root is valid"));
    const QDir fakeImageryRoot(fakeImageryProject.path());
    writeDummyFile(fakeImageryRoot.filePath(QStringLiteral("data/maps/imagery/sentinel2/dummy.tif")),
                   QByteArrayLiteral("not a real geotiff"));
    writeDummyFile(fakeImageryRoot.filePath(QStringLiteral("data/maps/vaporview_with_sentinel2_imagery.earth")),
                   QByteArrayLiteral("imagery/sentinel2/sentinel2.vrt"));
    const ProcessResult imageryMissingGdal =
        runProcess(python,
                   {imageryScript,
                    QStringLiteral("sentinel2"),
                    QStringLiteral("--project-root"),
                    fakeImageryRoot.absolutePath(),
                    QStringLiteral("--check")},
                   environmentWithoutGdalTools());
    require(imageryMissingGdal.started,
            QStringLiteral("prepare-local-imagery.py --check starts: %1").arg(imageryMissingGdal.standardError));
    require(!imageryMissingGdal.timedOut,
            QStringLiteral("prepare-local-imagery.py --check does not time out"));
    require(imageryMissingGdal.exitCode == 2,
            QStringLiteral("prepare-local-imagery.py --check exits 2 when GDAL is missing, stdout=%1 stderr=%2")
                .arg(imageryMissingGdal.standardOutput, imageryMissingGdal.standardError));
    require(imageryMissingGdal.standardError.contains(QStringLiteral("gdalbuildvrt was not found")),
            QStringLiteral("prepare-local-imagery.py reports missing gdalbuildvrt clearly"));
    require(imageryMissingGdal.standardError.contains(QStringLiteral("set GDAL_BIN"))
                && imageryMissingGdal.standardError.contains(QStringLiteral("--gdal-bin"))
                && imageryMissingGdal.standardError.contains(QStringLiteral("Searched:")),
            QStringLiteral("prepare-local-imagery.py reports GDAL tool search hints"));

    QTemporaryDir customImageryProject;
    require(customImageryProject.isValid(), QStringLiteral("temporary custom imagery project root is valid"));
    const QDir customImageryRoot(customImageryProject.path());
    const QString customImageryDir = customImageryRoot.filePath(QStringLiteral("external_imagery_tiles"));
    writeDummyFile(QDir(customImageryDir).filePath(QStringLiteral("custom_tile.tif")),
                   QByteArrayLiteral("not a real geotiff"));
    writeDummyFile(customImageryRoot.filePath(QStringLiteral("data/maps/vaporview_with_landsat_imagery.earth")),
                   QByteArrayLiteral("imagery/landsat/landsat.vrt"));
    const ProcessResult customImageryMissingGdal =
        runProcess(python,
                   {imageryScript,
                    QStringLiteral("landsat"),
                    QStringLiteral("--project-root"),
                    customImageryRoot.absolutePath(),
                    QStringLiteral("--imagery-dir"),
                    customImageryDir,
                    QStringLiteral("--check")},
                   environmentWithoutGdalTools());
    require(customImageryMissingGdal.started,
            QStringLiteral("prepare-local-imagery.py --imagery-dir --check starts: %1")
                .arg(customImageryMissingGdal.standardError));
    require(!customImageryMissingGdal.timedOut,
            QStringLiteral("prepare-local-imagery.py --imagery-dir --check does not time out"));
    require(customImageryMissingGdal.exitCode == 2,
            QStringLiteral("prepare-local-imagery.py --imagery-dir --check exits 2 without GDAL, stdout=%1 stderr=%2")
                .arg(customImageryMissingGdal.standardOutput, customImageryMissingGdal.standardError));
    require(customImageryMissingGdal.standardError.contains(QStringLiteral("gdalbuildvrt was not found"))
                && !customImageryMissingGdal.standardError.contains(QStringLiteral("no GeoTIFF imagery tiles found")),
            QStringLiteral("prepare-local-imagery.py --imagery-dir uses the external tile directory before checking GDAL"));
    require(customImageryMissingGdal.standardError.contains(QStringLiteral("--gdal-bin")),
            QStringLiteral("prepare-local-imagery.py --imagery-dir missing GDAL hint mentions explicit tool directory"));

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
    require(missingOsmOutputs.standardError.contains(QStringLiteral("set GDAL_BIN"))
                && missingOsmOutputs.standardError.contains(QStringLiteral("--gdal-bin"))
                && missingOsmOutputs.standardError.contains(QStringLiteral("Searched:")),
            QStringLiteral("prepare-osm-local-data.py reports GDAL/OGR tool search hints"));

    QTemporaryDir fakeOgrinfoProject;
    require(fakeOgrinfoProject.isValid(), QStringLiteral("temporary fake ogrinfo project root is valid"));
    const QDir fakeOgrinfoRoot(fakeOgrinfoProject.path());
    const QString fakeOsmDir = fakeOgrinfoRoot.filePath(QStringLiteral("data/maps/osm"));
    for (const QString& fileName : {QStringLiteral("roads.gpkg"),
                                   QStringLiteral("water.gpkg"),
                                   QStringLiteral("buildings.gpkg"),
                                   QStringLiteral("places.gpkg")})
    {
        writeDummyFile(QDir(fakeOsmDir).filePath(fileName), QByteArrayLiteral("dummy gpkg"));
    }

    const QString fakeToolDir = fakeOgrinfoRoot.filePath(QStringLiteral("fake_gdal_bin"));
#ifdef Q_OS_WIN
    const QString fakeOgrinfoPath = QDir(fakeToolDir).filePath(QStringLiteral("ogrinfo.cmd"));
    writeTextFile(fakeOgrinfoPath,
                  QStringLiteral("@echo off\r\n"
                                 "echo Layer name: %4\r\n"
                                 "if \"%4\"==\"buildings\" echo name: String\r\n"
                                 "exit /b 0\r\n"));
#else
    const QString fakeOgrinfoPath = QDir(fakeToolDir).filePath(QStringLiteral("ogrinfo"));
    writeTextFile(fakeOgrinfoPath,
                  QStringLiteral("#!/bin/sh\n"
                                 "echo \"Layer name: $4\"\n"
                                 "if [ \"$4\" = \"buildings\" ]; then echo \"name: String\"; fi\n"
                                 "exit 0\n"));
    QFile::setPermissions(fakeOgrinfoPath,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                              QFile::ReadGroup | QFile::ExeGroup |
                              QFile::ReadOther | QFile::ExeOther);
#endif

    const ProcessResult missingExtrusionField =
        runProcess(python,
                   {osmScript,
                    fakeOgrinfoRoot.filePath(QStringLiteral("data/maps/osm/local_extract.osm.pbf")),
                    QStringLiteral("--project-root"),
                    fakeOgrinfoRoot.absolutePath(),
                    QStringLiteral("--check")},
                   environmentWithOnlyPath(fakeToolDir));
    require(missingExtrusionField.started,
            QStringLiteral("prepare-osm-local-data.py --check with fake ogrinfo starts: %1")
                .arg(missingExtrusionField.standardError));
    require(!missingExtrusionField.timedOut,
            QStringLiteral("prepare-osm-local-data.py --check with fake ogrinfo does not time out"));
    require(missingExtrusionField.exitCode == 2,
            QStringLiteral("prepare-osm-local-data.py --check exits 2 when buildings layer lacks extrusion_height_m, stdout=%1 stderr=%2")
                .arg(missingExtrusionField.standardOutput, missingExtrusionField.standardError));
    require(missingExtrusionField.standardError.contains(QStringLiteral("extrusion_height_m")),
            QStringLiteral("prepare-osm-local-data.py --check reports missing building extrusion field"));

    writeTextFile(fakeOgrinfoPath,
#ifdef Q_OS_WIN
                  QStringLiteral("@echo off\r\n"
                                 "echo Layer name: %4\r\n"
                                 "if \"%4\"==\"buildings\" echo extrusion_height_m: Real\r\n"
                                 "exit /b 0\r\n")
#else
                  QStringLiteral("#!/bin/sh\n"
                                 "echo \"Layer name: $4\"\n"
                                 "if [ \"$4\" = \"buildings\" ]; then echo \"extrusion_height_m: Real\"; fi\n"
                                 "exit 0\n")
#endif
    );
#ifndef Q_OS_WIN
    QFile::setPermissions(fakeOgrinfoPath,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                              QFile::ReadGroup | QFile::ExeGroup |
                              QFile::ReadOther | QFile::ExeOther);
#endif
    const ProcessResult validOsmCheck =
        runProcess(python,
                   {osmScript,
                    fakeOgrinfoRoot.filePath(QStringLiteral("data/maps/osm/local_extract.osm.pbf")),
                    QStringLiteral("--project-root"),
                    fakeOgrinfoRoot.absolutePath(),
                    QStringLiteral("--check")},
                   environmentWithOnlyPath(fakeToolDir));
    require(validOsmCheck.started,
            QStringLiteral("prepare-osm-local-data.py --check with extrusion field starts: %1")
                .arg(validOsmCheck.standardError));
    require(!validOsmCheck.timedOut,
            QStringLiteral("prepare-osm-local-data.py --check with extrusion field does not time out"));
    require(validOsmCheck.exitCode == 0,
            QStringLiteral("prepare-osm-local-data.py --check exits 0 when all layer contracts are valid, stdout=%1 stderr=%2")
                .arg(validOsmCheck.standardOutput, validOsmCheck.standardError));
    require(validOsmCheck.standardOutput.contains(QStringLiteral("CHECK buildings: extrusion_height_m field exists")),
            QStringLiteral("prepare-osm-local-data.py --check confirms building extrusion field"));

    return 0;
}
