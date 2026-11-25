#pragma once

#include "MapHttp.h"
#include "GroundDetailEllipse.h"
#include "map3d/Map3DLoadTracker.h"
#include <osgEarth/AltitudeSymbol>
#include <osgEarth/ExtrusionSymbol>
#include <osgEarth/FilterContext>
#include <osgEarth/GeometryCompiler>
#include <osgEarth/MVT>
#include <osgEarth/PagedNode>
#include <osgEarth/PolygonSymbol>
#include <osgEarth/Progress>
#include <osgEarth/SimplePager>
#include <osgEarth/TiledModelLayer>
#include <QDeadlineTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QPainterPath>
#include <cmath>

namespace VaporView::Map3D::Detail {

class OpenFreeMapBuildings final : public osgEarth::TiledModelLayer
{
public:
    static constexpr auto layerName = "OpenFreeMap buildings";
    static constexpr unsigned sourceLevel = 14;

    explicit OpenFreeMapBuildings(const QPainterPath& localCoverage = {},
                                  std::shared_ptr<const GroundDetailEllipse> ellipse = {})
        : TiledModelLayer(&options_, &originalOptions_), localCoverage_(localCoverage),
          profile_(osgEarth::Profile::create(osgEarth::Profile::SPHERICAL_MERCATOR)), ellipse_(std::move(ellipse))
    {
        using namespace osgEarth;
        TiledModelLayer::init();
        setName(layerName);
        setAttribution("OpenFreeMap · © OpenMapTiles · © OpenStreetMap contributors");
        setMinLevel(sourceLevel);
        setMaxLevel(sourceLevel);
        setRangeFactor(2.0f);
        options().nvgl() = false;
        // Qt computes this bound lazily. Populate it before the paging threads share the mask.
        (void)localCoverage_.controlPointRect();

        auto* extrusion = style_.getOrCreate<ExtrusionSymbol>();
        extrusion->height() = Expression<Distance>("[wall_height]");
        extrusion->wallGradientPercentage() = 0.20f;
        style_.getOrCreate<PolygonSymbol>()->fill()->color() = Color("#dad2bf");
        auto* altitude = style_.getOrCreate<AltitudeSymbol>();
        altitude->clamping() = AltitudeSymbol::CLAMP_TO_TERRAIN;
        altitude->binding() = AltitudeSymbol::BINDING_CENTROID;
        altitude->verticalOffset() = Expression<Distance>("[render_min_height]");
    }

    const osgEarth::Profile* getProfile() const override { return profile_.get(); }
    bool hasLocalCoverage() const { return !localCoverage_.isEmpty(); }

protected:
    void create() override
    {
        TiledModelLayer::create();
        // A PagedNode retains a raw pointer to its pager even in the pending merge queue.
        // Keep that queue in the same detachable subtree as its pager.
        class BuildingPagingManager final : public osgEarth::Util::PagingManager
        {
        public:
            BuildingPagingManager() : PagingManager("oe.nodepager") {}
            void traverse(osg::NodeVisitor& visitor) override
            {
                osg::ref_ptr<PagingManager> enclosing;
                osgEarth::ObjectStorage::get(&visitor, enclosing);
                PagingManager::traverse(visitor);
                if (enclosing) osgEarth::ObjectStorage::set(&visitor, enclosing.get());
                else osgEarth::ObjectStorage::remove(&visitor, static_cast<PagingManager*>(this));
            }
        };
        if (_root->getNumChildren() == 1 && !dynamic_cast<BuildingPagingManager*>(_root->getChild(0)))
        {
            auto* pager = dynamic_cast<osgEarth::Util::SimplePager*>(_root->getChild(0));
            if (!pager) return;
            // osgEarth's asynchronous GL precompile retains the cull visitor by
            // raw pointer. It can outlive the view on reload. Draw the meshes
            // with the active context once they have merged instead.
            pager->setCreatePagedNodeFunction([ellipse = ellipse_](const osgEarth::TileKey&) {
                auto* node = new osgEarth::Util::PagedNode2;
                if (ellipse) node->addCullCallback(new BuildingEllipseCull(ellipse));
                node->setPreCompileGLObjects(false);
                return node;
            });
            // The base layer built its root nodes before installing the factory.
            pager->removeChildren(0, pager->getNumChildren());
            pager->build();
            osg::ref_ptr<BuildingPagingManager> manager = new BuildingPagingManager;
            manager->addChild(pager);
            _root->setChild(0, manager.get());
        }
    }

