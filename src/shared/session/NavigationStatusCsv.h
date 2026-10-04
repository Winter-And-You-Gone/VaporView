#pragma once

#include "RtcmStatusModel.h"

#include <QString>
#include <QVariantMap>
#include <limits>

namespace VaporView::Session
{
struct NavigationStatusRecord
{
    quint64 timestampUs = 0;
    quint64 epsilonTimeUs = 0;
    quint64 skyReportTimeUs = 0;
    quint64 skyBootId = 0;
    quint64 rtcmStreamId = 0;
    QString sourceMode;
    RtcmStatusSnapshot rtcm;
    bool rtkServiceKnown = false;
    bool rtkServiceRunning = false;
    bool navigationAvailable = false;
    bool positionAvailable = false;
    bool filterStatusAvailable = false;
    int gnssFixCode = -1;
    int satellites = 0;
    quint16 filterStatusBits = 0;
    quint16 updateStatusBits = 0;
    double horizontalAccuracyM = std::numeric_limits<double>::quiet_NaN();
    double verticalAccuracyM = std::numeric_limits<double>::quiet_NaN();
    double latitudeDeg = std::numeric_limits<double>::quiet_NaN();
    double longitudeDeg = std::numeric_limits<double>::quiet_NaN();
    double heightM = std::numeric_limits<double>::quiet_NaN();
};

QString navigationStatusCsvHeader();
QString navigationStatusCsvRow(const NavigationStatusRecord& record);
QString rtcmHealthCode(RtcmHealth health);

// Emits only discrete status changes. Metric fluctuations remain in the CSV.
class NavigationStatusChangeTracker
{
public:
    QVariantMap observe(const NavigationStatusRecord& record);
    void reset() { *this = {}; }
private:
    bool initialized_ = false;
    NavigationStatusRecord previous_;
};

// One periodic row per second, plus rows for discrete state changes.
class NavigationStatusSampleGate
{
public:
    bool accept(const NavigationStatusRecord& record);
    void reset() { *this = {}; }
private:
    quint64 last_time_us_ = 0;
    NavigationStatusChangeTracker changes_;
};
}
