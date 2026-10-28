#pragma once

#include <array>

namespace VaporView::Ground::Devices
{
// Synthetic WGS84 navigation used by the FPGA protocol simulator.
struct FpgaSimulatorNavigation
{
    double latitudeRadians;
    double longitudeRadians;
    double heightM;
    double northM;
    double eastM;
    double downM;
    std::array<double, 3> velocityNed;
    std::array<double, 3> accelerationNed;
    double headingRadians;
    double headingRateRadiansPerSecond;
};

FpgaSimulatorNavigation fpgaSimulatorNavigationAt(double seconds);
double fpgaSimulatorNavigationLoopSeconds();
}
