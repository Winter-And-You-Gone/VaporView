#include "ground/session/SessionMapCoordinator.h"
#include "ground/session/SessionViewerPages.h"
#include "ground/session/SessionViewerWindow.h"
#include "ground/trajectory/TrajectoryViewerDialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QHeaderView>
#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QSpinBox>
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

QLabel *labelWithText(QWidget& widget, const QString& text)
{
    for (QLabel *label : widget.findChildren<QLabel *>())
    {
        if (label->text() == text)
        {
            return label;
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
    QPushButton *chooseButton = buttonWithText(overview, QStringLiteral("Open Data"));
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
    SessionOverviewSummary summary;
    summary.sessionName = QStringLiteral("test-session");
    summary.recordingOrigin = QStringLiteral("Sky");
    overview.setSummary(summary);
    require(labelWithText(overview, QStringLiteral("Origin:")) != nullptr &&
                labelWithText(overview, QStringLiteral("Sky")) != nullptr,
            "overview summary displays the recording origin");

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

    waveform.configureFrames(144783);
    waveform.setFramePreviewInfo(74142, 144783, false);
    auto *frameNumber = waveform.findChild<QSpinBox *>(QStringLiteral("sessionViewerFrameNumberSpin"));
    auto *frameInfo = waveform.findChild<QLabel *>(QStringLiteral("sessionViewerFrameInfoLabel"));
    require(frameNumber && frameInfo && frameNumber->value() == 74143 &&
                !frameInfo->text().contains(QStringLiteral("74143")) &&
                !frameInfo->text().contains(QStringLiteral("144783")),
            "waveform preview uses one editable frame counter without duplicating its numbers in the message");
    waveform.setFrameDetails(74143, 144783, 1782446038573000ULL, QStringLiteral("per_frame"), 1,
                            0.1, 0.9, 0.9, QStringLiteral("tcp_wave.dat"), QStringLiteral("CSV match"), false);
    require(frameNumber->value() == 74144 && frameInfo->text().contains(QStringLiteral("min=")) &&
                !frameInfo->text().contains(QStringLiteral("74144")) &&
                !frameInfo->text().contains(QStringLiteral("144783")),
            "waveform details keep the editable counter and remove the repeated frame numbers");

    SessionDeviceDataWidget deviceData;
    deviceData.setEnglish(true);
    QVector<quint64> timestamps;
    QVector<QStringList> rows;
    const QString headerDrivenColumn =
        QStringLiteral("timestamp_with_an_intentionally_wide_header_us");
    const QString sampledValueText =
        QStringLiteral("the first two CSV rows determine this column width");
    const QString ignoredLaterValueText =
        QStringLiteral("this intentionally much wider value appears after the first two rows and must not affect the default width");
    timestamps.reserve(1100);
    rows.reserve(1100);
    for (int index = 0; index < 1100; ++index)
    {
        const quint64 timestamp = static_cast<quint64>(index + 1) * 1000;
        timestamps.push_back(timestamp);
        rows.push_back({QString::number(timestamp),
                        index == 1
                            ? sampledValueText
                            : (index == 1099 ? ignoredLaterValueText : QStringLiteral("row %1").arg(index + 1))});
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
    require(table->columnWidth(3) >= table->fontMetrics().horizontalAdvance(sampledValueText),
            "device data page sizes a CSV column from the first two rows");
    require(table->columnWidth(3) < table->fontMetrics().horizontalAdvance(ignoredLaterValueText),
            "device data page does not scan later CSV rows for column width");
    const SessionCsvHighlightResult highlight = deviceData.highlightTimestamp(timestamps, 1700, true);
    require(highlight.primaryRow == 1, "device data page selects the closest timestamp row");
    require(highlight.description.contains(QStringLiteral("CSV row")),
            "device data page reports highlighted row timing");

    const auto requireTopHighlightedPair = [&](quint64 timestamp, int firstRow, int primaryRow) {
        const auto result = deviceData.highlightTimestamp(timestamps, timestamp, true);
        QCoreApplication::processEvents();
        require(result.primaryRow == primaryRow, "CSV following preserves the closest timestamp match");
        const QModelIndex firstIndex = table->model()->index(firstRow, 0);
        const QModelIndex secondIndex = table->model()->index(firstRow + 1, 0);
        const QRect firstRect = table->visualRect(firstIndex);
        const QRect secondRect = table->visualRect(secondIndex);
        require(table->rowAt(0) == firstRow && firstRect.top() == 0 &&
                    secondRect.top() == table->rowHeight(firstRow) &&
                    table->viewport()->rect().contains(secondRect),
                "the highlighted CSV pair occupies the first two fully visible rows");
        const int unhighlightedRow = firstRow == 0 ? 2 : 0;
        const QVariant normalBackground = table->model()->index(unhighlightedRow, 0).data(Qt::BackgroundRole);
        require(firstIndex.data(Qt::BackgroundRole) != normalBackground &&
                    secondIndex.data(Qt::BackgroundRole) != normalBackground,
                "both CSV rows at the top retain their highlight backgrounds");
    };
    requireTopHighlightedPair(90'400, 89, 89);
    requireTopHighlightedPair(90'700, 89, 90);
    requireTopHighlightedPair(10'000, 8, 9);
    requireTopHighlightedPair(500, 0, 0);
    requireTopHighlightedPair(1'100'500, 1098, 1099);
    table->verticalScrollBar()->setValue(table->verticalScrollBar()->maximum());
    QCoreApplication::processEvents();
    require(table->rowAt(0) == 1098, "scrolling to the CSV end keeps the final pair at the top");
    deviceData.resize(960, deviceData.height() + 240);
    QCoreApplication::processEvents();
    require(table->rowAt(0) == 1098, "resizing the CSV viewport preserves the matched pair at the top");
    requireTopHighlightedPair(1'099'700, 1098, 1099);
    const int followedScrollValue = table->verticalScrollBar()->value();
    deviceData.highlightTimestamp(timestamps, 10'000, false);
    QCoreApplication::processEvents();
    require(table->verticalScrollBar()->value() == followedScrollValue,
            "device data page preserves CSV scroll position when following is disabled");

    deviceData.setRows({QStringLiteral("timestamp_us")}, {{QStringLiteral("1000")}, {QStringLiteral("2000")}});
    deviceData.highlightTimestamp({1000, 2000}, 1700, true);
    QCoreApplication::processEvents();
    require(table->rowAt(0) == 0 && table->verticalScrollBar()->maximum() == 0,
            "a two-row CSV shows both matches without empty leading rows");
    deviceData.setRows({QStringLiteral("timestamp_us")}, {{QStringLiteral("1000")}});
    require(deviceData.highlightTimestamp({1000}, 1700, true).primaryRow == 0,
            "single-row CSV highlighting remains valid");
    deviceData.clear();
    QCoreApplication::processEvents();
    require(table->verticalScrollBar()->maximum() == 0 &&
                deviceData.highlightTimestamp({}, 1700, true).primaryRow == -1,
            "clearing the CSV removes the extended scroll range and highlights");

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
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory is available");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());
    QApplication app(argc, argv);
    testPages();
    testMapCoordinator();
    std::cout << "session_viewer_components_test passed\n";
    return 0;
}
