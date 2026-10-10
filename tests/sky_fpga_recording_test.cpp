#include "SkySessionRecorder.h"
#include "FailingRecordingStorage.h"
#include "FpgaVlp1.h"
#include "shared/session/FpgaSessionArchive.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <thread>
#include <cstdlib>
#include <iostream>

using namespace VaporView;
namespace {
void require(bool ok, const char *message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
quint64 nowUs() { return quint64(QDateTime::currentMSecsSinceEpoch()) * 1000; }
QByteArray frame(quint16 source, quint16 message) {
    const auto bytes = FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Data, 1, source,
        message, {}, true, 0, 18446744073709551610ULL);
    return QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
}
void archiveRoundTrip() {
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary archive");
    SkySessionRecorder recorder;
    FpgaControlConfig configuration;
    configuration.recordSensors = false; configuration.recordDlia = false;
    recorder.setFpgaConfiguration(configuration);
    recorder.recordFpgaSnapshot(nowUs(), {{"event", "connected"}, {"serial", "simulated"}});
    QString error;
    require(recorder.start(temporary.path(), "", 0, &error), "start archive");
    const auto raw = frame(0x10, 0x1000);
    const auto dlia = frame(0x10, 0x1001);
    const auto sensor = frame(0x40, 0x1100);
    const auto event = frame(1, 0x1234);
    require(recorder.recordFpgaFrame(nowUs(), raw), "filtered RAW");
    require(recorder.recordFpgaFrame(nowUs(), dlia), "filtered DLIA");
    require(recorder.recordFpgaFrame(nowUs(), sensor), "filtered sensors");
    require(recorder.recordFpgaFrame(nowUs(), event), "unfiltered frame");
    const QByteArray usb = QByteArrayLiteral("garbage") + raw + dlia + sensor;
    require(recorder.recordFpgaUsbBytes(nowUs(), usb.left(9)), "partial USB first");
    require(recorder.recordFpgaUsbBytes(nowUs(), usb.mid(9)), "partial USB rest");
    require(recorder.recordFpgaCommand(nowUs(), QByteArrayLiteral("out")), "exact command");
    recorder.pause();
    const auto pausedCount = recorder.rawFpgaRecordCount();
    require(!recorder.recordFpgaUsbBytes(nowUs(), usb), "pause rejects capture");
    require(recorder.start(temporary.path(), "", 0, &error), "resume archive");
    require(recorder.recordFpgaSnapshot(nowUs(), {{"event", "disconnected"}}), "disconnect segment");
    require(recorder.stop(&error), "stop and drain archive");
    require(recorder.rawFpgaRecordCount() > pausedCount, "resume boundaries saved");
    int frames = 0, commands = 0, snapshots = 0;
    QByteArray recovered;
    bool configurationSaved = false, resumeSaved = false, pauseSaved = false;
    require(Session::FpgaSessionArchive::visit(recorder.sessionDirectory(),
        [&](const SessionRawDat::RawRecordHeader& h, const QByteArray& bytes) {
            require(h.sourceId == 8, "independent source8");
            if (h.recordType == 1) {
                ++frames; require(bytes == event, "frame options respected");
                const auto description = Session::FpgaSessionArchive::describe(h.hostTimestampUs,
                    Session::FpgaArchiveKind::Frame, bytes);
                require(description.value("fpga_tick").toString() == "18446744073709551610", "uint64 tick exact");
            }
            if (h.recordType == 2) { ++commands; require(bytes == "out", "OUT unchanged"); }
            if (h.recordType == 4) recovered += bytes;
            if (h.recordType == 3) {
                ++snapshots;
                const auto object = QJsonDocument::fromJson(bytes).object();
                configurationSaved |= object.contains("configuration") && object.value("serial").toString() == "simulated";
                resumeSaved |= object.value("event").toString() == "resume";
                pauseSaved |= object.value("event").toString() == "pause";
            }
            return true;
        }, &error), "read archive");
    require(frames == 1 && commands == 1 && snapshots >= 5, "record types");
    require(recovered == usb, "USB keeps filtered data and transfer boundaries");
    require(configurationSaved && resumeSaved && pauseSaved, "configuration and segments");
    QFile manifest(recorder.sessionDirectory() + "/session.json");
    require(manifest.open(QIODevice::ReadOnly), "open manifest");
    const auto object = QJsonDocument::fromJson(manifest.readAll()).object();
    require(object.value("raw_files").toObject().value("fpga_vlp1").toObject()
        .value("records").toString().toULongLong() == recorder.rawFpgaRecordCount(), "manifest count");
}
void failures() {
    for (bool overflow : {false, true}) {
        QTemporaryDir temporary;
        auto storage = std::make_shared<FailingRecordingStorage>();
        SkySessionRecorder recorder(storage);
        QString error;
        require(recorder.start(temporary.path(), "", 0, &error), "start failure case");
        if (overflow) {
            require(!recorder.recordFpgaUsbBytes(nowUs(), QByteArray(16 * 1024 * 1024, 'x')), "bounded queue rejection");
        } else {
            storage->failWrites = true;
            recorder.recordFpgaUsbBytes(nowUs(), QByteArrayLiteral("short write"));
        }
        // Error callback may log synchronously; drain/flush must not hold files_mutex_.
        recorder.setStorageFailureCallback([&] { LogRecord log; log.message = "storage failure"; recorder.appendEvent(log); });
        recorder.pause();
        require(recorder.storageFailed(), "failure surfaced");
        require(!recorder.stop(&error), "failure stop incomplete");
        QFile manifest(recorder.sessionDirectory() + "/session.json");
        require(manifest.open(QIODevice::ReadOnly), "open incomplete manifest");
        require(QJsonDocument::fromJson(manifest.readAll()).object().value("state").toString() == "incomplete", "incomplete manifest");
    }
}
void concurrentLifecycle() {
    QTemporaryDir temporary;
    SkySessionRecorder recorder;
    std::atomic_bool running{true};
    std::atomic_uint attempts{0};
    const auto bytes = frame(1, 0x1234);
    // Models DirectConnection delivery while the main thread resets the writer,
    // swaps configuration, pauses and seals successive sessions.
    std::thread producer([&] {
        while (running.load()) {
            recorder.recordFpgaUsbBytes(nowUs(), bytes);
            recorder.recordFpgaFrame(nowUs(), bytes);
            recorder.recordFpgaCommand(nowUs(), QByteArrayLiteral("out"));
            recorder.recordFpgaSnapshot(nowUs(), {{"event", "connected"}, {"generation", int(attempts.load())}});
            ++attempts;
            QThread::msleep(1);
        }
    });
    bool valid = true;
    QString error;
    for (int iteration = 0; iteration < 12 && valid; ++iteration) {
        valid = recorder.start(temporary.path(), "", 0, &error);
        if (!valid) break;
        FpgaControlConfig configuration;
        configuration.pressureSource = iteration % 2 ? 0x43 : 0x40;
        recorder.setFpgaConfiguration(configuration);
        QThread::msleep(2);
        recorder.pause();
        valid = recorder.start(temporary.path(), "", 0, &error);
        if (!valid) break;
        QThread::msleep(2);
        valid = recorder.stop(&error);
        if (!valid) break;
        const auto count = recorder.rawFpgaRecordCount();
        const auto directory = recorder.sessionDirectory();
        const auto index = Session::FpgaSessionArchive::scan(directory);
        valid = index.error.isEmpty() && quint64(index.records.size()) == count;
        // Subsequent direct deliveries must not append to a sealed session.
        QThread::msleep(2);
        valid = valid && recorder.rawFpgaRecordCount() == count;
    }
    running = false;
    producer.join();
    require(attempts.load() > 0, "concurrent callback delivery exercised");
    require(valid, "concurrent stop/restart preserves sealed archives");
}
}
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    archiveRoundTrip(); failures(); concurrentLifecycle();
    std::cout << "Sky FPGA archive passed\n";
    return 0;
}
