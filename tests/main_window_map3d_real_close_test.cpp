#include "MainWindow.h"
#include "map3d/OsgEarthViewWidget.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>

#include <cstdlib>
#include <iostream>

namespace {

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
    const QDir sourceRoot(QStringLiteral(VAPORVIEW_SOURCE_DIR));
    const QString earthPath =
        sourceRoot.filePath(QStringLiteral("resources/maps/vaporview_real3d_local.earth"));
    const QString tilesetPath =
        sourceRoot.filePath(QStringLiteral("resources/maps/tiles3d/local/tileset.json"));

    if (!QFileInfo::exists(earthPath) || !QFileInfo::exists(tilesetPath))
    {
        std::cout << "SKIP: Hangzhou Xihu real-3D local data is not installed\n";
        return 77;
    }

    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), QStringLiteral("temporary settings directory"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewMainWindowMap3DRealCloseTest"));
    app.setApplicationName(QStringLiteral("main_window_map3d_real_close_test"));

    auto* window = new MainWindow;
    window->resize(1000, 700);
    window->show();
    processEventsFor(250);

    QAction* mapAction = findActionByText(window,
                                          {QStringLiteral("三维地图..."),
                                           QStringLiteral("3D Map...")});
    require(mapAction != nullptr, QStringLiteral("3D map action exists"));
    mapAction->trigger();
    processEventsFor(250);

    QWidget* mapWindow = findMap3DWindow();
    require(mapWindow != nullptr && mapWindow->isVisible(), QStringLiteral("3D map window opens from MainWindow"));

    auto* view = mapWindow->findChild<VaporView::Map3D::OsgEarthViewWidget*>(QStringLiteral("map3DView"));
    require(view != nullptr, QStringLiteral("real OSG/osgEarth 3D view exists in MainWindow path"));

    bool loaded = false;
    const qint64 loadDeadline = QDateTime::currentMSecsSinceEpoch() + 20000;
    while (QDateTime::currentMSecsSinceEpoch() < loadDeadline)
    {
        processEventsFor(100);
        const VaporView::Map3D::Local3DTilesLoadDiagnostics tileDiagnostics =
            view->local3DTilesLoadDiagnostics();
        if (view->hasEarthMap() && tileDiagnostics.loaded)
        {
            loaded = true;
            break;
        }
    }

    require(loaded, QStringLiteral("MainWindow path loads the Xihu earth map and building overlay"));
    const VaporView::Map3D::Local3DTilesLoadDiagnostics tileDiagnostics =
        view->local3DTilesLoadDiagnostics();
    require(tileDiagnostics.loadedPayloadCount == 55,
            QStringLiteral("MainWindow path loads all 55 building payloads, got %1")
                .arg(tileDiagnostics.loadedPayloadCount));

    processEventsFor(3000);
    window->close();
    processEventsFor(250);
    delete window;
    processEventsFor(500);

    std::cout << "main_window_map3d_real_close_test passed: MainWindow opened and closed real 3D map\n";
    return 0;
}
