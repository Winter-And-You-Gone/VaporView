#include "FpgaDeviceSession.h"
#include "FpgaUartExchange.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace VaporView;
using namespace VaporView::Ground::Devices;

namespace {
void require(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
quint32 word(const std::vector<std::uint8_t>& bytes, size_t offset)
{
    return quint32(bytes[offset]) | (quint32(bytes[offset + 1]) << 8)
        | (quint32(bytes[offset + 2]) << 16) | (quint32(bytes[offset + 3]) << 24);
}
void append16(QByteArray& bytes, quint16 value)
{
    bytes.append(char(value)); bytes.append(char(value >> 8));
}
void append32(QByteArray& bytes, quint32 value)
{
    for (int i = 0; i < 4; ++i) bytes.append(char(value >> (8 * i)));
}
QByteArray response(const FpgaVlp1::Frame& command, const QByteArray& payload)
{
    const auto bytes = FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Response,
        command.header.sequence, command.header.source, command.header.message,
        std::vector<std::uint8_t>(payload.begin(), payload.end()));
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
}

// A wire-level UART bridge model: the test speaks actual VLP frames, and its
// FIFO pop, delayed UART bytes and acknowledgement order are independent of
// the exchange implementation's state transitions.
class UartTransport final : public FpgaUsbTransport
{
public:
    bool open(const QString&) override { opened = true; return true; }
    void close() override { opened = false; }
    bool isOpen() const override { return opened; }
    FpgaUsbDiagnostic diagnostic() const override { return {}; }
    qint64 read(QByteArray& bytes, qint64 maxBytes) override
    {
        if (readChunkBytes > 0) maxBytes = qMin(maxBytes, readChunkBytes);
        bytes = incoming.left(maxBytes); incoming.remove(0, bytes.size()); return bytes.size();
    }
    qint64 write(const QByteArray& bytes) override
    {
        const auto commands = parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()), size_t(bytes.size()));
        QVector<QByteArray> replies;
        bool transmitted = false;
        int txFrames = 0;
        for (const auto& command : commands) {
            QByteArray payload; append32(payload, 0);
            const quint32 address = command.payload.size() >= 4 ? word(command.payload, 0) : 0;
            if (command.header.message == 2) {
                const quint16 count = quint16(command.payload[4]) | (quint16(command.payload[5]) << 8);
                append32(payload, address); append16(payload, count); append16(payload, 0);
                for (quint16 i = 0; i < count; ++i) append32(payload, readRegister(address + 4 * i));
                if (dropPopReply && (address == 0x681c || address == 0x691c)) continue;
            } else if (command.header.message == 3) {
                require(address == 0x6818 || address == 0x6918, "exchange never changes UART or instrument configuration");
                require(command.payload[4] == 1 && command.payload[5] == 0, "FIFO writes contain exactly one register");
                lastBase = address & ~255u;
                tx.append(char(word(command.payload, 8))); transmitted = true; ++txFrames;
                if (dropTxAck) continue;
            } else if (command.header.message == 0xfe) {
                payload.append(reinterpret_cast<const char*>(command.payload.data()), qsizetype(command.payload.size()));
            } else if (command.header.message == 12) {
                ++motorSafetyWrites;
            }
            replies.append(response(command, payload));
        }
        if (txFrames) { ++txTransfers; maxTxFrames = qMax(maxTxFrames, txFrames); }
        qint64 replyBytes = 0;
        for (const auto& reply : replies) replyBytes += reply.size();
        // The RTL transport TX FIFO holds 2048 32-bit words. This deliberately
        // strict model rejects an OUT whose ACKs cannot fit before the host
        // next drains IN; it does not emulate additional FX3 buffering or
        // silently grow an unbounded ACK queue under backpressure.
        if (responseCapacityBytes > 0 && incoming.size() + replyBytes > responseCapacityBytes) {
            responseCapacityExceeded = true;
            return -1;
        }
        // Return burst ACKs in reverse order to exercise matching by seq.
        for (auto i = replies.crbegin(); i != replies.crend(); ++i) incoming.append(*i);
        maxQueuedResponseBytes = qMax(maxQueuedResponseBytes, qint64(incoming.size()));
        generatedResponseBytes += replyBytes;
        if (transmitted && !suppressResponse) {
            const auto reply = deviceReply;
            QTimer::singleShot(replyDelayMs, this, [this, reply] {
                if (newFramingError) ++framingCount;
                for (char byte : reply) fifo.append(quint32(quint8(byte)));
            });
            if (!secondReply.isEmpty()) {
                const auto tail = secondReply;
                QTimer::singleShot(replyDelayMs + 8, this, [this, tail] {
                    for (char byte : tail) fifo.append(quint32(quint8(byte)));
                });
            }
        }
        return bytes.size();
    }
    quint32 readRegister(quint32 address)
    {
        const auto base = address & ~255u;
        switch (address - base) {
        case 0x00: return base == 0x6800 ? 0x00480100 : 0x00490100;
        case 0x04: return 1;
        case 0x08: return 3 | (fifo.isEmpty() ? 0 : 0x200) | (forceStatusError ? 0x80 : 0);
        case 0x10: return baud;
        case 0x14: return 0x108;
        case 0x1c: {
            ++popCount;
            require(!fifo.isEmpty(), "RX register is never popped when empty");
            const auto byte = fifo.takeFirst(); return byte | (badRxByte ? 0x100 : 0);
        }
        case 0x24: return quint32(fifo.size());
        case 0x38: return framingCount;
        case 0x3c: return 256;
        default: return 0;
        }
    }
    FpgaVlp1::StreamParser parser;
    QByteArray incoming, tx;
    QByteArray deviceReply = QByteArray("OK:FPV=4370@\r\n\0", 15), secondReply;
    QVector<quint32> fifo;
    quint32 lastBase = 0, baud = 38400, framingCount = 0;
    qint64 responseCapacityBytes = 0, readChunkBytes = 0;
    qint64 maxQueuedResponseBytes = 0, generatedResponseBytes = 0;
    int popCount = 0, txTransfers = 0, maxTxFrames = 0, motorSafetyWrites = 0, replyDelayMs = 4;
    bool opened = false, suppressResponse = false, dropTxAck = false;
    bool dropPopReply = false, forceStatusError = false, badRxByte = false, newFramingError = false;
    bool responseCapacityExceeded = false;
};

