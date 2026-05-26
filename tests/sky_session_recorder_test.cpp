#include "SkySessionRecorder.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTemporaryDir>
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
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("sky_session_recorder_test"));
    app.setApplicationVersion(QStringLiteral("test"));

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary recording directory");

    VaporView::SkySessionRecorder recorder;
    QString error;
    require(recorder.start(tempDir.path(), QStringLiteral("COM50"), 921600, &error), "start recorder");
    require(error.isEmpty(), "start recorder error text");
    require(recorder.isRecording(), "recorder state");
    require(!recorder.sessionName().isEmpty(), "session name");

    const QString sessionDir = recorder.sessionDirectory();
    require(QFileInfo::exists(sessionDir), "session directory");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/session.json")), "session metadata");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/sensors/devices.csv")), "devices csv");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/waveform_features.csv")), "waveform features csv");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/waveform_index.csv")), "waveform index csv");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/raw/epsilon.dat")), "epsilon raw dat");
    require(QFileInfo::exists(sessionDir + QStringLiteral("/raw/tcp_wave.dat")), "tcp wave raw dat");

    recorder.stop();
    require(!recorder.isRecording(), "recorder stopped");
    return 0;
}
