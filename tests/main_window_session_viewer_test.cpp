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
#include <QPushButton>
#include <QSettings>
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
    const QSize originalSize = window.size();
    const bool originalDark = VaporView::isDarkThemeEnabled();
    waveform->configureFrames(144783);
    waveform->setFrameValueSilently(74143);
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
            VaporViewTest::processEventsFor(100);
            QHoverEvent leave(QEvent::HoverLeave, QPointF(-1, -1), QPointF(-1, -1));
            QCoreApplication::sendEvent(slider, &leave);
            QEvent outside(QEvent::Leave);
            QCoreApplication::sendEvent(slider, &outside);
            const QColor accent = VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark);
            const auto capture = [&]() {
                VaporViewTest::processEventsFor(40);
                QImage image = slider->grab().toImage();
                QRect bounds;
                int pixels = 0;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x)
                        if (image.pixelColor(x, y).rgb() == accent.rgb())
                        {
                            ++pixels;
                            bounds |= QRect(x, y, 1, 1);
                        }
                return qMakePair(bounds, pixels);
            };
            const auto normal = capture();
            QStyleOptionSlider option;
            option.initFrom(slider);
            option.orientation = Qt::Horizontal;
            option.minimum = slider->minimum();
            option.maximum = slider->maximum();
            option.sliderPosition = slider->sliderPosition();
            option.sliderValue = slider->value();
            const QPoint center = slider->style()->subControlRect(QStyle::CC_Slider, &option,
                                                                 QStyle::SC_SliderHandle, slider).center();
            QEnterEvent enter(center, center, slider->mapToGlobal(center));
            QCoreApplication::sendEvent(slider, &enter);
            QHoverEvent hover(QEvent::HoverMove, center, QPointF(-1, -1));
            QCoreApplication::sendEvent(slider, &hover);
            const auto hovered = capture();
            QMouseEvent press(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(slider, &press);
            const auto pressed = capture();
            QMouseEvent release(QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(slider, &release);
            require(normal.second > 50 && hovered.second > normal.second * 1.3 && pressed.second >= hovered.second,
                    "frame slider retains its theme accent and grows on hover and press in the real application");
            for (const auto& rendered : {normal, hovered, pressed})
                require(std::abs(rendered.first.width() - rendered.first.height()) <= 2 &&
                            rendered.second < rendered.first.width() * rendered.first.height() * 0.85,
                        "frame slider handle remains circular in every mouse state and font scale");
        }
    }
    QAction scaleAction(&window);
    scaleAction.setData(100);
    QMetaObject::invokeMethod(&window, "onFontScaleTriggered", Qt::DirectConnection, Q_ARG(QAction *, &scaleAction));
    if (VaporView::isDarkThemeEnabled() != originalDark)
        QMetaObject::invokeMethod(&window, "onToggleTheme", Qt::DirectConnection);
    window.resize(originalSize);
    waveform->configureFrames(0);
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
