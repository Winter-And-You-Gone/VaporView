#include "CesiumTerrain.h"
#include "Map3DRuntime.h"
#include "MapHttp.h"

#include <osgEarth/ElevationLayer>
#include <osgEarth/Map>
#include <osgEarth/Progress>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRegularExpression>
#include <QSettings>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace VaporView::Map3D::Detail {
namespace {

constexpr auto layerName = "Cesium World Terrain";
constexpr int gridSize = 65;

// Quantized-mesh 1.0 uses delta-coded vertex coordinates and high-water-mark
// triangle indices. Sample the triangles onto osgEarth's south-to-north grid
// so its elevation stack can fill individual holes along a local DEM border.
osg::ref_ptr<osg::HeightField> sampleMesh(const QByteArray& bytes)
{
    constexpr int headerSize = 88;
    if (bytes.size() < headerSize + 4) return {};
    const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
    const auto heightAt = [data](int offset) {
        const quint32 bits = qFromLittleEndian<quint32>(data + offset);
        float height;
        std::memcpy(&height, &bits, sizeof(height));
        return height;
    };
    const double low = heightAt(24), high = heightAt(28);
    const quint32 count = qFromLittleEndian<quint32>(data + headerSize);
    if (!std::isfinite(low) || !std::isfinite(high) || high < low || count < 3 || count > 1000000) return {};
    qsizetype pos = headerSize + 4;
    if (pos + 6LL * count > bytes.size()) return {};
    std::vector<std::array<double, 3>> vertices(count);
    for (int axis = 0; axis < 3; ++axis)
    {
        int value = 0;
        for (auto& vertex : vertices)
        {
            const int delta = qFromLittleEndian<quint16>(data + pos);
            pos += 2;
            value += (delta >> 1) ^ -(delta & 1);
            if (value < 0 || value > 32767) return {};
            vertex[axis] = value / 32767.0 * (axis == 2 ? high - low : gridSize - 1);
        }
    }

    const int width = count > 65536 ? 4 : 2;
    pos = (pos + width - 1) / width * width;
    if (pos + 4 > bytes.size()) return {};
    const quint32 triangles = qFromLittleEndian<quint32>(data + pos);
    pos += 4;
    if (!triangles || triangles > 2ULL * count || pos + 3LL * triangles * width > bytes.size()) return {};
    osg::ref_ptr<osg::HeightField> field = new osg::HeightField;
    field->allocate(gridSize, gridSize);
    std::fill(field->getHeightList().begin(), field->getHeightList().end(), NO_DATA_VALUE);
    quint32 highest = 0;
    for (quint32 i = 0; i < triangles; ++i)
    {
        std::array<quint32, 3> triangle;
        for (auto& index : triangle)
        {
            const quint32 code = width == 2 ? qFromLittleEndian<quint16>(data + pos)
                                             : qFromLittleEndian<quint32>(data + pos);
            pos += width;
            if (code > highest || highest - code >= count) return {};
            index = highest - code;
            if (!code) ++highest;
        }
        const auto& a = vertices[triangle[0]];
        const auto& b = vertices[triangle[1]];
        const auto& c = vertices[triangle[2]];
        const double bx = b[0] - a[0], by = b[1] - a[1];
        const double cx = c[0] - a[0], cy = c[1] - a[1];
        const double det = bx * cy - by * cx;
        if (det == 0.0) continue;
        const int xmax = int(std::max({a[0], b[0], c[0]}));
        const int ymax = int(std::max({a[1], b[1], c[1]}));
        for (int y = int(std::ceil(std::min({a[1], b[1], c[1]}))); y <= ymax; ++y)
        {
            for (int x = int(std::ceil(std::min({a[0], b[0], c[0]}))); x <= xmax; ++x)
            {
                const double u = ((x - a[0]) * cy - (y - a[1]) * cx) / det;
                const double v = (bx * (y - a[1]) - by * (x - a[0])) / det;
                if (u >= -1e-9 && v >= -1e-9 && u + v <= 1.0 + 1e-9)
                    field->setHeight(x, y, float(low + a[2] + u * (b[2] - a[2]) + v * (c[2] - a[2])));
            }
        }
    }
    return field;
}

struct TerrainEndpoint {
    QUrl base;
    QString tileTemplate;
    QByteArray bearer;
    QJsonArray availability;
    int maxZoom = 0;
};

QNetworkRequest ionRequest(QUrl url, const QByteArray& token)
{
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + token);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    return request;
}

class CesiumTerrain final : public osgEarth::ElevationLayer
{
public:
    explicit CesiumTerrain(QByteArray token)
        : ElevationLayer(&options_, &originalOptions_), token_(std::move(token))
    {
        ElevationLayer::init();
        setName(layerName);
        setProfile(osgEarth::Profile::create(osgEarth::Profile::GLOBAL_GEODETIC));
        setTileSize(gridSize);
        setMinLevel(kTerrainMinLevel);
        setMaxDataLevel(19);
        setCachePolicy(osgEarth::CachePolicy::NO_CACHE);
    }

    QString credit(bool collapsible) const
    {
        QMutexLocker lock(&creditMutex_);
        return credits_[collapsible];
    }

private:
    osgEarth::Status openImplementation() override
    {
        auto status = ElevationLayer::openImplementation();
        if (status.isOK())
            addDataExtent(osgEarth::DataExtent(getProfile()->getExtent(), getMinLevel(), getMaxDataLevel()));
        return status;
    }

