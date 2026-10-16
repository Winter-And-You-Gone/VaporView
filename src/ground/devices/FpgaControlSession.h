#pragma once

#include "FpgaDeviceController.h"
#include "ground/devices/RemoteSkyController.h"
#include <QThread>
#include <chrono>

namespace VaporView::Ground::Devices
{
class FpgaControlSession
{
public:
    enum class Submission { Accepted, NotConnected, Busy, NotReady, Invalid };
    FpgaControlSession() = default;
    ~FpgaControlSession();
    FpgaControlSession(const FpgaControlSession&) = delete;
    FpgaControlSession& operator=(const FpgaControlSession&) = delete;
    FpgaDeviceController* controller() const { return controller_; }
    void startWorker();
    void shutdown();
    Submission submitRemote(RemoteSkyController& remote, const QJsonObject& request,
                            quint64 discovery, QString *error);
    bool matches(quint32 request, const RemoteSkyController& remote) const;
    void finishRemote() { requestId = 0; }
    void resetRemote();
    void updateRemoteStatus(const QJsonObject& status);
    bool remoteReady() const;

    FpgaControlConfig remoteConfig;
    QJsonObject remoteStatus, remoteControl;
    quint32 requestId = 0;
    QString operation;
    quint64 discoveryGeneration = 0;
    std::chrono::steady_clock::time_point statusTime{};
private:
    QThread worker_;
    FpgaDeviceController *controller_ = nullptr;
    quint64 linkGeneration_ = 0;
};
} // namespace VaporView::Ground::Devices
