#pragma once

#include "data_types.h"

#include <QString>

namespace VaporView::SessionSensorCsv
{

QString header();

QString formatRow(quint64 recordTimestampUs,
                  quint64 epsilonHostTimestampUs,
                  const EpsilonData& epsilon,
                  bool hasEpsilon,
                  const PtbData& ptb,
                  bool hasPtb,
                  const HmpData& hmp,
                  bool hasHmp,
                  const LidarData& lidar,
                  bool hasLidar);

QString escape(const QString& value);

}  // namespace VaporView::SessionSensorCsv
