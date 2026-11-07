#pragma once

#include <osgEarth/Map>

namespace VaporView::Map3D::Detail {

inline void removeLayerByName(osgEarth::Map* map, const char* layerName)
{
    if (!map || !layerName)
    {
        return;
    }

    while (true)
    {
        osg::ref_ptr<osgEarth::Layer> existing = map->getLayerByName(layerName);
        if (!existing)
        {
            return;
        }
        // Map::removeLayer otherwise closes the driver and clears its extents
        // while REX jobs can still be reading a reference-counted layer snapshot.
        // Detach immediately; let the last reader release the driver and caches.
        const bool openAutomatically = existing->getOpenAutomatically();
        existing->setOpenAutomatically(false);
        map->removeLayer(existing.get());
        existing->setOpenAutomatically(openAutomatically);
    }
}

} // namespace VaporView::Map3D::Detail
