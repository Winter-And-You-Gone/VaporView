#pragma once

#include <QString>
#include <QStringList>

namespace VaporView::Map3D {

QStringList map3DRuntimeRootCandidates();
QString map3DProjectDataDirectory();
QString firstExistingMap3DFile(const QStringList& roots, const QStringList& relatives);
void initializeMap3DRuntime();

} // namespace VaporView::Map3D
