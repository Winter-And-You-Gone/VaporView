#include "ground/wave/WaterProfileAccumulator.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
void check(bool ok, const char *message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool close(double a, double b) { return std::abs(a - b) < 1e-9; }
}

int main()
{
    WaterProfileAccumulator profile;
    int seq = 0;
    const auto add = [&](double height, double value) {
        ++seq;
        check(profile.append(0, QString::number(seq), seq, value, height), "accept valid sample");
    };
    for (int i = 0; i < 50; ++i) add(183.5, 9);
    auto layers = profile.layers(0);
    check(layers.size() == 1 && layers[0].sampleCount == 50 && layers[0].stdDev == 0, "stable hover");
    check(layers[0].minHeightM == 180 && layers[0].maxHeightM == 185 && layers[0].centerHeightM == 182.5, "layer geometry");
    profile.clearAll();
    for (int i = 1; i <= 3; ++i) add(101.01, i);
    layers = profile.layers(0, 1);
    check(layers.size() == 1 && layers.first().meanConcentration == 2 && layers.first().stdDev == 1, "Welford sample SD");
    profile.clearAll();
    for (int h : {0, 5, 10, 25, 10, 5, 0}) add(h, 3.5);
    layers = profile.layers(0);
    check(layers.size() == 4 && layers[2].sampleCount == 2 && layers[3].binIndex - layers[2].binIndex == 3, "descent and gap");
    for (const auto &layer : layers) check(layer.meanConcentration == 3.5, "vertical profile");

    profile.clearAll();
    std::vector<std::pair<double,double>> samples;
    for (int i = 0; i < 1600; ++i) {
        const double h = i == 0 ? 0 : 80.5 + i % 37;
        const double value = 99876.5 + (i % 9) * 0.125 + (i % 7);
        samples.emplace_back(h, value);
        add(h, value);
    }
    for (int step : {1, 5, 10, 20, 50, 1}) {
        layers = profile.layers(0, step);
        check(layers.first().binIndex == 0 && layers.first().sampleCount == 1, "low altitude survives 1000 samples");
        int total = 0, previous = -1;
        for (const auto &layer : layers) {
            check(layer.binIndex > previous, "ascending layers");
            previous = layer.binIndex;
            double sum = 0, squared = 0;
            int count = 0;
            for (auto [h, value] : samples) if (int(h) / step == layer.binIndex) { ++count; sum += value; }
            const double mean = sum / count;
            for (auto [h, value] : samples) if (int(h) / step == layer.binIndex) squared += (value - mean) * (value - mean);
            check(count == layer.sampleCount && close(mean, layer.meanConcentration), "Chan weighted mean");
            check(count == 1 ? std::isnan(layer.stdDev) : close(std::sqrt(squared / (count - 1)), layer.stdDev), "Chan sample SD");
            total += count;
        }
        check(total == samples.size(), "resolution preserves every sample");
    }
    profile.clearAll();
    const auto before = profile.revision();
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    for (double h : {nan, inf, -0.1, 20000.01}) check(!profile.append(0, "pair", 1, 3, h), "reject invalid height");
    for (double v : {nan, inf, -0.1}) check(!profile.append(0, "pair", 1, v, 0), "reject invalid concentration");
    check(profile.recentIdCount(0) == 0 && !profile.latest(0), "invalid samples reserve no IDs");
    check(profile.revision() == before, "invalid samples leave the revision unchanged");
    check(profile.append(0, "pair", 10, 0, 20000) && profile.append(1, "pair", 2, 5, 0), "late match and separate channels");
    check(profile.append(0, "older", 9, 2, 3) && !profile.append(0, "pair", 10, 0, 20000), "nonconsecutive duplicate");
    check(profile.revision() == before + 3, "only accepted points advance the revision, including older samples");
    check(profile.latest(0)->timeMs == 10 && profile.latest(0)->heightM == 20000 && profile.latest(0)->concentration == 0, "latest by time");
    profile.clearChannel(0);
    check(!profile.latest(0) && profile.layers(0).isEmpty() && profile.latest(1)->concentration == 5, "channel reset");
    profile.clearChannel(0);
    profile.clearChannel(-1);
    check(profile.revision() == before + 4, "only clearing a populated channel advances the revision");
    for (int i = 0; i < 100000; ++i) add(i % 20001, i % 13);
    check(profile.layers(0, 1).size() == 20001 && profile.recentIdCount(0) == profile.recentIdCapacity, "bounded bins and IDs");
    const auto populated = profile.revision();
    profile.clearAll();
    profile.clearAll();
    check(profile.revision() == populated + 1, "clearing both populated channels is one mutation; empty clear is a no-op");
    check(!profile.latest(1) && profile.recentIdCount(0) == 0 && profile.layers(0).isEmpty(), "full reset");
}
