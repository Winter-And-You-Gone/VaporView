#include "Map3DRuntime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

#include <osgDB/Registry>
#include <osgEarth/Registry>

#include <mutex>

namespace VaporView::Map3D {
namespace {

QString firstExistingDirectory(const QStringList& roots, const QStringList& relatives)
{
    for (const QString& root : roots)
    {
        for (const QString& relative : relatives)
        {
            const QString candidate = QDir::cleanPath(QDir(root).absoluteFilePath(relative));
            if (QFileInfo(candidate).isDir())
            {
                return QFileInfo(candidate).absoluteFilePath();
            }
        }
    }
    return {};
}

QString firstExistingDirectoryMatching(const QStringList& roots,
                                       const QStringList& relatives,
                                       const QString& pattern)
{
    for (const QString& root : roots)
    {
        for (const QString& relative : relatives)
        {
            QDir directory(QDir::cleanPath(QDir(root).absoluteFilePath(relative)));
            const QFileInfoList matches = directory.entryInfoList(
                {pattern}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            if (!matches.isEmpty())
            {
                return matches.constFirst().absoluteFilePath();
            }
        }
    }
    return {};
}

void prependEnvironmentPath(const char* name, const QString& path)
{
    if (path.isEmpty())
    {
        return;
    }
    const QByteArray pathBytes = QDir::toNativeSeparators(path).toLocal8Bit();
    const QByteArray current = qgetenv(name);
    if (current.isEmpty())
    {
        qputenv(name, pathBytes);
    }
    else if (!current.split(';').contains(pathBytes))
    {
        qputenv(name, pathBytes + ';' + current);
    }
}

void setEnvironmentIfMissing(const char* name, const QString& path)
{
    if (!path.isEmpty() && qgetenv(name).isEmpty())
    {
        qputenv(name, QDir::toNativeSeparators(path).toLocal8Bit());
    }
}

} // namespace

QStringList map3DRuntimeRootCandidates()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList roots{appDir, QDir(appDir).absoluteFilePath(QStringLiteral("../.."))};
    if (qEnvironmentVariableIsSet("VAPORVIEW_MAP3D_DEV_SEARCH_PATHS"))
    {
        roots.push_back(QDir::currentPath());
    }
    roots.removeDuplicates();
    return roots;
}

QString map3DProjectDataDirectory()
{
    return QDir::cleanPath(
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../data")));
}

QString firstExistingMap3DFile(const QStringList& roots, const QStringList& relatives)
{
    for (const QString& root : roots)
    {
        for (const QString& relative : relatives)
        {
            const QString candidate = QDir::cleanPath(QDir(root).absoluteFilePath(relative));
            if (QFileInfo(candidate).isFile())
            {
                return QFileInfo(candidate).absoluteFilePath();
            }
        }
    }
    return {};
}

void initializeMap3DRuntime()
{
    static std::once_flag once;
    std::call_once(once, [] {
        const QStringList roots = map3DRuntimeRootCandidates();
        QString pluginDir = firstExistingDirectory(
            roots,
            {QStringLiteral("osgPlugins-3.6.5"),
             QStringLiteral("plugins/osgPlugins-3.6.5"),
             QStringLiteral(".local_deps/vcpkg_installed/x64-windows/plugins/osgPlugins-3.6.5")});
        if (pluginDir.isEmpty())
        {
            pluginDir = firstExistingDirectoryMatching(
                roots,
                {QStringLiteral("."),
                 QStringLiteral("plugins"),
                 QStringLiteral(".local_deps/vcpkg_installed/x64-windows/plugins")},
                QStringLiteral("osgPlugins-*"));
        }
        const QString gdalData = firstExistingDirectory(
            roots,
            {QStringLiteral("share/gdal"),
             QStringLiteral(".local_deps/vcpkg_installed/x64-windows/share/gdal")});
        const QString projData = firstExistingDirectory(
            roots,
            {QStringLiteral("share/proj"),
             QStringLiteral("share/proj4"),
             QStringLiteral(".local_deps/vcpkg_installed/x64-windows/share/proj"),
             QStringLiteral(".local_deps/vcpkg_installed/x64-windows/share/proj4")});
        prependEnvironmentPath("OSG_LIBRARY_PATH", pluginDir);
        setEnvironmentIfMissing("GDAL_DATA", gdalData);
        setEnvironmentIfMissing("PROJ_LIB", projData);
        setEnvironmentIfMissing("PROJ_DATA", projData);
        if (!pluginDir.isEmpty())
        {
            osgDB::Registry::instance()->getLibraryFilePathList().push_front(pluginDir.toStdString());
        }
        osgEarth::initialize();
    });
}

} // namespace VaporView::Map3D
