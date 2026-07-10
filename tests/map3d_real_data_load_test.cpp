#include "map3d/OsgEarthViewWidget.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const QString& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message.toStdString() << '\n';
        std::exit(1);
    }
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QDir sourceRoot(QStringLiteral(VAPORVIEW_SOURCE_DIR));
    const QString earthPath =
        sourceRoot.filePath(QStringLiteral("resources/maps/vaporview_real3d_local.earth"));
    const QString tilesetPath =
        sourceRoot.filePath(QStringLiteral("resources/maps/tiles3d/local/tileset.json"));

    if (!QFileInfo::exists(earthPath) || !QFileInfo::exists(tilesetPath))
    {
        std::cout << "SKIP: Hangzhou Xihu real-3D local data is not installed\n";
        return 77;
    }

    VaporView::Map3D::OsgEarthViewWidget view;
    require(view.loadEarthFile(earthPath),
            QStringLiteral("load Hangzhou Xihu real-3D earth file"));

    const VaporView::Map3D::EarthLoadDiagnostics earthDiagnostics =
        view.earthLoadDiagnostics();
    require(earthDiagnostics.loaded,
            QStringLiteral("earth diagnostics report successful load"));
    require(earthDiagnostics.foundMapNode,
            QStringLiteral("earth file contains an osgEarth MapNode"));

    require(view.loadLocal3DTilesPreview(tilesetPath),
            QStringLiteral("load Hangzhou Xihu building tileset"));

    const VaporView::Map3D::Local3DTilesLoadDiagnostics tileDiagnostics =
        view.local3DTilesLoadDiagnostics();
    require(tileDiagnostics.loaded,
            QStringLiteral("building diagnostics report successful load"));
    require(tileDiagnostics.payloadCount == 55,
            QStringLiteral("tileset contains 55 building payloads, got %1")
                .arg(tileDiagnostics.payloadCount));
    require(tileDiagnostics.loadedPayloadCount == 55,
            QStringLiteral("all 55 building payloads load successfully, got %1")
                .arg(tileDiagnostics.loadedPayloadCount));
    require(tileDiagnostics.warnings.isEmpty(),
            QStringLiteral("building tiles load without warnings: %1")
                .arg(tileDiagnostics.warnings.join(QStringLiteral(" | "))));

    view.shutdown();
    std::cout << "map3d_real_data_load_test passed: 55/55 building tiles loaded\n";
    return 0;
}
