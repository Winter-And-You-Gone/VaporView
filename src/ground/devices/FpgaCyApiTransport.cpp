#include "FpgaCyApiTransport.h"

#include <QStringList>
#include <QElapsedTimer>
#include <limits>
#if defined(Q_OS_WIN) && defined(VAPORVIEW_HAS_CYAPI)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <CyAPI.h>
#endif

namespace VaporView::Ground::Devices
{
struct FpgaCyApiTransport::Impl
{
#if defined(Q_OS_WIN) && defined(VAPORVIEW_HAS_CYAPI)
    std::unique_ptr<CCyUSBDevice> device;
    CCyUSBEndPoint *in = nullptr;
    CCyUSBEndPoint *out = nullptr;
#endif
};

FpgaCyApiTransport::FpgaCyApiTransport(QObject *parent)
    : FpgaUsbTransport(parent), impl_(std::make_unique<Impl>())
{
#if defined(Q_OS_WIN) && defined(VAPORVIEW_HAS_CYAPI)
    diagnostic_.hardwareBackendAvailable = true;
    diagnostic_.detail = QStringLiteral("Cypress CyAPI backend (CyUSB driver)");
#else
    diagnostic_.detail = QStringLiteral("Cypress CyAPI backend unavailable: this build has no Windows CyAPI SDK");
#endif
    diagnostic_.fields.insert(QStringLiteral("backend"), QStringLiteral("cypress"));
}
FpgaCyApiTransport::~FpgaCyApiTransport() { close(); }
bool FpgaCyApiTransport::fail(const QString &message)
{
    close();
    diagnostic_.state = FpgaUsbTransportState::Error;
    diagnostic_.detail = message;
    publishState(diagnostic_.state);
    publishError(message);
    return false;
}
bool FpgaCyApiTransport::open(const QString &locator)
{
    close();
#if defined(Q_OS_WIN) && defined(VAPORVIEW_HAS_CYAPI)
    diagnostic_.state = FpgaUsbTransportState::Opening;
    publishState(diagnostic_.state);
    // CyAPI's default constructor may open the first device; disable that behavior.
    impl_->device = std::make_unique<CCyUSBDevice>(nullptr, CYUSBDRV_GUID, false);
    QStringList candidates;
    int selected = -1;
    const int count = impl_->device->DeviceCount();
    for (int i=0; i<count; ++i)
    {
        if (!impl_->device->Open(UCHAR(i))) continue;
        if (impl_->device->VendorID == FpgaUsbTransportInfo::VendorId
            && impl_->device->ProductID == FpgaUsbTransportInfo::ProductId)
        {
            const QString path = QString::fromLocal8Bit(impl_->device->DevPath);
            candidates.append(path);
            if (locator.isEmpty() || locator.compare(path, Qt::CaseInsensitive) == 0) selected = i;
        }
        impl_->device->Close();
    }
    diagnostic_.fields.insert(QStringLiteral("locators"), candidates);
    if (locator.isEmpty() && candidates.size() > 1)
    {
        const QString message = QStringLiteral("Multiple Cypress GP01 devices found; select a USB locator: %1").arg(candidates.join(QStringLiteral("; ")));
        fail(message);
        diagnostic_.fields.insert(QStringLiteral("locators"), candidates);
        return false;
    }
    if (selected < 0 || !impl_->device->Open(UCHAR(selected)))
        return fail(QStringLiteral("Cypress GP01 device not found for the requested locator"));
    impl_->in = impl_->device->EndPointOf(FpgaUsbTransportInfo::BulkInEndpoint);
    impl_->out = impl_->device->EndPointOf(FpgaUsbTransportInfo::BulkOutEndpoint);
    if (!impl_->in || !impl_->out || (impl_->in->Attributes & 3) != 2 || (impl_->out->Attributes & 3) != 2)
        return fail(QStringLiteral("Cypress GP01 bulk endpoints 0x86 IN / 0x02 OUT not found"));
    impl_->in->TimeOut = 50;
    impl_->out->TimeOut = 1000;
    if (!impl_->device->ControlEndPt) return fail(QStringLiteral("Cypress control endpoint missing"));
    impl_->device->ControlEndPt->TimeOut = 1000;
    QByteArray b1, b0;
    if (!readDiagnostic(FpgaUsbTransportInfo::DiagnosticB1, b1) || !readDiagnostic(FpgaUsbTransportInfo::DiagnosticB0, b0))
        return fail(QStringLiteral("Cypress device failed GP01 diagnostic identity verification"));
    diagnostic_.fields.insert(QStringLiteral("backend"), QStringLiteral("cypress"));
    diagnostic_.fields.insert(QStringLiteral("locator"), QString::fromLocal8Bit(impl_->device->DevPath));
    diagnostic_.fields.insert(QStringLiteral("diagnostic_b1"), QString::fromLatin1(b1.toHex()));
    diagnostic_.fields.insert(QStringLiteral("diagnostic_b0"), QString::fromLatin1(b0.toHex()));
    diagnostic_.state = FpgaUsbTransportState::Open;
    diagnostic_.detail = QStringLiteral("GP01 FX3 Cypress CyAPI open");
    publishState(diagnostic_.state);
    return true;
#else
    Q_UNUSED(locator);
    return fail(QStringLiteral("Cypress CyAPI backend unavailable: this build has no Windows CyAPI SDK"));
#endif
}
void FpgaCyApiTransport::close()
{
#if defined(Q_OS_WIN) && defined(VAPORVIEW_HAS_CYAPI)
    if (impl_->device) impl_->device->Close();
    impl_->device.reset();
    impl_->in = nullptr;
    impl_->out = nullptr;
#endif
    diagnostic_.fields.clear();
    diagnostic_.fields.insert(QStringLiteral("backend"), QStringLiteral("cypress"));
    diagnostic_.state = FpgaUsbTransportState::Offline;
    publishState(diagnostic_.state);
}
bool FpgaCyApiTransport::isOpen() const
{
#if defined(Q_OS_WIN) && defined(VAPORVIEW_HAS_CYAPI)
    return impl_->device && impl_->device->IsOpen();
#else
    return false;
#endif
}
qint64 FpgaCyApiTransport::read(QByteArray &destination, qint64 maxBytes)
{
    destination.clear();
#if defined(Q_OS_WIN) && defined(VAPORVIEW_HAS_CYAPI)
    if (!isOpen() || maxBytes <= 0 || maxBytes > std::numeric_limits<LONG>::max()) return -1;
    destination.resize(qsizetype(maxBytes));
    LONG length = LONG(maxBytes);
    QElapsedTimer elapsed;
    elapsed.start();
    if (!impl_->in->XferData(reinterpret_cast<PUCHAR>(destination.data()), length))
    {
        const auto error = impl_->in->LastError;
        destination.clear();
        // Only an actual timeout is non-fatal; disconnect/abort must clear state.
        // CyAPI XferData aborts its pending request at TimeOut but older SDKs
        // leave LastError=ERROR_IO_PENDING; FinishDataXfer reports cancellation.
        if (error == ERROR_SEM_TIMEOUT || error == WAIT_TIMEOUT
            || (error == ERROR_IO_PENDING && elapsed.elapsed() >= impl_->in->TimeOut
                && impl_->in->NtStatus == 0xC0000120UL)) return 0;
        fail(QStringLiteral("Cypress bulk IN failed (%1)").arg(error));
        return -1;
    }
    if (length < 0 || length > maxBytes) { destination.clear(); fail(QStringLiteral("Cypress invalid read length")); return -1; }
    destination.resize(length);
    diagnostic_.bytesRead += quint64(length);
    if (length) emit bytesReceived(destination);
    return length;
#else
    Q_UNUSED(maxBytes);
    return -1;
#endif
}
qint64 FpgaCyApiTransport::write(const QByteArray &source)
{
#if defined(Q_OS_WIN) && defined(VAPORVIEW_HAS_CYAPI)
    if (!isOpen() || source.isEmpty() || source.size()%4 || source.size() > std::numeric_limits<LONG>::max()) return -1;
    QByteArray copy = source; // The vendor API takes a mutable buffer.
    LONG length = LONG(copy.size());
    if (!impl_->out->XferData(reinterpret_cast<PUCHAR>(copy.data()), length))
    { fail(QStringLiteral("Cypress bulk OUT failed (%1)").arg(impl_->out->LastError)); return -1; }
    diagnostic_.bytesWritten += quint64(length);
    if (length != source.size()) { fail(QStringLiteral("Cypress partial bulk OUT write")); return -1; }
    return length;
#else
    Q_UNUSED(source);
    return -1;
#endif
}
FpgaUsbDiagnostic FpgaCyApiTransport::diagnostic() const { return diagnostic_; }
bool FpgaCyApiTransport::readDiagnostic(quint8 request, QByteArray &response)
{
    response.clear();
#if defined(Q_OS_WIN) && defined(VAPORVIEW_HAS_CYAPI)
    if (!isOpen() || (request != FpgaUsbTransportInfo::DiagnosticB0 && request != FpgaUsbTransportInfo::DiagnosticB1)) return false;
    auto *control = impl_->device->ControlEndPt;
    control->Target = TGT_DEVICE;
    control->ReqType = REQ_VENDOR;
    control->Direction = DIR_FROM_DEVICE;
    control->ReqCode = request;
    control->Value = 0;
    control->Index = 0;
    response.resize(32);
    LONG length = 32;
    if (!control->Read(reinterpret_cast<PUCHAR>(response.data()), length) || length != 32)
    { response.clear(); return false; }
    if (request == FpgaUsbTransportInfo::DiagnosticB1 && response.left(8) != QByteArray::fromHex("47503031ac100000"))
    { response.clear(); return false; }
    return true;
#else
    Q_UNUSED(request);
    return false;
#endif
}
} // namespace VaporView::Ground::Devices
