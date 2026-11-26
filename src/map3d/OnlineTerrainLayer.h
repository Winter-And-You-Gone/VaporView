#pragma once

#include <osgEarth/ElevationLayer>
#include <osgEarth/GeoData>
#include <QMutex>
#include <atomic>

namespace VaporView::Map3D::Detail {

class OnlineTerrainLayer : public osgEarth::ElevationLayer
{
public:
    void deferDownloads()
    {
        State expected = Ready;
        state_.compare_exchange_strong(expected, Deferred);
    }

    bool resumeDownloads(osgEarth::GeoExtent* outMissedExtent = nullptr)
    {
        const bool missed = state_.exchange(Ready) == Missed;
        QMutexLocker lock(&missedExtentMutex_);
        if (outMissedExtent)
        {
            *outMissedExtent = missed ? missedExtent_ : osgEarth::GeoExtent::INVALID;
        }
        missedExtent_ = osgEarth::GeoExtent::INVALID;
        return missed;
    }

protected:
    using ElevationLayer::ElevationLayer;

    bool shouldDeferDownload(const osgEarth::GeoExtent& extent = osgEarth::GeoExtent::INVALID) const
    {
        // Record the miss before the GUI resumes and invalidates the terrain.
        State expected = Deferred;
        const bool deferred = state_.compare_exchange_strong(expected, Missed) || expected == Missed;
        if (deferred && extent.isValid())
        {
            QMutexLocker lock(&missedExtentMutex_);
            if (!missedExtent_.isValid())
            {
                missedExtent_ = extent;
            }
            else
            {
                missedExtent_.expandToInclude(extent);
            }
        }
        return deferred;
    }

private:
    enum State { Ready, Deferred, Missed };
    mutable std::atomic<State> state_{Ready};
    mutable QMutex missedExtentMutex_;
    mutable osgEarth::GeoExtent missedExtent_ = osgEarth::GeoExtent::INVALID;
};

} // namespace VaporView::Map3D::Detail
