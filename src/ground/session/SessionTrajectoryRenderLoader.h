#pragma once

#include "geo/TrajectoryHeatmap.h"

#include <QString>
#include <QtGlobal>

#include <vector>

namespace VaporView::Ground::Session
{

struct SessionTrajectoryRenderLoadResult
{
    bool success = false;
    QString error;
    QString warning;
    QString sourceCsvPath;
    qsizetype totalRows = 0;
    qsizetype rejectedRows = 0;
    std::vector<VaporView::Geo::TrajectoryRenderSample> samples;
    std::vector<qsizetype> sourceCsvRows;
};

class SessionTrajectoryRenderLoader final
{
public:
    static SessionTrajectoryRenderLoadResult loadSessionDirectory(const QString& sessionDir);
};

}  // namespace VaporView::Ground::Session
