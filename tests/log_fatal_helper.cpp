#include "LogService.h"

#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2)
    {
        return 2;
    }

    VaporView::LogService service(QStringLiteral("VaporViewFatalTest"),
                                  &app,
                                  app.arguments().at(1));
    service.installQtMessageHandler();
    qFatal("fatal-persistence-sentinel");
    return 0;
}
