#include "RtcmLinkTracker.h"
#include "SkyRuntime.h"
#include "LogService.h"
#include "ground/devices/RemoteSkyController.h"
#include "ground/devices/RtcmStatusModel.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace VaporView;
using namespace VaporView::Ground::Devices;

static void require(bool value, const char *message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

template<class Predicate> static bool waitUntil(Predicate predicate)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < 3000) QCoreApplication::processEvents();
    return predicate();
}

static TelemetryStatus status(quint64 bytes, quint64 reportUs, quint64 lastUs)
{
    TelemetryStatus result;
    result.rtcm_observability_version = 1;
    result.rtcm_boot_id = 1;
    result.rtcm_forward_enabled = 1;
    result.rtcm_report_time_us = reportUs;
    result.rtcm_correction_last_receive_time_us = lastUs;
    result.rtcm_correction_bytes_received = bytes;
    result.rtcm_correction_chunks_received = bytes / 100;
    result.rtcm_link_stream_id = 1;
    result.rtcm_link_frames_received = bytes / 100;
    return result;
}

static void testModel()
{
    RtcmStatusModel model;
    model.setContext(true, true);
    require(model.snapshot(true, 0).health == RtcmHealth::Waiting, "wait for first Sky status");
    auto current = status(0, 1000000, 0);
    model.receiveStatus(current, 0);
    require(model.snapshot(true, 0).ageMs == -1, "never received has no fabricated age");
    current = status(1600, 2000000, 1915000);
    model.receiveStatus(current, 1000);
    auto state = model.snapshot(true, 1000);
    require(state.health == RtcmHealth::Normal && state.ageMs == 85, "fresh arrival is Normal without synchronized clocks");
    require(std::abs(state.bytesPerSecond - 1600) < 1e-9, "counter delta yields 1.6 kB/s");
    require(state.lossAvailable && state.lossPercent == 0, "continuous RTCM has measured zero loss");
    for (const auto& test : {std::make_pair(1000, RtcmHealth::Warning),
                            std::make_pair(3000, RtcmHealth::Delayed),
                            std::make_pair(5000, RtcmHealth::Interrupted)})
    {
        current.rtcm_report_time_us = 1915000 + static_cast<quint64>(test.first) * 1000;
        model.receiveStatus(current, 1000 + test.first / 2);
        require(model.snapshot(true, 1000 + test.first / 2).health == test.second, "exact age threshold changes health");
    }
    current = status(1800, 8000000, 8000000);
    model.receiveStatus(current, 4000);
    require(model.snapshot(true, 4000).health == RtcmHealth::Normal, "new RTCM restores Normal");
    for (int i = 1; i <= 6; ++i)
    {
        current.rtcm_report_time_us += 1000000;
        model.receiveStatus(current, 4000 + i * 1000);
    }
    require(model.snapshot(true, 10000).bytesPerSecond == 0, "quiet stream decays to zero within rate window");
    current = status(0, 16000000, 0);
    model.receiveStatus(current, 11000);
    require(model.snapshot(true, 11000).bytesPerSecond == 0 && !model.snapshot(true, 11000).lossAvailable,
            "counter reset cannot create a rate or loss spike");
    current = status(100, 17000000, 17000000);
    current.rtcm_correction_dropped_chunks = 1;
    current.rtcm_correction_dropped_bytes = 100;
    current.rtcm_link_frames_lost = 1;
    model.receiveStatus(current, 12000);
    state = model.snapshot(true, 12000);
    require(state.droppedChunks == 1 && state.droppedBytes == 100 && state.recentDroppedChunks == 1,
            "Sky local drops reach the Ground model separately");
    require(state.lossAvailable && state.lossPercent == 50 && state.health == RtcmHealth::Warning,
            "link loss denominator includes received and lost frames");
    state = model.snapshot(false, 12100);
    require(state.health == RtcmHealth::LinkDisconnected && state.ageMs == 100 &&
                state.bytesPerSecond == 0 && !state.lossAvailable && state.droppedChunks == 1,
            "disconnect retains diagnostic counters and advancing age but disables rate and loss");
    require(model.snapshot(true, 16001).health == RtcmHealth::LinkDisconnected,
            "stale status detects silent serial/TCP link even when handle remains open");
    model.reconnect();
    require(model.snapshot(true, 17000).health == RtcmHealth::Waiting, "reconnect waits for fresh status");
    current.rtcm_boot_id = 2;
    current.rtcm_correction_bytes_received = 90000000;
    model.receiveStatus(current, 17000);
    require(model.snapshot(true, 17000).bytesPerSecond == 0, "reconnect baselines cumulative counters");
    current.rtcm_boot_id = 3;
    current.rtcm_correction_bytes_received++;
    model.receiveStatus(current, 18000);
    require(model.snapshot(true, 18000).bytesPerSecond == 0, "boot identity rebases even without a counter decrease");
    current.rtcm_correction_bytes_received = std::numeric_limits<quint64>::max();
    current.rtcm_boot_id++;
    model.receiveStatus(current, 19000);
    current.rtcm_correction_bytes_received = 0;
    model.receiveStatus(current, 20000);
    require(model.snapshot(true, 20000).bytesPerSecond == 0, "u64 counter wrap is a reset");
    model.setContext(false, true);
    state = model.snapshot(true, 20000);
    require(state.health == RtcmHealth::Local && !state.available && !state.lossAvailable,
            "Local mode exposes no fictional Sky measurements");
    model.setContext(true, false);
    require(model.snapshot(true, 20000).health == RtcmHealth::Disabled, "RTK stop disables monitoring");
    model.setContext(true, true);
    require(model.snapshot(true, 20000).bytesPerSecond == 0, "RTK restart clears window");
    current.rtcm_forward_enabled = 0;
    model.receiveStatus(current, 21000);
    require(model.snapshot(true, 21000).health == RtcmHealth::Disabled, "Sky config disable is reflected");
    current.rtcm_forward_enabled = 1;
    current.rtcm_link_stream_id = 2;
    model.receiveStatus(current, 22000);
    require(!model.snapshot(true, 22000).lossAvailable, "stream/config changes rebase windows");
    current.rtcm_observability_version = 0;
    model.receiveStatus(current, 23000);
    require(model.snapshot(true, 23000).health == RtcmHealth::StatusUnavailable,
            "old status never fabricates health or link loss");

    RtcmStatusModel slow;
    slow.setContext(true, true);
    current = status(100, 1000000, 1000000);
    current.status_rate_hz = 0.2f;
    slow.receiveStatus(current, 0);
    require(slow.snapshot(true, 8000).health == RtcmHealth::Interrupted,
            "slow configured status cadence is not misclassified as link disconnect");
    require(slow.snapshot(true, 16000).health == RtcmHealth::LinkDisconnected,
            "link deadline adapts to three configured status periods");

    // Loss events leave the recent window even when cumulative losses remain.
    RtcmStatusModel recent;
    recent.setContext(true, true);
    current = status(0, 1000000, 1000000);
    recent.receiveStatus(current, 0);
    for (int i = 1; i <= 12; ++i)
    {
        current.rtcm_link_frames_received += 20;
        current.rtcm_link_frames_lost = 2;
        current.rtcm_report_time_us += 1000000;
        current.rtcm_correction_last_receive_time_us = current.rtcm_report_time_us;
        recent.receiveStatus(current, i * 1000);
        if (i == 1) require(recent.snapshot(true, 1000).lossWarning, "significant recent link loss marks Warning");
    }
    require(recent.snapshot(true, 12000).lossAvailable && recent.snapshot(true, 12000).lossPercent == 0 &&
            recent.snapshot(true, 12000).linkFramesLost == 2,
            "recent link loss recovers to measured zero independently of cumulative losses");
}

