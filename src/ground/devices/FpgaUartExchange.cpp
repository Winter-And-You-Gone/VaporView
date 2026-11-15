#include "FpgaUartExchange.h"
#include "FpgaDeviceSession.h"

namespace VaporView::Ground::Devices
{
FpgaUartExchange::FpgaUartExchange(FpgaDeviceSession* session, QObject* parent)
    : QObject(parent), session_(session)
{
    deadlineTimer_.setSingleShot(true);
    receiveTimer_.setSingleShot(true);
    connect(&deadlineTimer_, &QTimer::timeout, this, [this] {
        finish(false, QStringLiteral("UART response deadline expired; the command was not retried"));
    });
    connect(&receiveTimer_, &QTimer::timeout, this, [this] {
        ++receiveTimerTimeoutCount_;
        pollReceive();
    });
    if (!session)
        return;
    connect(session, &FpgaDeviceSession::registerReadCompleted, this,
        [this](quint32 sequence, quint32, const QVector<quint32>& values) {
            if (active_ && pending_.contains(sequence)) {
                if (phase_ == Phase::Receive && values.size() == 1)
                    receivedWords_.insert(sequence, values.front());
                else
                    readValues_ = values;
            }
        });
    connect(session, &FpgaDeviceSession::commandCompleted, this,
        [this](quint32 sequence, quint16, bool ok, quint32 status, const QByteArray&) {
            if (!active_ || !pending_.remove(sequence))
                return;
            if (!ok) {
                finish(false, QStringLiteral("UART register transaction failed: 0x%1")
                    .arg(status, 8, 16, QLatin1Char('0')));
                return;
            }
            if (!pending_.isEmpty())
                return;
            if (phase_ == Phase::Transmit)
                pollReceive();
            else
                completeRead();
        });
    connect(session, &FpgaDeviceSession::commandTimedOut, this,
        [this](quint32 sequence, quint16) {
            if (active_ && pending_.contains(sequence))
                finish(false, QStringLiteral("UART register response timed out; the command was not retried"));
        });
    connect(session, &FpgaDeviceSession::stateChanged, this,
        [this](FpgaSessionState state, const QString& detail) {
            if (active_ && state != FpgaSessionState::Ready)
                finish(false, detail);
        });
    connect(session, &QObject::destroyed, this, [this] {
        finish(false, QStringLiteral("FPGA session was destroyed"));
    });
}

FpgaUartExchange::~FpgaUartExchange()
{
    cancelPending();
}

bool FpgaUartExchange::start(const QJsonObject& request)
{
    if (active_)
        return false;
    channel_ = request.value(QStringLiteral("channel")).toInt();
    const int timeout = request.value(QStringLiteral("timeout_ms")).toInt(1500);
    maxRx_ = request.value(QStringLiteral("max_rx")).toInt(4096);
    quietMs_ = request.value(QStringLiteral("quiet_ms")).toInt(20);
    const auto decoded = QByteArray::fromBase64Encoding(request.value(QStringLiteral("data")).toString().toLatin1(),
        QByteArray::AbortOnBase64DecodingErrors);
    active_ = true;
    transmitted_ = false;
    received_.clear();
    receivedWords_.clear();
    readValues_.clear();
    quietTimer_.invalidate();
    receivePollCount_ = 0;
    receiveTimerTimeoutCount_ = 0;
    if (request.value(QStringLiteral("op")).toString() != QStringLiteral("uart_exchange")
        || (channel_ != 1 && channel_ != 2) || timeout < 200 || timeout > 5000
        || maxRx_ < 1 || maxRx_ > 16384 || quietMs_ < 10 || quietMs_ > 200
        || quietMs_ >= timeout || !decoded || decoded.decoded.isEmpty() || decoded.decoded.size() > 256) {
        finish(false, QStringLiteral("Invalid UART exchange request"));
        return true;
    }
    if (!session_ || !session_->isOpen()) {
        finish(false, QStringLiteral("FPGA session is not open"));
        return true;
    }
    transmit_ = decoded.decoded;
    base_ = channel_ == 1 ? 0x6800 : 0x6900;
    expectedId_ = channel_ == 1 ? 0x00480100 : 0x00490100;
    deadlineTimer_.start(timeout);
    read(base_, 6, Phase::Configuration);
    return true;
}

void FpgaUartExchange::read(quint32 address, quint16 count, Phase phase)
{
    if (!active_ || !session_)
        return;
    phase_ = phase;
    readValues_.clear();
    const quint32 sequence = session_->readRegisters(address, count);
    if (!sequence) {
        finish(false, QStringLiteral("UART register read could not be queued"));
        return;
    }
    pending_.insert(sequence);
}

void FpgaUartExchange::completeRead()
{
    switch (phase_) {
    case Phase::Configuration:
        if (readValues_.size() != 6 || readValues_[0] != expectedId_
            || !(readValues_[1] & 1) || !(readValues_[2] & 1)
            || (readValues_[2] & (1u << 3))
            || readValues_[4] != 38400 || readValues_[5] != 0x108) {
            finish(false, QStringLiteral("UART bridge must be enabled, idle and configured for 38400/8N1"));
            return;
        }
        read(base_ + 0x20, 8, Phase::InitialLevels);
        return;
    case Phase::InitialLevels: {
        if (readValues_.size() != 8 || readValues_[0] || readValues_[1]
            || readValues_[7] < quint32(transmit_.size())) {
            finish(false, QStringLiteral("UART TX/RX FIFOs must be empty and the request must fit the TX FIFO"));
            return;
        }
        // Sticky flags may describe a previously unplugged input. Preserve
        // them and reject new errors by counters and per-byte flags instead.
        initialDrops_ = readValues_[4];
        initialParity_ = readValues_[5];
        initialFraming_ = readValues_[6];
        phase_ = Phase::Transmit;
        transmitted_ = true;
        const auto sequences = session_->writeRepeatedRegister(base_ + 0x18, transmit_);
        if (!active_)
            return;
        for (quint32 sequence : sequences)
            pending_.insert(sequence);
        if (sequences.size() != transmit_.size())
            finish(false, QStringLiteral("UART byte burst could not be sent; execution may be unknown"));
        return;
    }
    case Phase::Status:
        if (readValues_.size() != 1 || !(readValues_[0] & 1)) {
            finish(false, QStringLiteral("UART was disabled during the exchange"));
            return;
        }
        transmitterBusy_ = readValues_[0] & (1u << 3);
        read(base_ + 0x20, 7, Phase::Levels);
        return;
    case Phase::Levels:
        if (readValues_.size() != 7 || readValues_[4] != initialDrops_
            || readValues_[5] != initialParity_ || readValues_[6] != initialFraming_) {
            finish(false, QStringLiteral("UART receive error counters changed during the exchange"));
            return;
        }
        remainingRx_ = readValues_[1];
        if (quint64(received_.size()) + remainingRx_ > quint64(maxRx_)) {
            finish(false, QStringLiteral("UART response exceeded max_rx"));
            return;
        }
        if (remainingRx_) {
            // Keep request and response bursts below the FPGA/FX3 buffering
            // capacity so synchronous Bulk OUT cannot stall behind pending IN.
            receiveBytes(quint16(qMin(remainingRx_, 32u)));
            return;
        }
        if (!received_.isEmpty() && !readValues_[0] && !transmitterBusy_
            && quietTimer_.isValid() && quietTimer_.elapsed() >= quietMs_) {
            finish(true, QStringLiteral("Raw UART response collected; instrument semantics require separate validation"));
            return;
        }
        receiveTimer_.start(2);
        return;
    case Phase::Receive:
        for (quint32 word : receivedWords_) {
            if (word & 0x300) {
                finish(false, QStringLiteral("UART received a byte with parity or framing error"));
                return;
            }
            received_.append(char(word & 0xff));
        }
        remainingRx_ -= quint32(receivedWords_.size());
        quietTimer_.restart();
        if (remainingRx_)
            receiveBytes(quint16(qMin(remainingRx_, 32u)));
        else
            pollReceive();
        return;
    default:
        finish(false, QStringLiteral("Unexpected UART exchange state"));
    }
}

void FpgaUartExchange::pollReceive()
{
    ++receivePollCount_;
    read(base_ + 0x08, 1, Phase::Status);
}

void FpgaUartExchange::receiveBytes(quint16 count)
{
    if (!active_ || !session_)
        return;
    phase_ = Phase::Receive;
    receivedWords_.clear();
    const auto sequences = session_->readRepeatedRegister(base_ + 0x1c, count);
    if (!active_)
        return;
    for (quint32 sequence : sequences)
        pending_.insert(sequence);
    if (sequences.size() != count)
        finish(false, QStringLiteral("UART FIFO read burst could not be queued"));
}

void FpgaUartExchange::cancelPending()
{
    deadlineTimer_.stop();
    receiveTimer_.stop();
    const auto sequences = pending_.values();
    pending_.clear();
    if (session_ && !sequences.isEmpty())
        session_->cancelCommands(QVector<quint32>(sequences.begin(), sequences.end()));
}

void FpgaUartExchange::finish(bool ok, const QString& detail)
{
    if (!active_)
        return;
    QJsonObject result{{QStringLiteral("ok"), ok},
        {QStringLiteral("channel"), channel_},
        {QStringLiteral("data"), QString::fromLatin1(received_.toBase64())},
        {QStringLiteral("detail"), detail},
        {QStringLiteral("unverified"), !ok && transmitted_}};
    if (!ok) {
        static const char* const phases[] = {"idle", "configuration", "initial_levels", "transmit", "status", "levels", "receive"};
        result.insert(QStringLiteral("uart_diagnostics"), QJsonObject{
            {QStringLiteral("phase"), QString::fromLatin1(phases[int(phase_)])},
            {QStringLiteral("pending"), pending_.size()},
            {QStringLiteral("quiet_elapsed_ms"), quietTimer_.isValid() ? double(quietTimer_.elapsed()) : -1.0},
            {QStringLiteral("receive_timer_active"), receiveTimer_.isActive()},
            {QStringLiteral("receive_timer_remaining_ms"), receiveTimer_.remainingTime()},
            {QStringLiteral("receive_poll_count"), receivePollCount_},
            {QStringLiteral("receive_timer_timeout_count"), receiveTimerTimeoutCount_}});
    }
    active_ = false;
    phase_ = Phase::Idle;
    cancelPending();
    emit finished(result);
}
}
