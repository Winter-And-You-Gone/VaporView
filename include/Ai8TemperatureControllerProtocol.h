#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <array>

namespace VaporView::Ai8TemperatureControllerProtocol
{

constexpr int kChannelCount = 8;
constexpr int kParameterGroupCount = 4;
constexpr int kControlStatusRegisterCount = (kChannelCount + 1) / 2;
constexpr int kAlarmStatusRegisterCount = (kChannelCount + 1) / 2;
constexpr int kDefaultBaudRate = 19200;
inline constexpr std::array<int, 6> kSupportedBaudRates = {
    4800, 9600, 19200, 38400, 57600, 115200};

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
    ProgramNumberBase = 0x02A0,
    WorkModeBase = 0x0300,
    ManualOutputBase = 0x0360,
    HighAlarmBase = 0x03C0,
    LowAlarmBase = 0x0420,
    DisplayedSetpointBase = 0x0480,
    MeasuredValueBase = 0x0600,
    HighResolutionMeasuredValueBase = 0x0660,
    AlarmStatusBase = 0x0680,
    ControlStatusBase = 0x06C0,
    InputTypeBase = 0x0800,
    ScaleLowBase = 0x0804,
    ScaleHighBase = 0x0808,
    FilterBase = 0x080C,
    DeviationHighAlarmBase = 0x0810,
    DeviationLowAlarmBase = 0x0814,
    AlarmResetBase = 0x0818,
    HysteresisBase = 0x081C,
    OutputLowBase = 0x0820,
    OutputHighBase = 0x0824,
    OutputHighThresholdBase = 0x0828,
    ControlActionBase = 0x082C,
    RiseSlopeBase = 0x0830,
    FallSlopeBase = 0x0834,
    SetpointLowLimitBase = 0x0838,
    SetpointHighLimitBase = 0x083C,
    Address = 0x0840,
    BaudRate = 0x0841,
    LocalInputChannelCount = 0x0842,
    ExpansionInputChannelCount = 0x0843,
    ControlChannelCount = 0x0844,
    RunState = 0x0845,
    ControlCycle = 0x0846,
    CommonAlarmOutput = 0x0847,
    IndependentAlarmChannelCount = 0x0848,
    IndependentAlarmMask = 0x0849,
    AlarmFunctionA = 0x084A,
    AlarmFunctionB = 0x084B,
    ParityFlags = 0x084C,
    AlarmPolarity = 0x084D,
    SampleMode = 0x084E,
    ExtraHysteresis = 0x084F,
    DecimalPoint = 0x0850,
    MainStatus = 0x0851,
    ParameterLock = 0x0852,
    ModelFeature = 0x0853,
    SerialNumberHigh = 0x0854,
    SerialNumberLow = 0x0855,
    OutputStartChannel = 0x0856,
    HighResolutionFilter = 0x0857,
    Aif1 = 0x0858,
    Aif2 = 0x0859,
    P1faAif3 = 0x085A,
    Difa = 0x085B,
    Spsr = 0x085C,
    AtFunction = 0x085D,
    AiflP1pr = 0x085E,
    P1tiOpsn = 0x085F,
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
    int channelInputGroup = 1;
    int correctionEntry = 0;
    double measurementOffset = 0.0;
    int channelOutputGroupRaw = 0;
    int programNumber = 0;
    int workMode = 0;
    double manualOutputPercent = 0.0;
    double highAlarmC = 3200.0;
    double lowAlarmC = -999.0;
    double displayedSetpointC = 0.0;
    quint8 alarmStatusRaw = 0;
    bool alarmStatusValid = false;
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
    double deviationHighAlarm = 3200.0;
    double deviationLowAlarm = -999.0;
    double hysteresis = 0.0;
    int outputLowPercent = 0;
    int outputHighPercent = 100;
    double outputHighThreshold = 3200.0;
    double riseSlope = 0.0;
    double fallSlope = 0.0;
    double setpointLowLimit = -999.0;
    double setpointHighLimit = 3200.0;
    int alarmResetFlags = 0;
};

struct GlobalParameters
{
    int address = 1;
    int baudRate = 19200;
    int localInputChannelCount = kChannelCount;
    int expansionInputChannelCount = kChannelCount;
    int controlChannelCount = kChannelCount;
    double controlCycleS = 0.0;
    quint16 runStateRaw = 0;
    bool runStateIsDocumented = true;
    bool runStateWriteRequested = false;
    quint16 runStateWriteValue = 0;
    int commonAlarmOutput = 0;
    int independentAlarmChannelCount = 0;
    int independentAlarmMask = 0;
    int alarmFunctionA = 0;
    int alarmFunctionB = 0;
    int parameterLock = 0;
    int sampleMode = 0;
    int decimalPoint = 1;
    int parityFlags = 0;
    int alarmPolarity = 0;
    double extraHysteresis = 0.0;
    quint16 mainStatusRaw = 0;
    int modelFeature = 0;
    quint32 serialNumber = 0;
    int outputStartChannel = 0;
    int highResolutionFilter = 0;
    int aif1 = 0;
    int aif2 = 0;
    int p1faAif3 = 0;
    int difa = 0;
    int spsr = 0;
    int atFunction = 0;
    int aiflP1pr = 0;
    int p1tiOpsn = 0;
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
    std::array<quint16, kAlarmStatusRegisterCount> alarmStatusRegisters{};
    bool valid = false;
    bool controlStatesValid = false;
    bool alarmStatusValid = false;
    quint16 mainStatusRaw = 0;
    bool mainStatusValid = false;
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
quint8 decodeChannelAlarmStatus(quint16 statusRegister, int channel);
ChannelControlState decodeChannelControlState(quint16 statusRegister, int channel);
QString channelControlStateName(ChannelControlState state, bool english);
quint16 encodeChannelInput(int group, int correctionEntry);
int decodeChannelInputGroup(quint16 value);
int decodeCorrectionEntry(quint16 value);
quint16 encodeBaudRate(int baudRate);
int decodeBaudRate(quint16 value);
constexpr bool isSupportedBaudRate(int baudRate)
{
    for (int supportedRate : kSupportedBaudRates)
    {
        if (baudRate == supportedRate)
        {
            return true;
        }
    }
    return false;
}
bool isDocumentedRunState(quint16 value);
QString pageName(Page page, bool english);

} // namespace VaporView::Ai8TemperatureControllerProtocol

Q_DECLARE_METATYPE(VaporView::Ai8TemperatureControllerProtocol::LiveData)
