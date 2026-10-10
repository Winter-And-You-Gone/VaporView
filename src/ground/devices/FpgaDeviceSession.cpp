#include "FpgaDeviceSession.h"

#include <QDateTime>

#include <algorithm>
#include <chrono>
#include <limits>

namespace VaporView::Ground::Devices
{
namespace
{
constexpr quint16 kGetCapabilities = 0x0001;
constexpr quint16 kReadRegister = 0x0002;
constexpr quint16 kWriteRegister = 0x0003;
constexpr quint16 kMaskedWrite = 0x0004;
constexpr quint16 kCommitConfig = 0x0005;
constexpr quint16 kStartAcquisition = 0x0006;
constexpr quint16 kStopAcquisition = 0x0007;
constexpr quint16 kSetStreamMask = 0x000A;
constexpr quint16 kSensorAction = 0x000B;
constexpr quint16 kPing = 0x00FE;
constexpr quint8 kResponse = static_cast<quint8>(FpgaVlp1::FrameType::Response);
constexpr quint8 kData = static_cast<quint8>(FpgaVlp1::FrameType::Data);

void appendU16(QByteArray& bytes, quint16 value)
{
    bytes.append(char(value & 0xFF));
    bytes.append(char((value >> 8) & 0xFF));
}

void appendU32(QByteArray& bytes, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        bytes.append(char((value >> (8 * i)) & 0xFF));
}

void appendU64(QByteArray& bytes, quint64 value)
{
    for (int i = 0; i < 8; ++i)
        bytes.append(char((value >> (8 * i)) & 0xFF));
}

std::vector<std::uint8_t> toBytes(const QByteArray& data)
{
    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(data.size()));
    for (const auto byte : data)
        result.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
    return result;
}

}  // namespace

FpgaDeviceSession::FpgaDeviceSession(std::unique_ptr<FpgaUsbTransport> transport,
                                     QObject *parent)
    : QObject(parent)
    , transport_(transport ? std::move(transport) : makeFpgaUsbHardwareTransport())
{
    if (transport_)
        transport_->setParent(this);
    pollTimer_.setParent(this);
    pollTimer_.setInterval(1);
    connect(&pollTimer_, &QTimer::timeout, this, &FpgaDeviceSession::poll);
    qRegisterMetaType<FpgaSensor::Reading>();
    qRegisterMetaType<FpgaCapabilities>();
    qRegisterMetaType<FpgaSessionState>();
    qRegisterMetaType<FpgaWave::CompletedStream>();
}

FpgaDeviceSession::~FpgaDeviceSession()
{
    close();
    if (transport_)
        transport_->setParent(nullptr);
}

bool FpgaDeviceSession::open(const QString& locator, QString *errorMessage)
{
    close();
    if (!transport_)
    {
        const QString message = QStringLiteral("FPGA transport is not configured");
        setState(FpgaSessionState::Error, message);
        if (errorMessage)
            *errorMessage = message;
        return false;
    }

    setState(FpgaSessionState::Opening, QStringLiteral("opening FPGA transport"));
    parser_ = FpgaVlp1::StreamParser();
    waveformAssembler_.clear();
    commandQueue_.clear();
    pendingCommand_.reset();
    capabilities_ = {};
    if (!transport_->open(locator))
    {
        const QString message = transport_->diagnostic().detail.isEmpty()
            ? QStringLiteral("FPGA USB transport open failed")
            : transport_->diagnostic().detail;
        setState(FpgaSessionState::Error, message);
        if (errorMessage)
            *errorMessage = message;
        return false;
    }

    setState(FpgaSessionState::Ready, QStringLiteral("FPGA transport open"));
    pollTimer_.start();
    return true;
}

void FpgaDeviceSession::close()
{
    pollTimer_.stop();
    commandQueue_.clear();
    pendingCommand_.reset();
    parser_ = FpgaVlp1::StreamParser();
    flushWaveforms();
    waveformAssembler_.clear();
    if (transport_ && transport_->isOpen())
        transport_->close();
    if (state_ != FpgaSessionState::Closed)
        setState(FpgaSessionState::Closed, QStringLiteral("FPGA transport closed"));
}

bool FpgaDeviceSession::isOpen() const
{
    return transport_ && transport_->isOpen();
}

FpgaSessionState FpgaDeviceSession::state() const
{
    return state_;
}

QString FpgaDeviceSession::stateDetail() const
{
    return stateDetail_;
}

const FpgaCapabilities& FpgaDeviceSession::capabilities() const
{
    return capabilities_;
}

