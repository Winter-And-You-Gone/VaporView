#include "SkySessionRecorder.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
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
    recorder.recordWaveformSnapshot(1000, 900, QVector<float>{1.0f, 2.0f, 3.0f, 4.0f});

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

    QFile metadataFile(sessionDir + QStringLiteral("/session.json"));
    require(metadataFile.open(QIODevice::ReadOnly), "open metadata");
    const QJsonDocument metadata = QJsonDocument::fromJson(metadataFile.readAll());
    require(metadata.isObject(), "metadata object");
    const QJsonObject root = metadata.object();
    require(root.value(QStringLiteral("waveform_export_mode")).toString() == QStringLiteral("per_frame"),
            "per-frame waveform export mode");
    require(root.value(QStringLiteral("waveform_frames")).toString().toULongLong() == 1,
            "waveform frame count");
    require(root.value(QStringLiteral("waveform_file_count")).toString().toULongLong() == 1,
            "waveform file count");
    return 0;
}
