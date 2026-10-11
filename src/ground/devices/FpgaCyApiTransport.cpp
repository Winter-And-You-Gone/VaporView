#include "FpgaCyApiTransport.h"

#include <QStringList>
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
    QByteArray readBuffer;
    OVERLAPPED readOverlapped{};
    PUCHAR readContext = nullptr;
    ULONG readBeginError = ERROR_SUCCESS;
    unsigned cancelledReads = 0;

    void cancelRead()
    {
        if (readContext) {
            in->Abort();
            WaitForSingleObject(readOverlapped.hEvent, INFINITE);
            LONG length = LONG(readBuffer.size());
            in->FinishDataXfer(reinterpret_cast<PUCHAR>(readBuffer.data()), length, &readOverlapped, readContext);
            readContext = nullptr;
        }
        if (readOverlapped.hEvent) CloseHandle(readOverlapped.hEvent);
        readOverlapped = {};
        readBuffer.clear();
    }
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
    impl_->cancelRead();
    impl_->cancelledReads = 0;
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
    auto *endpoint = impl_->in;
    // Keep the read pending across idle polls. XferData aborts on every timeout,
    // and its hidden timeout cannot reliably be inferred from elapsed time.
    if (!impl_->readContext) {
        impl_->readBuffer.resize(qsizetype(maxBytes));
        impl_->readOverlapped.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!impl_->readOverlapped.hEvent) {
            fail(QStringLiteral("Cypress bulk IN event creation failed (%1)").arg(GetLastError()));
            return -1;
        }
        SetLastError(ERROR_SUCCESS);
        impl_->readContext = endpoint->BeginDataXfer(reinterpret_cast<PUCHAR>(impl_->readBuffer.data()),
                                                   LONG(maxBytes), &impl_->readOverlapped);
        impl_->readBeginError = endpoint->LastError;
    }
    const auto beginError = impl_->readBeginError;
    DWORD waitResult = WAIT_OBJECT_0;
    if (beginError == ERROR_IO_PENDING)
        waitResult = WaitForSingleObject(impl_->readOverlapped.hEvent, endpoint->TimeOut);
    if (impl_->readContext && waitResult == WAIT_TIMEOUT) return 0;
    if (waitResult != WAIT_OBJECT_0) {
        fail(QStringLiteral("Cypress bulk IN wait failed (%1)").arg(GetLastError()));
        return -1;
    }
    LONG length = LONG(impl_->readBuffer.size());
    SetLastError(ERROR_SUCCESS);
    const bool finished = impl_->readContext && endpoint->FinishDataXfer(
        reinterpret_cast<PUCHAR>(impl_->readBuffer.data()), length, &impl_->readOverlapped, impl_->readContext);
    const DWORD completionError = finished ? ERROR_SUCCESS : GetLastError();
    impl_->readContext = nullptr;
    CloseHandle(impl_->readOverlapped.hEvent);
    impl_->readOverlapped = {};
    if (!finished)
    {
        // A cancelled, empty IN request can be rearmed without replaying any
        // command. Keep this bounded until real data arrives; removal, stalls
        // and partial transfers must still fail rather than hide data loss.
        if (length == 0 && endpoint->NtStatus == 0xc0000120UL
            && endpoint->UsbdStatus == 0xc0010000UL && ++impl_->cancelledReads <= 3) {
            const auto key = QStringLiteral("cancelled_read_retries");
            diagnostic_.fields.insert(key, diagnostic_.fields.value(key).toULongLong() + 1);
            impl_->readBuffer.clear();
            return 0;
        }
        fail(QStringLiteral("Cypress bulk IN failed (Win32=%1, NT=0x%2, USBD=0x%3, bytes=%4)")
                 .arg(completionError).arg(endpoint->NtStatus, 8, 16, QLatin1Char('0'))
                 .arg(endpoint->UsbdStatus, 8, 16, QLatin1Char('0')).arg(length));
        return -1;
    }
    if (length < 0 || length > impl_->readBuffer.size() || length > maxBytes) {
        fail(QStringLiteral("Cypress invalid read length")); return -1;
    }
    if (length) impl_->cancelledReads = 0;
    destination = impl_->readBuffer.left(length);
    impl_->readBuffer.clear();
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