FpgaUsbTransport* FpgaDeviceSession::transport() const
{
    return transport_.get();
}

void FpgaDeviceSession::poll()
{
    if (!isOpen())
        return;

    QByteArray bytes;
    const qint64 count = transport_->read(bytes, 16 * 1024);
    if (count < 0)
    {
        const QString message = QStringLiteral("FPGA transport read failed");
        pollTimer_.stop();
        cancelQueuedCommands();
        flushWaveforms();
        transport_->close();
        setState(FpgaSessionState::Error, message);
        emit errorOccurred(message);
        return;
    }

    if (!bytes.isEmpty())
    {
        ingestReplayBytes(bytes);
    }

    if (pendingCommand_ && pendingCommand_->timer.elapsed() > commandTimeoutMs_)
    {
        const auto command = pendingCommand_->command;
        pendingCommand_.reset();
        emit commandTimedOut(command.sequence, command.message);
        dispatchNext();
    }
}

void FpgaDeviceSession::cancelQueuedCommands()
{
    commandQueue_.clear();
    pendingCommand_.reset();
}

void FpgaDeviceSession::ingestReplayBytes(const QByteArray& bytes)
{
    emit usbBytesReceived(bytes);
    const auto frames = parser_.feed(
        reinterpret_cast<const std::uint8_t *>(bytes.constData()),
        static_cast<std::size_t>(bytes.size()));
    for (const auto& frame : frames)
        processFrame(frame);
}

void FpgaDeviceSession::flushWaveforms()
{
    for (const auto& stream : waveformAssembler_.flush())
        emit waveformCompleted(stream);
}

quint32 FpgaDeviceSession::ping(const QByteArray& echo, quint16 source)
{
    const quint32 sequence = nextSequence_++;
    return enqueue(source, kPing, FpgaVlp1::buildPing(sequence, toBytes(echo), source));
}

quint32 FpgaDeviceSession::requestCapabilities(quint16 source)
{
    const quint32 sequence = nextSequence_++;
    return enqueue(source, kGetCapabilities,
                   FpgaVlp1::buildGetCapabilities(sequence, source));
}

quint32 FpgaDeviceSession::readRegisters(quint32 address, quint16 count, quint16 source)
{
    const quint32 sequence = nextSequence_++;
    try
    {
        return enqueue(source, kReadRegister,
                       FpgaVlp1::buildReadReg(sequence, address, count, source));
    }
    catch (const std::exception& exception)
    {
        emit errorOccurred(QString::fromUtf8(exception.what()));
        return 0;
    }
}

quint32 FpgaDeviceSession::writeRegisters(quint32 address, const QVector<quint32>& values,
                                          quint16 flags, quint16 source)
{
    const quint32 sequence = nextSequence_++;
    std::vector<std::uint32_t> words;
    words.reserve(static_cast<std::size_t>(values.size()));
    for (const quint32 value : values)
        words.push_back(value);
    try
    {
        return enqueue(source, kWriteRegister,
                       FpgaVlp1::buildWriteReg(sequence, address, words, flags, source));
    }
    catch (const std::exception& exception)
    {
        emit errorOccurred(QString::fromUtf8(exception.what()));
        return 0;
    }
}

quint32 FpgaDeviceSession::maskedWrite(quint32 address, quint32 mask, quint32 value,
                                       quint16 source)
{
    QByteArray payload;
    appendU32(payload, address);
    appendU32(payload, mask);
    appendU32(payload, value);
    const quint32 sequence = nextSequence_++;
    return enqueue(source, kMaskedWrite,
                   FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Command, sequence, source,
                                         kMaskedWrite, toBytes(payload)));
}

quint32 FpgaDeviceSession::commitConfig(quint64 moduleMask, quint16 source)
{
    QByteArray payload;
    appendU64(payload, moduleMask);
    const quint32 sequence = nextSequence_++;
    return enqueue(source, kCommitConfig,
                   FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Command, sequence, source,
                                         kCommitConfig, toBytes(payload)));
}

quint32 FpgaDeviceSession::startAcquisition(quint64 sourceMask, quint16 source)
{
    QByteArray payload;
    appendU64(payload, sourceMask);
    appendU32(payload, 0);
    appendU32(payload, 0);
    const quint32 sequence = nextSequence_++;
    return enqueue(source, kStartAcquisition,
                   FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Command, sequence, source,
                                         kStartAcquisition, toBytes(payload)));
}

