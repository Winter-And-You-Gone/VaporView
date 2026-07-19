#include "ground/session/SessionMapCoordinator.h"
#include "ground/session/SessionViewerPages.h"
#include "ground/session/SessionViewerWindow.h"
#include "ground/trajectory/TrajectoryViewerDialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QHeaderView>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTableView>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <utility>

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

QPushButton *buttonWithText(QWidget& widget, const QString& text)
{
    for (QPushButton *button : widget.findChildren<QPushButton *>())
    {
        if (button->text() == text)
        {
            return button;
        }
    }
    return nullptr;
}

void testPages()
{
    using namespace VaporView::Ground::SessionUi;
    SessionOverviewWidget overview;
    int chooseCount = 0;
    int trajectoryCount = 0;
    QObject::connect(&overview, &SessionOverviewWidget::chooseSessionRequested,
                     [&chooseCount]() { ++chooseCount; });
    QObject::connect(&overview, &SessionOverviewWidget::trajectoryRequested,
                     [&trajectoryCount]() { ++trajectoryCount; });
    overview.setEnglish(true);
    QPushButton *chooseButton = buttonWithText(overview, QStringLiteral("Open Data..."));
    QPushButton *trajectoryButton = buttonWithText(overview, QStringLiteral("View Trajectory"));
    require(chooseButton && trajectoryButton, "overview page creates its command controls");
    chooseButton->click();
    require(chooseCount == 1, "overview choose command emits a semantic signal");
    require(!trajectoryButton->isEnabled(), "trajectory command stays disabled without a track");
    overview.setTrajectoryAvailable(true);
    trajectoryButton->click();
    require(trajectoryCount == 1, "overview trajectory command emits a semantic signal");
    overview.setControlsEnabled(false);
    require(!chooseButton->isEnabled() && !trajectoryButton->isEnabled(),
            "overview loading state disables commands");

    SessionWaveformWidget waveform;
    require(waveform.findChild<QWidget *>(QStringLiteral("sessionViewerWaveformPlot")) != nullptr,
            "waveform page preserves waveform plot object name");
    require(waveform.findChild<QWidget *>(QStringLiteral("sessionViewerPeakPlot")) != nullptr,
            "waveform page preserves peak plot object name");
    waveform.configureFrames(3);
    waveform.setFrameValueSilently(2);
    require(waveform.frameValue() == 2 && waveform.frameValueInRange(3),
            "waveform page owns frame control state");
    waveform.setEnvironmentSeries({20.0, 21.0}, {50.0, 51.0}, {1000.0, 1001.0});
    waveform.setEnvironmentCurrentIndex(1, true);
    waveform.setEnvironmentRange(0, 2);

    SessionDeviceDataWidget deviceData;
    deviceData.setEnglish(true);
    QVector<quint64> timestamps;
    QVector<QStringList> rows;
    const QString headerDrivenColumn =
        QStringLiteral("timestamp_with_an_intentionally_wide_header_us");
    const QString valueDrivenText =
        QStringLiteral("the widest CSV value must determine this column's default width");
    timestamps.reserve(1100);
    rows.reserve(1100);
    for (int index = 0; index < 1100; ++index)
    {
        const quint64 timestamp = static_cast<quint64>(index + 1) * 1000;
        timestamps.push_back(timestamp);
        rows.push_back({QString::number(timestamp),
                        index == 1099 ? valueDrivenText : QStringLiteral("row %1").arg(index + 1)});
    }
    deviceData.setRows(
        {headerDrivenColumn, QStringLiteral("note")},
        std::move(rows));
    auto *table = deviceData.findChild<QTableView *>(QStringLiteral("sessionViewerCsvTable"));
    require(table && table->model()->rowCount() == 1100, "device data page owns its virtual CSV table");
    deviceData.resize(960, deviceData.minimumSizeHint().height());
    deviceData.show();
    QCoreApplication::processEvents();
    require(table->viewport()->height() >= table->verticalHeader()->defaultSectionSize() * 5,
            "device data page keeps at least five CSV rows visible");
    require(table->columnWidth(2) >=
                table->horizontalHeader()->fontMetrics().horizontalAdvance(headerDrivenColumn),
            "device data page sizes a CSV column from its widest header");
    require(table->columnWidth(3) >= table->fontMetrics().horizontalAdvance(valueDrivenText),
            "device data page sizes a CSV column from its widest value");
    const SessionCsvHighlightResult highlight = deviceData.highlightTimestamp(timestamps, 1700, true);
    require(highlight.primaryRow == 1, "device data page selects the closest timestamp row");
    require(highlight.description.contains(QStringLiteral("CSV row")),
            "device data page reports highlighted row timing");

    const SessionCsvHighlightResult distantHighlight =
        deviceData.highlightTimestamp(timestamps, 90'400, true);
    QCoreApplication::processEvents();
    const QModelIndex distantIndex = table->model()->index(distantHighlight.primaryRow, 0);
    const QRect distantRect = table->visualRect(distantIndex);
    require(table->verticalScrollBar()->value() > 0 &&
                table->viewport()->rect().intersects(distantRect),
            "device data page scrolls the primary CSV match into view");
    const int followedScrollValue = table->verticalScrollBar()->value();
    deviceData.highlightTimestamp(timestamps, 10'000, false);
    QCoreApplication::processEvents();
    require(table->verticalScrollBar()->value() == followedScrollValue,
            "device data page preserves CSV scroll position when following is disabled");

    SessionViewerWindow viewer;
    auto *splitter = viewer.findChild<QSplitter *>(QStringLiteral("sessionViewerContentSplitter"));
    require(splitter && !splitter->childrenCollapsible(),
            "session viewer keeps both content panes non-collapsible");
}

void testMapCoordinator()
{
    QWidget owner;
    VaporView::Ground::SessionMapCoordinator coordinator(&owner);
    require(!coordinator.isCreated(), "map coordinator starts without a map dialog");
    require(!coordinator.showTrajectory(&owner, {}, {}), "map coordinator rejects an empty track");

    VaporView::Ground::SessionTrackPoint point;
    point.latitude = 30.25;
    point.longitude = 120.15;
    point.timestamp_us = 1'000;
    QVector<VaporView::Ground::SessionTrackPoint> points{point};
    VaporView::Ground::SessionTrackStats stats;
    stats.accepted_points = 1;
    require(coordinator.showTrajectory(&owner, points, stats), "map coordinator creates the trajectory dialog");
    QCoreApplication::processEvents();
    require(coordinator.isCreated() && coordinator.isVisible(), "map coordinator shows its managed dialog");
    const auto dialogs = owner.findChildren<TrajectoryViewerDialog *>();
    require(dialogs.size() == 1, "map coordinator creates only one dialog");
    require(coordinator.showTrajectory(&owner, points, stats), "map coordinator supports repeated open");
    require(owner.findChildren<TrajectoryViewerDialog *>().size() == 1,
            "repeated map open reuses the existing dialog");

    int activatedIndex = -1;
    QObject::connect(&coordinator, &VaporView::Ground::SessionMapCoordinator::trackPointActivated,
                     [&activatedIndex](int index) { activatedIndex = index; });
    require(QMetaObject::invokeMethod(dialogs.first(), "trackPointActivated", Q_ARG(int, 0)),
            "managed map signal can be invoked");
    require(activatedIndex == 0, "map coordinator forwards track activation");

    coordinator.closeTrajectory();
    QCoreApplication::processEvents();
    require(coordinator.isCreated() && !coordinator.isVisible(),
            "closing the map keeps a safe reusable dialog");
    require(coordinator.showTrajectory(&owner, points, stats) && coordinator.isVisible(),
            "map coordinator reopens after close");
    coordinator.closeTrajectory();

    VaporView::Ground::SessionUi::SessionLoadingDialog loading(&owner);
    loading.begin(QStringLiteral("Loading"), true);
    loading.update(QStringLiteral("Half"), 50);
    require(owner.findChild<QProgressDialog *>() != nullptr, "loading dialog is composed outside the window");
    loading.finish(QStringLiteral("Done"));
}

}  // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testPages();
    testMapCoordinator();
    std::cout << "session_viewer_components_test passed\n";
    return 0;
}
