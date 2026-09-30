#pragma once
#include "ppk/PpkNavigation.h"
#include <QJsonObject>
#include <QStringList>
#include <atomic>
#include <functional>

namespace VaporView::Ppk
{
struct PpkConfig
{
    QString roverObs;
    QString baseObs;
    QStringList navigationFiles;
    int receiver = 1;
    int frequencies = 3;
    double elevationMaskDeg = 15;
    int constellations = 1 | 4 | 8 | 16 | 32;
    std::array<double, 3> imuToAntennaBodyM{};
};
struct PpkProcessResult
{
    bool success = false;
    bool cancelled = false;
    QString error;
    PpkTrajectory trajectory;
    QJsonObject quality;
};
struct PpkStatus
{
    bool roverAvailable = false;
    bool baseAvailable = false;
    bool navigationAvailable = false;
    bool completed = false;
    QString state;
    QString error;
    QJsonObject quality;
    bool ready() const
    {
        return roverAvailable && baseAvailable && navigationAvailable;
    }
};
class PpkProcessor final
{
  public:
    using Progress = std::function<void(int percent, const QString &message)>;
    // GUI callers execute this synchronous service on a worker.
    static PpkProcessResult process(const QString &sessionDirectory, const PpkConfig &config,
                                    const std::atomic_bool *cancel = nullptr, const Progress &progress = {});
    static PpkConfig loadConfig(const QString &sessionDirectory);
    static bool saveConfig(const QString &sessionDirectory, const PpkConfig &config, QString *error = nullptr);
    static PpkStatus status(const QString &sessionDirectory);
    static bool importBase(const QString &sessionDirectory, const QString &file, QString *error = nullptr);
    static bool importNavigation(const QString &sessionDirectory, const QString &file, QString *error = nullptr);
    static bool clearResult(const QString &sessionDirectory, QString *error = nullptr);
};
} // namespace VaporView::Ppk
