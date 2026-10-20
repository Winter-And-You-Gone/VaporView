#include "FpgaSensorAdapter.h"
#include "EpsilonProtocol.h"

#include <cmath>

namespace VaporView::FpgaSensor
{
namespace
{
double unsigned16(const QByteArray &b, int i)
{
    return quint16(quint8(b[i])) | (quint16(quint8(b[i + 1])) << 8);
}
double signed16(const QByteArray &b, int i) { return qint16(unsigned16(b, i)); }
double signed8(const QByteArray &b, int i) { return qint8(b[i]); }
bool compensate(const QByteArray &b, quint32 rawP, quint32 rawT, double &p, double &t)
{
    if (b.size() != 21 || rawP >= 0x1000000 || rawT >= 0x1000000) return false;
    // Bosch BMP390 Table 24 / appendix 8.4--8.6; calibration 0x31..0x45.
    const double t1 = std::ldexp(unsigned16(b, 0), 8);
    const double t2 = std::ldexp(unsigned16(b, 2), -30);
    const double t3 = std::ldexp(signed8(b, 4), -48);
    const double p1 = std::ldexp(signed16(b, 5) - 16384, -20);
    const double p2 = std::ldexp(signed16(b, 7) - 16384, -29);
    const double p3 = std::ldexp(signed8(b, 9), -32);
    const double p4 = std::ldexp(signed8(b, 10), -37);
    const double p5 = std::ldexp(unsigned16(b, 11), 3);
    const double p6 = std::ldexp(unsigned16(b, 13), -6);
    const double p7 = std::ldexp(signed8(b, 15), -8);
    const double p8 = std::ldexp(signed8(b, 16), -15);
    const double p9 = std::ldexp(signed16(b, 17), -48);
    const double p10 = std::ldexp(signed8(b, 19), -48);
    const double p11 = std::ldexp(signed8(b, 20), -65);
    const double delta = rawT - t1;
    t = delta * t2 + delta * delta * t3;
    const double tSquared = t * t, tCubed = tSquared * t;
    const double raw = rawP;
    p = p5 + p6*t + p7*tSquared + p8*tCubed
        + raw*(p1 + p2*t + p3*tSquared + p4*tCubed)
        + raw*raw*(p9 + p10*t) + raw*raw*raw*p11;
    return std::isfinite(p) && std::isfinite(t);
}
bool llhFinite(const EpsilonData &d)
{
    return std::isfinite(d.latitude_deg) && std::abs(d.latitude_deg) <= 90
        && std::isfinite(d.longitude_deg) && std::abs(d.longitude_deg) <= 180
        && std::isfinite(d.height_m);
}
bool velocityFinite(const EpsilonData &d)
{
    return std::isfinite(d.vel_n_mps) && std::isfinite(d.vel_e_mps) && std::isfinite(d.vel_d_mps);
}
}

FpgaSensorAdapter::FpgaSensorAdapter(quint64 maxAge, int channel)
    : epsilonStatusMaxAgeTicks_(maxAge), ai8Channel_(channel) {}

void FpgaSensorAdapter::reset()
{
    rates_.clear();
    bmpCalibration_.clear();
    epsilon_ = EpsilonData{};
    epsilonStatusTimestamp_.reset();
    epsilonPosition_={};epsilonVelocity_={};
    ai8ScanValues_={};ai8ScanTicks_={};
}

double FpgaSensorAdapter::updateRate(const Reading &reading, quint16 key)
{
    // Count before presentation coalesces readings. FPGA ticks describe the
    // data cadence even when USB delivers a burst or a replay runs faster.
    if (!(reading.flags & 1)) return 0.0;
    auto it = rates_.find(key);
    if (it == rates_.end() || reading.timestamp < it->last
        || reading.timestamp - it->last > 500000000) {
        rates_[key] = {reading.timestamp, reading.timestamp, 0, 0.0};
        return 0.0;
    }
    it->last = reading.timestamp;
    ++it->count;
    const quint64 elapsed = reading.timestamp - it->start;
    if (elapsed >= 100000000) {
        it->hz = double(it->count) * 100000000.0 / double(elapsed);
        it->start = reading.timestamp;
        it->count = 0;
    }
    return it->hz;
}

AdaptedMeasurements FpgaSensorAdapter::accept(const Reading &input)
{
    AdaptedMeasurements out;
    out.reading = input;
    if (!input.validity.structure || !input.validity.crc) return out;
    if (input.kind == SensorKind::Bmp390)
    {
        if (!input.bmpCalibration.isEmpty())
            bmpCalibration_ = input.bmpCalibration.size() == 21 ? input.bmpCalibration : QByteArray{};
        out.reading.pressurePa.reset();
        out.reading.temperatureC.reset();
        out.reading.validity.measurement = false;
        double p, t;
        if (input.validity.measurement && input.bmpPressureRaw && input.bmpTemperatureRaw
            && compensate(bmpCalibration_, *input.bmpPressureRaw, *input.bmpTemperatureRaw, p, t))
        {
            out.reading.pressurePa = p;
            out.reading.temperatureC = t;
            out.reading.validity.measurement = true;
        }
    }
    else if (input.kind == SensorKind::Ai8 && (input.schema == 2 || input.schema == 3))
    {
        Ai8Sample s;
        s.channel = input.schema==3?int(input.ai8Channel.value_or(0)):ai8Channel_;
        s.deviceStatus = input.ai8DeviceStatus.value_or(0);
        s.deviceError = input.ai8DeviceError.value_or(0);
        s.sampleCounter = input.ai8SampleCounter.value_or(0);
        s.online = (s.deviceStatus & 1) != 0;
        s.measurementValid = input.validity.measurement && s.online
            && (s.deviceStatus & 0x1ffe) == 0 && input.ai8PvRaw && input.ai8SpRaw
            && input.ai8SvRaw && input.ai8OpRaw && input.ai8Alarm && input.ai8Control
            && input.ai8Host && input.ai8SetResult && input.ai8SampleCounter;
        s.measuredC = input.ai8PvRaw.value_or(0) / 10.;
        s.setpointC = input.ai8SpRaw.value_or(0) / 10.;
        s.displayedSetpointC = input.ai8SvRaw.value_or(0) / 10.;
        s.outputRaw = input.ai8OpRaw.value_or(0);
        s.alarm = quint8(input.ai8Alarm.value_or(0));
        s.control = quint8(input.ai8Control.value_or(0));
        s.host = quint16(input.ai8Host.value_or(0));
        s.alarmActive = s.alarm != 0 || (s.host & 0x300) != 0;
        s.setResultRaw = input.ai8SetResult.value_or(0);
        s.setResultValid = (s.setResultRaw & 0x10000) != 0;
        s.setSucceeded = s.setResultValid && (s.setResultRaw & 0xffff) == 0;
        out.ai8 = s;
        Ai8TemperatureControllerProtocol::LiveData live;
        live.measuredC.fill(std::numeric_limits<double>::quiet_NaN());
        if (s.channel >= 1 && s.channel <= Ai8TemperatureControllerProtocol::kChannelCount)
        {
            const int index = s.channel - 1;
            if (s.measurementValid) live.measuredC[index] = s.measuredC;
            const int shift = s.channel % 2 ? 8 : 0;
            live.alarmStatusRegisters[index / 2] = quint16(s.alarm) << shift;
            live.valid = s.measurementValid;
            if(s.measurementValid) {
                live.detailedChannel=quint8(s.channel);live.setpointC=s.setpointC;live.displayedSetpointC=s.displayedSetpointC;
                live.outputRaw=quint16(s.outputRaw);live.alarmRaw=s.alarm;live.controlRaw=s.control;
            }
            // The FPGA snapshot supplies exactly one channel, not all eight.
            // Global all-channel validity flags must remain false.
            live.mainStatusRaw = s.host;
            live.mainStatusValid = s.measurementValid;
        }
        if(input.schema==3 && s.channel>=1 && s.channel<=8) {
            for(int i=0;i<8;++i) {
                if(input.timestamp<ai8ScanTicks_[i] || input.timestamp-ai8ScanTicks_[i]>500000000)ai8ScanValues_[i].reset();
            }
            ai8ScanTicks_[s.channel-1]=input.timestamp;
            ai8ScanValues_[s.channel-1]=s.measurementValid?std::optional<double>(s.measuredC):std::nullopt;
            live.valid=false;
            for(int i=0;i<8;++i) {live.measuredC[i]=ai8ScanValues_[i].value_or(std::numeric_limits<double>::quiet_NaN());live.valid|=ai8ScanValues_[i].has_value();}
        }
        out.ai8Live = live;
        out.reading.validity.measurement = s.measurementValid;
    }
    else if (input.kind == SensorKind::Epsilon && input.validity.measurement && input.epsilonMessageId)
    {
        const quint8 id = *input.epsilonMessageId;
        if (epsilonStatusTimestamp_ && input.timestamp < *epsilonStatusTimestamp_)
        {
            epsilon_ = EpsilonData{};
            epsilonStatusTimestamp_.reset();
            epsilonPosition_={};epsilonVelocity_={};
        }
        // Raw FPGA ticks are a different epoch and remain in Reading.timestamp.
        // Business-model freshness uses the host's monotonic clock.
        epsilon_.timestamp = std::chrono::steady_clock::now();
        if (!EpsilonProtocol::decodeCorePacket(epsilon_, id,
                reinterpret_cast<const std::uint8_t *>(input.epsilonData.constData()),
                std::size_t(input.epsilonData.size()))) return out;
        if (id == 0x50 || id == 0x53) epsilonStatusTimestamp_ = input.timestamp;
        out.epsilonStatusFresh = epsilonStatusTimestamp_ && input.timestamp >= *epsilonStatusTimestamp_
            && input.timestamp - *epsilonStatusTimestamp_ <= epsilonStatusMaxAgeTicks_;
        const auto filter = epsilon_.filter_status_bits;
        out.orientationInitialized = out.epsilonStatusFresh && (filter & 1);
        out.navigationInitialized = out.epsilonStatusFresh && (filter & 2);
        out.headingInitialized = out.epsilonStatusFresh && (filter & 4);
        out.utcInitialized = out.epsilonStatusFresh && (filter & 8);
        EpsilonProtocol::resolveAttitudeState(epsilon_, epsilon_.timestamp);
        out.attitudeValid = (id == 0x41 || id == 0x63 || id == 0x64 || id == 0x50)
            && std::isfinite(epsilon_.roll_deg) && std::isfinite(epsilon_.pitch_deg)
            && std::isfinite(epsilon_.yaw_deg) && (id != 0x50 || out.orientationInitialized);
        const bool navigationPacket = id == 0x42 || id == 0x50 || id == 0x5c || id == 0x5d || id == 0x5f;
        bool finiteNavigation = id == 0x42 ? std::isfinite(epsilon_.ned_n_m) && std::isfinite(epsilon_.ned_e_m)
            && std::isfinite(epsilon_.ned_d_m) && velocityFinite(epsilon_)
            : id == 0x5f ? velocityFinite(epsilon_)
            : id == 0x5d ? std::isfinite(epsilon_.ecef_x_m) && std::isfinite(epsilon_.ecef_y_m) && std::isfinite(epsilon_.ecef_z_m)
            : llhFinite(epsilon_) && (id != 0x50 || velocityFinite(epsilon_));
        out.navigationValid = navigationPacket && out.navigationInitialized
            && !(epsilon_.system_status_bits & 1) && finiteNavigation;
        if (id == 0x59)
            out.navigationValid = epsilon_.gnss_fix_code >= 2 && epsilon_.gnss_fix_code <= 9 && llhFinite(epsilon_);
        out.utcValid = (id == 0x50 || id == 0x51 || id == 0x59)
            && (id == 0x59 ? epsilon_.gnss_time_valid : out.utcInitialized)
            && epsilon_.utc_unix_s >= 946684800 && epsilon_.utc_microseconds < 1000000;
        const auto remember = [&](NavigationSample& sample, bool valid) {
            sample={input.timestamp,epsilon_.timestamp,valid,id!=0x59};
        };
        if(id==0x50 || id==0x5c || id==0x59) remember(epsilonPosition_,out.navigationValid);
        if(id==0x42 || id==0x50 || id==0x5f || id==0x59)
            remember(epsilonVelocity_,out.navigationValid && (id!=0x59 || epsilon_.gnss_velocity_valid));
        const auto validTime = [&](const NavigationSample& sample)
            -> std::optional<std::chrono::steady_clock::time_point> {
            if(!sample.valid || !sample.tick || input.timestamp<*sample.tick
                || input.timestamp-*sample.tick>epsilonStatusMaxAgeTicks_) return std::nullopt;
            if(sample.requiresStatus ? !out.navigationInitialized || (epsilon_.system_status_bits&1)
                : epsilon_.gnss_fix_code<2 || epsilon_.gnss_fix_code>9) return std::nullopt;
            return sample.received;
        };
        out.epsilonPositionTime=validTime(epsilonPosition_);
        out.epsilonVelocityTime=validTime(epsilonVelocity_);
        const double rate=updateRate(input,quint16(0x4200+id));
        switch(id) {
        case 0x40: epsilon_.imu_packet_rate_hz=rate;break;
        case 0x41: epsilon_.ahrs_packet_rate_hz=rate;break;
        case 0x42: epsilon_.insgps_packet_rate_hz=rate;break;
        case 0x50: epsilon_.sys_state_packet_rate_hz=rate;break;
        case 0x53: epsilon_.status_packet_rate_hz=rate;break;
        case 0x59: epsilon_.raw_gnss_packet_rate_hz=rate;break;
        case 0x5a: epsilon_.satellite_packet_rate_hz=rate;break;
        case 0x5c: epsilon_.geodetic_packet_rate_hz=rate;break;
        case 0x5d: epsilon_.ecef_packet_rate_hz=rate;break;
        case 0x63: epsilon_.euler_orien_packet_rate_hz=rate;break;
        case 0x64: epsilon_.quat_orien_packet_rate_hz=rate;break;
        }
        epsilon_.valid = true; // A decoded packet; the separate flags describe each measurement.
        epsilon_.last_packet_id = id;
        epsilon_.last_serial_number = input.epsilonSequence.value_or(0);
        ++epsilon_.raw_frame_count;
        out.epsilon = epsilon_;
    }
    // Calibration is metadata; AI8 failed polls are not new snapshots.
    const bool sample = input.pressureMilliPa || input.bmpPressureRaw || input.bmpTemperatureRaw
        || (input.temperatureC && input.humidityPct) || input.distanceMm || out.epsilon
        || (out.ai8 && out.ai8->online && !(out.ai8->deviceStatus & 0x1ffe)) || input.rd105ActualMicroC;
    if (sample) out.sampleRateHz = updateRate(input,input.ai8Channel?quint16(0x4600+*input.ai8Channel):input.source);
    return out;
}
} // namespace VaporView::FpgaSensor