static void testCodecAndSky()
{
    const QByteArray correction = QByteArray::fromHex("D30000123456");
    QByteArray data;
    RtcmFrameSequence parsed;
    const auto extended = TelemetryCodec::serializeRtcmCorrectionData(correction, {9, 15});
    require(TelemetryCodec::parseRtcmCorrectionData(extended, data, &parsed) &&
            data == correction && parsed.stream_id == 9 && parsed.sequence == 15, "RTCM metadata round trip preserves exact correction bytes");
    require(!TelemetryCodec::parseRtcmCorrectionData(extended.chopped(1), data, &parsed), "truncated RTCM metadata is rejected");
    require(TelemetryCodec::parseRtcmCorrectionData(TelemetryCodec::serializeRtcmCorrectionData(correction), data, &parsed) &&
            parsed.stream_id == 0, "new decoder accepts legacy RTCM without sequence");
    auto input = status(100, 2000000, 1000000);
    input.rtcm_link_frames_lost = 7;
    TelemetryStatus output;
    const auto payload = TelemetryCodec::serializeTelemetryStatus(input);
    require(TelemetryCodec::parseTelemetryStatus(payload, output) &&
            output.rtcm_link_frames_lost == 7 && output.rtcm_boot_id == 1 && output.rtcm_forward_enabled == 1,
            "status observability extension round trip");
    require(!TelemetryCodec::parseTelemetryStatus(payload.chopped(1), output), "partial status extension is rejected");
    require(TelemetryCodec::parseTelemetryStatus(payload.chopped(56), output) && output.rtcm_observability_version == 0,
            "legacy status clears previously parsed extension");

    QTemporaryDir directory;
    SkyRuntimeOptions options;
    options.simulate_data = true;
    options.telemetry_tcp_port = 0;
    options.config_path = directory.filePath(QStringLiteral("sky.json"));
    SkyRuntime sky(options);
    require(sky.start(), "start isolated Sky runtime");
    TelemetryCodec codec;
    const auto inject = [&](MsgType type, const QByteArray& bytes, quint16 headerSeq) {
        const QByteArray frame = codec.encodeFrame(type, bytes, headerSeq, 100);
        require(QMetaObject::invokeMethod(&sky, "onBytesReceived", Qt::DirectConnection,
                Q_ARG(QByteArray, frame)), "deliver decoded transport bytes to Sky");
    };
    inject(MsgType::RtcmCorrectionData, TelemetryCodec::serializeRtcmCorrectionData(correction), 1);
    inject(MsgType::RtcmCorrectionData, TelemetryCodec::serializeRtcmCorrectionData(correction), 2);
    require(sky.currentStatus().rtcm_link_frames_received == 2 && sky.currentStatus().rtcm_link_stream_id == 0 &&
            sky.currentStatus().rtcm_link_frames_lost == 0,
            "legacy RTCM arrivals accumulate without claiming measurable continuity");
    inject(MsgType::RtcmCorrectionData, TelemetryCodec::serializeRtcmCorrectionData(correction, {1, 1}), 10);
    inject(MsgType::Heartbeat, {}, 11);
    inject(MsgType::TelemetryBasic, {}, 100);
    inject(MsgType::RtcmCorrectionData, TelemetryCodec::serializeRtcmCorrectionData(correction, {1, 2}), 101);
    require(sky.currentStatus().rtcm_link_frames_received == 2 && sky.currentStatus().rtcm_link_frames_lost == 0,
            "other MsgTypes and global header gaps do not count as RTCM loss");
    inject(MsgType::RtcmCorrectionData, TelemetryCodec::serializeRtcmCorrectionData(correction, {1, 4}), 105);
    auto skyStatus = sky.currentStatus();
    require(skyStatus.rtcm_link_frames_received == 3 && skyStatus.rtcm_link_frames_lost == 1 &&
            skyStatus.rtcm_correction_bytes_received == static_cast<quint64>(correction.size() * 5),
            "missing RTCM sequence is link loss independently of local byte counters");
    inject(MsgType::RtcmCorrectionData, TelemetryCodec::serializeRtcmCorrectionData(correction, {1, 4}), 106);
    require(sky.currentStatus().rtcm_link_frames_received == 3, "duplicate sequence is not another unique link frame");
    inject(MsgType::RtcmCorrectionData, TelemetryCodec::serializeRtcmCorrectionData(correction, {2, 1000}), 2);
    require(sky.currentStatus().rtcm_link_frames_lost == 0 && sky.currentStatus().rtcm_link_frames_received == 1,
            "new Ground stream begins at observed baseline without false loss");
    inject(MsgType::RtcmCorrectionData, TelemetryCodec::serializeRtcmCorrectionData(correction, {2, 1}), 3);
    require(sky.currentStatus().rtcm_link_frames_lost == 0, "sequence regression safely resets continuity");
    const auto bootId = sky.currentStatus().rtcm_boot_id;
    sky.stop();
    require(sky.start() && sky.currentStatus().rtcm_boot_id != bootId && sky.currentStatus().rtcm_link_frames_received == 0,
            "Sky restart changes boot identity and clears link baseline");
    sky.stop();

    SkyDeviceManager manager;
    CommandErrorCode error;
    require(!manager.receiveRtcmCorrectionData(correction, &error), "disabled forwarding drops received RTCM locally");
    const auto local = manager.rtcmCorrectionStats();
    require(local.bytes_received == static_cast<quint64>(correction.size()) && local.chunks_received == 1 &&
            local.dropped_chunks == 1 && local.dropped_bytes == local.bytes_received && local.last_receive_time_us != 0,
            "disabled/invalid forwarding still counts arrival plus separate local drop");
}

