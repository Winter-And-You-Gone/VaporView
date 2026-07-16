#include "ground/session/SessionExportService.h"
#include "ground/session/SessionTrajectoryController.h"

#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

VaporView::Ground::SessionTrackPoint samplePoint(quint64 timestampUs, int csvRow)
{
    VaporView::Ground::SessionTrackPoint point;
    point.latitude = 30.12345678;
    point.longitude = 120.87654321;
    point.height_m = 42.125;
    point.cumulative_distance_m = 12.5;
    point.segment_distance_m = 2.25;
    point.speed_mps = 3.5;
    point.timestamp_us = timestampUs;
    point.csv_row = csvRow;
    point.gnss_fix = QStringLiteral("RTK,\"FIX\"");
    point.has_height = true;
    point.has_speed = true;
    return point;
}

void testExportService()
{
    QTemporaryDir temporaryDirectory;
    require(temporaryDirectory.isValid(), "temporary export directory is valid");
    const QString filename = temporaryDirectory.filePath(QStringLiteral("trajectory.csv"));

    QVector<VaporView::Ground::SessionTrackPoint> points;
    points.push_back(samplePoint(1'000'000, 2));
    auto second = samplePoint(2'000'000, 4);
    second.has_peak_value = true;
    second.peak_value = 4.25f;
    second.has_waveform_match = true;
    second.waveform_frame_index = 6;
    second.waveform_timestamp_us = 2'001'000;
    second.waveform_delta_us = 1'000;
    points.push_back(second);

    const auto result = VaporView::Ground::SessionExportService::exportTrajectoryCsv(filename, points);
    require(result.success, "trajectory export succeeds");
    require(result.rowsWritten == 2, "trajectory export writes every point");
    QFile file(filename);
    require(file.open(QIODevice::ReadOnly | QIODevice::Text), "trajectory export can be reopened");
    const QString csv = QString::fromUtf8(file.readAll());
    const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
    require(lines.size() == 3, "trajectory export contains header and two rows");
    require(lines.first() == VaporView::Ground::SessionExportService::trajectoryCsvHeader(),
            "trajectory CSV header and field order stay compatible");
    require(lines.at(1).contains(QStringLiteral("\"RTK,\"\"FIX\"\"\"")),
            "trajectory CSV quotes commas and quotes");
    require(lines.at(2).contains(QStringLiteral(",4.250000,7,2001000,1.000")),
            "trajectory CSV preserves peak and waveform fields");

    const QString rangedFilename = temporaryDirectory.filePath(QStringLiteral("trajectory_range.csv"));
    const auto ranged = VaporView::Ground::SessionExportService::exportTrajectoryCsv(
        rangedFilename, points, 2'000'000, 2'000'000);
    require(ranged.success && ranged.rowsWritten == 1, "inclusive time range exports its boundary point");
    QFile rangedFile(rangedFilename);
    require(rangedFile.open(QIODevice::ReadOnly | QIODevice::Text), "ranged export can be reopened");
    const QString rangedCsv = QString::fromUtf8(rangedFile.readAll());
    require(rangedCsv.contains(QStringLiteral("\n2,5,")), "ranged export preserves the original point index");
    require(!rangedCsv.contains(QStringLiteral("\n1,3,")), "ranged export excludes points outside the range");

    const QString emptyFilename = temporaryDirectory.filePath(QStringLiteral("empty.csv"));
    require(!VaporView::Ground::SessionExportService::exportTrajectoryCsv(emptyFilename, {}).success,
            "empty trajectory export is rejected");
    require(!QFile::exists(emptyFilename), "empty export does not create a file");
    require(!VaporView::Ground::SessionExportService::exportTrajectoryCsv(
                temporaryDirectory.filePath(QStringLiteral("invalid-range.csv")), points, 3, 2).success,
            "invalid export time range is rejected");
    require(!VaporView::Ground::SessionExportService::exportTrajectoryCsv(
                temporaryDirectory.filePath(QStringLiteral("missing/trajectory.csv")), points).success,
            "unwritable export path reports failure");
}

void testTrajectoryController()
{
    VaporView::Ground::SessionTrajectoryController controller;
    require(!controller.hasTrack(), "trajectory controller starts empty");
    require(!controller.focusForPoint(0).valid, "empty trajectory focus is invalid");

    QVector<VaporView::Ground::SessionTrackPoint> points;
    points.push_back(samplePoint(1'000, 0));
    points.push_back(samplePoint(2'000, 1));
    VaporView::Ground::SessionTrackStats stats;
    stats.accepted_points = 2;
    controller.setTrackData(std::move(points), stats);
    require(controller.hasTrack(), "trajectory controller stores track data");
    require(controller.trackPoints().size() == 2, "trajectory point order is preserved");
    require(controller.trackStats().accepted_points == 2, "trajectory statistics are preserved");
    require(!controller.focusForPoint(-1).valid && !controller.focusForPoint(2).valid,
            "out-of-range trajectory focus is rejected");

    controller.attachWaveformPeaks({900, 2'100}, {1.5f, 2.5f});
    const auto firstFocus = controller.focusForPoint(0);
    const auto secondFocus = controller.focusForPoint(1);
    require(firstFocus.valid && firstFocus.csvRow == 0, "trajectory focus retains CSV mapping");
    require(firstFocus.waveformFrameIndex == 0 && secondFocus.waveformFrameIndex == 1,
            "trajectory focus follows waveform timestamp mapping");
    require(controller.trackPoints().at(0).has_peak_value &&
                controller.trackPoints().at(0).peak_value == 1.5f,
            "trajectory peak values are attached without reordering points");

    const auto range = controller.sensorRangeForWaveformRange(
        {800, 1'200, 1'900, 2'200},
        {900, 2'100},
        0,
        2);
    require(range.valid && range.startIndex == 0 && range.count == 4,
            "waveform view range maps to the matching sensor timeline range");
    require(!controller.sensorRangeForWaveformRange({}, {900}, 0, 1).valid,
            "empty sensor timeline produces no range");
    require(!controller.sensorRangeForWaveformRange({800}, {}, 0, 1).valid,
            "empty waveform timeline produces no range");
    require(!controller.sensorRangeForWaveformRange({800}, {900}, 0, 0).valid,
            "zero visible frame count produces no range");

    controller.clear();
    require(!controller.hasTrack() && controller.trackStats().accepted_points == 0,
            "trajectory controller clears points and statistics");
}

}  // namespace

int main()
{
    testExportService();
    testTrajectoryController();
    std::cout << "session_export_trajectory_test passed\n";
    return 0;
}
