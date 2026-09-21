#include "ground/main/MainWindow.h"
#include "map3d/OsgEarthViewWidget.h"

#include <QAction>
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
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewMap3DOpenTest"));
    app.setApplicationName(QStringLiteral("main_window_map3d_open_test"));

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

    emit view->renderingFailed(QStringLiteral("test renderer failure"));
    processEventsFor(100);
    require(mapWindow->isVisible() && renderPlaceholder->isVisible()
                && renderPlaceholder->text().contains(QStringLiteral("test renderer failure")),
            QStringLiteral("render failure leaves the window open with a diagnostic"));

    mapWindow->close();
    window.close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    processEventsFor(200);

    std::cout << "main_window_map3d_open_test passed\n";
    return 0;
}
