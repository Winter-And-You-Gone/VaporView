#pragma once

#include <cmath>
#include <QRandomGenerator>

namespace VaporView {
enum class SimulatorDataMode { Realistic, Random };

namespace Sounding {
inline double noise() { return 2.0 * QRandomGenerator::global()->generateDouble() - 1.0; }

// Constant cruise speed with two seconds of acceleration/deceleration at each end.
inline double leg(double t, double duration, double from, double to)
{
    constexpr double ramp = 2.0;
    const double speed = (to - from) / (duration - ramp);
    if (t < ramp) return from + speed * t * t / (2 * ramp);
    if (t > duration - ramp) return to - speed * (duration - t) * (duration - t) / (2 * ramp);
    return from + speed * (t - ramp / 2);
}

inline double soundingHeightM(double seconds)
{
    const double t = std::fmod(seconds, 112.0);
    if (t < 16) return leg(t, 16, 5, 60);
    if (t < 21) return 60;
    if (t < 40) return leg(t - 21, 19, 60, 130);
    if (t < 45) return 130;
    if (t < 64) return leg(t - 45, 19, 130, 200);
    if (t < 69) return 200;
    return leg(t - 69, 43, 200, 5);
}

struct Observation {
    double heightM, pressurePa, temperatureC, humidityPct, cabinC, cabinHumidityPct, concentrationPpmv;
};

inline Observation observe(double t, SimulatorDataMode mode)
{
    if (mode == SimulatorDataMode::Random)
        return {105 + 95 * noise(), 100000 + 2200 * noise(), 23 + 4 * noise(), 45 + 10 * noise(),
                28 + 2 * noise(), 36 + 5 * noise(), 300 + 175 * noise()};
    const double h = soundingHeightM(t);
    return {h + 0.04 * noise(), 101325 * std::pow(1 - h / 44330, 5.255) + 0.6 * noise(),
            23.8 - 0.0065 * h + 0.015 * std::sin(t), 45.7 - 0.007 * h + 0.02 * std::sin(t),
            28.5 + 0.12 * std::sin(t / 48), 36 + 0.24 * std::cos(t / 53),
            35 + 440 * std::exp(-h / 240)};
}

// z is the offset from the line center in units of Gaussian linewidth (sigma).
inline double absorption(double z) { return std::exp(-0.5 * z * z); }
inline double secondHarmonic(double z) { return (1 - z * z) * absorption(z); }
}
}
