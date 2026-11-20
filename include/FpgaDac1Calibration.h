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

constexpr qint32 DefaultBias = 245455370;
// The RTL maps normalized signed samples to 20-bit offset binary at unity gain.
constexpr double dacCodeAtBias(double volts)
{
    return (1.0 + (volts - BiasAtZero) / BiasGain) * (0xfffff / 2.0);
}
// Round inwards so both output codes are inside the calibrated DC rails.
inline const quint32 MinDacCode = quint32(std::ceil(dacCodeAtBias(MinInputV)));
inline const quint32 MaxDacCode = quint32(std::floor(dacCodeAtBias(MaxInputV)));
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

struct TargetRange {
    double lowV, highV;

    bool withinInputRange() const
    {
        // Each of the three requested components rounds to the nearest Q1.31 code.
        constexpr double roundingV = (BiasGain + SawGain + ModulationGain) / (2 * Q31Scale);
        return lowV >= MinInputV - roundingV && highV <= MaxInputV + roundingV;
    }
};

inline TargetRange targetRange(qint32 bias, qint32 scan, qint32 modulation)
{
    const double peakV = std::abs(scanVolts(scan)) + std::abs(modulationVolts(modulation));
    return {biasVolts(bias) - peakV, biasVolts(bias) + peakV};
}

} // namespace VaporView::FpgaDac1Calibration
