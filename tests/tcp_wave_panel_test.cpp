#include "TcpWavePanel.h"

#include <QApplication>
#include <QByteArray>
#include <QSettings>
#include <QTemporaryDir>
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

void testInvalidTcpStreamDoesNotGrowBacklog()
{
    TcpWavePanel panel;
    panel.setEnglish(true);

    const QByteArray invalidChunk(8192, static_cast<char>(0x7f));
    for (int i = 0; i < 1024; ++i)
    {
        panel.testFeedSocketBytes(invalidChunk);
    }

    require(panel.testBufferedByteCount() <= 3, "invalid TCP stream backlog should stay bounded");
}

}  // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewTcpWavePanelTest"));
    app.setApplicationName(QStringLiteral("tcp_wave_panel_test"));

    testInvalidTcpStreamDoesNotGrowBacklog();
    std::cout << "tcp_wave_panel_test passed\n";
    return 0;
}
