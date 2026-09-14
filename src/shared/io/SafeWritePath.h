#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>

namespace VaporView
{
// Validate immediately before automatic writes. Do not follow links supplied
// by an imported package, including links in intermediate directories.
inline bool isContainedWritePath(const QString& root, const QString& target)
{
    if (root.isEmpty() || target.isEmpty() || target.contains(QChar::Null)) return false;
    const QDir directory(QFileInfo(root).absoluteFilePath());
    if (!directory.exists()) return false;
    const QString relative = QDir::fromNativeSeparators(directory.relativeFilePath(
        QDir::cleanPath(QFileInfo(target).absoluteFilePath())));
    if (relative == QStringLiteral(".") || QDir::isAbsolutePath(relative) ||
        relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../"))) return false;
    QString current = directory.absolutePath();
    for (const QString& part : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts))
    {
        if (part.contains(QLatin1Char(':'))) return false;
        current = QDir(current).filePath(part);
        const QFileInfo info(current);
        if (info.isSymLink() || info.isJunction()) return false;
    }
    return true;
}
}
