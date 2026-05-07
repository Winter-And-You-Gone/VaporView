#include "SkyConfig.h"
#include "TelemetryCodec.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void testFrameRoundTrip()
{
    VaporView::TelemetryCodec codec;
    VaporView::TelemetryBasic basic;
    basic.host_time_us = 11;
    basic.epsilon_time_us = 22;
    basic.latitude_deg = 31.25;
    basic.longitude_deg = 121.5;
    basic.height_m = 1234.5;
    basic.ecef_x_m = 1.0;
    basic.ecef_y_m = 2.0;
    basic.ecef_z_m = 3.0;
    basic.lidar_height_m = 44.0f;
    basic.temperature_c = 25.0f;
    basic.humidity_percent = 60.0f;
    basic.pressure_hpa = 900.0f;
    basic.status_bits = 0x55AA;
    basic.filter_status_bits = 0x0060;
    basic.update_status_bits = 0x0003;
    basic.gnss_fix_code = 6;
    basic.validity_flags = VaporView::BasicHasEpsilonTime |
                           VaporView::BasicHasPosition |
                           VaporView::BasicHasEcef |
                           VaporView::BasicHasLidar |
                           VaporView::BasicHasTemperature |
                           VaporView::BasicHasHumidity |
                           VaporView::BasicHasPressure;

    const QByteArray payload = VaporView::TelemetryCodec::serializeBasicTelemetry(basic);
    const QByteArray frame = codec.encodeFrame(VaporView::MsgType::TelemetryBasic, payload, 7, 99);
    const QByteArray noisy = QByteArray("noise") + frame.left(frame.size() / 2);
    require(codec.feedBytes(noisy).isEmpty(), "partial frame should not decode");
    const QVector<VaporView::TelemetryFrame> frames = codec.feedBytes(frame.mid(frame.size() / 2) + frame);
    require(frames.size() == 2, "sticky frame decode count");
    VaporView::TelemetryBasic parsed;
    require(VaporView::TelemetryCodec::parseBasicTelemetry(frames.front().payload, parsed), "parse basic telemetry");
    require(parsed.host_time_us == basic.host_time_us, "basic host time");
    require(std::fabs(parsed.latitude_deg - basic.latitude_deg) < 0.000001, "basic latitude");
    require(parsed.filter_status_bits == basic.filter_status_bits, "basic filter status");
    require(parsed.gnss_fix_code == basic.gnss_fix_code, "basic gnss fix");
    require(parsed.validity_flags == basic.validity_flags, "basic validity flags");
}

void testCrcError()
{
    VaporView::TelemetryCodec encoder;
    VaporView::TelemetryCodec decoder;
    QByteArray frame = encoder.encodeFrame(VaporView::MsgType::Heartbeat, QByteArray("abc"), 1, 2);
    frame[frame.size() - 1] = static_cast<char>(frame.at(frame.size() - 1) ^ 0x7F);
    require(decoder.feedBytes(frame).isEmpty(), "bad crc should drop frame");
    require(decoder.crcErrorCount() > 0, "crc error count");
}

void testCommandAndDevicePayload()
{
    VaporView::CommandMessage command;
    command.command_id = VaporView::CommandId::ConnectDevice;
    command.command_seq = 42;
    command.payload = VaporView::TelemetryCodec::serializeDeviceCommand(VaporView::SkyDeviceId::Lidar);
    VaporView::CommandMessage parsed;
    require(VaporView::TelemetryCodec::parseCommand(VaporView::TelemetryCodec::serializeCommand(command), parsed), "parse command");
    require(parsed.command_id == VaporView::CommandId::ConnectDevice, "command id");
    VaporView::SkyDeviceId id = VaporView::SkyDeviceId::All;
    require(VaporView::TelemetryCodec::parseDeviceCommand(parsed.payload, id), "parse device payload");
    require(id == VaporView::SkyDeviceId::Lidar, "device id");

    VaporView::CommandAck ack;
    ack.command_id = command.command_id;
    ack.command_seq = command.command_seq;
    ack.result = 1;
    ack.error_code = VaporView::CommandErrorCode::DeviceConnectFailed;
    VaporView::CommandAck parsedAck;
    require(VaporView::TelemetryCodec::parseCommandAck(VaporView::TelemetryCodec::serializeCommandAck(ack), parsedAck), "parse ack");
    require(parsedAck.error_code == VaporView::CommandErrorCode::DeviceConnectFailed, "ack error");
}

void testWaveform()
{
    QVector<float> original;
    original.resize(50000);
    for (int i = 0; i < original.size(); ++i)
    {
        original[i] = static_cast<float>(i);
    }
    VaporView::DownsampledWaveform waveform;
    waveform.original_point_count = static_cast<quint32>(original.size());
    waveform.channel_id = 4;
    waveform.x_step = 10.0f;
    for (int i = 0; i < original.size(); i += 10)
    {
        waveform.samples.push_back(original.at(i));
    }
    waveform.downsampled_point_count = static_cast<quint32>(waveform.samples.size());
    VaporView::DownsampledWaveform parsed;
    require(VaporView::TelemetryCodec::parseDownsampledWaveform(VaporView::TelemetryCodec::serializeDownsampledWaveform(waveform), parsed), "parse waveform");
    require(parsed.samples.size() == 5000, "downsample count");
    require(parsed.samples.at(1234) == 12340.0f, "downsample value");
}

void testSkyConfigDiff()
{
    VaporView::SkyConfig a = VaporView::SkyConfig::defaults();
    VaporView::SkyConfig b = a;
    require(!a.diff(b).epsilon_changed, "unchanged epsilon");
    b.epsilon.baud_rate = 115200;
    const VaporView::SkyConfigDiff diff = a.diff(b);
    require(diff.epsilon_changed, "epsilon changed");
    require(!diff.ptb_changed && !diff.telemetry_changed, "other config unchanged");

    const QJsonObject json = b.toJson();
    VaporView::SkyConfig parsed;
    QString error;
    require(VaporView::SkyConfig::fromJson(json, parsed, &error), "sky config parse");
    require(parsed.epsilon.baud_rate == 115200, "sky config baud");
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testFrameRoundTrip();
    testCrcError();
    testCommandAndDevicePayload();
    testWaveform();
    testSkyConfigDiff();
    std::cout << "telemetry_codec_test passed\n";
    return 0;
}
