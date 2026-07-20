#pragma once

#include "geo/GeoTypes.h"

#include <QtCore/QString>
#include <vector>

namespace VaporView::Geo {

struct SessionTrackReadResult {
    bool ok = false;
    QString error;
    QString warning;
    QString sourceCsvPath;
    qsizetype totalRows = 0;
    qsizetype rejectedRows = 0;
    std::vector<NavSample> samples;
};

SessionTrackReadResult readSessionTrack(const QString& sessionDir);

} // namespace VaporView::Geo
