#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <QIcon>
#include <QDir>
#include <QFileInfo>
#include <QStyleFactory>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("VaporView");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("VaporView");
    app.setStyle(QStyleFactory::create("Fusion"));

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList iconCandidates = {
        QDir(appDir).filePath("resources/app.ico"),
        QDir(appDir).filePath("../resources/app.ico"),
        QDir(appDir).filePath("../../resources/app.ico")
    };

    QIcon windowIcon;
    for (const QString& path : iconCandidates)
    {
        if (QFileInfo::exists(path))
        {
            windowIcon = QIcon(path);
            if (!windowIcon.isNull())
            {
                break;
            }
        }
    }
    if (!windowIcon.isNull())
    {
        app.setWindowIcon(windowIcon);
    }

    MainWindow mainWindow;
    mainWindow.setWindowTitle("VaporView");
    if (!app.windowIcon().isNull())
    {
        mainWindow.setWindowIcon(app.windowIcon());
    }
    mainWindow.resize(1024, 768);
    mainWindow.show();

    return app.exec();
}

