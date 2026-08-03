#include "Ai8TemperatureControllerCollector.h"

#include "TemperatureControllerProtocol.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace VaporView
{
namespace
{

using namespace Ai8TemperatureControllerProtocol;

void sleepMs(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

QString registerError(quint16 address, bool english)
{
    return QString(english
        ? "AI-8288 register 0x%1 did not pass write/read-back confirmation. Check Loc and wiring."
        : "AI-8288 寄存器 0x%1 写入后回读不一致，请检查 Loc 参数锁和接线。")
        .arg(address, 4, 16, QLatin1Char('0')).toUpper();
}

} // namespace

Ai8TemperatureControllerProtocol::LiveData Ai8TemperatureControllerCollector::getLatestData()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return latestData_;
}

void Ai8TemperatureControllerCollector::setSlaveAddress(quint8 slaveAddress)
{
    slaveAddress_.store(slaveAddress == 0 ? 1 : slaveAddress);
}

quint8 Ai8TemperatureControllerCollector::slaveAddress() const
{
    return slaveAddress_.load();
}

bool Ai8TemperatureControllerCollector::initialize()
{
    serial_.setNonBlocking(true);
    return true;
}

bool Ai8TemperatureControllerCollector::readResponseFrame(quint8 functionCode,
                                                           std::vector<quint8>& frame,
                                                           int waitMs)
{
    frame.clear();
    std::vector<quint8> buffer;
    buffer.reserve(80);
    const auto started = std::chrono::steady_clock::now();
    quint8 chunk[80];
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - started).count() < waitMs)
    {
        const ssize_t count = serial_.read(chunk, sizeof(chunk));
        if (count > 0)
        {
            buffer.insert(buffer.end(), chunk, chunk + count);
            while (buffer.size() >= 5)
            {
                const quint8 expectedAddress = slaveAddress_.load();
                const auto start = std::find(buffer.begin(), buffer.end(), expectedAddress);
                if (start == buffer.end())
                {
                    buffer.clear();
                    break;
                }
                buffer.erase(buffer.begin(), start);
                if (buffer.size() < 5)
                {
                    break;
                }
                const quint8 receivedFunction = buffer[1];
                size_t frameSize = 0;
                if (receivedFunction == functionCode)
                {
                    frameSize = functionCode == 0x03
                        ? static_cast<size_t>(buffer[2]) + 5u
                        : (functionCode == 0x10 ? 8u : 0u);
                }
                else if (receivedFunction == static_cast<quint8>(functionCode | 0x80u))
                {
                    frameSize = 5;
                }
                if (frameSize == 0)
                {
                    buffer.erase(buffer.begin());
                    continue;
                }
                if (buffer.size() < frameSize)
                {
                    break;
                }
                frame.assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(frameSize));
                return true;
            }
        }
        else
        {
            sleepMs(5);
        }
    }
    return false;
}

bool Ai8TemperatureControllerCollector::readRegisters(quint16 address,
                                                       quint16 count,
                                                       std::vector<quint16>& values,
                                                       int waitMs)
{
    std::lock_guard<std::mutex> lock(modbusMutex_);
    return readRegistersUnlocked(address, count, values, waitMs);
}

bool Ai8TemperatureControllerCollector::readRegistersUnlocked(quint16 address,
                                                               quint16 count,
                                                               std::vector<quint16>& values,
                                                               int waitMs)
{
    using namespace TemperatureControllerProtocol;
    if (count == 0 || count > 32)
    {
        return false;
    }
    const QByteArray request = buildReadRegistersRequest(slaveAddress_.load(), address, count);
    sleepMs(4);
    serial_.flush();
    if (serial_.write(request.constData(), static_cast<size_t>(request.size())) != request.size())
    {
        return false;
    }
    std::vector<quint8> response;
    if (!readResponseFrame(0x03, response, waitMs))
    {
        return false;
    }
    const QByteArray bytes(reinterpret_cast<const char *>(response.data()),
                           static_cast<int>(response.size()));
    const ReadResponse parsed = parseReadRegistersResponse(bytes, slaveAddress_.load(), count);
    if (parsed.status != FrameStatus::Ok)
    {
        return false;
    }
    values.assign(parsed.registers.cbegin(), parsed.registers.cend());
    return true;
}

bool Ai8TemperatureControllerCollector::readRegisterValue(quint16 address,
                                                           quint16& value,
                                                           QString *errorMessage)
{
    std::lock_guard<std::mutex> lock(modbusMutex_);
    return readRegisterValueUnlocked(address, value, errorMessage);
}