    osg::ref_ptr<osg::Node> createTileImplementation(const osgEarth::TileKey& key,
        osgEarth::ProgressCallback* progress) const override
    {
        using namespace osgEarth;
        if (key.getLOD() != sourceLevel || (progress && progress->isCanceled())) return {};
        auto map = getMap();
        if (!map) return {};
        const auto& extent = key.getExtent();
        const auto geo = extent.transform(profile_->getSRS()->getGeographicSRS());
        const QRectF rect(QPointF(geo.west(), geo.south()), QPointF(geo.east(), geo.north()));
        if (localCoverage_.contains(rect)) return {};
        const bool nearLocal = localCoverage_.intersects(rect);

        ScopedMapLoadTask loadTask(MapLoadCategory::Buildings);
        QString url = tileTemplate(progress);
        if (progress && progress->isCanceled())
        {
            loadTask.cancel();
            return {};
        }
        if (url.isEmpty())
        {
            loadTask.finish();
            return {};
        }
        url.replace("{z}", QString::number(sourceLevel)).replace("{x}", QString::number(key.getTileX()))
            .replace("{y}", QString::number(key.getTileY()));
        const auto tile = mapHttpGet(QNetworkRequest(QUrl(url)), progress);
        if (progress && progress->isCanceled())
        {
            loadTask.cancel();
            return {};
        }
        FeatureList decoded, buildings;
        if (tile.body.isEmpty() || !MVT::readTile(tile.body.toStdString(), key, decoded, {"building"}))
        {
            loadTask.finish();
            return {};
        }
        for (auto& feature : decoded)
        {
            auto* shape = feature->getGeometry();
            if (!shape || !shape->isPolygon() || feature->getBool("hide_3d")) continue;
            const auto bounds = shape->getBounds();
            if (nearLocal)
            {
                const GeoPoint centre(extent.getSRS(), bounds.center());
                const auto ll = centre.transform(geo.getSRS());
                if (localCoverage_.contains(QPointF(ll.x(), ll.y()))) continue;
            }
            const double bottom = feature->getDouble("render_min_height", 0.0);
            const double height = feature->getDouble("render_height", 10.0) - bottom;
            if (!std::isfinite(height) || !std::isfinite(bottom) || height <= 0) continue;
            // Vector tiles include a margin also supplied by the neighbouring tile.
            if (!extent.contains(bounds))
            {
                osg::ref_ptr<Geometry> clipped = shape->crop(extent.bounds());
                if (!clipped || !clipped->isPolygon() || !clipped->isValid()) continue;
                feature->setGeometry(clipped.get());
            }
            feature->set("render_min_height", bottom);
            feature->set("wall_height", height);
            buildings.push_back(feature);
        }
        if (progress && progress->isCanceled())
        {
            loadTask.cancel();
            return {};
        }
        if (buildings.empty())
        {
            loadTask.finish();
            return {};
        }
        osg::ref_ptr<Session> session = new Session(map.get());
        osg::ref_ptr<FeatureProfile> features = new FeatureProfile(extent);
        FilterContext context(session.get(), features.get(), extent);
        auto mesh = GeometryCompiler().compile(buildings, style_, context);
        if (mesh && ellipse_) mesh->addCullCallback(new BuildingEllipseCull(ellipse_));
        loadTask.finish();
        return mesh;
    }

private:
    QString tileTemplate(osgEarth::ProgressCallback* progress) const
    {
        QMutexLocker lock(&metadataMutex_);
        if (!refreshAfter_.hasExpired() || (progress && progress->isCanceled())) return template_;
        // TileJSON points to a versioned release. Never bake a dated tile URL into the app.
        const auto response = mapHttpGet(QNetworkRequest(QUrl(QStringLiteral("https://tiles.openfreemap.org/planet"))), progress);
        if (progress && progress->isCanceled()) return {};
        const auto tiles = QJsonDocument::fromJson(response.body).object().value("tiles").toArray();
        const QString candidate = tiles.at(0).toString();
        if (candidate.startsWith("https://") && candidate.contains("{z}")
            && candidate.contains("{x}") && candidate.contains("{y}"))
        {
            template_ = candidate;
            refreshAfter_.setRemainingTime(60 * 60 * 1000);
        }
        else refreshAfter_.setRemainingTime(30000);
        return template_;
    }

    Options options_;
    const Options originalOptions_;
    const QPainterPath localCoverage_;
    osg::ref_ptr<const osgEarth::Profile> profile_;
    std::shared_ptr<const GroundDetailEllipse> ellipse_;
    osgEarth::Style style_;
    mutable QMutex metadataMutex_;
    mutable QDeadlineTimer refreshAfter_;
    mutable QString template_;
};

} // namespace VaporView::Map3D::Detail
