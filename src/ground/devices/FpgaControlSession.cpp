#include "ground/devices/FpgaControlSession.h"
#include "FpgaTelemetry.h"

namespace VaporView::Ground::Devices
{
FpgaControlSession::~FpgaControlSession() { shutdown(); }

void FpgaControlSession::startWorker()
{
    if (worker_.isRunning()) return;
    controller_ = new FpgaDeviceController;
    controller_->moveToThread(&worker_);
    QObject::connect(&worker_, &QThread::finished, controller_, &QObject::deleteLater);
    worker_.setObjectName(QStringLiteral("fpgaUsbWorkerThread"));
    worker_.start();
}

void FpgaControlSession::shutdown()
{
    if (!worker_.isRunning()) return;
    worker_.requestInterruption();
    QMetaObject::invokeMethod(controller_, &FpgaDeviceController::disconnectDevice, Qt::BlockingQueuedConnection);
    worker_.quit();
    worker_.wait();
    controller_ = nullptr;
}

bool FpgaControlSession::remoteReady() const
{
    return remoteStatus.value("ready").toBool() &&
        std::chrono::steady_clock::now() - statusTime < std::chrono::seconds(4);
}

FpgaControlSession::Submission FpgaControlSession::submitRemote(RemoteSkyController& remote,
    const QJsonObject& request, quint64 discovery, QString *error)
{
    if (!remote.isOpen()) return Submission::NotConnected;
    if (requestId) return Submission::Busy;
    const auto op = request.value("op").toString();
    if (op != "connect" && op != "disconnect" && op != "refresh" && op != "discover" && !remoteReady())
        return Submission::NotReady;
    if (!FpgaRemote::validateControl(request, error)) return Submission::Invalid;
    operation = op;
    remoteControl = request;
    discoveryGeneration = discovery;
    linkGeneration_ = remote.linkGeneration();
    requestId = remote.sendFpgaControl(request);
    return requestId ? Submission::Accepted : Submission::NotConnected;
}

bool FpgaControlSession::matches(quint32 request, const RemoteSkyController& remote) const
{
    return requestId && request == requestId && remote.isOpen() && remote.linkGeneration() == linkGeneration_;
}

void FpgaControlSession::resetRemote()
{
    remoteStatus = {};
    statusTime = {};
    requestId = 0;
    linkGeneration_ = 0;
}

void FpgaControlSession::updateRemoteStatus(const QJsonObject& status)
{
    remoteStatus = status;
    statusTime = std::chrono::steady_clock::now();
}
} // namespace VaporView::Ground::Devices