bool Ai8TemperatureControllerCollector::readRegisterValueUnlocked(quint16 address,
                                                                   quint16& value,
                                                                   QString *errorMessage)
{
    std::vector<quint16> values;
    if (!readRegistersUnlocked(address, 1, values, 250) || values.empty())
    {
        if (errorMessage)
        {
            *errorMessage = QString(isEnglishLog()
                ? "Failed to read AI-8288 register 0x%1."
                : "读取 AI-8288 寄存器 0x%1 失败。")
                .arg(address, 4, 16, QLatin1Char('0')).toUpper();
        }
        return false;
    }
    value = values.front();
    return true;
}

bool Ai8TemperatureControllerCollector::writeAndConfirm(quint16 address,
                                                         quint16 value,
                                                         QString *errorMessage)
{
    std::lock_guard<std::mutex> lock(modbusMutex_);
    return writeAndConfirmUnlocked(address, value, errorMessage);
}

bool Ai8TemperatureControllerCollector::writeAndConfirmUnlocked(quint16 address,
                                                                 quint16 value,
                                                                 QString *errorMessage)
{
    using namespace TemperatureControllerProtocol;
    const QByteArray request = buildWriteRegistersRequest(slaveAddress_.load(), address, {value});
    sleepMs(4);
    serial_.flush();
    if (serial_.write(request.constData(), static_cast<size_t>(request.size())) != request.size())
    {
        if (errorMessage) *errorMessage = registerError(address, isEnglishLog());
        return false;
    }
    std::vector<quint8> response;
    if (!readResponseFrame(0x10, response, 250))
    {
        if (errorMessage) *errorMessage = registerError(address, isEnglishLog());
        return false;
    }
    const QByteArray bytes(reinterpret_cast<const char *>(response.data()),
                           static_cast<int>(response.size()));
    const WriteResponse parsed = parseWriteRegistersResponse(bytes, slaveAddress_.load(), address, 1);
    if (parsed.status != FrameStatus::Ok)
    {
        if (errorMessage) *errorMessage = registerError(address, isEnglishLog());
        return false;
    }
    quint16 readBack = 0;
    if (!readRegisterValueUnlocked(address, readBack, errorMessage) || readBack != value)
    {
        if (errorMessage) *errorMessage = registerError(address, isEnglishLog());
        return false;
    }
    return true;
}

bool Ai8TemperatureControllerCollector::checkDeviceResponse()
{
    quint16 modelFeature = 0;
    if (!readRegisterValue(static_cast<quint16>(Register::ModelFeature), modelFeature))
    {
        log(isEnglishLog()
            ? "[AI-8288] No valid Modbus response. Check RS485 A/B, station address, baud and 8N1."
            : "[AI-8288] 未收到有效 Modbus 应答，请检查 RS485 A/B、站号、波特率和 8N1。" );
        return false;
    }
    log((isEnglishLog()
            ? "[AI-8288] Modbus response confirmed; model feature word: "
            : "[AI-8288] Modbus 应答正常，型号特征字：") + std::to_string(modelFeature));
    return true;
}

bool Ai8TemperatureControllerCollector::readChannelPage(const Selection& selection,
                                                         PageData& data,
                                                         QString *errorMessage)
{
    const int channel = std::clamp(selection.channel, 1, kChannelCount);
    auto read = [&](Register reg, quint16& value) {
        return readRegisterValue(channelRegister(reg, channel), value, errorMessage);
    };
    quint16 sp = 0, pv = 0, p = 0, i = 0, d = 0, at = 0, op = 0;
    if (!read(Register::SetpointBase, sp) || !read(Register::MeasuredValueBase, pv) ||
        !read(Register::ProportionalBandBase, p) || !read(Register::IntegralTimeBase, i) ||
        !read(Register::DerivativeTimeBase, d) || !read(Register::WorkModeBase, at) ||
        !read(Register::ManualOutputBase, op))
    {
        return false;
    }
    data.channel.setpointC = decodeSignedTenths(sp);
    data.channel.measuredC = decodeSignedTenths(pv);
    data.channel.proportionalBand = decodeUnsignedTenths(p);
    data.channel.integralTimeS = decodeUnsignedTenths(i);
    data.channel.derivativeTimeS = decodeSignedHundredths(d);
    data.channel.workMode = at;
    data.channel.manualOutputPercent = decodeManualOutput(op);
    return true;
}

