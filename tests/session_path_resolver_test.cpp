#include "shared/session/SessionPathResolver.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void writeFile(const QString& filename, const QByteArray& bytes = QByteArrayLiteral("fixture"))
{
    require(QDir().mkpath(QFileInfo(filename).absolutePath()), "fixture directory is created");
    QFile file(filename);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "fixture file opens");
    require(file.write(bytes) == bytes.size(), "fixture file is written");
}

void writeManifest(const QString& sessionDirectory, const QJsonObject& root)
{
    QFile file(QDir(sessionDirectory).filePath(QStringLiteral("session.json")));
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "manifest fixture opens");
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    require(file.write(bytes) == bytes.size(), "manifest fixture is written");
}

struct Mapping
{
    VaporView::Session::SessionFileKind kind;
    const char *preferredPath;
    const char *legacyPath;
    const char *manifestPathKey;
    const char *legacyManifestPathKey;
    const char *rawFileKey;
    const char *legacyRawFileKey;
};

const Mapping kMappings[] = {
    {VaporView::Session::SessionFileKind::SensorSummaryCsv,
     "sensors/sensor_summary.csv", "sensors/devices.csv",
     "sensor_summary_csv", "devices_csv", nullptr, nullptr},
    {VaporView::Session::SessionFileKind::TemperatureControllerCsv,
     "sensors/temperature_controller.csv", "sensors/rd105_temperature_controller.csv",
     "temperature_controller_csv", "temperature_controller_csv", nullptr, nullptr},
    {VaporView::Session::SessionFileKind::Ai8TemperatureControllerCsv,
     "sensors/ai8_temperature_controller.csv", nullptr,
     "ai8_temperature_controller_csv", nullptr, nullptr, nullptr},
    {VaporView::Session::SessionFileKind::NavigationRaw,
     "raw/navigation.dat", "raw/epsilon.dat",
     "navigation_raw", "epsilon_raw", "navigation", "epsilon"},
    {VaporView::Session::SessionFileKind::PressureRaw,
     "raw/pressure.dat", "raw/ptb.dat",
     "pressure_raw", "ptb_raw", "pressure", "ptb"},
    {VaporView::Session::SessionFileKind::TemperatureHumidityRaw,
     "raw/temperature_humidity.dat", "raw/hmp.dat",
     "temperature_humidity_raw", "hmp_raw", "temperature_humidity", "hmp"},
    {VaporView::Session::SessionFileKind::DistanceRaw,
     "raw/distance.dat", "raw/lidar.dat",
     "distance_raw", "lidar_raw", "distance", "lidar"},
    {VaporView::Session::SessionFileKind::WaveformRaw,
     "raw/waveform.dat", "raw/tcp_wave.dat",
     "waveform_raw", "tcp_wave_raw", "waveform", "tcp_wave"},
    {VaporView::Session::SessionFileKind::WaveformPeaksCsv,
     "raw/waveform_peaks.csv", "raw/tcp_wave_peaks.csv",
     "waveform_peaks_csv", "tcp_wave_peaks_csv", nullptr, nullptr}
};

void testDefaultAndLegacyResolution()
{
    for (const Mapping& mapping : kMappings)
    {
        if (!mapping.legacyPath)
        {
            continue;
        }
        QTemporaryDir session;
        require(session.isValid(), "temporary session is available");
        const QString preferred = session.filePath(QString::fromLatin1(mapping.preferredPath));
        const QString legacy = session.filePath(QString::fromLatin1(mapping.legacyPath));
        writeFile(legacy, QByteArrayLiteral("legacy"));

        auto context = VaporView::Session::loadSessionPathContext(session.path());
        auto resolved = VaporView::Session::resolveSessionPath(context, mapping.kind);
        require(resolved.exists && resolved.usedLegacyPath &&
                    resolved.absolutePath == QDir::fromNativeSeparators(legacy),
                "legacy path is used when the semantic path is absent");

        writeFile(preferred, QByteArrayLiteral("preferred"));
        context = VaporView::Session::loadSessionPathContext(session.path());
        resolved = VaporView::Session::resolveSessionPath(context, mapping.kind);
        require(resolved.exists && !resolved.usedLegacyPath &&
                    resolved.absolutePath == QDir::fromNativeSeparators(preferred),
                "semantic path wins when both paths exist");
        require(!resolved.warning.isEmpty(), "dual-path selection returns a diagnostic warning");

        QFile legacyFile(legacy);
        require(legacyFile.open(QIODevice::ReadOnly) && legacyFile.readAll() == QByteArrayLiteral("legacy"),
                "legacy file is not modified during resolution");
    }
}

