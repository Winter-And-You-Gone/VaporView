#ifndef VAPORVIEW_RTK_STREAM_SERVICE_H
#define VAPORVIEW_RTK_STREAM_SERVICE_H

#include <QString>

#include <array>
#include <memory>

struct RtkStreamConfig
{
    enum class OutputMode
    {
        Serial,
        TcpClient,
    };

    QString server;
    QString port;
    QString username;
    QString password;
    QString mountpoint;
    QString outputPort;
    QString outputPathOverride;
    OutputMode outputMode = OutputMode::Serial;
    int baudrate = 115200;
    int timeoutMs = 5000;
    int reconnectMs = 1000;
    int relayBack = 1;
    bool sendNmeaGga = false;
    int nmeaGgaCycleMs = 1000;
    double nmeaLatitudeDeg = 0.0;
    double nmeaLongitudeDeg = 0.0;
    double nmeaHeightM = 0.0;
};

struct RtkStreamStats
{
    bool running = false;
    int inputBytes = 0;
    int outputBytes = 0;
    int inputBps = 0;
    int outputBps = 0;
    std::array<int, 5> streamStates = {};
    QString streamStateMask;
    QString message;
};

class RtkStreamService
{
public:
    RtkStreamService();
    ~RtkStreamService();

    bool start(const RtkStreamConfig &config, QString *errorMessage = nullptr);
    void stop();
    bool isRunning() const;
    RtkStreamStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
