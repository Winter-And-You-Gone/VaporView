#pragma once

#include <QString>
#include <array>
#include <optional>
#include <vector>
#include <cstdint>

namespace VaporView::Ppk
{
struct AttitudeSample
{
    std::uint64_t utcNs = 0;
    std::uint64_t hostUs = 0;
    std::array<double, 4> bodyToNed{1, 0, 0, 0}; // w,x,y,z
};
struct PpkSample
{
    std::uint64_t utcNs = 0;
    std::uint64_t sessionUs = 0;
    std::array<double, 3> ecef{};
    double latitude = 0, longitude = 0, height = 0;
    int quality = 0, satelliteCount = 0;
    double sdN = 0, sdE = 0, sdU = 0, age = 0, ratio = 0;
};
using PpkTrajectory = std::vector<PpkSample>;

std::array<double, 4> quaternionFromEuler(double rollRad, double pitchRad, double yawRad);
std::array<double, 4> slerp(std::array<double, 4> a, std::array<double, 4> b, double fraction);

class PpkTimeAlignment final
{
  public:
    explicit PpkTimeAlignment(std::vector<AttitudeSample> attitudes);
    std::optional<AttitudeSample> atUtc(std::uint64_t utcNs) const;
    std::optional<std::uint64_t> utcForSession(std::uint64_t hostUs) const;
    bool correctToImu(PpkSample &sample, const std::array<double, 3> &imuToAntennaBodyM,
                      QString *error = nullptr) const;

  private:
    std::vector<AttitudeSample> attitudes_;
};

bool writePpkTrajectory(const QString &path, const PpkTrajectory &trajectory, QString *error = nullptr);
bool readPpkTrajectory(const QString &path, PpkTrajectory &trajectory, QString *error = nullptr);
std::optional<PpkSample> interpolateTrajectory(const PpkTrajectory &trajectory, std::uint64_t sessionUs);
} // namespace VaporView::Ppk
