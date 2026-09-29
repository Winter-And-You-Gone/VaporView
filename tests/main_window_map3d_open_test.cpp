#include "ground/main/MainWindow.h"
#include "map3d/OsgEarthViewWidget.h"
#include "map3d/MapLanguage.h"

#include <QAction>
#include <QToolButton>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QDialog>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QOpenGLContext>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>

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

void processEventsFor(int timeoutMs)
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

QAction* findActionByText(QWidget* root, const QStringList& expectedTexts)
{
    const QList<QAction*> actions = root->findChildren<QAction*>();
    for (QAction* action : actions)
    {
        if (action && expectedTexts.contains(action->text()))
        {
            return action;
        }
    }
    return nullptr;
}

QWidget* findMap3DWindow()
{
    const QWidgetList topLevelWidgets = QApplication::topLevelWidgets();
    for (QWidget* widget : topLevelWidgets)
    {
        if (widget && widget->objectName() == QStringLiteral("map3DWindow"))
        {
            return widget;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), QStringLiteral("temporary settings directory"));
    qputenv("OSGEARTH_CACHE_PATH", settingsDir.filePath(QStringLiteral("map-cache")).toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewMap3DOpenTest"));
    app.setApplicationName(QStringLiteral("main_window_map3d_open_test"));

    const QString originalPath = QStringLiteral("  请求路径： X:/地图/会话%2.earth");
    const QString translatedPath = VaporView::Map3D::mapRenderedText(originalPath, true);
    require(translatedPath == QStringLiteral("  Requested path: X:/地图/会话%2.earth"),
            QStringLiteral("translation preserves Chinese paths and literal placeholders"));
    require(VaporView::Map3D::mapRenderedText(translatedPath, false) == originalPath,
            QStringLiteral("diagnostic path round trip is lossless"));

    MainWindow window;
    window.resize(1000, 700);
    window.show();
    processEventsFor(200);

    QAction* mapAction = findActionByText(&window,
                                          {QStringLiteral("三维地图"),
                                           QStringLiteral("3D Map")});
    require(mapAction != nullptr, QStringLiteral("3D map action exists"));

    QElapsedTimer triggerTimer;
    triggerTimer.start();
    mapAction->trigger();
    const qint64 triggerMs = triggerTimer.elapsed();
    require(triggerMs < 750,
            QStringLiteral("3D map action should return quickly, elapsed %1 ms").arg(triggerMs));
    processEventsFor(250);

    QWidget* mapWindow = findMap3DWindow();
    require(mapWindow != nullptr && mapWindow->isVisible(),
            QStringLiteral("3D map window opens from MainWindow"));

    QLabel* mapStatusLabel = mapWindow->findChild<QLabel*>(QStringLiteral("map3DStatusLabel"));
    require(mapStatusLabel != nullptr, QStringLiteral("3D map status label exists"));
    QLabel* renderPlaceholder = mapWindow->findChild<QLabel*>(
        QStringLiteral("map3DRenderPlaceholder"));
    require(renderPlaceholder != nullptr, QStringLiteral("render failure placeholder exists"));
    require(mapWindow->findChild<QAction*>(QStringLiteral("map3DStartRenderingAction")) == nullptr,
            QStringLiteral("manual start action is removed"));
    processEventsFor(3000);
    auto* view = mapWindow->findChild<VaporView::Map3D::OsgEarthViewWidget*>(
        QStringLiteral("map3DView"));
    require(view != nullptr && view->isVisible(),
            QStringLiteral("3D map starts the real renderer without closing the window"));
    require(view->isRenderingStarted(),
            QStringLiteral("3D map activates osgEarth automatically when opened"));
    require(!view->framebufferSize().isEmpty(),
            QStringLiteral("3D map initializes an OpenGL framebuffer after rendering starts"));
    require(view->earthLoadDiagnostics().attempted,
            QStringLiteral("3D map starts loading the built-in Earth scene after rendering starts"));

    auto* notice = mapWindow->findChild<QLabel*>(QStringLiteral("map3DNoticeBubble"));
    require(notice && !notice->isVisible(), QStringLiteral("imagery notice is hidden until needed"));
    emit view->imageryFallbackNotice();
    processEventsFor(100);
    require(notice->isVisible() && notice->text().contains(QStringLiteral("影像")),
            QStringLiteral("missing imagery shows a temporary notice above the real renderer"));
    require(notice->testAttribute(Qt::WA_TransparentForMouseEvents),
            QStringLiteral("imagery notice does not intercept map interaction"));
    mapWindow->resize(850, 650);
    processEventsFor(100);
    require(notice->parentWidget()->rect().contains(notice->geometry())
                && notice->y() == 12,
            QStringLiteral("imagery notice remains inside the top of the resized map"));
    // Repeated tile failures must not restart the lifetime of the bubble.
    for (int i = 0; i < 5; ++i)
    {
        processEventsFor(1000);
        emit view->imageryFallbackNotice();
    }
    require(!notice->isVisible(), QStringLiteral("notice expires even while tile failures continue"));

    emit view->renderingFailed(QStringLiteral("test renderer failure"));
    processEventsFor(100);
    require(mapWindow->isVisible() && renderPlaceholder->isVisible()
                && renderPlaceholder->text().contains(QStringLiteral("test renderer failure")),
            QStringLiteral("render failure leaves the window open with a diagnostic"));

    auto* diagnosticsAction = mapWindow->findChild<QAction*>(QStringLiteral("map3DDiagnosticsAction"));
    diagnosticsAction->trigger();
    processEventsFor(100);
    auto* diagnostics = mapWindow->findChild<QPlainTextEdit*>();
    QToolButton* languageButton = nullptr;
    for (auto* button : mapWindow->findChildren<QToolButton*>())
        if (button->accessibleName() == QStringLiteral("titleLanguageButton")
            && button->window() == mapWindow) languageButton = button;
    require(languageButton && diagnostics, QStringLiteral("map language button and diagnostics exist"));
    mapWindow->findChild<QAction*>(QStringLiteral("map3DDisplayAction"))->trigger();
    mapWindow->findChild<QAction*>(QStringLiteral("map3DMapFilesAction"))->trigger();
    mapWindow->findChild<QAction*>(QStringLiteral("map3DMapResourcesAction"))->trigger();
    processEventsFor(100);
    auto* metricCombo = mapWindow->findChild<QComboBox*>(QStringLiteral("map3DHeatMetricCombo"));
    const int originalMetric = metricCombo->currentIndex();
    const bool originalEnglish = qApp->property("vaporViewEnglish").toBool();
    for (int i = 0; i < 2; ++i) {
        languageButton->click();
        processEventsFor(300);
        const bool english = qApp->property("vaporViewEnglish").toBool();
        require(english == (i == 0 ? !originalEnglish : originalEnglish), QStringLiteral("map language button switches global language"));
        require(mapWindow->windowTitle() == (english ? QStringLiteral("VaporView 3D Map") : QStringLiteral("VaporView 三维地图")), QStringLiteral("map title switches immediately"));
        require(diagnosticsAction->text() == (english ? QStringLiteral("Map diagnostics") : QStringLiteral("地图诊断")), QStringLiteral("map menu switches immediately"));
        require(diagnostics->toPlainText().contains(english ? QStringLiteral("Track data:") : QStringLiteral("轨迹数据：")), QStringLiteral("open diagnostics switches immediately"));
        require(diagnostics->window()->windowTitle() == (english ? QStringLiteral("3D Map Diagnostics") : QStringLiteral("三维地图数据诊断")), QStringLiteral("diagnostics title is fully translated"));
        require(diagnostics->toPlainText().contains(english ? QStringLiteral("Terrain detail:") : QStringLiteral("地形细节：")), QStringLiteral("cached diagnostic details switch language"));
        require(mapWindow->findChild<QDialog*>(QStringLiteral("map3DDisplayPanel"))->windowTitle() == (english ? QStringLiteral("Display settings") : QStringLiteral("显示设置")), QStringLiteral("display settings title switches"));
        require(metricCombo->currentIndex() == originalMetric, QStringLiteral("language switch preserves selected heat metric"));
        require(metricCombo->itemText(0) == (english ? QStringLiteral("Peak") : QStringLiteral("峰值")), QStringLiteral("heat metric options switch language"));
        require(mapWindow->findChild<QTableWidget*>(QStringLiteral("map3DMapFilesTable"))->horizontalHeaderItem(0)->text() == (english ? QStringLiteral("Map files") : QStringLiteral("地图文件")), QStringLiteral("map file table switches language"));
        auto* buttons = diagnostics->window()->findChild<QDialogButtonBox*>();
        require(buttons->button(QDialogButtonBox::Close)->text() == (english ? QStringLiteral("Close") : QStringLiteral("关闭")), QStringLiteral("dialog close button switches immediately"));
        require(mapWindow->findChild<VaporView::Map3D::OsgEarthViewWidget*>(QStringLiteral("map3DView")) == view, QStringLiteral("language change preserves renderer"));
    }
    mapWindow->close();
    window.close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    processEventsFor(200);

    std::cout << "main_window_map3d_open_test passed\n";
    return 0;
}
