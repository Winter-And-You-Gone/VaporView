#include "SkySessionRecorder.h"
#include "ground/session/GroundRecordingService.h"
#include "shared/session/UnifiedRawDat.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

QByteArray rawPayload()
{
    return QByteArray(2 * static_cast<int>(sizeof(float)), '\0');
}

QByteArray harmonicPayload(int pointCount)
{
    return QByteArray(pointCount * static_cast<int>(sizeof(float)), '\0');
}

QJsonObject readManifest(const QString& sessionDirectory)
{
    QFile file(QDir(sessionDirectory).filePath(QStringLiteral("session.json")));
    require(file.open(QIODevice::ReadOnly), "open session manifest");
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    require(document.isObject(), "session manifest object");
    return document.object();
}

int manifestPoints(const QString& sessionDirectory)
{
    return readManifest(sessionDirectory)
        .value(QStringLiteral("waveform_points_per_frame"))
        .toInt(-1);
}

QString outputDirectory(const QTemporaryDir& root, const QString& name)
{
    const QString path = QDir(root.path()).filePath(name);
    require(QDir().mkpath(path), "create recorder output directory");
    return path;
}

QString recordGround(const QString& baseDirectory, int pointCount)
{
    VaporView::Ground::Session::GroundRecordingService recorder;
    VaporView::Ground::Session::GroundRecordingOptions options;
    options.baseDirectory = baseDirectory;
    options.exportRateHz = 20;
    VaporView::Ground::Session::GroundRecordingStartError startError{};
    QString errorMessage;
    require(recorder.start(options, &startError, &errorMessage), "start ground recorder");
    require(startError == VaporView::Ground::Session::GroundRecordingStartError::None,
            "ground recorder start error");

    if (pointCount > 0)
    {
        require(recorder.recordTcpWaveFrame(1000,
                                            rawPayload(),
                                            harmonicPayload(pointCount),
                                            VaporView::TcpFloatEncoding::LittleEndian),
                "record ground waveform");
    }

    const auto summary = recorder.stop();
    require(summary.hadOpenSession, "ground stop reports open session");
    return summary.sessionDirectory;
}

QString recordSky(const QString& baseDirectory, int pointCount)
{
    VaporView::SkySessionRecorder recorder;
    QString errorMessage;
    require(recorder.start(baseDirectory, QStringLiteral("COM50"), 921600, &errorMessage),
            "start sky recorder");
    if (pointCount > 0)
    {
        recorder.recordRawTcpWaveFrame(1000,
                                       rawPayload(),
                                       harmonicPayload(pointCount),
                                       VaporView::TcpFloatEncoding::LittleEndian);
    }
    require(recorder.stop(&errorMessage), "stop sky recorder");
    require(errorMessage.isEmpty(), "sky recorder stop error");
    return recorder.sessionDirectory();
}

void verifyRawWaveform(const QString& sessionDirectory, int pointCount)
{
    QFile file(QDir(sessionDirectory).filePath(QStringLiteral("raw/waveform.dat")));
    require(file.open(QIODevice::ReadOnly), "open waveform raw DAT");
    VaporView::SessionRawDat::RawScanOptions options;
    options.expectedSourceId = VaporView::SessionRawDat::kSourceWaveform;
    const auto result = VaporView::SessionRawDat::scan(file, options);
    require(result.success(), "scan waveform raw DAT");
    require(result.records.size() == (pointCount > 0 ? 1 : 0), "waveform raw record count");
    if (pointCount <= 0)
    {
        return;
    }

    const auto& record = result.records.first();
    require(record.header.sourceId == VaporView::SessionRawDat::kSourceWaveform,
            "waveform raw source id");
    require(record.header.recordType == VaporView::SessionRawDat::kRecordTypeWaveformPayload,
            "waveform raw record type");
    require((record.header.flags & VaporView::SessionRawDat::kWaveformCombinedPayloadFlag) != 0,
            "waveform raw combined flag");
    require(file.seek(static_cast<qint64>(record.payloadOffset)), "seek waveform raw payload");
    const QByteArray actualPayload = file.read(record.header.payloadSize);
    QByteArray expectedPayload;
    require(VaporView::SessionRawDat::encodeWaveformPayload(rawPayload(), harmonicPayload(pointCount), &expectedPayload),
            "encode expected waveform payload");
    require(actualPayload == expectedPayload, "waveform raw payload remains unchanged");
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporaryDirectory;
    require(temporaryDirectory.isValid(), "temporary directory");

    const QString ground40000 = recordGround(outputDirectory(temporaryDirectory, QStringLiteral("ground_40000")), 40000);
    const QString sky40000 = recordSky(outputDirectory(temporaryDirectory, QStringLiteral("sky_40000")), 40000);
    require(manifestPoints(ground40000) == 40000, "ground records non-default waveform point count");
    require(manifestPoints(sky40000) == 40000, "sky records non-default waveform point count");
    require(manifestPoints(ground40000) == manifestPoints(sky40000), "ground and sky point count consistency");
    verifyRawWaveform(ground40000, 40000);
    verifyRawWaveform(sky40000, 40000);

    const QString ground50000 = recordGround(outputDirectory(temporaryDirectory, QStringLiteral("ground_50000")), 50000);
    require(manifestPoints(ground50000) == 50000, "ground records 50000-point waveform");
    verifyRawWaveform(ground50000, 50000);

    const QString groundEmpty = recordGround(outputDirectory(temporaryDirectory, QStringLiteral("ground_empty")), 0);
    const QString skyEmpty = recordSky(outputDirectory(temporaryDirectory, QStringLiteral("sky_empty")), 0);
    require(manifestPoints(groundEmpty) == 0, "ground empty session uses zero waveform point count");
    require(manifestPoints(skyEmpty) == 0, "sky empty session uses zero waveform point count");
    require(manifestPoints(groundEmpty) == manifestPoints(skyEmpty), "empty ground and sky sessions are consistent");
    verifyRawWaveform(groundEmpty, 0);
    verifyRawWaveform(skyEmpty, 0);

    std::cout << "waveform_points_per_frame_test passed\n";
    return 0;
}