bool Ai8TemperatureControllerCollector::readInputPage(const Selection& selection,
                                                       PageData& data,
                                                       QString *errorMessage)
{
    const int group = std::clamp(selection.inputGroup, 1, kParameterGroupCount);
    const int channel = std::clamp(selection.channel, 1, kChannelCount);
    auto readGroup = [&](Register reg, quint16& value) {
        return readRegisterValue(groupRegister(reg, group), value, errorMessage);
    };
    quint16 inp = 0, scl = 0, sch = 0, fil = 0, in = 0, sc = 0;
    if (!readGroup(Register::InputTypeBase, inp) || !readGroup(Register::ScaleLowBase, scl) ||
        !readGroup(Register::ScaleHighBase, sch) || !readGroup(Register::FilterBase, fil) ||
        !readRegisterValue(channelRegister(Register::ChannelInputBase, channel), in, errorMessage) ||
        !readRegisterValue(channelRegister(Register::MeasurementOffsetBase, channel), sc, errorMessage))
    {
        return false;
    }
    data.input.inputType = inp;
    data.input.scaleLow = decodeSignedTenths(scl);
    data.input.scaleHigh = decodeSignedTenths(sch);
    data.input.filter = fil;
    data.input.channelInputGroup = decodeChannelInputGroup(in);
    data.input.correctionEntry = decodeCorrectionEntry(in);
    data.input.measurementOffset = decodeSignedTenths(sc);
    return true;
}

bool Ai8TemperatureControllerCollector::readOutputPage(const Selection& selection,
                                                        PageData& data,
                                                        QString *errorMessage)
{
    const int group = std::clamp(selection.outputGroup, 1, kParameterGroupCount);
    auto read = [&](Register reg, quint16& value) {
        return readRegisterValue(groupRegister(reg, group), value, errorMessage);
    };
    quint16 act = 0, hys = 0, opl = 0, oph = 0, srh = 0, srl = 0, aaf = 0;
    if (!read(Register::ControlActionBase, act) || !read(Register::HysteresisBase, hys) ||
        !read(Register::OutputLowBase, opl) || !read(Register::OutputHighBase, oph) ||
        !read(Register::RiseSlopeBase, srh) || !read(Register::FallSlopeBase, srl) ||
        !read(Register::AlarmResetBase, aaf))
    {
        return false;
    }
    data.output.controlAction = act & 0x0001u;
    data.output.hysteresis = decodeSignedTenths(hys);
    data.output.outputLowPercent = opl;
    data.output.outputHighPercent = oph;
    data.output.riseSlope = decodeUnsignedTenths(srh);
    data.output.fallSlope = decodeUnsignedTenths(srl);
    data.output.alarmResetFlags = aaf;
    return true;
}

bool Ai8TemperatureControllerCollector::readGlobalPage(const Selection&,
                                                        PageData& data,
                                                        QString *errorMessage)
{
    std::vector<quint16> values;
    constexpr quint16 start = static_cast<quint16>(Register::Address);
    constexpr quint16 count = static_cast<quint16>(static_cast<quint16>(Register::SerialNumberLow) - start + 1);
    if (!readRegisters(start, count, values, 300))
    {
        if (errorMessage)
        {
            *errorMessage = isEnglishLog()
                ? QStringLiteral("Failed to read the AI-8288 global register block.")
                : QStringLiteral("读取 AI-8288 全局寄存器块失败。");
        }
        return false;
    }
    auto at = [&](Register reg) -> quint16 {
        return values.at(static_cast<int>(static_cast<quint16>(reg) - start));
    };
    data.global.address = at(Register::Address);
    data.global.baudRate = decodeBaudRate(at(Register::BaudRate));
    data.global.controlChannelCount = at(Register::ControlChannelCount);
    data.global.runState = at(Register::RunState);
    data.global.controlCycleS = decodeUnsignedTenths(at(Register::ControlCycle));
    data.global.parityFlags = at(Register::ParityFlags);
    data.global.sampleMode = at(Register::SampleMode);
    data.global.decimalPoint = at(Register::DecimalPoint);
    data.global.parameterLock = at(Register::ParameterLock);
    data.global.modelFeature = at(Register::ModelFeature);
    data.global.serialNumber = (static_cast<quint32>(at(Register::SerialNumberHigh)) << 16) |
                               at(Register::SerialNumberLow);
    return true;
}

bool Ai8TemperatureControllerCollector::readPage(Page page,
                                                  const Selection& selection,
                                                  PageData& data,
                                                  QString *errorMessage)
{
    data = PageData{};
    data.page = page;
    data.selection = selection;
    switch (page)
    {
    case Page::Channel: return readChannelPage(selection, data, errorMessage);
    case Page::InputGroup: return readInputPage(selection, data, errorMessage);
    case Page::OutputGroup: return readOutputPage(selection, data, errorMessage);
    case Page::Global: return readGlobalPage(selection, data, errorMessage);
    }
    return false;
}

