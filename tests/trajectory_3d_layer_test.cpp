#include "geo/TrajectoryHeatmap.h"
#include "map3d/Trajectory3DLayer.h"
#include "map3d/TrackSampling.h"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/LineWidth>
#include <osg/Point>
#include <osg/PrimitiveSet>
#include <osg/StateSet>
#include <osg/Texture1D>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

namespace
{

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

VaporView::Geo::NavSample sample(int index)
{
    VaporView::Geo::NavSample value;
    value.recordTimestampUs = static_cast<qint64>(index + 1) * 1000000;
    value.latDeg = 39.0 + static_cast<double>(index) * 0.000001;
    value.lonDeg = 116.0 + static_cast<double>(index) * 0.000001;
    value.heightM = 50.0;
    value.nedNM = static_cast<double>(index);
    value.nedEM = static_cast<double>(index) * 0.5;
    value.nedDM = -10.0;
    value.fixQuality = VaporView::Geo::FixQuality::Fixed;
    value.satellites = 12;
    value.hdop = 0.9;
    return value;
}

VaporView::Geo::TrajectoryRenderSample heatSample(int index,
                                                  std::optional<double> peak = std::nullopt)
{
    VaporView::Geo::TrajectoryRenderSample value;
    value.navigation = sample(index);
    value.heat.peak = peak;
    return value;
}

struct PrimitiveStats {
    int lineSegments = 0;
    int lineSegmentVertices = 0;
    int lineStrips = 0;
    int lineVertices = 0;
    int pointSets = 0;
    int pointVertices = 0;
};

PrimitiveStats primitiveStats(const osg::Geometry& geometry)
{
    PrimitiveStats stats;
    for (unsigned int index = 0; index < geometry.getNumPrimitiveSets(); ++index)
    {
        const osg::PrimitiveSet* primitive = geometry.getPrimitiveSet(index);
        const auto* drawArrays = dynamic_cast<const osg::DrawArrays*>(primitive);
        if (!primitive || !drawArrays)
        {
            continue;
        }
        if (primitive->getMode() == osg::PrimitiveSet::LINES)
        {
            ++stats.lineSegments;
            stats.lineSegmentVertices += static_cast<int>(drawArrays->getCount());
        }
        else if (primitive->getMode() == osg::PrimitiveSet::LINE_STRIP)
        {
            ++stats.lineStrips;
            stats.lineVertices += static_cast<int>(drawArrays->getCount());
        }
        else if (primitive->getMode() == osg::PrimitiveSet::POINTS)
        {
            ++stats.pointSets;
            stats.pointVertices += static_cast<int>(drawArrays->getCount());
        }
    }
    return stats;
}

int visibleDrawableCount(const osg::Geode& geode)
{
    int count = 0;
    for (unsigned int index = 0; index < geode.getNumDrawables(); ++index)
    {
        const osg::Drawable* drawable = geode.getDrawable(index);
        if (drawable && drawable->getNodeMask() != 0u)
        {
            ++count;
        }
    }
    return count;
}

bool nearlyEqual(double lhs, double rhs, double tolerance = 1.0e-9)
{
    return std::abs(lhs - rhs) <= tolerance;
}

} // namespace