void testManifestDeclaredLegacyPaths()
{
    for (const Mapping& mapping : kMappings)
    {
        if (!mapping.legacyPath)
        {
            continue;
        }
        QTemporaryDir session;
        require(session.isValid(), "temporary manifest session is available");
        const QString legacyRelative = QString::fromLatin1(mapping.legacyPath);
        writeFile(session.filePath(legacyRelative));

        QJsonObject root;
        if (mapping.legacyRawFileKey)
        {
            QJsonObject raw;
            raw.insert(QStringLiteral("path"), legacyRelative);
            QJsonObject rawFiles;
            rawFiles.insert(QString::fromLatin1(mapping.legacyRawFileKey), raw);
            root.insert(QStringLiteral("raw_files"), rawFiles);
        }
        else
        {
            QJsonObject paths;
            paths.insert(QString::fromLatin1(mapping.legacyManifestPathKey), legacyRelative);
            root.insert(QStringLiteral("paths"), paths);
        }
        writeManifest(session.path(), root);

        const auto context = VaporView::Session::loadSessionPathContext(session.path());
        const auto resolved = VaporView::Session::resolveSessionPath(context, mapping.kind);
        require(resolved.manifestDeclared && resolved.exists && resolved.usedLegacyPath,
                "manifest-declared legacy path is respected");
        require(resolved.relativePath == legacyRelative,
                "manifest-declared legacy relative path is preserved");
    }
}

void testManifestPriorityAndMissingDeclaration()
{
    QTemporaryDir session;
    require(session.isValid(), "temporary priority session is available");
    writeFile(session.filePath(QStringLiteral("raw/navigation.dat")));
    writeFile(session.filePath(QStringLiteral("custom/navigation_capture.dat")));

    QJsonObject paths;
    paths.insert(QStringLiteral("navigation_raw"), QStringLiteral("custom/navigation_capture.dat"));
    QJsonObject root;
    root.insert(QStringLiteral("paths"), paths);
    writeManifest(session.path(), root);

    auto context = VaporView::Session::loadSessionPathContext(session.path());
    auto resolved = VaporView::Session::resolveSessionPath(
        context, VaporView::Session::SessionFileKind::NavigationRaw);
    require(resolved.manifestDeclared && resolved.exists &&
                resolved.relativePath == QStringLiteral("custom/navigation_capture.dat"),
            "manifest path has priority over the semantic default");

    paths.insert(QStringLiteral("navigation_raw"), QStringLiteral("custom/missing.dat"));
    root.insert(QStringLiteral("paths"), paths);
    writeManifest(session.path(), root);
    context = VaporView::Session::loadSessionPathContext(session.path());
    resolved = VaporView::Session::resolveSessionPath(
        context, VaporView::Session::SessionFileKind::NavigationRaw);
    require(resolved.manifestDeclared && !resolved.exists &&
                resolved.relativePath == QStringLiteral("custom/missing.dat"),
            "missing manifest-declared path does not fall back to another file");
}

void testUnchangedWaveformFeaturesPath()
{
    const auto& aliases = VaporView::Session::sessionPathAliases(
        VaporView::Session::SessionFileKind::WaveformFeaturesCsv);
    require(aliases.preferredPath == QStringLiteral("sensors/waveform_features.csv") &&
                aliases.legacyPaths.isEmpty(),
            "waveform_features.csv remains the unchanged semantic standard");
}

}  // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    testDefaultAndLegacyResolution();
    testManifestDeclaredLegacyPaths();
    testManifestPriorityAndMissingDeclaration();
    testUnchangedWaveformFeaturesPath();
    return 0;
}
