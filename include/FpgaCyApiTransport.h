#pragma once
#include "FpgaUsbTransport.h"

namespace VaporView::Ground::Devices
{
// Optional Cypress C++ SDK backend for devices using the CyUSB driver.
// Builds without the SDK expose an explicit unavailable diagnostic.
class FpgaCyApiTransport final : public FpgaUsbTransport
{
public:
    explicit FpgaCyApiTransport(QObject *parent = nullptr);
    ~FpgaCyApiTransport() override;
    bool open(const QString &locator = {}) override;
    void close() override;
    bool isOpen() const override;
    qint64 read(QByteArray &destination, qint64 maxBytes) override;
    qint64 write(const QByteArray &source) override;
    FpgaUsbDiagnostic diagnostic() const override;
    bool readDiagnostic(quint8 request, QByteArray &response) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    FpgaUsbDiagnostic diagnostic_;
    bool fail(const QString &message);
};
} // namespace VaporView::Ground::Devices
