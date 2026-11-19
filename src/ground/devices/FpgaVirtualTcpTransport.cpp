#include "FpgaVirtualTcpTransport.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QNetworkProxy>
#include <QTcpSocket>

namespace VaporView::Ground::Devices
{
namespace
{
constexpr qint64 kBufferLimit = 4 * 1024 * 1024;
constexpr int kConnectTimeoutMs = 1000;

class FpgaVirtualTcpTransport final : public FpgaUsbTransport
{
public:
    explicit FpgaVirtualTcpTransport(quint16 port)
        : socket_(this), port_(port), locator_(QStringLiteral("tcp://127.0.0.1:%1").arg(port))
    {
        socket_.setProxy(QNetworkProxy::NoProxy);
        socket_.setReadBufferSize(kBufferLimit);
        diagnostic_.vendorId = diagnostic_.productId = 0;
        diagnostic_.bulkOutEndpoint = diagnostic_.bulkInEndpoint = 0;
        diagnostic_.fields = {{"mode", FpgaVirtualTcpBackend}, {"locator", locator_}};
    }

    QJsonObject discoverDevices() const override
    {
        return {{"devices", QJsonArray{QJsonObject{{"locator", locator_},
            {"label", QStringLiteral("Virtual FPGA (127.0.0.1:%1)").arg(port_)},
            {"driver", "TCP"}, {"backend", FpgaVirtualTcpBackend}, {"available", port_ != 0}}}}};
    }

    bool open(const QString &locator) override
    {
        close();
        diagnostic_.bytesRead = diagnostic_.bytesWritten = 0;
        if (!port_ || (!locator.isEmpty() && locator != locator_)) {
            setError(QStringLiteral("Virtual FPGA endpoint is fixed to %1").arg(locator_));
            return false;
        }
        setState(FpgaUsbTransportState::Opening);
        socket_.connectToHost(QHostAddress::LocalHost, port_);
        if (socket_.state() != QAbstractSocket::ConnectedState && !socket_.waitForConnected(kConnectTimeoutMs)) {
            setError(QStringLiteral("Cannot connect to virtual FPGA at %1: %2").arg(locator_, socket_.errorString()));
            socket_.abort();
            return false;
        }
        socket_.setSocketOption(QAbstractSocket::LowDelayOption, 1);
        setState(FpgaUsbTransportState::Open);
        return true;
    }

    void close() override
    {
        socket_.abort();
        diagnostic_.detail.clear();
        setState(FpgaUsbTransportState::Offline);
    }

    // Keep the session open until read() surfaces the loss after draining the
    // socket. The common polling loop stops polling when isOpen() is false.
    bool isOpen() const override { return diagnostic_.state == FpgaUsbTransportState::Open; }

    qint64 read(QByteArray &destination, qint64 maxBytes) override
    {
        destination.clear();
        if (!isOpen() || maxBytes <= 0) return 0;
        if (socket_.bytesAvailable() > 0) {
            destination = socket_.read(maxBytes);
            diagnostic_.bytesRead += destination.size();
            return destination.size();
        }
        if (socket_.state() == QAbstractSocket::ConnectedState) return 0;
        setError(QStringLiteral("Virtual FPGA TCP connection lost at %1: %2").arg(locator_, socket_.errorString()));
        return -1;
    }

    qint64 write(const QByteArray &source) override
    {
        if (!isOpen() || socket_.state() != QAbstractSocket::ConnectedState ||
            source.size() > kBufferLimit - socket_.bytesToWrite()) return -1;
        const qint64 written = socket_.write(source);
        if (written > 0) diagnostic_.bytesWritten += written;
        if (written >= 0 && written != source.size()) socket_.abort();
        return written;
    }

    FpgaUsbDiagnostic diagnostic() const override { return diagnostic_; }

private:
    void setState(FpgaUsbTransportState state)
    {
        if (diagnostic_.state == state) return;
        diagnostic_.state = state;
        publishState(state);
    }

    void setError(const QString &detail)
    {
        diagnostic_.detail = detail;
        setState(FpgaUsbTransportState::Error);
        publishError(detail);
    }

    QTcpSocket socket_;
    const quint16 port_;
    const QString locator_;
    FpgaUsbDiagnostic diagnostic_;
};
}

std::unique_ptr<FpgaUsbTransport> makeFpgaVirtualTcpTransport(quint16 port)
{
    return std::make_unique<FpgaVirtualTcpTransport>(port);
}
}
