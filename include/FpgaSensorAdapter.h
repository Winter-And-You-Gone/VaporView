#pragma once

#include "FpgaSensorDecoder.h"
#include "data_types.h"
#include "Ai8TemperatureControllerProtocol.h"

namespace VaporView::FpgaSensor
{
struct Ai8Sample
{
    int channel = 1;
    double measuredC = 0.0;
    double setpointC = 0.0;
    double displayedSetpointC = 0.0;
    quint32 outputRaw = 0;
    quint8 alarm = 0;
    quint8 control = 0;
    quint16 host = 0;
    quint32 deviceStatus = 0;
    quint32 deviceError = 0;
    quint32 sampleCounter = 0;
    quint32 setResultRaw = 0;
    bool online = false;
    bool measurementValid = false;
    bool alarmActive = false;
    bool setResultValid = false;
    bool setSucceeded = false;
};

struct AdaptedMeasurements
{
    Reading reading;
    std::optional<EpsilonData> epsilon;
    std::optional<Ai8Sample> ai8;
    std::optional<Ai8TemperatureControllerProtocol::LiveData> ai8Live;
    bool epsilonStatusFresh = false;
    bool orientationInitialized = false;
    bool navigationInitialized = false;
    bool headingInitialized = false;
    bool utcInitialized = false;
    bool attitudeValid = false;
    bool navigationValid = false;
    bool utcValid = false;
};

class FpgaSensorAdapter
{
public:
    // Host policy in FPGA timestamp ticks; deliberately not a protocol constant.
    explicit FpgaSensorAdapter(quint64 epsilonStatusMaxAgeTicks = 200000000,
                               int ai8Channel = 1);
    AdaptedMeasurements accept(const Reading &reading);
    void reset();
    void setAi8Channel(int channel) { ai8Channel_ = channel; }

private:
    QByteArray bmpCalibration_;
    EpsilonData epsilon_;
    std::optional<quint64> epsilonStatusTimestamp_;
    quint64 epsilonStatusMaxAgeTicks_;
    int ai8Channel_;
};
} // namespace VaporView::FpgaSensor
