#pragma once

#include "geo/GeoTypes.h"

namespace VaporView::Geo {

struct EcefPoint {
    double xM = 0.0;
    double yM = 0.0;
    double zM = 0.0;
};

struct LlhPoint {
    double latDeg = 0.0;
    double lonDeg = 0.0;
    double heightM = 0.0;
};

struct EnuPoint {
    double eastM = 0.0;
    double northM = 0.0;
    double upM = 0.0;
};

struct NedPoint {
    double northM = 0.0;
    double eastM = 0.0;
    double downM = 0.0;
};

class LocalTangentPlane {
public:
    LocalTangentPlane() = default;
    explicit LocalTangentPlane(const LlhPoint& originLlh);
    explicit LocalTangentPlane(const NavSample& originSample);

    bool isValid() const;
    LlhPoint originLlh() const;
    EcefPoint originEcef() const;

    EnuPoint ecefToEnu(const EcefPoint& point) const;
    EcefPoint enuToEcef(const EnuPoint& point) const;
    EnuPoint llhToEnu(const LlhPoint& point) const;
    LlhPoint enuToLlh(const EnuPoint& point) const;
    NedPoint llhToNed(const LlhPoint& point) const;
    LlhPoint nedToLlh(const NedPoint& point) const;

private:
    bool valid_ = false;
    LlhPoint originLlh_;
    EcefPoint originEcef_;
    double sinLat_ = 0.0;
    double cosLat_ = 1.0;
    double sinLon_ = 0.0;
    double cosLon_ = 1.0;
};

EcefPoint llhToEcef(const LlhPoint& point);
LlhPoint ecefToLlh(const EcefPoint& point);
EnuPoint nedToEnu(const NedPoint& point);
NedPoint enuToNed(const EnuPoint& point);
EnuPoint navSampleToEnu(const NavSample& sample, const LocalTangentPlane& localFrame);
NedPoint navSampleToNed(const NavSample& sample, const LocalTangentPlane& localFrame);

} // namespace VaporView::Geo
