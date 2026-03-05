#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("VaproView");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("VaproView");

    MainWindow mainWindow;
    mainWindow.setWindowTitle("VaproView");
    mainWindow.resize(1024, 768);
    mainWindow.show();

    return app.exec();
}
