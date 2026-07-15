#pragma once

#include <QVector>
#include <QtGlobal>

namespace VaporView::Ground::Session
{

int closestTimestampIndex(const QVector<quint64>& timestampsUs, quint64 timestampUs);
double measuredRateHz(const QVector<quint64>& timestampsUs);

}  // namespace VaporView::Ground::Session
