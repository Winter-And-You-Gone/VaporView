#pragma once

#include <QObject>
#include <QByteArray>
#include <QtGlobal>
#include <QString>
#include <QVariantMap>

#include <memory>

namespace VaporView::Ground::Devices
{

// GP01 FX3 application identity and bulk endpoint assignment.
struct FpgaUsbTransportInfo
{
    static constexpr quint16 VendorId = 0x04B4;
    static constexpr quint16 ProductId = 0x00F1;
    static constexpr quint8 BulkOutEndpoint = 0x02;
    static constexpr quint8 BulkInEndpoint = 0x86;
    static constexpr quint8 DiagnosticB0 = 0xB0;
    static constexpr quint8 DiagnosticB1 = 0xB1;
};

enum class FpgaUsbTransportState
{
    Offline,
    Opening,
    Open,
    Error,
};

struct FpgaUsbDiagnostic
{
    FpgaUsbTransportState state = FpgaUsbTransportState::Offline;
    QString detail;
    quint64 bytesRead = 0;
    quint64 bytesWritten = 0;
    quint16 vendorId = FpgaUsbTransportInfo::VendorId;
    quint16 productId = FpgaUsbTransportInfo::ProductId;
    quint8 bulkOutEndpoint = FpgaUsbTransportInfo::BulkOutEndpoint;
    quint8 bulkInEndpoint = FpgaUsbTransportInfo::BulkInEndpoint;
    bool hardwareBackendAvailable = false;
    QVariantMap fields;
};

// Transport boundary used by VLP framing. Implementations must not split a
// VLP frame at USB read boundaries; the caller owns reassembly and CRC checks.
class FpgaUsbTransport : public QObject
{
    Q_OBJECT

public:
    explicit FpgaUsbTransport(QObject *parent = nullptr);
    ~FpgaUsbTransport() override;

    virtual bool open(const QString &locator = QString()) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual qint64 read(QByteArray &destination, qint64 maxBytes) = 0;
    virtual qint64 write(const QByteArray &source) = 0;
    virtual FpgaUsbDiagnostic diagnostic() const = 0;

signals:
    void stateChanged(VaporView::Ground::Devices::FpgaUsbTransportState state);
    void bytesReceived(const QByteArray &bytes);
    void errorOccurred(const QString &message);

protected:
    void publishState(FpgaUsbTransportState state);
    void publishError(const QString &message);
};

// Safe placeholder for builds without CyUSB/WinUSB. It never claims that
// hardware is connected and leaves the integration point replaceable.
class FpgaUsbHardwareTransport final : public FpgaUsbTransport
{
    Q_OBJECT

public:
    explicit FpgaUsbHardwareTransport(QObject *parent = nullptr);
    ~FpgaUsbHardwareTransport() override;

    bool open(const QString &locator = QString()) override;
    void close() override;
    bool isOpen() const override;
    qint64 read(QByteArray &destination, qint64 maxBytes) override;
    qint64 write(const QByteArray &source) override;
    FpgaUsbDiagnostic diagnostic() const override;

    // Read one of the GP01 vendor diagnostic pages (B0 or B1), 32 bytes.
    // This is available only when the native Windows backend is compiled.
    bool readDiagnostic(quint8 request, QByteArray &response);

private:
    FpgaUsbDiagnostic diagnostic_;
    void *deviceHandle_ = nullptr;
    void *interfaceHandle_ = nullptr;
};

// Deterministic offline transport. incoming bytes are consumed by read(); all
// writes are retained for assertions or later VLP replay tooling.
class FpgaUsbReplayTransport final : public FpgaUsbTransport
{
    Q_OBJECT

public:
    explicit FpgaUsbReplayTransport(QByteArray incoming = {}, QObject *parent = nullptr);

    bool open(const QString &locator = QString()) override;
    void close() override;
    bool isOpen() const override;
    qint64 read(QByteArray &destination, qint64 maxBytes) override;
    qint64 write(const QByteArray &source) override;
    FpgaUsbDiagnostic diagnostic() const override;

    void appendIncoming(const QByteArray &bytes);
    QByteArray writtenBytes() const;

private:
    FpgaUsbDiagnostic diagnostic_;
    QByteArray incoming_;
    QByteArray written_;
    bool open_ = false;
};

std::unique_ptr<FpgaUsbTransport> makeFpgaUsbHardwareTransport(QObject *parent = nullptr);
std::unique_ptr<FpgaUsbTransport> makeFpgaUsbReplayTransport(const QByteArray &incoming = {},
                                                              QObject *parent = nullptr);

}  // namespace VaporView::Ground::Devices

Q_DECLARE_METATYPE(VaporView::Ground::Devices::FpgaUsbTransportState)