static void testGroundNegotiation()
{
    QTemporaryDir logDirectory;
    QStringList events;
    // LogService publishes its shutdown event from its destructor, so the
    // signal collector must outlive it as well as the test's controllers.
    LogService logs(QStringLiteral("RtcmObservabilityTest"), nullptr, logDirectory.path(), logDirectory.path());
    QObject::connect(&logs, &LogService::recordPublished, [&](const LogRecord& record) {
        events.append(record.fields.value(QStringLiteral("event")).toString());
    });
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "start isolated fake Sky peer");
    RemoteSkyController controller;
    require(controller.openTcp(QStringLiteral("127.0.0.1"), server.serverPort()), "Ground opens telemetry TCP");
    require(waitUntil([&] { return server.hasPendingConnections(); }), "accept Ground peer");
    auto peer = std::unique_ptr<QTcpSocket>(server.nextPendingConnection());
    GroundTelemetryService *service = controller.telemetryService();
    TelemetryCodec codec;
    const QByteArray correction("RTCM");
    auto readFrame = [&]() {
        QVector<TelemetryFrame> frames;
        require(waitUntil([&] { frames += codec.feedBytes(peer->readAll()); return !frames.isEmpty(); }), "read Ground frame");
        return frames.front();
    };
    require(controller.sendRtcmCorrectionData(correction), "legacy send before capabilities");
    auto frame = readFrame();
    QByteArray data;
    RtcmFrameSequence first;
    require(TelemetryCodec::parseRtcmCorrectionData(frame.payload, data, &first) && first.stream_id == 0,
            "older Sky gets original wire format");
    auto current = status(0, 1000000, 0);
    current.rtcm_correction_dropped_chunks = 100;
    const auto report = codec.encodeFrame(MsgType::TelemetryStatus, TelemetryCodec::serializeTelemetryStatus(current), 1, 1000000);
    peer->write(report);
    require(waitUntil([&] { return controller.lastStatusMs() != 0; }), "Ground consumes real capability/status over TCP");
    controller.rtcmStatus(true, true);
    require(!events.contains(QStringLiteral("rtcm_sky_drops_increased")),
            "first cumulative drop count establishes diagnostic baseline without an alarm");
    require(controller.sendRtcmCorrectionData(correction), "extended RTCM send after advertised support");
    frame = readFrame();
    require(TelemetryCodec::parseRtcmCorrectionData(frame.payload, data, &first) && first.stream_id != 0,
            "Ground uses RTCM sequence only with supporting Sky");
    controller.sendCommand(CommandId::RequestStatus);
    require(readFrame().type == MsgType::Command, "command interleaves on same transport");
    require(controller.sendRtcmCorrectionData(correction), "send another RTCM");
    RtcmFrameSequence second;
    require(TelemetryCodec::parseRtcmCorrectionData(readFrame().payload, data, &second) &&
            second.stream_id == first.stream_id && second.sequence == first.sequence + 1,
            "RTCM sequence is independent of command frames");
    current.rtcm_correction_dropped_chunks = 4;
    current.rtcm_report_time_us += 1000000;
    peer->write(codec.encodeFrame(MsgType::TelemetryStatus, TelemetryCodec::serializeTelemetryStatus(current), 2, 2000000));
    require(waitUntil([&] { return controller.rtcmStatus(true, true).droppedChunks == 4; }), "drop counters survive Ground service/controller delivery");
    controller.close();
    require(controller.rtcmStatus(true, true).health == RtcmHealth::LinkDisconnected, "closed real transport immediately disconnects health");
    require(controller.openTcp(QStringLiteral("127.0.0.1"), server.serverPort()), "reopen Ground TCP");
    require(waitUntil([&] { return server.hasPendingConnections(); }), "accept reconnected Ground peer");
    peer.reset(server.nextPendingConnection());
    require(waitUntil([&] { return controller.rtcmStatus(true, true).health == RtcmHealth::Waiting; }), "reconnect clears cached health");
    current.rtcm_boot_id++;
    current.rtcm_correction_dropped_chunks = 100000;
    peer->write(codec.encodeFrame(MsgType::TelemetryStatus,
        TelemetryCodec::serializeTelemetryStatus(current), 1, current.rtcm_report_time_us));
    require(waitUntil([&] { return controller.rtcmStatus(true, true).available; }), "capability reacquired after reconnect");
    require(!events.contains(QStringLiteral("rtcm_sky_drops_increased")),
            "reconnect/new Sky baseline cannot trigger false drop growth alarms");
    require(controller.sendRtcmCorrectionData(correction), "reconnected RTCM send");
    require(TelemetryCodec::parseRtcmCorrectionData(readFrame().payload, data, &second) && second.stream_id != first.stream_id,
            "reconnect rotates Ground stream identity");
    controller.close();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testModel();
    testCodecAndSky();
    testGroundNegotiation();
    std::cout << "rtcm_observability_test passed\n";
}