quint32 FpgaDeviceSession::stopAcquisition(quint64 sourceMask, quint16 source)
{
    QByteArray payload;
    appendU64(payload, sourceMask);
    appendU32(payload, 0);
    appendU32(payload, 0);
    const quint32 sequence = nextSequence_++;
    return enqueue(source, kStopAcquisition,
                   FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Command, sequence, source,
                                         kStopAcquisition, toBytes(payload)));
}

quint32 FpgaDeviceSession::setStreamMask(quint64 enableMask, quint64 rawEnableMask,
                                         quint16 source)
{
    QByteArray payload;
    appendU64(payload, enableMask);
    appendU64(payload, rawEnableMask);
    const quint32 sequence = nextSequence_++;
    return enqueue(source, kSetStreamMask,
                   FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Command, sequence, source,
                                         kSetStreamMask, toBytes(payload)));
}

quint32 FpgaDeviceSession::sensorAction(quint32 action, quint32 value, quint16 source)
{
    QByteArray payload;
    appendU32(payload, action);
    appendU32(payload, value);
    const quint32 sequence = nextSequence_++;
    return enqueue(source, kSensorAction,
                   FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Command, sequence, source,
                                         kSensorAction, toBytes(payload)));
}

quint32 FpgaDeviceSession::enqueue(quint16 source, quint16 message,
                                   const std::vector<std::uint8_t>& bytes)
{
    if (!isOpen())
    {
        emit errorOccurred(QStringLiteral("FPGA command rejected: transport is not open"));
        return 0;
    }
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return 0;
    QueuedCommand command;
    command.sequence = nextSequence_ == 0 ? ++nextSequence_ : nextSequence_;
    // Builders already received their sequence. Recover it from the VLP header.
    if (bytes.size() >= 16)
    {
        command.sequence = quint32(bytes[12]) | (quint32(bytes[13]) << 8)
            | (quint32(bytes[14]) << 16) | (quint32(bytes[15]) << 24);
    }
    command.source = source;
    command.message = message;
    command.bytes = QByteArray(reinterpret_cast<const char *>(bytes.data()),
                               static_cast<int>(bytes.size()));
    const quint32 sequence = command.sequence;
    commandQueue_.enqueue(std::move(command));
    dispatchNext();
    return sequence;
}

void FpgaDeviceSession::dispatchNext()
{
    if (pendingCommand_ || commandQueue_.isEmpty() || !isOpen())
        return;
    QueuedCommand command = commandQueue_.dequeue();
    if (transport_->write(command.bytes) != command.bytes.size())
    {
        const QString message = QStringLiteral("FPGA command write failed");
        emit errorOccurred(message);
        setState(FpgaSessionState::Error, message);
        return;
    }
    PendingCommand pending;
    pending.command = std::move(command);
    pending.timer.start();
    pendingCommand_ = std::move(pending);
    emit commandSent(pendingCommand_->command.bytes);
}

