#pragma once

#include <QtGlobal>
#include <cmath>

namespace VaporView::FpgaDac1Calibration {

// Driver input (Keysight CH1, DC, 1 MΩ), 100 Hz sawtooth, 500 kS/s DAC updates.
constexpr double Q31Scale = 2147483648.0;
constexpr double BiasAtZero = 3.58121;
constexpr double BiasGain = 4.65836;
constexpr double SawGain = 4.70943;
constexpr double SawPeakIntercept = 0.03312;

// Guarded interval from the widest measured sweep (3.62 to 4.62 V).
constexpr qint32 MinSample = 30143642;
constexpr qint32 MaxSample = 460767098;
constexpr qint32 DefaultBias = 245455370;
constexpr quint32 MinDacCode = 0x81cbf;
constexpr quint32 MaxDacCode = 0x9b76b;
constexpr quint32 ClearCode = 0x8ea15;
constexpr quint32 DeviceControl = 0x312;
constexpr quint32 UnityGain = 0x40000000;

inline double biasVolts(qint32 code) { return BiasAtZero + code * BiasGain / Q31Scale; }
inline qint32 biasCode(double volts) { return qint32(std::llround((volts - BiasAtZero) * Q31Scale / BiasGain)); }

inline double scanVolts(qint32 code)
{
    return code == 0 ? 0 : code * SawGain / Q31Scale + std::copysign(SawPeakIntercept, double(code));
}

inline qint32 scanCode(double volts)
{
    const double peak = std::abs(volts) - SawPeakIntercept;
    return peak > 0 ? qint32(std::llround(std::copysign(peak, volts) * Q31Scale / SawGain)) : 0;
}

inline double modulationVolts(qint32 code) { return code * BiasGain / Q31Scale; }
inline qint32 modulationCode(double volts) { return qint32(std::llround(volts * Q31Scale / BiasGain)); }

inline bool withinLimits(qint32 bias, qint32 scan, qint32 modulation)
{
    const qint64 peak = std::abs(qint64(scan)) + std::abs(qint64(modulation));
    return qint64(bias) - peak >= MinSample && qint64(bias) + peak <= MaxSample;
}

} // namespace VaporView::FpgaDac1Calibration
