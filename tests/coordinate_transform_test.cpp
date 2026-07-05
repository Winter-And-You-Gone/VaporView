#include "geo/CoordinateTransform.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void requireNear(double actual, double expected, double tolerance, const char* message)
{
    if (std::fabs(actual - expected) > tolerance)
    {
        std::cerr << "FAIL: " << message << " actual=" << actual
                  << " expected=" << expected << " tolerance=" << tolerance << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    const VaporView::Geo::LlhPoint zero{0.0, 0.0, 0.0};
    const VaporView::Geo::EcefPoint zeroEcef = VaporView::Geo::llhToEcef(zero);
    requireNear(zeroEcef.xM, 6378137.0, 0.001, "equator x matches WGS84 semi-major axis");
    requireNear(zeroEcef.yM, 0.0, 0.001, "equator y is zero");
    requireNear(zeroEcef.zM, 0.0, 0.001, "equator z is zero");

    const VaporView::Geo::LlhPoint shanghai{31.2304, 121.4737, 18.0};
    const VaporView::Geo::EcefPoint ecef = VaporView::Geo::llhToEcef(shanghai);
    const VaporView::Geo::LlhPoint roundTrip = VaporView::Geo::ecefToLlh(ecef);
    requireNear(roundTrip.latDeg, shanghai.latDeg, 0.0000001, "LLH round-trip latitude");
    requireNear(roundTrip.lonDeg, shanghai.lonDeg, 0.0000001, "LLH round-trip longitude");
    requireNear(roundTrip.heightM, shanghai.heightM, 0.001, "LLH round-trip height");

    VaporView::Geo::LocalTangentPlane local(shanghai);
    require(local.isValid(), "valid local tangent plane");
    const VaporView::Geo::EnuPoint originEnu = local.llhToEnu(shanghai);
    requireNear(originEnu.eastM, 0.0, 0.001, "origin east is zero");
    requireNear(originEnu.northM, 0.0, 0.001, "origin north is zero");
    requireNear(originEnu.upM, 0.0, 0.001, "origin up is zero");

    const VaporView::Geo::EnuPoint testEnu{12.5, -34.0, 5.75};
    const VaporView::Geo::LlhPoint shifted = local.enuToLlh(testEnu);
    const VaporView::Geo::EnuPoint shiftedBack = local.llhToEnu(shifted);
    requireNear(shiftedBack.eastM, testEnu.eastM, 0.001, "ENU round-trip east");
    requireNear(shiftedBack.northM, testEnu.northM, 0.001, "ENU round-trip north");
    requireNear(shiftedBack.upM, testEnu.upM, 0.001, "ENU round-trip up");

    const VaporView::Geo::NedPoint ned = VaporView::Geo::enuToNed(testEnu);
    requireNear(ned.northM, testEnu.northM, 0.0, "ENU to NED north");
    requireNear(ned.eastM, testEnu.eastM, 0.0, "ENU to NED east");
    requireNear(ned.downM, -testEnu.upM, 0.0, "ENU to NED down");

    VaporView::Geo::NavSample nedSample;
    nedSample.nedNM = 1.0;
    nedSample.nedEM = 2.0;
    nedSample.nedDM = -3.0;
    const VaporView::Geo::EnuPoint sampleEnu = VaporView::Geo::navSampleToEnu(nedSample, local);
    requireNear(sampleEnu.eastM, 2.0, 0.0, "NavSample NED east maps to ENU east");
    requireNear(sampleEnu.northM, 1.0, 0.0, "NavSample NED north maps to ENU north");
    requireNear(sampleEnu.upM, 3.0, 0.0, "NavSample NED down maps to ENU up");

    return 0;
}
