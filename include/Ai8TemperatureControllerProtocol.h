#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <array>

namespace VaporView::Ai8TemperatureControllerProtocol
{

constexpr int kChannelCount = 8;
constexpr int kParameterGroupCount = 4;
constexpr int kControlStatusRegisterCount = (kChannelCount + 1) / 2;

enum class ChannelControlState : quint8
{
    Unknown = 0,
    ApidOutput,
    AutoTuning,
    Stopped,
};

enum class Page
{
    Channel = 0,
    InputGroup = 1,
    OutputGroup = 2,
    Global = 3,
};

enum class Register : quint16
{
    SetpointBase = 0x0000,
    ProportionalBandBase = 0x0060,
    IntegralTimeBase = 0x00C0,
    DerivativeTimeBase = 0x0120,
    ChannelInputBase = 0x0180,
    MeasurementOffsetBase = 0x01E0,
    ChannelOutputBase = 0x0240,
    WorkModeBase = 0x0300,
    ManualOutputBase = 0x0360,
    MeasuredValueBase = 0x0600,
    ControlStatusBase = 0x06C0,
    InputTypeBase = 0x0800,
    ScaleLowBase = 0x0804,
    ScaleHighBase = 0x0808,
    FilterBase = 0x080C,
    AlarmResetBase = 0x0818,
    HysteresisBase = 0x081C,
    OutputLowBase = 0x0820,
    OutputHighBase = 0x0824,
    ControlActionBase = 0x082C,
    RiseSlopeBase = 0x0830,
    FallSlopeBase = 0x0834,
    Address = 0x0840,
    BaudRate = 0x0841,
    ControlChannelCount = 0x0844,
    RunState = 0x0845,
    ControlCycle = 0x0846,
    ParityFlags = 0x084C,
    SampleMode = 0x084E,
    DecimalPoint = 0x0850,
    MainStatus = 0x0851,
    ParameterLock = 0x0852,
    ModelFeature = 0x0853,
    SerialNumberHigh = 0x0854,
    SerialNumberLow = 0x0855,
};

struct Selection
{
    int channel = 1;
    int inputGroup = 1;
    int outputGroup = 1;
};

struct ChannelParameters
{
    double setpointC = 0.0;
    double measuredC = 0.0;
    double proportionalBand = 0.0;
    double integralTimeS = 0.0;
    double derivativeTimeS = 0.0;
    int workMode = 0;
    double manualOutputPercent = 0.0;
};

struct InputParameters
{
    int inputType = 0;
    double scaleLow = 0.0;
    double scaleHigh = 100.0;
    int filter = 0;
    int channelInputGroup = 1;
    double measurementOffset = 0.0;
    int correctionEntry = 0;
};

struct OutputParameters
{
    int controlAction = 0;
    double hysteresis = 0.0;
    int outputLowPercent = 0;
    int outputHighPercent = 100;
    double riseSlope = 0.0;
    double fallSlope = 0.0;
    int alarmResetFlags = 0;
};

struct GlobalParameters
{
    int address = 1;
    int baudRate = 19200;
    int controlChannelCount = kChannelCount;
    double controlCycleS = 0.0;
    quint16 runStateRaw = 0;
    bool runStateIsDocumented = true;
    bool runStateWriteRequested = false;
    quint16 runStateWriteValue = 0;
    int parameterLock = 0;
    int sampleMode = 0;
    int decimalPoint = 1;
    int parityFlags = 0;
    int modelFeature = 0;
    quint32 serialNumber = 0;
};

struct PageData
{
    Page page = Page::Channel;
    Selection selection;
    ChannelParameters channel;
    InputParameters input;
    OutputParameters output;
    GlobalParameters global;
};

struct LiveData
{
    std::array<double, kChannelCount> measuredC{};
    std::array<ChannelControlState, kChannelCount> controlStates{};
    bool valid = false;
    bool controlStatesValid = false;
    QString errorMessage;
};

quint16 channelRegister(Register base, int channel);
quint16 groupRegister(Register base, int group);
quint16 encodeSignedTenths(double value);
quint16 encodeUnsignedTenths(double value);
quint16 encodeSignedHundredths(double value);
quint16 encodeManualOutput(double percent);
double decodeSignedTenths(quint16 value);
double decodeUnsignedTenths(quint16 value);
double decodeSignedHundredths(quint16 value);
double decodeManualOutput(quint16 value);
ChannelControlState decodeChannelControlState(quint16 statusRegister, int channel);
QString channelControlStateName(ChannelControlState state, bool english);
quint16 encodeChannelInput(int group, int correctionEntry);
int decodeChannelInputGroup(quint16 value);
int decodeCorrectionEntry(quint16 value);
quint16 encodeBaudRate(int baudRate);
int decodeBaudRate(quint16 value);
bool isSupportedBaudRate(int baudRate);
bool isDocumentedRunState(quint16 value);
QString pageName(Page page, bool english);

} // namespace VaporView::Ai8TemperatureControllerProtocol
