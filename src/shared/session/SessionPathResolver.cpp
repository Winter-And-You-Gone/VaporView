#include "shared/session/SessionPathResolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

namespace VaporView::Session
{
namespace
{

QString firstDeclaredPath(const QJsonObject& manifest,
                          const SessionPathAliases& aliases)
{
    const QJsonObject rawFiles = manifest.value(QStringLiteral("raw_files")).toObject();
    for (const QString& key : aliases.manifestRawFileKeys)
    {
        const QString path = rawFiles.value(key).toObject().value(QStringLiteral("path")).toString().trimmed();
        if (!path.isEmpty())
        {
            return path;
        }
    }

    const QJsonObject paths = manifest.value(QStringLiteral("paths")).toObject();
    for (const QString& key : aliases.manifestPathKeys)
    {
        const QString path = paths.value(key).toString().trimmed();
        if (!path.isEmpty())
        {
            return path;
        }
    }
    return {};
}

bool isLegacyPath(const QString& path, const QStringList& legacyPaths)
{
    const QString normalized = QDir::fromNativeSeparators(path);
    for (const QString& legacyPath : legacyPaths)
    {
        if (normalized.compare(QDir::fromNativeSeparators(legacyPath), Qt::CaseInsensitive) == 0)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

SessionPathContext loadSessionPathContext(const QString& sessionDirectory)
{
    SessionPathContext context;
    context.sessionDirectory = QDir::fromNativeSeparators(QDir(sessionDirectory).absolutePath());

    const QString manifestPath = sessionPackageFilePath(
        context.sessionDirectory,
        standardSessionPackageLayout().manifestPath);
    QFile manifestFile(manifestPath);
    if (!manifestFile.exists())
    {
        return context;
    }

    context.manifestPresent = true;
    if (!manifestFile.open(QIODevice::ReadOnly))
    {
        context.manifestError = QStringLiteral("Failed to open %1: %2")
                                    .arg(manifestPath, manifestFile.errorString());
        return context;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    if (!document.isObject())
    {
        context.manifestError = QStringLiteral("Invalid session metadata %1: %2")
                                    .arg(manifestPath, parseError.errorString());
        return context;
    }
    context.manifest = document.object();
    return context;
}

SessionPathResolution resolveSessionPath(const SessionPathContext& context,
                                         const SessionPathAliases& aliases)
{
    SessionPathResolution result;
    const QString declaredPath = firstDeclaredPath(context.manifest, aliases);
    if (!declaredPath.isEmpty())
    {
        result.relativePath = QDir::fromNativeSeparators(declaredPath);
        result.absolutePath = sessionPackageFilePath(context.sessionDirectory, result.relativePath);
        result.exists = QFileInfo::exists(result.absolutePath);
        result.manifestDeclared = true;
        result.usedLegacyPath = isLegacyPath(result.relativePath, aliases.legacyPaths);
        return result;
    }

    const QString preferredAbsolutePath = sessionPackageFilePath(
        context.sessionDirectory,
        aliases.preferredPath);
    const bool preferredExists = QFileInfo::exists(preferredAbsolutePath);
    QStringList existingLegacyPaths;
    for (const QString& legacyPath : aliases.legacyPaths)
    {
        if (QFileInfo::exists(sessionPackageFilePath(context.sessionDirectory, legacyPath)))
        {
            existingLegacyPaths.push_back(legacyPath);
        }
    }

    if (preferredExists)
    {
        result.relativePath = aliases.preferredPath;
        result.absolutePath = preferredAbsolutePath;
        result.exists = true;
        if (!existingLegacyPaths.isEmpty())
        {
            result.warning = QStringLiteral(
                "Both preferred session file %1 and legacy file %2 exist; using the preferred file only.")
                                 .arg(aliases.preferredPath, existingLegacyPaths.constFirst());
        }
        return result;
    }

    if (!existingLegacyPaths.isEmpty())
    {
        result.relativePath = existingLegacyPaths.constFirst();
        result.absolutePath = sessionPackageFilePath(context.sessionDirectory, result.relativePath);
        result.exists = true;
        result.usedLegacyPath = true;
        if (existingLegacyPaths.size() > 1)
        {
            result.warning = QStringLiteral(
                "Multiple legacy session files exist for %1; using %2 only.")
                                 .arg(aliases.preferredPath, result.relativePath);
        }
        return result;
    }

    result.relativePath = aliases.preferredPath;
    result.absolutePath = preferredAbsolutePath;
    return result;
}

SessionPathResolution resolveSessionPath(const SessionPathContext& context,
                                         SessionFileKind kind)
{
    return resolveSessionPath(context, sessionPathAliases(kind));
}

}  // namespace VaporView::Session