struct Fixture {
    UartTransport* wire;
    FpgaDeviceSession session;
    FpgaUartExchange exchange;
    Fixture() : Fixture(std::make_unique<UartTransport>()) {}
    explicit Fixture(std::unique_ptr<UartTransport> transport)
        : wire(transport.get()), session(std::move(transport)), exchange(&session)
    { require(session.open(), "open UART fixture"); }
};
QJsonObject request(int channel = 1, int timeout = 500)
{
    return {{"op", "uart_exchange"}, {"channel", channel},
        {"data", QString::fromLatin1(QByteArray("FPV=?@").toBase64())},
        {"timeout_ms", timeout}, {"max_rx", 4096}};
}
QJsonObject run(Fixture& fixture, const QJsonObject& operation)
{
    QJsonObject result;
    QEventLoop loop;
    const auto connection = QObject::connect(&fixture.exchange, &FpgaUartExchange::finished,
        &loop, [&](const QJsonObject& value) { result = value; loop.quit(); });
    require(fixture.exchange.start(operation), "start UART exchange");
    // Match the explicit hardware-readonly probe's event pumping, including
    // its yield between processEvents calls instead of a nested exec loop.
    QElapsedTimer deadline;
    deadline.start();
    while (result.isEmpty() && deadline.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    QObject::disconnect(connection);
    require(!result.isEmpty(), "UART exchange terminates within its deadline");
    return result;
}
void exactResponseAndBurst()
{
    Fixture f;
    f.wire->deviceReply = "OK:FPV=4370@";
    f.wire->secondReply = QByteArray("\r\n\0", 3);
    const auto result = run(f, request(2));
    require(result.value("ok").toBool(), "delayed multi-part UART reply succeeds");
    require(QByteArray::fromBase64(result.value("data").toString().toLatin1())
        == QByteArray("OK:FPV=4370@\r\n\0", 15), "raw response retains optional colon, CRLF and trailing NUL");
    require(f.wire->lastBase == 0x6900 && f.wire->tx == "FPV=?@", "physical channel 2 selects LC1 without editing instrument bytes");
    require(f.wire->txTransfers == 1 && f.wire->maxTxFrames == 6, "all TX bytes share a single Bulk OUT before ACKs");
    require(f.wire->popCount == 15, "each response byte is popped exactly once");
}
void longResponse()
{
    Fixture f;
    f.wire->responseCapacityBytes = 2048 * sizeof(quint32);
    // Split headers, payloads and CRCs across USB completions independently of
    // both the VLP response size and the production FIFO-pop batch size.
    f.wire->readChunkBytes = 97;
    f.wire->deviceReply = QByteArray(230, 'x');
    f.wire->secondReply = QByteArray(220, 'y');
    auto operation = request();
    operation.insert("quiet_ms", 100);
    const auto result = run(f, operation);
    require(result.value("ok").toBool(), "response larger than the FIFO is drained across batches");
    require(QByteArray::fromBase64(result.value("data").toString().toLatin1())
        == f.wire->deviceReply + f.wire->secondReply, "out-of-order READ ACKs preserve FIFO request order");
    require(f.wire->popCount == 450, "long response contains no duplicated pops");
    require(!f.wire->responseCapacityExceeded &&
        f.wire->maxQueuedResponseBytes <= f.wire->responseCapacityBytes &&
        f.wire->generatedResponseBytes > f.wire->responseCapacityBytes,
        "long response drains finite 8 KB ACK storage instead of requiring an unbounded response burst");
    require(!result.contains("uart_diagnostics"), "successful exchanges do not attach failure diagnostics");
}
void rejectsUnsafePreconditions()
{
    Fixture wrongBaud;
    wrongBaud.wire->baud = 115200;
    require(!run(wrongBaud, request()).value("ok").toBool() && wrongBaud.wire->tx.isEmpty(),
        "wrong baud is rejected without reconfiguring hardware or transmitting");
    Fixture stale;
    stale.wire->fifo.append('x');
    const auto result = run(stale, request());
    require(!result.value("ok").toBool() && !result.value("unverified").toBool()
        && stale.wire->popCount == 0 && stale.wire->tx.isEmpty(), "stale FIFO is rejected without destroying old data");
}
void failuresAndRecovery()
{
    Fixture historical;
    historical.wire->forceStatusError = true;
    historical.wire->framingCount = 7;
    require(run(historical, request()).value("ok").toBool() && historical.wire->framingCount == 7,
        "historical sticky errors survive a clean exchange without blocking reconnection");
    historical.wire->newFramingError = true;
    require(!run(historical, request()).value("ok").toBool() && historical.wire->framingCount == 8,
        "a new framing error is rejected even when an older sticky flag was already set");
    Fixture timeout;
    timeout.wire->suppressResponse = true;
    const auto failed = run(timeout, request(1, 200));
    require(!failed.value("ok").toBool() && failed.value("unverified").toBool()
        && timeout.wire->txTransfers == 1, "absent instrument response fails without retransmission");
    const auto diagnostics = failed.value("uart_diagnostics").toObject();
    require(!diagnostics.value("phase").toString().isEmpty() &&
        diagnostics.value("quiet_elapsed_ms").toDouble() == -1 &&
        diagnostics.value("receive_poll_count").toInt() > 0 &&
        diagnostics.contains("pending") && diagnostics.contains("receive_timer_active") &&
        diagnostics.contains("receive_timer_remaining_ms") && diagnostics.contains("receive_timer_timeout_count"),
        "failed exchange retains stage, quiet timer and poll diagnostics before cancellation");
    timeout.wire->suppressResponse = false;
    require(run(timeout, request()).value("ok").toBool(), "deadline clears only owned requests so a later exchange can proceed");
    Fixture parity;
    parity.wire->badRxByte = true;
    require(!run(parity, request()).value("ok").toBool(), "UART byte parity error is rejected");
    Fixture bound;
    auto operation = request(); operation.insert("max_rx", 1);
    require(!run(bound, operation).value("ok").toBool() && bound.wire->popCount == 0,
        "max_rx is enforced before destructive reads");
    Fixture ack;
    ack.wire->dropTxAck = true;
    require(!run(ack, request(1, 200)).value("ok").toBool() && ack.wire->txTransfers == 1,
        "missing byte ACK fails without resending an uncertain write");
    ack.wire->fifo.clear(); ack.wire->dropTxAck = false;
    require(run(ack, request()).value("ok").toBool(), "timed-out burst leaves no lease blocking subsequent traffic");
}
void safetyTrafficAndReadTimeout()
{
    Fixture f;
    f.wire->dropTxAck = true;
    QTimer::singleShot(30, &f.session, [&] { require(f.session.motorSafetyAction(5) != 0, "motor heartbeat bypasses UART burst lease"); });
    run(f, request(1, 200));
    require(f.wire->motorSafetyWrites == 1, "motor heartbeat is delivered during an outstanding burst");
    f.wire->fifo.clear(); f.wire->fifo.append('a'); f.wire->dropPopReply = true;
    int timeouts = 0;
    QObject::connect(&f.session, &FpgaDeviceSession::commandTimedOut,
        [&](quint32, quint16) { ++timeouts; });
    f.session.readRegisters(0x681c, 1);
    QThread::msleep(10020);
    f.session.poll();
    require(timeouts == 1 && f.wire->popCount == 1 && f.wire->fifo.isEmpty(),
        "destructive RX read times out once and is never retried");
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    exactResponseAndBurst(); longResponse(); rejectsUnsafePreconditions();
    failuresAndRecovery(); safetyTrafficAndReadTimeout();
    std::puts("FPGA UART exchange tests passed");
    return 0;
}
