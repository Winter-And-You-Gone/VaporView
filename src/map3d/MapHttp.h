#pragma once

#include <QNetworkRequest>

namespace osgEarth { class ProgressCallback; }

namespace VaporView::Map3D::Detail {

struct MapHttpResult {
    QByteArray body;
    int status = 0;
    bool fromCache = false;
};

MapHttpResult mapHttpGet(QNetworkRequest request, osgEarth::ProgressCallback* progress);

} // namespace VaporView::Map3D::Detail
