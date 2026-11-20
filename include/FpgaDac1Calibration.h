#pragma once

#include <QtGlobal>
#include <cmath>

namespace VaporView::FpgaDac1Calibration {

// Driver input (Keysight CH2, DC, 1 MΩ), 100 Hz saw + 20 kHz sine,
// 500 kS/s DAC updates. Fit separate waveform components, excluding reset edges.
constexpr double Q31Scale = 2147483648.0;
constexpr double BiasAtZero = 3.578914;
constexpr double BiasGain = 4.740717;
constexpr double SawGain = 4.749054;
constexpr double ModulationGain = 4.717095;

constexpr double MinInputV = 0.0;
constexpr double MaxInputV = 5.0;

// Fixed hardware clamp inside the measured loaded sweep (3.62 to 4.62 V).
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

inline double scanVolts(qint32 code) { return code * SawGain / Q31Scale; }
inline qint32 scanCode(double volts) { return qint32(std::llround(volts * Q31Scale / SawGain)); }

inline double modulationVolts(qint32 code) { return code * ModulationGain / Q31Scale; }
inline qint32 modulationCode(double volts) { return qint32(std::llround(volts * Q31Scale / ModulationGain)); }

// Signed Vpp keeps the polarity of previously saved waveforms.
inline double scanVpp(qint32 code) { return 2 * scanVolts(code); }
inline qint32 scanCodeFromVpp(double vpp) { return scanCode(vpp / 2); }
inline double modulationVpp(qint32 code) { return 2 * modulationVolts(code); }
inline qint32 modulationCodeFromVpp(double vpp) { return modulationCode(vpp / 2); }

enum class Limit { WithinGuard, OutsideHardwareGuard, OutsideInputRange };
struct TargetRange { double lowV, highV; Limit limit; };

inline TargetRange targetRange(qint32 bias, qint32 scan, qint32 modulation)
{
    const double peakV = std::abs(scanVolts(scan)) + std::abs(modulationVolts(modulation));
    const double lowV = biasVolts(bias) - peakV, highV = biasVolts(bias) + peakV;
    // Each of the three target values rounds to the nearest Q1.31 code.
    constexpr double roundingV = (BiasGain + SawGain + ModulationGain) / (2 * Q31Scale);
    if (lowV < MinInputV - roundingV || highV > MaxInputV + roundingV)
        return {lowV, highV, Limit::OutsideInputRange};
    const qint64 peak = std::abs(qint64(scan)) + std::abs(qint64(modulation));
    const bool inside = qint64(bias) - peak >= MinSample && qint64(bias) + peak <= MaxSample;
    return {lowV, highV, inside ? Limit::WithinGuard : Limit::OutsideHardwareGuard};
}

} // namespace VaporView::FpgaDac1Calibration
