#include "ground/main/MainWindow.h"
#include "ground/session/GroundRecordingService.h"
#include "ground/session/SessionViewerWindow.h"
#include "ground/session/SessionViewerPages.h"
#include "shared/theme/AppTheme.h"
#include "test_ui_helpers.h"

#include <QApplication>
#include <QAction>
#include <QDir>
#include <QEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHoverEvent>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

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

void requireSamePath(const QString& actual, const QString& expected, const char *message)
{
    const QString actualPath = QFileInfo(actual).absoluteFilePath();
    const QString expectedPath = QFileInfo(expected).absoluteFilePath();
    if (actualPath != expectedPath)
    {
        std::cerr << "FAIL: " << message << "\n"
                  << "  actual: " << actualPath.toStdString() << "\n"
                  << "  expected: " << expectedPath.toStdString() << '\n';
        std::exit(1);
    }
}

SessionViewerWindow *visibleSessionViewerWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets())
    {
        auto *viewer = qobject_cast<SessionViewerWindow *>(widget);
        if (viewer && viewer->isVisible() && !viewer->isMinimized())
        {
            return viewer;
        }
    }
    return nullptr;
}

QString expectedDataSelectionDirectory()
{
    const QString defaultDirectory =
        VaporView::Ground::Session::GroundRecordingService::defaultRecordingDirectory();
    return QFileInfo(defaultDirectory).isDir()
        ? QFileInfo(defaultDirectory).absoluteFilePath()
        : QFileInfo(QDir::currentPath()).absoluteFilePath();
}

QString expectedRecordingDirectoryDialogFallback()
{
    const QFileInfo defaultDirectory(
        VaporView::Ground::Session::GroundRecordingService::defaultRecordingDirectory());
    return defaultDirectory.isDir()
        ? defaultDirectory.absoluteFilePath()
        : defaultDirectory.absoluteDir().absolutePath();
}

void testOverviewLayout(SessionViewerWindow& viewer)
{
    auto *overview = viewer.findChild<VaporView::Ground::SessionUi::SessionOverviewWidget *>();
    auto *path = overview ? overview->findChild<QLineEdit *>() : nullptr;
    auto *summary = overview ? overview->findChild<QGroupBox *>() : nullptr;
    auto *grid = summary ? qobject_cast<QGridLayout *>(summary->layout()) : nullptr;
    require(overview && path && grid, "data viewer exposes its overview layout");
    const QSize originalSize = viewer.size();
    for (bool english : {false, true})
    {
        viewer.setEnglish(english);
        overview->setSummary({QStringLiteral("2026-10-02_023000_ground"), QStringLiteral("Ground"),
                              QStringLiteral("2026-10-02 02:30:00"), QStringLiteral("2026-10-02 03:00:00"),
                              QStringLiteral("30:00"), QStringLiteral("10 Hz"), QStringLiteral("18000"),
                              QStringLiteral("10 Hz"), QStringLiteral("3"), QStringLiteral("18000")});
        for (int width : {1280, 900})
        {
            viewer.resize(width, 800);
            VaporViewTest::processEventsFor(180);
            const int columns = width == 1280 ? 4 : 2;
            QVector<int> fieldCounts(columns, 0);
            for (QLabel *title : summary->findChildren<QLabel *>(QStringLiteral("fieldLabel")))
            {
                int row, column, rowSpan, columnSpan;
                grid->getItemPosition(grid->indexOf(title), &row, &column, &rowSpan, &columnSpan);
                require(column / 2 < columns && row < 12 / columns,
                        "overview fields fill a compact balanced grid");
                ++fieldCounts[column / 2];
                require(title->geometry().right() < summary->width(),
                        "overview field labels remain inside the summary");
                require(title->width() >= title->fontMetrics().horizontalAdvance(title->text()),
                        "overview field labels remain readable in both languages");
            }
            for (int count : fieldCounts)
                require(count == 12 / columns, "each overview column contains the same number of fields");
            const auto sameRow = [summary](const QString& left, const QString& right) {
                QLabel *leftLabel = nullptr;
                QLabel *rightLabel = nullptr;
                for (QLabel *label : summary->findChildren<QLabel *>())
                {
                    if (label->text() == left) leftLabel = label;
                    if (label->text() == right) rightLabel = label;
                }
                return leftLabel && rightLabel &&
                    leftLabel->geometry().center().y() == rightLabel->geometry().center().y();
            };
            require(sameRow(english ? QStringLiteral("PPK Status:") : QStringLiteral("PPK 状态:"),
                            english ? QStringLiteral("Navigation source:") : QStringLiteral("导航来源:")) &&
                        sameRow(english ? QStringLiteral("Start:") : QStringLiteral("开始时间:"),
                                english ? QStringLiteral("End:") : QStringLiteral("结束时间:")) &&
                        sameRow(english ? QStringLiteral("Wave Files:") : QStringLiteral("波形文件数:"),
                                english ? QStringLiteral("Wave Frames:") : QStringLiteral("波形帧数:")),
                    "overview keeps related navigation, time, and waveform fields adjacent");
            for (QPushButton *button : overview->findChildren<QPushButton *>())
            {
                const QRect rect(button->mapTo(overview, QPoint()), button->size());
                require(overview->rect().contains(rect) && !path->geometry().intersects(rect),
                        "overview path and command buttons fit without overlap");
                if (width == 1280)
                    require(std::abs(path->geometry().center().y() - rect.center().y()) <= 1,
                            "overview path and all commands share one row");
            }
        }
    }
    viewer.setEnglish(false);
    viewer.resize(originalSize);
    VaporViewTest::processEventsFor(180);
}

