#pragma once

#include <osgEarth/ElevationLayer>
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

    bool resumeDownloads() { return state_.exchange(Ready) == Missed; }

protected:
    using ElevationLayer::ElevationLayer;

    bool shouldDeferDownload() const
    {
        // Record the miss before the GUI resumes and invalidates the terrain.
        State expected = Deferred;
        return state_.compare_exchange_strong(expected, Missed) || expected == Missed;
    }

private:
    enum State { Ready, Deferred, Missed };
    mutable std::atomic<State> state_{Ready};
};

} // namespace VaporView::Map3D::Detail
