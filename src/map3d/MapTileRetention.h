#pragma once

#include <QHash>
#include <QString>
#include <string>

namespace VaporView::Map3D::Detail {
inline constexpr auto terrainCacheBin = "vaporview-copernicus-glo30-v1";
inline constexpr auto osmCacheBin = "vaporview-osm-shortbread-v1";
inline constexpr qint64 tileBudgetBytes = 2LL * 1024 * 1024 * 1024;

void startTileRetention(const QString& filesystemRoot);
void recordTileVisit(const char* bin, const std::string& key);

// Paths are relative to the filesystem cache. Days are UTC days since the Unix epoch.
bool maintainTileCache(const QString& root, const QHash<QString, qint64>& visits,
                       qint64 today, bool sweep, qint64 budget = tileBudgetBytes);
}