void testFrameSliderThemeAndHover(MainWindow& window, SessionViewerWindow& viewer)
{
    auto *waveform = viewer.findChild<VaporView::Ground::SessionUi::SessionWaveformWidget *>();
    auto *slider = viewer.findChild<QSlider *>(QStringLiteral("sessionViewerFrameSlider"));
    require(waveform && slider, "main-window viewer exposes its frame slider");
    auto *timeLabel = slider->findChild<QLabel *>(QStringLiteral("sessionViewerFrameTimeLabel"));
    auto *indexLabel = slider->findChild<QLabel *>(QStringLiteral("sessionViewerFrameIndexLabel"));
    require(timeLabel && indexLabel, "frame navigator has time and index annotations");
    const QSize originalSize = window.size();
    const bool originalDark = VaporView::isDarkThemeEnabled();
    auto *scrollArea = viewer.findChild<QScrollArea *>(QStringLiteral("sessionViewerScrollArea"));
    require(scrollArea, "viewer scroll area exposes the frame slider for incremental paint checks");
    const int originalScroll = scrollArea->verticalScrollBar()->value();
    class PaintObserver final : public QObject
    {
    public:
        QRegion painted;
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::Paint)
                painted |= static_cast<QPaintEvent *>(event)->region();
            return false;
        }
    } observer;
    slider->installEventFilter(&observer);
    viewer.raise();
    viewer.activateWindow();
    waveform->configureFrames(144783);
    waveform->setEnvironmentSeries({20.125, 21.5}, {50.0, 55.0}, {1000.125, 1001.0});
    waveform->setFrameValueSilently(74143);
    const auto logicalBounds = [slider](const QRect& pixels) {
        const qreal ratio = slider->devicePixelRatioF();
        return QRectF(pixels.x() / ratio, pixels.y() / ratio,
                      pixels.width() / ratio, pixels.height() / ratio).toAlignedRect();
    };
    for (int scale : {100, 130, 160})
    {
        QAction scaleAction(&window);
        scaleAction.setData(scale);
        require(QMetaObject::invokeMethod(&window, "onFontScaleTriggered", Qt::DirectConnection,
                                         Q_ARG(QAction *, &scaleAction)), "frame slider uses the actual application font scaling");
        for (bool dark : {false, true})
        {
            if (VaporView::isDarkThemeEnabled() != dark)
                require(QMetaObject::invokeMethod(&window, "onToggleTheme", Qt::DirectConnection),
                        "frame slider uses the actual application theme switch");
            for (int frame : {1, 74143, 144783})
            {
                waveform->setFramePreviewInfo(frame - 1, 144783, false, 1782446038573000ULL);
                VaporViewTest::processEventsFor(100);
                auto *sidebar = viewer.findChild<QWidget *>(QStringLiteral("appSidebar"));
                require(sidebar && sidebar->width() >= sidebar->layout()->minimumSize().width(),
                        "scaled viewer navigation fits inside its compact sidebar");
                scrollArea->ensureWidgetVisible(slider, 0, 0);
                VaporViewTest::processEventsFor(100);
                QHoverEvent leave(QEvent::HoverLeave, QPointF(-1, -1), QPointF(-1, -1));
                QCoreApplication::sendEvent(slider, &leave);
                QEvent outside(QEvent::Leave);
                QCoreApplication::sendEvent(slider, &outside);
                const QColor accent = VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark);
                const QColor track = VaporView::appThemeColor(VaporView::AppThemeColor::BorderStrong, dark);
                QRect trackBounds;
                const auto capture = [&]() {
                    VaporViewTest::processEventsFor(40);
                    QImage image = slider->grab().toImage();
                    QRect bounds;
                    trackBounds = QRect();
                    int pixels = 0;
                    for (int y = 0; y < image.height(); ++y)
                        for (int x = 0; x < image.width(); ++x)
                            if (image.pixelColor(x, y).rgb() == accent.rgb())
                            {
                                ++pixels;
                                bounds |= QRect(x, y, 1, 1);
                            }
                            else if (image.pixelColor(x, y).rgb() == track.rgb())
                                trackBounds |= QRect(x, y, 1, 1);
                    return qMakePair(bounds, pixels);
                };
                const auto normal = capture();
                const QRect handleBounds = logicalBounds(normal.first);
                require(timeLabel->text().endsWith(QStringLiteral(".573000")) && indexLabel->text() == QString::number(frame),
                        "navigator annotations follow the preview time and frame index");
                require(timeLabel->geometry().bottom() < handleBounds.top() &&
                            indexLabel->geometry().top() > handleBounds.bottom() &&
                            slider->rect().contains(timeLabel->geometry()) && slider->rect().contains(indexLabel->geometry()),
                        "navigator annotations stay above and below the handle without endpoint clipping");
                QStyleOptionSlider option;
                option.initFrom(slider);
                option.orientation = Qt::Horizontal;
                option.state |= QStyle::State_Horizontal;
                option.minimum = slider->minimum();
                option.maximum = slider->maximum();
                option.sliderPosition = slider->sliderPosition();
                option.sliderValue = slider->value();
                const QPoint center = slider->style()->subControlRect(QStyle::CC_Slider, &option,
                                                                     QStyle::SC_SliderHandle, slider).center();
                const QPoint outsideHandle(1, 1);
                QEnterEvent enter(outsideHandle, outsideHandle, slider->mapToGlobal(outsideHandle));
                QCoreApplication::sendEvent(slider, &enter);
                VaporViewTest::processEventsFor(100);
                observer.painted = QRegion();
                QHoverEvent hover(QEvent::HoverMove, center, outsideHandle);
                QCoreApplication::sendEvent(slider, &hover);
                VaporViewTest::processEventsFor(100);
                // Record the actual incremental paint before grab() forces a full
                // render and hides clipping outside the native handle's dirty area.
                const QRegion hoverPaint = observer.painted;
                const auto hovered = capture();
                require(std::abs(normal.first.center().x() - hovered.first.center().x()) <= 1,
                        "hover enlarges the frame handle around its original center at every frame");
                require(QRegion(logicalBounds(normal.first).united(logicalBounds(hovered.first)))
                            .subtracted(hoverPaint).isEmpty(),
                        "hover repaints both the old and enlarged circular handle without clipping");
                observer.painted = QRegion();
                QHoverEvent leaveHandle(QEvent::HoverMove, QPointF(1, 1), center);
                QCoreApplication::sendEvent(slider, &leaveHandle);
                VaporViewTest::processEventsFor(100);
                require(QRegion(logicalBounds(hovered.first)).subtracted(observer.painted).isEmpty(),
                        "leaving the handle repaints the enlarged circle without residual edges");
                QCoreApplication::sendEvent(slider, &hover);
                QMouseEvent press(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(slider, &press);
                const auto pressed = capture();
                require(std::abs(normal.first.center().x() - pressed.first.center().x()) <= 1,
                        "pressing the frame handle preserves its center at both endpoints and in between");
                if (frame == 1)
                {
                    waveform->setFrameValueSilently(74143);
                    capture();
                    waveform->setFrameValueSilently(frame);
                    require(std::abs(normal.first.center().x() - trackBounds.left()) <= trackBounds.height() / 2 + 2,
                            "the first frame is centered at the visible track's left endpoint");
                }
                if (frame == 144783)
                {
                    waveform->setFrameValueSilently(74143);
                    capture();
                    waveform->setFrameValueSilently(frame);
                    require(std::abs(normal.first.center().x() - trackBounds.right()) <= trackBounds.height() / 2 + 2,
                            "the last frame is centered at the visible track's right endpoint");
                }
                const QRect logicalTrack = logicalBounds(trackBounds);
                const int trackLeft = slider->mapTo(waveform, QPoint(logicalTrack.left(), 0)).x();
                const int trackRight = slider->mapTo(waveform, QPoint(logicalTrack.right(), 0)).x();
                for (const QString& plotName : {QStringLiteral("sessionViewerWaveformPlot"), QStringLiteral("sessionViewerPeakPlot"),
                                               QStringLiteral("sessionViewerTemperaturePlot"), QStringLiteral("sessionViewerHumidityPlot"),
                                               QStringLiteral("sessionViewerPressurePlot")})
                {
                    QWidget *plot = waveform->findChild<QWidget *>(plotName);
                    require(plot != nullptr, "navigator alignment plot exists");
                    const QImage rendered = plot->grab().toImage();
                    const qreal ratio = plot->devicePixelRatioF();
                    const QColor background = rendered.pixelColor(0, rendered.height() / 2);
                    const int scanTop = rendered.height() - qRound(80 * ratio);
                    const int scanBottom = rendered.height() - qRound(40 * ratio);
                    int left = rendered.width();
                    int right = 0;
                    for (int x = 0; x < rendered.width(); ++x)
                    {
                        int linePixels = 0;
                        for (int y = scanTop; y < scanBottom; ++y)
                            if (rendered.pixelColor(x, y).rgb() != background.rgb())
                                ++linePixels;
                        if (linePixels > (scanBottom - scanTop) * 0.8)
                        {
                            left = std::min(left, x);
                            right = std::max(right, x);
                        }
                    }
                    const int plotX = plot->mapTo(waveform, QPoint()).x();
                    require(std::abs(plotX + qRound(left / ratio) - trackLeft) <= 3 &&
                                std::abs(plotX + qRound(right / ratio) - trackRight) <= 3,
                            "navigator track aligns with the rendered plot area of every waveform and trend chart");
                }
                QPoint releasePoint = center;
                if (frame == 74143)
                {
                    option.sliderPosition = slider->minimum();
                    const int left = slider->style()->subControlRect(QStyle::CC_Slider, &option,
                                                                     QStyle::SC_SliderHandle, slider).center().x();
                    option.sliderPosition = slider->maximum();
                    const int right = slider->style()->subControlRect(QStyle::CC_Slider, &option,
                                                                      QStyle::SC_SliderHandle, slider).center().x();
                    for (qreal fraction : {0.5, 0.25, 0.0, 0.1, 0.4, 0.75, 1.0, 0.9, 0.5})
                    {
                        releasePoint = QPoint(qRound(left + fraction * (right - left)), center.y());
                        QMouseEvent move(QEvent::MouseMove, releasePoint, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                        QCoreApplication::sendEvent(slider, &move);
                        const QRect dragged = logicalBounds(capture().first);
                        if (std::abs(dragged.center().x() - releasePoint.x()) > 1)
                            std::cerr << "drag cursor x=" << releasePoint.x()
                                      << ", painted handle x=" << dragged.center().x()
                                      << ", frame=" << slider->sliderPosition() << '\n';
                        require(slider->isSliderDown() && std::abs(dragged.center().x() - releasePoint.x()) <= 1,
                                "dragged frame handle follows the cursor across the aligned track in both directions");
                        require(indexLabel->text() == QString::number(slider->sliderPosition()),
                                "dragged frame index follows the cursor position");
                    }
                }
                QMouseEvent release(QEvent::MouseButtonRelease, releasePoint, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(slider, &release);
                if (frame == 74143)
                {
                    option.sliderPosition = slider->sliderPosition();
                    const QRect handle = slider->style()->subControlRect(QStyle::CC_Slider, &option,
                                                                         QStyle::SC_SliderHandle, slider);
                    const int offset = handle.width() / 4;
                    const QPoint anchor = handle.center() + QPoint(offset, 0);
                    QMouseEvent offsetPress(QEvent::MouseButtonPress, anchor, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(slider, &offsetPress);
                    for (int delta : {-120, 90, -60, 0})
                    {
                        releasePoint = anchor + QPoint(delta, 0);
                        QMouseEvent move(QEvent::MouseMove, releasePoint, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                        QCoreApplication::sendEvent(slider, &move);
                        const QRect dragged = logicalBounds(capture().first);
                        require(slider->isSliderDown() && std::abs(dragged.center().x() + offset - releasePoint.x()) <= 1,
                                "pressing off-center preserves the cursor's grab offset throughout dragging");
                    }
                    QMouseEvent offsetRelease(QEvent::MouseButtonRelease, releasePoint, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(slider, &offsetRelease);
                    require(!slider->isSliderDown(), "releasing the dragged handle ends the preview gesture");
                }
                require(normal.second > 50 && hovered.second > normal.second * 1.3 && pressed.second >= hovered.second,
                        "frame slider retains its theme accent and grows on hover and press in the real application");
                for (const auto& rendered : {normal, hovered, pressed})
                    require(std::abs(rendered.first.width() - rendered.first.height()) <= 2 &&
                                rendered.second < rendered.first.width() * rendered.first.height() * 0.85,
                            "frame slider handle remains circular in every mouse state and font scale");
            }
        }
    }
    QAction scaleAction(&window);
    scaleAction.setData(100);
    QMetaObject::invokeMethod(&window, "onFontScaleTriggered", Qt::DirectConnection, Q_ARG(QAction *, &scaleAction));
    if (VaporView::isDarkThemeEnabled() != originalDark)
        QMetaObject::invokeMethod(&window, "onToggleTheme", Qt::DirectConnection);
    window.resize(originalSize);
    waveform->configureFrames(0);
    waveform->setEnvironmentSeries({}, {}, {});
    scrollArea->verticalScrollBar()->setValue(originalScroll);
    slider->removeEventFilter(&observer);
    VaporViewTest::processEventsFor(100);
    require(viewer.findChild<QWidget *>(QStringLiteral("appSidebar"))->width() == 62,
            "restoring standard font scale restores single-icon sidebar width");
}

void testMainWindowDataViewerOpenCanReopen()
{
    using VaporViewTest::processEventsFor;
    using VaporViewTest::processEventsUntil;
    using VaporViewTest::waitForWindowExposed;

    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary session directory for data viewer startup");
    QTemporaryDir recordingDir;
    require(recordingDir.isValid(), "temporary configured recording directory");
    QTemporaryDir updatedRecordingDir;
    require(updatedRecordingDir.isValid(), "temporary updated recording directory");
    QTemporaryDir menuRecordingDir;
    require(menuRecordingDir.isValid(), "temporary menu recording directory");

    {
        QSettings mainSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        mainSettings.remove(QStringLiteral("recording_directory"));
        SessionViewerWindow viewerWithDefaultDirectory;
        requireSamePath(viewerWithDefaultDirectory.dataSelectionDirectory(),
                        expectedDataSelectionDirectory(),
                        "data viewer defaults to the available recording directory");

        QString providedDirectory = recordingDir.path();
        viewerWithDefaultDirectory.setRecordingDirectoryProvider([&providedDirectory]() {
            return providedDirectory;
        });
        requireSamePath(viewerWithDefaultDirectory.dataSelectionDirectory(),
                        recordingDir.path(),
                        "data viewer reads its directory from the configured provider");
        providedDirectory = updatedRecordingDir.path();
        requireSamePath(viewerWithDefaultDirectory.dataSelectionDirectory(),
                        updatedRecordingDir.path(),
                        "data viewer reads a changed recording directory without reopening");

        mainSettings.setValue(QStringLiteral("recording_directory"), recordingDir.path());
        mainSettings.sync();
    }

    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("SessionViewer"));
        settings.setValue(QStringLiteral("last_session_directory"), sessionDir.path());
    }

    QSettings("VaporView", "MainWindow").setValue("font_scale_percent", 100);
    MainWindow window;
    window.resize(1280, 800);
    window.show();
    require(waitForWindowExposed(&window), "main window becomes exposed for data viewer reopen test");

    auto openRecordingDirectoryDialog = [&window]() {
        QString openedDirectory;
        QTimer dialogObserver;
        QObject::connect(&dialogObserver, &QTimer::timeout, [&openedDirectory]() {
            for (QWidget *widget : QApplication::topLevelWidgets())
            {
                auto *dialog = qobject_cast<QFileDialog *>(widget);
                if (!dialog)
                {
                    continue;
                }
                openedDirectory = dialog->directory().absolutePath();
                dialog->reject();
                return;
            }
        });
        dialogObserver.start(10);
        require(QMetaObject::invokeMethod(&window, "onChooseRecordingDirectoryClicked", Qt::DirectConnection),
                "main window can invoke recording directory action");
        dialogObserver.stop();
        return openedDirectory;
    };

    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        settings.setValue(QStringLiteral("recording_directory"),
                          menuRecordingDir.filePath(QStringLiteral("missing")));
        settings.sync();
        requireSamePath(openRecordingDirectoryDialog(),
                        expectedRecordingDirectoryDialogFallback(),
                        "recording directory action falls back to the available recording directory for a missing setting");

        settings.setValue(QStringLiteral("recording_directory"), menuRecordingDir.path());
        settings.sync();
        requireSamePath(openRecordingDirectoryDialog(),
                        menuRecordingDir.path(),
                        "recording directory action opens the directory currently stored in settings");
    }

    require(QMetaObject::invokeMethod(&window, "onOpenSessionViewerClicked", Qt::DirectConnection),
            "main window can invoke data viewer action");
    require(processEventsUntil(2000, []() {
                return visibleSessionViewerWindow() != nullptr;
            }),
            "data viewer opens from main window action");

    auto *viewer = visibleSessionViewerWindow();
    require(viewer != nullptr, "active data viewer is available");
    for (QWidget *host : {static_cast<QWidget *>(&window), static_cast<QWidget *>(viewer)})
    {
        auto *sidebar = host->findChild<QWidget *>(QStringLiteral("appSidebar"));
        require(sidebar != nullptr, "window has navigation sidebar");
        const QSize originalSize = host->size();
        for (const QSize size : {QSize(1280, 800), QSize(1800, 1000), originalSize})
        {
            host->resize(size);
            processEventsFor(100);
            require(sidebar->width() == 62, "compact sidebar keeps default single-icon width after resize");
        }
    }
    requireSamePath(viewer->dataSelectionDirectory(),
                    menuRecordingDir.path(),
                    "data viewer uses the recording directory configured by the main menu");
    testFrameSliderThemeAndHover(window, *viewer);
    testOverviewLayout(*viewer);
    auto *minimizeButton = viewer->findChild<QToolButton *>(QStringLiteral("windowMinimizeButton"));
    require(minimizeButton != nullptr, "data viewer minimize button exists before reopen");
    minimizeButton->click();
    require(processEventsUntil(1000, [viewer]() {
                return viewer->isMinimized() ||
                       viewer->windowState().testFlag(Qt::WindowMinimized);
            }),
            "data viewer is minimized before reopen action");

    require(QMetaObject::invokeMethod(&window, "onOpenSessionViewerClicked", Qt::DirectConnection),
            "main window can invoke data viewer action while viewer is minimized");
    require(processEventsUntil(2000, [viewer]() {
                return viewer->isVisible() &&
                       !viewer->isMinimized() &&
                       !viewer->windowState().testFlag(Qt::WindowMinimized);
            }),
            "data viewer action restores minimized retained window");
    viewer->close();
    processEventsFor(200);

    require(QMetaObject::invokeMethod(&window, "onOpenSessionViewerClicked", Qt::DirectConnection),
            "main window can reopen data viewer after close");
    require(processEventsUntil(2000, []() {
                return visibleSessionViewerWindow() != nullptr;
            }),
            "data viewer reopens from retained main window instance");
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewMainWindowSessionViewerTest"));
    app.setApplicationName(QStringLiteral("main_window_session_viewer_test"));
    app.setProperty(VaporView::kAppDarkThemeProperty, false);
    app.setPalette(VaporView::appThemePalette(false));

    testMainWindowDataViewerOpenCanReopen();
    std::cout << "main window session viewer test passed\n";
    return 0;
}