bool Ai8TemperatureControllerCollector::writeChannelPage(const PageData& data, QString *errorMessage)
{
    const int channel = std::clamp(data.selection.channel, 1, kChannelCount);
    auto write = [&](Register reg, quint16 value) {
        return writeAndConfirm(channelRegister(reg, channel), value, errorMessage);
    };
    if (!write(Register::SetpointBase, encodeSignedTenths(data.channel.setpointC)) ||
        !write(Register::ProportionalBandBase, encodeUnsignedTenths(data.channel.proportionalBand)) ||
        !write(Register::IntegralTimeBase, encodeUnsignedTenths(data.channel.integralTimeS)) ||
        !write(Register::DerivativeTimeBase, encodeSignedHundredths(data.channel.derivativeTimeS)) ||
        !write(Register::WorkModeBase, static_cast<quint16>(data.channel.workMode)))
    {
        return false;
    }
    return data.channel.workMode != 3 ||
           write(Register::ManualOutputBase, encodeManualOutput(data.channel.manualOutputPercent));
}

bool Ai8TemperatureControllerCollector::writeInputPage(const PageData& data, QString *errorMessage)
{
    const int group = std::clamp(data.selection.inputGroup, 1, kParameterGroupCount);
    const int channel = std::clamp(data.selection.channel, 1, kChannelCount);
    auto writeGroup = [&](Register reg, quint16 value) {
        return writeAndConfirm(groupRegister(reg, group), value, errorMessage);
    };
    return writeGroup(Register::InputTypeBase, static_cast<quint16>(data.input.inputType)) &&
           writeGroup(Register::ScaleLowBase, encodeSignedTenths(data.input.scaleLow)) &&
           writeGroup(Register::ScaleHighBase, encodeSignedTenths(data.input.scaleHigh)) &&
           writeGroup(Register::FilterBase, static_cast<quint16>(data.input.filter)) &&
           writeAndConfirm(channelRegister(Register::ChannelInputBase, channel),
                           encodeChannelInput(data.input.channelInputGroup, data.input.correctionEntry),
                           errorMessage) &&
           writeAndConfirm(channelRegister(Register::MeasurementOffsetBase, channel),
                           encodeSignedTenths(data.input.measurementOffset),
                           errorMessage);
}

bool Ai8TemperatureControllerCollector::writeOutputPage(const PageData& data, QString *errorMessage)
{
    const int group = std::clamp(data.selection.outputGroup, 1, kParameterGroupCount);
    auto address = [&](Register reg) { return groupRegister(reg, group); };
    quint16 currentAction = 0;
    if (!readRegisterValue(address(Register::ControlActionBase), currentAction, errorMessage))
    {
        return false;
    }
    const quint16 updatedAction = static_cast<quint16>((currentAction & ~0x0001u) |
                                                       (data.output.controlAction & 0x0001));
    return writeAndConfirm(address(Register::ControlActionBase), updatedAction, errorMessage) &&
           writeAndConfirm(address(Register::HysteresisBase), encodeSignedTenths(data.output.hysteresis), errorMessage) &&
           writeAndConfirm(address(Register::OutputLowBase), static_cast<quint16>(data.output.outputLowPercent), errorMessage) &&
           writeAndConfirm(address(Register::OutputHighBase), static_cast<quint16>(data.output.outputHighPercent), errorMessage) &&
           writeAndConfirm(address(Register::RiseSlopeBase), encodeUnsignedTenths(data.output.riseSlope), errorMessage) &&
           writeAndConfirm(address(Register::FallSlopeBase), encodeUnsignedTenths(data.output.fallSlope), errorMessage) &&
           writeAndConfirm(address(Register::AlarmResetBase), static_cast<quint16>(data.output.alarmResetFlags), errorMessage);
}

