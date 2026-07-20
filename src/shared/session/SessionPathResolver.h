#pragma once

#include "shared/session/SessionPackageLayout.h"

#include <QJsonObject>
#include <QString>

namespace VaporView::Session
{

struct SessionPathContext
{
    QString sessionDirectory;
    bool manifestPresent = false;
    QJsonObject manifest;
    QString manifestError;
};

struct SessionPathResolution
{
    QString relativePath;
    QString absolutePath;
    bool exists = false;
    bool manifestDeclared = false;
    bool usedLegacyPath = false;
    QString warning;
};

SessionPathContext loadSessionPathContext(const QString& sessionDirectory);
SessionPathResolution resolveSessionPath(const SessionPathContext& context,
                                         const SessionPathAliases& aliases);
SessionPathResolution resolveSessionPath(const SessionPathContext& context,
                                         SessionFileKind kind);

}  // namespace VaporView::Session
