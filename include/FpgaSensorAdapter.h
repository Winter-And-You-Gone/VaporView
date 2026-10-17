#pragma once

#include "FpgaSensorDecoder.h"
#include "data_types.h"
#include "Ai8TemperatureControllerProtocol.h"
#include <QMap>

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
    double sampleRateHz = 0.0;
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
    void resetRate(quint16 source) {
        rates_.remove(source);
        if(source==0x46) {for(int i=1;i<=8;++i)rates_.remove(quint16(0x4600+i));ai8ScanValues_={};ai8ScanTicks_={};}
    }
    void setAi8Channel(int channel) { ai8Channel_ = channel; }

private:
    struct RateWindow {
        quint64 start = 0, last = 0, count = 0;
        double hz = 0.0;
    };
    double updateRate(const Reading &reading);
    QMap<quint16, RateWindow> rates_;
    QByteArray bmpCalibration_;
    EpsilonData epsilon_;
    std::optional<quint64> epsilonStatusTimestamp_;
    quint64 epsilonStatusMaxAgeTicks_;
    int ai8Channel_;
    std::array<std::optional<double>,8> ai8ScanValues_;
    std::array<quint64,8> ai8ScanTicks_{};
};
} // namespace VaporView::FpgaSensor