bool Ai8TemperatureControllerCollector::writeGlobalPage(const PageData& data, QString *resultMessage)
{
    PageData current;
    QString error;
    if (!readGlobalPage(data.selection, current, &error))
    {
        if (resultMessage) *resultMessage = error;
        return false;
    }
    const bool connectionSettingsChanged =
        current.global.address != data.global.address ||
        current.global.baudRate != data.global.baudRate;
    auto write = [&](Register reg, quint16 value) {
        return writeAndConfirm(static_cast<quint16>(reg), value, &error);
    };

    const quint16 targetLock = static_cast<quint16>(
        (current.global.parameterLock & ~0x0020u) |
        (data.global.parameterLock & 0x0020));
    if ((current.global.parameterLock & 0x0020) != 0 &&
        (targetLock & 0x0020) == 0 &&
        !write(Register::ParameterLock, targetLock))
    {
        if (resultMessage) *resultMessage = error;
        return false;
    }
    if (!write(Register::ControlChannelCount,
               static_cast<quint16>(std::clamp(data.global.controlChannelCount, 1, kChannelCount))) ||
        !write(Register::ControlCycle, encodeUnsignedTenths(data.global.controlCycleS)) ||
        !write(Register::RunState, static_cast<quint16>(data.global.runState)) ||
        !write(Register::SampleMode, static_cast<quint16>(data.global.sampleMode)) ||
        !write(Register::DecimalPoint, static_cast<quint16>(data.global.decimalPoint)) ||
        ((targetLock & 0x0020) != 0 && targetLock != current.global.parameterLock &&
         !write(Register::ParameterLock, targetLock)))
    {
        if (resultMessage) *resultMessage = error;
        return false;
    }

    if (resultMessage)
    {
        *resultMessage = connectionSettingsChanged
            ? (isEnglishLog()
                   ? QStringLiteral("Other global parameters were confirmed. Addr/bAud were intentionally not changed until hardware switchover behavior is verified.")
                   : QStringLiteral("其他全局参数已回读确认；Addr/bAud 涉及通信切换，实机验证前暂不下发。"))
            : (isEnglishLog()
                   ? QStringLiteral("Global parameters were written and confirmed by read-back.")
                   : QStringLiteral("全局参数已写入并回读确认。"));
    }
    return true;
}

bool Ai8TemperatureControllerCollector::writePage(const PageData& data, QString *resultMessage)
{
    QString error;
    bool success = false;
    switch (data.page)
    {
    case Page::Channel: success = writeChannelPage(data, &error); break;
    case Page::InputGroup: success = writeInputPage(data, &error); break;
    case Page::OutputGroup: success = writeOutputPage(data, &error); break;
    case Page::Global: return writeGlobalPage(data, resultMessage);
    }
    if (resultMessage)
    {
        *resultMessage = success
            ? (isEnglishLog()
                   ? QStringLiteral("Parameters were written and confirmed by read-back.")
                   : QStringLiteral("参数已写入并回读确认。"))
            : error;
    }
    return success;
}

bool Ai8TemperatureControllerCollector::readLiveData(LiveData& data)
{
    std::vector<quint16> values;
    if (!readRegisters(static_cast<quint16>(Register::MeasuredValueBase), kChannelCount, values, 250) ||
        values.size() != kChannelCount)
    {
        data.valid = false;
        data.errorMessage = isEnglishLog()
            ? QStringLiteral("AI-8288 PV polling failed")
            : QStringLiteral("AI-8288 测量值轮询失败");
        return false;
    }
    for (int index = 0; index < kChannelCount; ++index)
    {
        data.measuredC[static_cast<size_t>(index)] = decodeSignedTenths(values[static_cast<size_t>(index)]);
    }
    data.valid = true;

    std::vector<quint16> statusValues;
    if (!readRegisters(static_cast<quint16>(Register::ControlStatusBase),
                       kControlStatusRegisterCount,
                       statusValues,
                       250) ||
        statusValues.size() != kControlStatusRegisterCount)
    {
        data.controlStatesValid = false;
        data.errorMessage = isEnglishLog()
            ? QStringLiteral("AI-8288 control status polling failed")
            : QStringLiteral("AI-8288 输出状态轮询失败");
        return true;
    }
    for (int index = 0; index < kChannelCount; ++index)
    {
        const int registerIndex = index / 2;
        data.controlStates[static_cast<size_t>(index)] = decodeChannelControlState(
            statusValues[static_cast<size_t>(registerIndex)], index + 1);
    }
    data.controlStatesValid = true;
    data.errorMessage.clear();
    return true;
}

void Ai8TemperatureControllerCollector::run()
{
    while (running_.load())
    {
        const auto started = std::chrono::steady_clock::now();
        LiveData sample;
        const bool readOk = readLiveData(sample);
        DataCallback callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            latestData_ = sample;
            callback = data_callback_;
        }
        if (readOk)
        {
            recordDataReceived();
            if (callback && shouldEmitData())
            {
                updateLastEmitTime();
                callback();
            }
        }

        const int intervalMs = std::max(1, 1000 / std::max(1, getSampleRate()));
        const int elapsedMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        if (elapsedMs < intervalMs)
        {
            sleepMs(intervalMs - elapsedMs);
        }
    }
}

} // namespace VaporView
