#include "map3d/MapResourceManager.h"

#include <QCoreApplication>
#include <QDebug>

namespace {

int fail(const QString& message)
{
    qCritical().noquote() << message;
    return 1;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QUrl manifestUrl(QStringLiteral("https://example.test/maps/manifest.json"));
    const QByteArray valid = R"json({
        "schema": "vaporview.map-resources",
        "schemaVersion": 1,
        "resources": [{
            "id": "natural-earth",
            "displayName": "Natural Earth",
            "version": "1.0.0",
            "requiredFiles": ["resources/maps/vaporview_default.earth"],
            "files": [{
                "relativePath": "resources/maps/vaporview_default.earth",
                "url": "natural-earth/v1/vaporview_default.earth",
                "sizeBytes": 398,
                "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
            }]
        }]
    })json";

    QVector<VaporView::Map3D::MapResourcePackage> packages;
    QString error;
    if (!VaporView::Map3D::MapResourceManifest::parse(valid, manifestUrl, &packages, &error))
    {
        return fail(QStringLiteral("valid manifest rejected: %1").arg(error));
    }
    if (packages.size() != 1 || packages.first().files.size() != 1 ||
        packages.first().files.first().url.toString() != QStringLiteral("https://example.test/maps/natural-earth/v1/vaporview_default.earth"))
    {
        return fail(QStringLiteral("manifest URL resolution or package shape is incorrect"));
    }

    const QByteArray unsafe = R"json({
        "resources": [{
            "id": "unsafe",
            "displayName": "Unsafe",
            "version": "1.0.0",
            "files": [{
                "relativePath": "../outside/file.tif",
                "url": "https://example.test/file.tif"
            }]
        }]
    })json";
    packages.clear();
    if (VaporView::Map3D::MapResourceManifest::parse(unsafe, manifestUrl, &packages, &error))
    {
        return fail(QStringLiteral("unsafe relative path was accepted"));
    }

    const QByteArray insecureFileUrl = R"json({
        "resources": [{
            "id": "insecure-file",
            "displayName": "Insecure File",
            "version": "1.0.0",
            "files": [{
                "relativePath": "resources/maps/file.tif",
                "url": "http://example.test/file.tif"
            }]
        }]
    })json";
    packages.clear();
    if (VaporView::Map3D::MapResourceManifest::parse(insecureFileUrl, manifestUrl, &packages, &error))
    {
        return fail(QStringLiteral("remote HTTP file URL was accepted"));
    }

    const QByteArray insecurePackageUrl = R"json({
        "resources": [{
            "id": "insecure-package",
            "displayName": "Insecure Package",
            "version": "1.0.0",
            "downloadUrl": "http://example.test/package.7z",
            "installPath": "resources/maps/package.7z"
        }]
    })json";
    packages.clear();
    if (VaporView::Map3D::MapResourceManifest::parse(insecurePackageUrl, manifestUrl, &packages, &error))
    {
        return fail(QStringLiteral("remote HTTP package URL was accepted"));
    }

    return 0;
}