void FpgaDeviceSession::processFrame(const FpgaVlp1::Frame& frame)
{
    const QByteArray payload(reinterpret_cast<const char *>(frame.payload.data()),
                             static_cast<int>(frame.payload.size()));
    const QByteArray frameBytes(reinterpret_cast<const char *>(frame.bytes.data()),
                                static_cast<int>(frame.bytes.size()));
    emit frameReceived(static_cast<quint8>(frame.header.frameType), frame.header.sequence,
                       frame.header.source, frame.header.message, frame.header.timestamp,
                       frame.header.flags, payload);
    emit rawFrameReceived(frame.header.sequence, frame.header.source, frame.header.message,
                          frameBytes);

    if (static_cast<quint8>(frame.header.frameType) == kData)
    {
        if (frame.header.source == 0x0020 || frame.header.source == 0x0021
            || frame.header.source == 0x0030 || frame.header.source == 0x0031)
        {
            const auto stream = waveformAssembler_.accept({
                frame.header.source,
                frame.header.message,
                frame.header.flags,
                frame.header.cycleId,
                frame.header.timestamp,
                payload});
            if (stream)
                emit waveformCompleted(*stream);
            for (const auto& expired : waveformAssembler_.takeExpired())
                emit waveformCompleted(expired);
            return;
        }
        const auto reading = FpgaSensor::FpgaSensorDecoder::decode(
            payload, frame.header.source, frame.header.message, frame.header.flags,
            frame.header.timestamp);
        emit sensorReadingReceived(reading);
        const auto now = std::chrono::steady_clock::now();
        if (reading.kind == FpgaSensor::SensorKind::Ptb210 && reading.pressurePa)
        {
            PtbData data;
            data.pressure_hpa = *reading.pressurePa / 100.0;
            data.timestamp = now;
            data.valid = reading.validity.measurement;
            emit ptbDataUpdated(data);
        }
        else if (reading.kind == FpgaSensor::SensorKind::Sht45 &&
                 reading.temperatureC && reading.humidityPct)
        {
            HmpData data;
            data.temperature = *reading.temperatureC;
            data.humidity = *reading.humidityPct;
            data.timestamp = now;
            data.valid = reading.validity.measurement;
            emit hmpDataUpdated(data);
        }
        else if (reading.kind == FpgaSensor::SensorKind::Tfa1500 && reading.distanceMm)
        {
            LidarData data;
            data.distance_m = static_cast<double>(*reading.distanceMm) / 1000.0;
            data.timestamp = now;
            data.valid = reading.validity.measurement;
            emit lidarDataUpdated(data);
        }
        return;
    }

    if (!pendingCommand_)
        return;
    const FpgaVlp1::ResponseMatch expected{
        pendingCommand_->command.sequence,
        pendingCommand_->command.source,
        pendingCommand_->command.message};
    if (!FpgaVlp1::matchesResponse(frame, expected))
        return;

    quint32 status = std::numeric_limits<quint32>::max();
    bool success = FpgaVlp1::responseSucceeded(frame, &status);
    const auto command = pendingCommand_->command;
    if (success && command.message == kGetCapabilities)
    {
        FpgaCapabilities capabilities;
        success = parseCapabilities(payload, capabilities);
        if (success)
        {
            capabilities_ = capabilities;
            emit capabilitiesChanged(capabilities_);
        }
    }
    if (success && command.message == kReadRegister)
    {
        FpgaRegisterReadResult result;
        success = parseRegisterRead(payload, result)
            && command.bytes.size() >= 48
            && result.address == readU32(command.bytes.constData() + 40)
            && result.values.size() == (quint16(quint8(command.bytes[44]))
                | (quint16(quint8(command.bytes[45])) << 8));
        if (success)
            emit registerReadCompleted(command.sequence, result.address, result.values);
    }
    if (success && command.message == kPing)
        success = payload.mid(4) == command.bytes.mid(40, readU32(command.bytes.constData() + 36));
    if (!success && status == 0)
        status = 12; // A CRC-valid response can still violate the command payload contract.
    pendingCommand_.reset();
    emit commandCompleted(command.sequence, command.message, success, status, payload);
    dispatchNext();
}

void FpgaDeviceSession::setState(FpgaSessionState state, const QString& detail)
{
    state_ = state;
    stateDetail_ = detail;
    emit stateChanged(state_, stateDetail_);
}

quint32 FpgaDeviceSession::readU32(const char *data)
{
    return quint32(static_cast<unsigned char>(data[0]))
        | (quint32(static_cast<unsigned char>(data[1])) << 8)
        | (quint32(static_cast<unsigned char>(data[2])) << 16)
        | (quint32(static_cast<unsigned char>(data[3])) << 24);
}

bool FpgaDeviceSession::parseCapabilities(const QByteArray& payload,
                                           FpgaCapabilities& result)
{
    if (payload.size() != 28)
        return false;
    result.status = readU32(payload.constData());
    result.capabilities = readU32(payload.constData() + 4);
    result.protocolVersion = readU32(payload.constData() + 8);
    result.timeClockHz = readU32(payload.constData() + 12);
    result.maxBulkPayload = readU32(payload.constData() + 16);
    result.maxCommandFrame = readU32(payload.constData() + 20);
    result.extensions = readU32(payload.constData() + 24);
    result.valid = result.status == 0;
    return true;
}

bool FpgaDeviceSession::parseRegisterRead(const QByteArray& payload,
                                          FpgaRegisterReadResult& result)
{
    if (payload.size() < 12)
        return false;
    const quint32 status = readU32(payload.constData());
    const quint16 count = quint16(static_cast<unsigned char>(payload[8]))
        | (quint16(static_cast<unsigned char>(payload[9])) << 8);
    if (status != 0 || count == 0 || payload.size() != 12 + 4 * count
        || payload[10] != 0 || payload[11] != 0)
        return false;
    result.address = readU32(payload.constData() + 4);
    result.values.clear();
    result.values.reserve(count);
    for (quint16 i = 0; i < count; ++i)
        result.values.push_back(readU32(payload.constData() + 12 + 4 * i));
    return true;
}

}  // namespace VaporView::Ground::Devices