int main()
{
    VaporView::Map3D::Trajectory3DLayer layer;
    require(layer.sampleCount() == 0, "new layer has no samples");
    require(layer.segmentSize() == 4096, "segment size is fixed at 4096 samples");

    std::vector<VaporView::Geo::NavSample> samples;
    samples.reserve(9000);
    for (int index = 0; index < 9000; ++index)
    {
        samples.push_back(sample(index));
    }

    layer.appendNavigationSamples(samples);
    require(layer.sampleCount() == 9000, "layer keeps total sample count");
    require(!layer.heatRenderingEnabled(),
            "navigation samples do not activate heat rendering");
    require(layer.visibleSampleCount() == 9000, "all samples visible by default");
    require(layer.segmentCount() == 3, "9000 samples are split into three segments");

    auto* geode = dynamic_cast<osg::Geode*>(layer.node());
    require(geode != nullptr, "trajectory node is a geode");
    require(geode->getNumDrawables() == 6, "geode has one line and one sphere drawable per segment");
    require(layer.sphereMarkerCount() == 9000, "short trajectory renders every sample as a solid sphere marker");
    auto* firstSphereGeometry = dynamic_cast<osg::Geometry*>(geode->getDrawable(1));
    require(firstSphereGeometry != nullptr && firstSphereGeometry->getVertexArray() != nullptr,
            "trajectory sphere marker geometry exists");
    require(firstSphereGeometry->getVertexArray()->getNumElements() == 4096 * 42,
            "trajectory markers use one-subdivision icospheres instead of faceted polyhedrons");
    const auto* firstSphereVertices =
        dynamic_cast<const osg::Vec3dArray*>(firstSphereGeometry->getVertexArray());
    require(firstSphereVertices != nullptr
                && nearlyEqual((firstSphereVertices->front() - osg::Vec3d(0.0, 0.0, 10.0)).length(),
                               0.5,
                               1.0e-9),
            "trajectory ball radius is reduced to half a metre");

    layer.setMaxVisibleSamples(5000);
    require(layer.sampleCount() == 5000, "visible cap releases old retained samples");
    require(layer.visibleSampleCount() == 5000, "visible sample count is capped");
    require(geode->getDrawable(0)->getNodeMask() != 0u, "boundary segment remains visible");
    require(geode->getDrawable(2)->getNodeMask() != 0u, "second retained segment remains visible");
    require(geode->getNumDrawables() == 4, "old trajectory drawables are released at the cap");
    auto* boundaryGeometry = dynamic_cast<osg::Geometry*>(geode->getDrawable(0));
    require(boundaryGeometry != nullptr, "boundary segment drawable is geometry");
    require(boundaryGeometry->getVertexArray() != nullptr, "boundary segment has vertex array");
    require(boundaryGeometry->getVertexArray()->getNumElements() == 4096,
            "retained boundary segment contains its full sample block");

    layer.appendNavigationSample(sample(9000));
    require(layer.sampleCount() == 5000, "appendSample keeps retained sample count bounded");
    require(layer.segmentCount() == 2, "appendSample reuses current retained segments");
    require(boundaryGeometry->getVertexArray()->getNumElements() == 4095,
            "oldest retained segment advances after append");

    layer.setMaxVisibleSamples(9000);
    require(layer.visibleSampleCount() == 5000, "expanded cap does not resurrect discarded live samples");
    auto* restoredFirstGeometry = dynamic_cast<osg::Geometry*>(geode->getDrawable(0));
    require(restoredFirstGeometry != nullptr, "restored first segment drawable is geometry");
    require(restoredFirstGeometry->getVertexArray() != nullptr, "restored first segment has vertex array");
    require(restoredFirstGeometry->getVertexArray()->getNumElements() == 4096,
            "expanded cap resegments currently retained geometry without restoring discarded samples");

    VaporView::Map3D::Trajectory3DLayer longLayer;
    std::vector<VaporView::Geo::NavSample> longSamples;
    constexpr int kLongTrackSampleCount = 100000;
    longSamples.reserve(kLongTrackSampleCount);
    for (int index = 0; index < kLongTrackSampleCount; ++index)
    {
        longSamples.push_back(sample(index));
    }

    longLayer.appendNavigationSamples(longSamples);
    require(longLayer.sampleCount() == kLongTrackSampleCount, "long layer keeps 100000 samples");
    require(longLayer.visibleSampleCount() == kLongTrackSampleCount, "long layer shows all samples under default cap");
    const int expectedLongSegments =
        (kLongTrackSampleCount + longLayer.segmentSize() - 1) / longLayer.segmentSize();
    require(longLayer.segmentCount() == expectedLongSegments, "long layer splits 100000 samples into segments");

    auto* longGeode = dynamic_cast<osg::Geode*>(longLayer.node());
    require(longGeode != nullptr, "long trajectory node is a geode");
    require(static_cast<int>(longGeode->getNumDrawables()) == expectedLongSegments * 2,
            "long trajectory has line and sphere drawables per segment");
    require(longLayer.sphereMarkerCount() > 0 && longLayer.sphereMarkerCount() <= 12000,
            "long trajectory caps smoother sphere markers to preserve the geometry budget");

    longLayer.setMaxVisibleSamples(10000);
    require(longLayer.sampleCount() == 10000,
            "long layer discards retained samples outside the live display cap");
    require(longLayer.visibleSampleCount() == 10000,
            "long layer max-visible cap limits rendered samples");
    require(longLayer.segmentCount() == 3,
            "long layer releases old segment drawables outside the retained window");
    require(visibleDrawableCount(*longGeode) == 6,
            "all retained long-track line and sphere drawables remain visible");
    const int longRebuildsBeforeTailAppend = longLayer.fullRebuildCount();
    const int longSegmentRebuildsBeforeTailAppend = longLayer.segmentGeometryRebuildCount();
    longLayer.appendNavigationSample(sample(kLongTrackSampleCount));
    require(longLayer.sampleCount() == 10000,
            "navigation tail append preserves the retained sample cap");
    require(longLayer.fullRebuildCount() == longRebuildsBeforeTailAppend,
            "single navigation append does not rebuild the full trajectory");
    require(longLayer.segmentGeometryRebuildCount() <= longSegmentRebuildsBeforeTailAppend + 2,
            "single navigation append only touches the tail and trim boundary segments");

    auto* longBoundaryGeometry =
        dynamic_cast<osg::Geometry*>(longGeode->getDrawable(0));
    require(longBoundaryGeometry != nullptr, "long layer boundary segment drawable is geometry");
    require(longBoundaryGeometry->getVertexArray() != nullptr,
            "long layer boundary segment has vertex array");
    require(static_cast<int>(longBoundaryGeometry->getVertexArray()->getNumElements()) <= longLayer.segmentSize(),
            "long layer boundary segment stays within the fixed segment budget");

    VaporView::Map3D::Trajectory3DLayer boundaryLayer;
    std::vector<VaporView::Geo::NavSample> boundarySamples;
    boundarySamples.reserve(4097);
    for (int index = 0; index < 4097; ++index)
    {
        boundarySamples.push_back(sample(index));
    }
    boundaryLayer.appendNavigationSamples(boundarySamples);
    auto* boundaryGeode = dynamic_cast<osg::Geode*>(boundaryLayer.node());
    require(boundaryGeode != nullptr && boundaryGeode->getNumDrawables() == 4,
            "4097 samples span two trajectory segments with sphere drawables");
    auto* secondBoundaryGeometry = dynamic_cast<osg::Geometry*>(boundaryGeode->getDrawable(2));
    require(secondBoundaryGeometry != nullptr,
            "second trajectory segment is geometry");
    const PrimitiveStats boundaryStats = primitiveStats(*secondBoundaryGeometry);
    require(boundaryStats.lineStrips == 1 && boundaryStats.lineVertices == 2,
            "trajectory segment boundary repeats the previous point to preserve line continuity");

    constexpr int kCircleSampleCount = 6336;
    constexpr int kCircleVisibleSamples = 1000;
    constexpr double kCircleRadiusM = 75.0;
    constexpr double kTwoPi = 6.28318530717958647692;
    std::vector<VaporView::Geo::NavSample> circleSamples;
    circleSamples.reserve(kCircleSampleCount);
    for (int index = 0; index < kCircleSampleCount; ++index)
    {
        VaporView::Geo::NavSample value = sample(index);
        const double angle =
            kTwoPi * static_cast<double>(index) / static_cast<double>(kCircleSampleCount - 1);
        value.nedNM = std::cos(angle) * kCircleRadiusM;
        value.nedEM = std::sin(angle) * kCircleRadiusM;
        circleSamples.push_back(value);
    }

    const std::vector<VaporView::Geo::NavSample> sampledCircle =
        VaporView::Map3D::uniformlySampleTrack(circleSamples, kCircleVisibleSamples);
    require(static_cast<int>(sampledCircle.size()) == kCircleVisibleSamples,
            "full-track sampling respects the visible sample limit");
    require(sampledCircle.front().recordTimestampUs == circleSamples.front().recordTimestampUs,
            "full-track sampling preserves the first sample");
    require(sampledCircle.back().recordTimestampUs == circleSamples.back().recordTimestampUs,
            "full-track sampling preserves the last sample");

    double minNorth = kCircleRadiusM;
    double maxNorth = -kCircleRadiusM;
    double minEast = kCircleRadiusM;
    double maxEast = -kCircleRadiusM;
    bool hasNorthEast = false;
    bool hasNorthWest = false;
    bool hasSouthEast = false;
    bool hasSouthWest = false;
    for (const VaporView::Geo::NavSample& value : sampledCircle)
    {
        minNorth = (std::min)(minNorth, value.nedNM);
        maxNorth = (std::max)(maxNorth, value.nedNM);
        minEast = (std::min)(minEast, value.nedEM);
        maxEast = (std::max)(maxEast, value.nedEM);
        hasNorthEast = hasNorthEast || (value.nedNM > 0.0 && value.nedEM > 0.0);
        hasNorthWest = hasNorthWest || (value.nedNM > 0.0 && value.nedEM < 0.0);
        hasSouthEast = hasSouthEast || (value.nedNM < 0.0 && value.nedEM > 0.0);
        hasSouthWest = hasSouthWest || (value.nedNM < 0.0 && value.nedEM < 0.0);
    }
    require(maxNorth - minNorth > kCircleRadiusM * 1.99,
            "full-track sampling preserves the circle north-south extent");
    require(maxEast - minEast > kCircleRadiusM * 1.99,
            "full-track sampling preserves the circle east-west extent");
    require(hasNorthEast && hasNorthWest && hasSouthEast && hasSouthWest,
            "full-track sampling preserves all four circle quadrants");
    require(nearlyEqual(VaporView::Map3D::trackFocusRangeM(500.0, true), 6000.0),
            "Earth track focus keeps enough range for local imagery to remain visible");
    require(nearlyEqual(VaporView::Map3D::trackFocusRangeM(500.0, false), 1500.0),
            "local track focus still fits the sampled track extent");
    require(nearlyEqual(VaporView::Map3D::trackFocusRangeM(2500.0, true), 7500.0),
            "large Earth tracks expand beyond the minimum focus range");

    VaporView::Map3D::Trajectory3DLayer sampledCircleLayer;
    sampledCircleLayer.appendNavigationSamples(sampledCircle);
    const VaporView::Map3D::TrajectoryQualityStats sampledCircleStats =
        sampledCircleLayer.qualityStats();
    require(sampledCircleStats.lineSamples == kCircleVisibleSamples,
            "sampled circle remains one usable trajectory");
    require(sampledCircleStats.jumpSamples == 0,
            "uniform sampling does not introduce false trajectory jumps");

    VaporView::Map3D::Trajectory3DLayer qualityLayer;
    std::vector<VaporView::Geo::NavSample> qualitySamples;
    for (int index = 0; index < 8; ++index)
    {
        qualitySamples.push_back(sample(index));
    }
    qualitySamples[2].fixQuality = VaporView::Geo::FixQuality::Invalid;
    qualitySamples[5].recordTimestampUs = qualitySamples[4].recordTimestampUs - 1;

    qualityLayer.appendNavigationSamples(qualitySamples);
    auto* qualityGeode = dynamic_cast<osg::Geode*>(qualityLayer.node());
    require(qualityGeode != nullptr, "quality trajectory node is a geode");
    require(qualityGeode->getNumDrawables() == 2, "quality trajectory fits in one segment with sphere markers");
    auto* qualityGeometry = dynamic_cast<osg::Geometry*>(qualityGeode->getDrawable(0));
    require(qualityGeometry != nullptr, "quality trajectory drawable is geometry");
    const osg::StateSet* qualityStateSet = qualityGeometry->getStateSet();
    require(qualityStateSet != nullptr, "quality trajectory drawable has render state");
    const auto* qualityLineWidth = dynamic_cast<const osg::LineWidth*>(
        qualityStateSet->getAttribute(osg::StateAttribute::LINEWIDTH));
    require(qualityLineWidth != nullptr && nearlyEqual(qualityLineWidth->getWidth(), 5.0),
            "trajectory uses a high-visibility line width");
    require((qualityStateSet->getMode(GL_DEPTH_TEST) & osg::StateAttribute::ON) == 0
                && (qualityStateSet->getMode(GL_DEPTH_TEST) & osg::StateAttribute::OVERRIDE) != 0,
            "trajectory stays visible over map geometry");
    require((qualityStateSet->getMode(GL_LINE_SMOOTH) & osg::StateAttribute::ON) != 0,
            "trajectory enables line smoothing");
    const PrimitiveStats stats = primitiveStats(*qualityGeometry);
    require(stats.lineStrips == 3, "invalid and jump samples split the continuous trajectory line");
    require(stats.lineVertices == 6, "only usable non-jump samples participate in line strips");
    require(stats.pointSets == 0 && stats.pointVertices == 0,
            "legacy quality line geometry keeps point markers in a separate drawable");
    auto* qualityMarkerGeometry = dynamic_cast<osg::Geometry*>(qualityGeode->getDrawable(1));
    require(qualityMarkerGeometry != nullptr
                && qualityMarkerGeometry->getVertexArray() != nullptr
                && qualityMarkerGeometry->getVertexArray()->getNumElements() == 8 * 42,
            "legacy quality marker geometry renders all visible samples as smooth spheres");
    const VaporView::Map3D::TrajectoryQualityStats qualityStats = qualityLayer.qualityStats();
    require(qualityStats.fixedSamples == 6, "quality stats count fixed line samples");
    require(qualityStats.invalidSamples == 1, "quality stats count invalid marker samples");
    require(qualityStats.jumpSamples == 1, "quality stats count jump marker samples");
    require(qualityStats.lineSamples == 6, "quality stats count line samples");
    require(qualityStats.markerSamples == 2, "quality stats count marker samples");

    VaporView::Map3D::Trajectory3DLayer outlierLayer;
    std::vector<VaporView::Geo::NavSample> outlierSamples;
    for (int index = 0; index < 5; ++index)
    {
        outlierSamples.push_back(sample(index));
    }
    outlierSamples[2].nedNM = 10000.0;
    outlierSamples[2].nedEM = 10000.0;
    outlierLayer.appendNavigationSamples(outlierSamples);
    auto* outlierGeode = dynamic_cast<osg::Geode*>(outlierLayer.node());
    require(outlierGeode != nullptr, "outlier trajectory node is a geode");
    require(outlierGeode->getNumDrawables() == 2, "outlier trajectory fits in one segment with sphere markers");
    auto* outlierGeometry = dynamic_cast<osg::Geometry*>(outlierGeode->getDrawable(0));
    require(outlierGeometry != nullptr, "outlier trajectory drawable is geometry");
    const PrimitiveStats outlierStats = primitiveStats(*outlierGeometry);
    require(outlierStats.lineStrips == 2, "single outlier splits the line into two runs");
    require(outlierStats.lineVertices == 4, "samples after an outlier reconnect from the last valid track point");
    require(outlierStats.pointVertices == 0, "outlier markers are kept in the marker drawable");
    auto* outlierMarkerGeometry = dynamic_cast<osg::Geometry*>(outlierGeode->getDrawable(1));
    require(outlierMarkerGeometry != nullptr
                && outlierMarkerGeometry->getVertexArray() != nullptr
                && outlierMarkerGeometry->getVertexArray()->getNumElements() == 5 * 42,
            "outlier marker geometry still renders every visible short-track sample");
    const VaporView::Map3D::TrajectoryQualityStats outlierQualityStats = outlierLayer.qualityStats();
    require(outlierQualityStats.jumpSamples == 1, "only the outlier is counted as a jump");
    require(outlierQualityStats.lineSamples == 4, "normal samples after the outlier remain line samples");

    VaporView::Map3D::Trajectory3DLayer heatLayer;
    std::vector<VaporView::Geo::TrajectoryRenderSample> heatSamples = {
        heatSample(0, 1.0),
        heatSample(1),
        heatSample(2, 3.0),
    };
    heatSamples[0].heat.temperatureC = 10.0;
    heatSamples[1].heat.temperatureC = 20.0;
    heatSamples[2].heat.temperatureC = 30.0;
    heatLayer.appendRenderSamples(heatSamples);
    require(heatLayer.heatRenderingEnabled(),
            "render samples explicitly activate heat rendering");
    auto* heatGeode = dynamic_cast<osg::Geode*>(heatLayer.node());
    require(heatGeode != nullptr && heatGeode->getNumDrawables() == 2,
            "heat trajectory creates separate line and point drawables");
    auto* heatLineGeometry = dynamic_cast<osg::Geometry*>(heatGeode->getDrawable(0));
    auto* heatPointGeometry = dynamic_cast<osg::Geometry*>(heatGeode->getDrawable(1));
    require(heatLineGeometry != nullptr && heatPointGeometry != nullptr,
            "heat trajectory line and point drawables are geometries");
    const PrimitiveStats heatLineStats = primitiveStats(*heatLineGeometry);
    const PrimitiveStats heatPointStats = primitiveStats(*heatPointGeometry);
    require(heatLineStats.lineSegments == 1 && heatLineStats.lineSegmentVertices == 4,
            "heat trajectory uses two-endpoint GL_LINES segments");
    require(heatPointStats.pointSets == 1 && heatPointStats.pointVertices == 3,
            "heat trajectory renders every visible sample as a GL_POINTS vertex");
    const auto* heatLineColors =
        dynamic_cast<const osg::Vec4Array*>(heatLineGeometry->getColorArray());
    const auto* heatPointColors =
        dynamic_cast<const osg::Vec4Array*>(heatPointGeometry->getColorArray());
    require(heatLineColors != nullptr && heatLineColors->size() == 4
                && heatPointColors != nullptr && heatPointColors->size() == 3,
            "heat line and point color counts match their vertex counts");
    const VaporView::Geo::HeatRange peakRange = heatLayer.heatRange();
    require(peakRange.valid && peakRange.validCount == 2
                && nearlyEqual(peakRange.minimum, 1.0)
                && nearlyEqual(peakRange.maximum, 3.0),
            "heat range ignores missing values");
    const VaporView::Geo::HeatColor neutral = VaporView::Geo::neutralHeatColor();
    require(nearlyEqual((*heatPointColors)[1].r(), neutral.r)
                && nearlyEqual((*heatPointColors)[1].g(), neutral.g)
                && nearlyEqual((*heatPointColors)[1].b(), neutral.b),
            "missing heat values use the neutral fallback color");
    const int heatFullRebuildsBeforeAppend = heatLayer.fullRebuildCount();
    const int heatSegmentRebuildsBeforeAppend = heatLayer.segmentGeometryRebuildCount();
    heatLayer.appendRenderSample(heatSample(3, 5.0));
    const VaporView::Geo::HeatRange appendedPeakRange = heatLayer.heatRange();
    require(appendedPeakRange.valid && appendedPeakRange.validCount == 3
                && nearlyEqual(appendedPeakRange.maximum, 5.0),
            "heat range extends from incremental statistics without a full scan");
    require(heatLayer.fullRebuildCount() == heatFullRebuildsBeforeAppend,
            "heat append updates only the tail geometry when the range changes");
    require(heatLayer.segmentGeometryRebuildCount() == heatSegmentRebuildsBeforeAppend + 1,
            "heat append rebuilds exactly the affected tail segment");
    heatLayer.setHeatMetric(VaporView::Geo::HeatMetric::Temperature);
    require(heatLayer.heatRange().validCount == 3
                && nearlyEqual(heatLayer.heatRange().minimum, 10.0)
                && nearlyEqual(heatLayer.heatRange().maximum, 30.0),
            "switching heat metrics recomputes the range");
    const int heatFullRebuildsBeforePalette = heatLayer.fullRebuildCount();
    heatLayer.setHeatPalette(VaporView::Geo::HeatPalette::BlueRedFast);
    heatPointGeometry = dynamic_cast<osg::Geometry*>(heatGeode->getDrawable(1));
    const auto* paletteTexture = heatPointGeometry
        ? dynamic_cast<const osg::Texture1D*>(
              heatPointGeometry->getStateSet()->getTextureAttribute(0, osg::StateAttribute::TEXTURE))
        : nullptr;
    const auto expectedPaletteStart = VaporView::Geo::heatPaletteColor(
        0.0, VaporView::Geo::HeatPalette::BlueRedFast);
    const osg::Image* paletteImage = paletteTexture ? paletteTexture->getImage() : nullptr;
    const unsigned char* paletteStart = paletteImage ? paletteImage->data(0, 0) : nullptr;
    require(heatLayer.fullRebuildCount() == heatFullRebuildsBeforePalette,
            "switching a heat palette does not rebuild historical geometry");
    require(paletteStart != nullptr
                && nearlyEqual(static_cast<double>(paletteStart[0]) / 255.0,
                               expectedPaletteStart.r,
                               0.01)
                && nearlyEqual(static_cast<double>(paletteStart[1]) / 255.0,
                               expectedPaletteStart.g,
                               0.01)
                && nearlyEqual(static_cast<double>(paletteStart[2]) / 255.0,
                               expectedPaletteStart.b,
                               0.01),
            "switching heat palette updates the shader palette texture");

    VaporView::Geo::HeatRange manualHeatRange;
    manualHeatRange.minimum = 12.0;
    manualHeatRange.maximum = 28.0;
    manualHeatRange.validCount = 2;
    manualHeatRange.valid = true;
    const int heatFullRebuildsBeforeRangeOverride = heatLayer.fullRebuildCount();
    heatLayer.setHeatRange(manualHeatRange);
    require(heatLayer.fullRebuildCount() == heatFullRebuildsBeforeRangeOverride
                && nearlyEqual(heatLayer.heatRange().minimum, 12.0)
                && nearlyEqual(heatLayer.heatRange().maximum, 28.0),
            "changing heat range updates uniforms without rebuilding history");
    heatLayer.clearHeatRangeOverride();
    require(heatLayer.fullRebuildCount() == heatFullRebuildsBeforeRangeOverride,
            "clearing heat range override does not rebuild historical geometry");

    heatLayer.setTrackLineVisible(false);
    require(heatGeode->getDrawable(0)->getNodeMask() == 0u
                && heatGeode->getDrawable(1)->getNodeMask() != 0u,
            "hiding heat lines keeps heat points visible");
    heatLayer.setTrackLineVisible(true);
    heatLayer.setTrackPointsVisible(false);
    require(heatGeode->getDrawable(0)->getNodeMask() != 0u
                && heatGeode->getDrawable(1)->getNodeMask() == 0u,
            "hiding heat points keeps heat lines visible");
    heatLayer.setTrackLineWidth(9.0f);
    heatLineGeometry = dynamic_cast<osg::Geometry*>(heatGeode->getDrawable(0));
    const auto* heatLineWidth = heatLineGeometry
        ? dynamic_cast<const osg::LineWidth*>(
              heatLineGeometry->getStateSet()->getAttribute(osg::StateAttribute::LINEWIDTH))
        : nullptr;
    require(heatLineWidth != nullptr && nearlyEqual(heatLineWidth->getWidth(), 9.0),
            "heat line width is configurable");
    heatLayer.setTrackPointSize(11.0f);
    heatPointGeometry = dynamic_cast<osg::Geometry*>(heatGeode->getDrawable(1));
    const auto* heatPointSize = heatPointGeometry
        ? dynamic_cast<const osg::Point*>(
              heatPointGeometry->getStateSet()->getAttribute(osg::StateAttribute::POINT))
        : nullptr;
    require(heatPointSize != nullptr && nearlyEqual(heatPointSize->getSize(), 11.0),
            "heat point size is configurable");
    heatLayer.clear();
    require(heatLayer.sampleCount() == 0 && heatGeode->getNumDrawables() == 0,
            "clearing heat trajectory removes line and point geometry");

    VaporView::Map3D::Trajectory3DLayer slidingHeatLayer;
    slidingHeatLayer.setMaxVisibleSamples(1000);
    std::vector<VaporView::Geo::TrajectoryRenderSample> slidingSamples;
    slidingSamples.reserve(1000);
    for (int index = 0; index < 1000; ++index)
    {
        auto value = heatSample(index, 10.0 + static_cast<double>(index));
        value.heat.humidityRh = 100.0 - static_cast<double>(index);
        slidingSamples.push_back(value);
    }
    slidingSamples[0].heat.peak = 1.0;
    slidingSamples[1].heat.peak = 1.0;
    slidingSamples[2].heat.peak = 3.0;
    slidingSamples[3].heat.peak = 3.0;
    slidingSamples[4].heat.peak = std::numeric_limits<double>::quiet_NaN();
    slidingHeatLayer.appendRenderSamples(slidingSamples);
    slidingHeatLayer.resetHeatRangeInstrumentation();
    require(slidingHeatLayer.heatRange().valid
                && nearlyEqual(slidingHeatLayer.heatRange().minimum, 1.0)
                && nearlyEqual(slidingHeatLayer.heatRange().maximum, 1009.0),
            "sliding heat range starts with valid extrema and ignores invalid values");
    slidingHeatLayer.appendRenderSample(heatSample(1000, 5.0));
    require(slidingHeatLayer.heatRange().valid
                && nearlyEqual(slidingHeatLayer.heatRange().minimum, 1.0)
                && nearlyEqual(slidingHeatLayer.heatRange().maximum, 1009.0),
            "evicting a non-extreme sample preserves the extrema");
    slidingHeatLayer.appendRenderSample(heatSample(1001, 6.0));
    require(slidingHeatLayer.heatRange().valid
                && nearlyEqual(slidingHeatLayer.heatRange().minimum, 3.0)
                && nearlyEqual(slidingHeatLayer.heatRange().maximum, 1009.0),
            "evicting duplicate minima updates the range only after both duplicates leave");
    slidingHeatLayer.setHeatMetric(VaporView::Geo::HeatMetric::Humidity);
    require(slidingHeatLayer.heatRange().valid
                && nearlyEqual(slidingHeatLayer.heatRange().minimum, -899.0)
                && nearlyEqual(slidingHeatLayer.heatRange().maximum, 98.0),
            "metric switching rebuilds the active range once");
    slidingHeatLayer.setHeatRange(manualHeatRange);
    slidingHeatLayer.appendRenderSample(heatSample(1002, 7.0));
    slidingHeatLayer.clearHeatRangeOverride();
    require(slidingHeatLayer.heatRange().valid
                && nearlyEqual(slidingHeatLayer.heatRange().minimum, -899.0)
                && nearlyEqual(slidingHeatLayer.heatRange().maximum, 97.0),
            "manual range does not corrupt auto statistics after eviction");
    require(slidingHeatLayer.heatRangeFullScanCount() == 2
                && slidingHeatLayer.heatRangeEvictionCount() == 3
                && slidingHeatLayer.heatRangeIncrementalAppendCount() == 3,
            "sliding heat append and eviction use incremental statistics");

    VaporView::Map3D::Trajectory3DLayer smallWindowHeatLayer;
    smallWindowHeatLayer.setMaxVisibleSamples(4);
    smallWindowHeatLayer.appendRenderSamples({
        heatSample(0, 1.0), heatSample(1, 4.0), heatSample(2, 2.0), heatSample(3, 3.0)});
    require(smallWindowHeatLayer.sampleCount() == 4
                && nearlyEqual(smallWindowHeatLayer.heatRange().minimum, 1.0)
                && nearlyEqual(smallWindowHeatLayer.heatRange().maximum, 4.0),
            "four-sample heat window starts with the expected extrema");
    smallWindowHeatLayer.appendRenderSample(heatSample(4, 5.0));
    require(smallWindowHeatLayer.sampleCount() == 4
                && nearlyEqual(smallWindowHeatLayer.heatRange().minimum, 2.0)
                && nearlyEqual(smallWindowHeatLayer.heatRange().maximum, 5.0),
            "four-sample eviction updates both extrema immediately");
    smallWindowHeatLayer.appendRenderSample(heatSample(5, std::numeric_limits<double>::quiet_NaN()));
    require(nearlyEqual(smallWindowHeatLayer.heatRange().minimum, 2.0)
                && nearlyEqual(smallWindowHeatLayer.heatRange().maximum, 5.0),
            "inserting an invalid heat sample leaves extrema unchanged");
    smallWindowHeatLayer.appendRenderSample(heatSample(6, 6.0));
    require(nearlyEqual(smallWindowHeatLayer.heatRange().minimum, 3.0)
                && nearlyEqual(smallWindowHeatLayer.heatRange().maximum, 6.0),
            "evicting a valid sample after invalid data keeps extrema correct");
    smallWindowHeatLayer.appendRenderSample(heatSample(7, 7.0));
    smallWindowHeatLayer.appendRenderSample(heatSample(8, 8.0));
    require(nearlyEqual(smallWindowHeatLayer.heatRange().minimum, 6.0)
                && nearlyEqual(smallWindowHeatLayer.heatRange().maximum, 8.0),
            "invalid sample eviction does not rebuild or corrupt extrema");
    smallWindowHeatLayer.appendRenderSample(heatSample(9, 9.0));
    require(nearlyEqual(smallWindowHeatLayer.heatRange().minimum, 6.0)
                && nearlyEqual(smallWindowHeatLayer.heatRange().maximum, 9.0),
            "range remains correct after the invalid sample leaves the window");

    VaporView::Map3D::Trajectory3DLayer duplicateHeatLayer;
    duplicateHeatLayer.setMaxVisibleSamples(4);
    duplicateHeatLayer.appendRenderSamples({
        heatSample(0, 1.0), heatSample(1, 1.0), heatSample(2, 3.0), heatSample(3, 3.0)});
    duplicateHeatLayer.appendRenderSample(heatSample(4, 4.0));
    require(nearlyEqual(duplicateHeatLayer.heatRange().minimum, 1.0)
                && nearlyEqual(duplicateHeatLayer.heatRange().maximum, 4.0),
            "duplicate extrema survive eviction of only one duplicate");
    duplicateHeatLayer.appendRenderSample(heatSample(5, 5.0));
    require(nearlyEqual(duplicateHeatLayer.heatRange().minimum, 3.0)
                && nearlyEqual(duplicateHeatLayer.heatRange().maximum, 5.0),
            "duplicate extrema are removed only after both samples leave");

    VaporView::Map3D::Trajectory3DLayer switchingHeatLayer;
    switchingHeatLayer.setMaxVisibleSamples(4);
    auto switchingSample = [](int index, double peak, double humidity) {
        auto value = heatSample(index, peak);
        value.heat.humidityRh = humidity;
        return value;
    };
    switchingHeatLayer.appendRenderSamples({
        switchingSample(0, 1.0, 10.0),
        switchingSample(1, 4.0, 40.0),
        switchingSample(2, 2.0, 20.0),
        switchingSample(3, 3.0, 30.0)});
    switchingHeatLayer.appendRenderSample(switchingSample(4, 5.0, 50.0));
    switchingHeatLayer.setHeatMetric(VaporView::Geo::HeatMetric::Humidity);
    switchingHeatLayer.appendRenderSample(switchingSample(5, 6.0, 60.0));
    require(nearlyEqual(switchingHeatLayer.heatRange().minimum, 20.0)
                && nearlyEqual(switchingHeatLayer.heatRange().maximum, 60.0),
            "switching to a second metric keeps its sliding range current");
    switchingHeatLayer.setHeatMetric(VaporView::Geo::HeatMetric::Peak);
    require(nearlyEqual(switchingHeatLayer.heatRange().minimum, 2.0)
                && nearlyEqual(switchingHeatLayer.heatRange().maximum, 6.0),
            "switching back restores the first metric without stale extrema");

    VaporView::Map3D::Trajectory3DLayer consecutiveOutlierLayer;
    consecutiveOutlierLayer.appendNavigationSample(sample(0));
    for (int index = 1; index <= 5000; ++index)
    {
        VaporView::Geo::NavSample outlier = sample(index);
        outlier.nedNM += 1000000.0 + static_cast<double>(index) * 1000.0;
        outlier.nedEM += 1000000.0 + static_cast<double>(index) * 1000.0;
        consecutiveOutlierLayer.appendNavigationSample(outlier);
    }
    consecutiveOutlierLayer.appendNavigationSample(sample(1));
    const VaporView::Map3D::TrajectoryQualityStats consecutiveOutlierStats =
        consecutiveOutlierLayer.qualityStats();
    require(consecutiveOutlierStats.jumpSamples == 5000,
            "consecutive outliers are counted without losing the last valid line sample");
    require(consecutiveOutlierStats.lineSamples == 2,
            "normal input reconnects after a long consecutive outlier run");

    VaporView::Map3D::Trajectory3DLayer worldLayer;
    worldLayer.setUseWorldCoordinates(true);
    std::vector<VaporView::Geo::NavSample> worldSamples;
    worldSamples.push_back(sample(0));
    worldSamples.push_back(sample(1));
    worldSamples[0].ecefXM = -2173583.123456789;
    worldSamples[0].ecefYM = 4381234.987654321;
    worldSamples[0].ecefZM = 4079876.543210987;
    worldSamples[1].ecefXM = -2173582.876543211;
    worldSamples[1].ecefYM = 4381235.123456789;
    worldSamples[1].ecefZM = 4079876.876543211;
    const osg::Vec3d worldOrigin(worldSamples[0].ecefXM, worldSamples[0].ecefYM, worldSamples[0].ecefZM);
    worldLayer.setWorldOrigin(worldOrigin);
    worldLayer.appendNavigationSamples(worldSamples);
    auto* worldGeode = dynamic_cast<osg::Geode*>(worldLayer.node());
    require(worldGeode != nullptr, "world trajectory node is a geode");
    require(worldGeode->getNumDrawables() == 2, "world trajectory fits in one segment with sphere markers");
    auto* worldGeometry = dynamic_cast<osg::Geometry*>(worldGeode->getDrawable(0));
    require(worldGeometry != nullptr, "world trajectory drawable is geometry");
    auto* worldVertices = dynamic_cast<osg::Vec3dArray*>(worldGeometry->getVertexArray());
    require(worldVertices != nullptr,
            "world trajectory stores local world offsets as double precision");
    require(worldVertices->size() >= 2, "world trajectory keeps both ECEF vertices");
    require(nearlyEqual((*worldVertices)[0].x(), 0.0)
                && nearlyEqual((*worldVertices)[0].y(), 0.0)
                && nearlyEqual((*worldVertices)[0].z(), 0.0),
            "world trajectory first vertex is relative to the local world origin");
    require(nearlyEqual((*worldVertices)[1].x(), worldSamples[1].ecefXM - worldSamples[0].ecefXM)
                && nearlyEqual((*worldVertices)[1].y(), worldSamples[1].ecefYM - worldSamples[0].ecefYM)
                && nearlyEqual((*worldVertices)[1].z(), worldSamples[1].ecefZM - worldSamples[0].ecefZM),
            "world trajectory stores small local offsets instead of ECEF-scale vertices");

    VaporView::Map3D::Trajectory3DLayer selectionLayer;
    selectionLayer.appendNavigationSamples({sample(0), sample(1), sample(2)});
    auto* selectionGeode = dynamic_cast<osg::Geode*>(selectionLayer.node());
    require(selectionLayer.selectedSampleIndex() == -1, "trajectory starts with no selected sample");
    require(selectionGeode != nullptr && selectionGeode->getNumDrawables() == 2,
            "selection test starts with line and sphere drawables");
    selectionLayer.setSelectedSampleIndex(1);
    require(selectionLayer.selectedSampleIndex() == 1, "trajectory stores selected sample index");
    require(selectionGeode->getNumDrawables() == 3,
            "selected sample adds a highlighted solid sphere drawable");
    auto* selectedSphereGeometry = dynamic_cast<osg::Geometry*>(selectionGeode->getDrawable(2));
    require(selectedSphereGeometry != nullptr
                && selectedSphereGeometry->getVertexArray() != nullptr
                && selectedSphereGeometry->getVertexArray()->getNumElements() == 42,
            "selected sample uses the same smooth icosphere mesh");
    const auto* selectedSphereVertices =
        dynamic_cast<const osg::Vec3dArray*>(selectedSphereGeometry->getVertexArray());
    osg::Vec3d selectedPosition;
    require(selectionLayer.displayPositionForSample(1, selectedPosition)
                && nearlyEqual(selectedPosition.x(), 0.5)
                && nearlyEqual(selectedPosition.y(), 1.0)
                && nearlyEqual(selectedPosition.z(), 10.0),
            "selected sample display position is available for picking");
    require(selectedSphereVertices != nullptr
                && nearlyEqual((selectedSphereVertices->front() - selectedPosition).length(),
                               1.0,
                               1.0e-9),
            "selected sphere keeps a one-metre highlight radius");
    selectionLayer.setSelectedSampleIndex(-1);
    require(selectionLayer.selectedSampleIndex() == -1
                && selectionGeode->getNumDrawables() == 2,
            "clearing selection removes the highlighted sphere drawable");

    layer.clear();
    require(layer.sampleCount() == 0, "clear removes samples");
    require(layer.segmentCount() == 0, "clear removes segments");
    require(geode->getNumDrawables() == 0, "clear removes drawables");

    return 0;
}