    TerrainEndpoint endpoint(osgEarth::ProgressCallback* progress, const QByteArray& rejected = {}) const
    {
        QMutexLocker lock(&endpointMutex_);
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (now < refreshAfter_ && (rejected.isEmpty() || rejected != endpoint_.bearer)) return endpoint_;
        // One metadata request per burst, including after a network failure.
        endpoint_ = {};
        refreshAfter_ = now + 15;
        auto request = ionRequest(QUrl("https://api.cesium.com/v1/assets/1/endpoint"), token_);
        request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        const auto asset = QJsonDocument::fromJson(mapHttpGet(request, progress).body).object();
        TerrainEndpoint next;
        next.base = QUrl(asset.value("url").toString());
        next.bearer = asset.value("accessToken").toString().toUtf8();
        if (asset.value("type") != "TERRAIN" || next.base.scheme() != "https" || next.bearer.isEmpty()) return {};
        const auto manifest = QJsonDocument::fromJson(mapHttpGet(
            ionRequest(next.base.resolved(QUrl("layer.json")), next.bearer), progress).body).object();
        if (manifest.value("format") != "quantized-mesh-1.0" ||
            manifest.value("scheme").toString("tms") != "tms" ||
            manifest.value("projection").toString("EPSG:4326") != "EPSG:4326") return {};
        next.tileTemplate = manifest.value("tiles").toArray().first().toString();
        if (next.tileTemplate.isEmpty()) return {};
        next.tileTemplate.replace("{version}", manifest.value("version").toString());
        next.availability = manifest.value("available").toArray();
        next.maxZoom = manifest.value("maxzoom").toInt(19);
        QStringList labels[2];
        for (const auto item : asset.value("attributions").toArray())
        {
            const auto entry = item.toObject();
            auto html = entry.value("html").toString();
            // QLabel has no remote image loader. Keep the linked logo's text alternative.
            html.replace(QRegularExpression(QStringLiteral("<img[^>]*alt=\"([^\"]+)\"[^>]*>")), QStringLiteral("\\1"));
            labels[entry.value("collapsible").toBool()].append(html);
        }
        {
            QMutexLocker creditLock(&creditMutex_);
            for (int i = 0; i < 2; ++i) credits_[i] = labels[i].join(QStringLiteral(" · "));
        }
        refreshAfter_ = now + 3600;
        endpoint_ = next;
        return next;
    }

    osgEarth::GeoHeightField createHeightFieldImplementation(
        const osgEarth::TileKey& key, osgEarth::ProgressCallback* progress) const override
    {
        const int z = int(key.getLOD()), x = int(key.getTileX());
        if (z > 19) return {};
        const int y = (1 << z) - 1 - int(key.getTileY());
        QByteArray rejected;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            if (progress && progress->isCanceled()) return {};
            const auto source = endpoint(progress, rejected);
            if (source.bearer.isEmpty() || z > source.maxZoom) return {};
            if (z < source.availability.size())
            {
                const auto ranges = source.availability.at(z).toArray();
                if (std::none_of(ranges.begin(), ranges.end(), [=](const QJsonValue& value) {
                    const auto r = value.toObject();
                    return x >= r.value("startX").toInt() && x <= r.value("endX").toInt()
                        && y >= r.value("startY").toInt() && y <= r.value("endY").toInt();
                })) return {};
            }
            QString path = source.tileTemplate;
            path.replace("{z}", QString::number(z)).replace("{x}", QString::number(x)).replace("{y}", QString::number(y));
            const QUrl url = source.base.resolved(QUrl(path));
            if (url.scheme() != source.base.scheme() || url.authority() != source.base.authority()) return {};
            auto request = ionRequest(url, source.bearer);
            request.setRawHeader("Accept", "application/vnd.quantized-mesh");
            const auto tile = mapHttpGet(request, progress);
            if (tile.status != 401)
            {
                auto field = sampleMesh(tile.body);
                return field ? osgEarth::GeoHeightField(field.get(), key.getExtent()) : osgEarth::GeoHeightField::INVALID;
            }
            rejected = source.bearer;
        }
        return {};
    }

    Options options_;
    const Options originalOptions_;
    QByteArray token_;
    mutable QMutex endpointMutex_, creditMutex_;
    mutable qint64 refreshAfter_ = 0;
    mutable TerrainEndpoint endpoint_;
    mutable QString credits_[2];
};
} // namespace

QByteArray configuredCesiumToken()
{
    QByteArray token = qgetenv("VAPORVIEW_CESIUM_ION_TOKEN").trimmed();
    if (token.isEmpty())
        token = QSettings(QSettings::defaultFormat(), QSettings::UserScope, "VaporView", "Map3D")
            .value("cesium/ion_token").toByteArray().trimmed();
    return token;
}

bool addCesiumTerrainFallback(osgEarth::Map* map)
{
    const QByteArray token = configuredCesiumToken();
    if (!map || token.isEmpty()) return false;
    // osgEarth samples elevation layers in reverse order, stopping at the first valid height.
    map->insertLayer(new CesiumTerrain(token), 0);
    return true;
}

QString cesiumTerrainCredit(const osgEarth::Map* map, bool collapsible)
{
    const auto* layer = map ? dynamic_cast<const CesiumTerrain*>(map->getLayerByName(layerName)) : nullptr;
    return layer ? layer->credit(collapsible) : QString();
}
} // namespace VaporView::Map3D::Detail
