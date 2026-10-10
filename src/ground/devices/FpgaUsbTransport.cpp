#include "FpgaUsbTransport.h"
#include "FpgaCyApiTransport.h"
#include <QStringList>

#include <QtGlobal>

#include <algorithm>
#include <limits>
#include <utility>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <setupapi.h>
#  include <initguid.h>
#  include <usbiodef.h>
#  include <winusb.h>
#  pragma comment(lib, "setupapi.lib")
#  pragma comment(lib, "winusb.lib")
#endif

namespace VaporView::Ground::Devices
{

FpgaUsbTransport::FpgaUsbTransport(QObject *parent)
    : QObject(parent)
{
}

FpgaUsbTransport::~FpgaUsbTransport() = default;

bool FpgaUsbTransport::readDiagnostic(quint8 request, QByteArray &response)
{
    Q_UNUSED(request);
    response.clear();
    return false;
}

void FpgaUsbTransport::publishState(FpgaUsbTransportState state)
{
    emit stateChanged(state);
}

void FpgaUsbTransport::publishError(const QString &message)
{
    emit errorOccurred(message);
}

FpgaUsbHardwareTransport::FpgaUsbHardwareTransport(QObject *parent)
    : FpgaUsbTransport(parent)
{
#ifdef Q_OS_WIN
    diagnostic_.hardwareBackendAvailable = true;
    diagnostic_.detail = QStringLiteral("native WinUSB backend (GP01 FX3)");
    diagnostic_.fields.insert(QStringLiteral("backend"), QStringLiteral("winusb"));
#else
    diagnostic_.detail = QStringLiteral("native WinUSB backend is available on Windows only");
#endif
}

FpgaUsbHardwareTransport::~FpgaUsbHardwareTransport()
{
    close();
}

bool FpgaUsbHardwareTransport::open(const QString &locator)
{
#ifdef Q_OS_WIN
    close();
    diagnostic_.state = FpgaUsbTransportState::Opening;
    publishState(diagnostic_.state);

    auto winError = [](const QString &prefix) {
        const DWORD code = GetLastError();
        wchar_t *message = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
        const QString text = message ? QString::fromWCharArray(message).trimmed() : QString();
        if (message)
            LocalFree(message);
        return QStringLiteral("%1 (Win32 error %2%3)")
            .arg(prefix).arg(code).arg(text.isEmpty() ? QString() : QStringLiteral(": ") + text);
    };

    QString path = locator;
    QStringList candidates;
    HDEVINFO info = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA interfaceData{};
    if (path.isEmpty()) {
        info = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_DEVICE, nullptr, nullptr,
                                    DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (info == INVALID_HANDLE_VALUE) {
            diagnostic_.detail = winError(QStringLiteral("SetupAPI device enumeration failed"));
            diagnostic_.state = FpgaUsbTransportState::Error;
            publishState(diagnostic_.state); publishError(diagnostic_.detail); return false;
        }
        interfaceData.cbSize = sizeof(interfaceData);
        for (DWORD index = 0; SetupDiEnumDeviceInterfaces(info, nullptr, &GUID_DEVINTERFACE_USB_DEVICE,
                                                           index, &interfaceData); ++index) {
            DWORD bytes = 0;
            SetupDiGetDeviceInterfaceDetailW(info, &interfaceData, nullptr, 0, &bytes, nullptr);
            if (!bytes) continue;
            QByteArray detailBuffer(static_cast<qsizetype>(bytes), Qt::Uninitialized);
            auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(detailBuffer.data());
            detail->cbSize = sizeof(*detail);
            if (!SetupDiGetDeviceInterfaceDetailW(info, &interfaceData, detail, bytes, nullptr, nullptr))
                continue;
            const QString candidate = QString::fromWCharArray(detail->DevicePath);
            if (candidate.contains(QStringLiteral("vid_04b4&pid_00f1"), Qt::CaseInsensitive)) {
                candidates.append(candidate);
            }
        }
        SetupDiDestroyDeviceInfoList(info);
        diagnostic_.fields.insert(QStringLiteral("locators"), candidates);
        if (candidates.size() > 1) {
            diagnostic_.detail = QStringLiteral("Multiple GP01 devices found; select a USB locator: %1").arg(candidates.join(QStringLiteral("; ")));
            diagnostic_.state = FpgaUsbTransportState::Error;
            publishState(diagnostic_.state); publishError(diagnostic_.detail); return false;
        }
        if (candidates.size() == 1) path = candidates.front();
    }
    if (path.isEmpty()) {
        diagnostic_.detail = QStringLiteral("GP01 FX3 device 04B4:00F1 was not found");
        diagnostic_.state = FpgaUsbTransportState::Error;
        publishState(diagnostic_.state); publishError(diagnostic_.detail); return false;
    }
    if (!path.contains(QStringLiteral("vid_04b4&pid_00f1"), Qt::CaseInsensitive)) {
        diagnostic_.detail = QStringLiteral("USB locator is not a GP01 device (expected VID_04B4&PID_00F1)");
        diagnostic_.state = FpgaUsbTransportState::Error;
        publishState(diagnostic_.state); publishError(diagnostic_.detail); return false;
    }
    const HANDLE device = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (device == INVALID_HANDLE_VALUE) {
        diagnostic_.detail = winError(QStringLiteral("opening GP01 USB interface failed"));
        diagnostic_.state = FpgaUsbTransportState::Error;
        publishState(diagnostic_.state); publishError(diagnostic_.detail); return false;
    }
    WINUSB_INTERFACE_HANDLE usb = nullptr;
    if (!WinUsb_Initialize(device, &usb)) {
        diagnostic_.detail = winError(QStringLiteral("WinUsb_Initialize failed"));
        CloseHandle(device); diagnostic_.state = FpgaUsbTransportState::Error;
        publishState(diagnostic_.state); publishError(diagnostic_.detail); return false;
    }
    USB_INTERFACE_DESCRIPTOR descriptor{};
    if (!WinUsb_QueryInterfaceSettings(usb, 0, &descriptor)) {
        diagnostic_.detail = winError(QStringLiteral("WinUsb_QueryInterfaceSettings failed"));
        WinUsb_Free(usb); CloseHandle(device); diagnostic_.state = FpgaUsbTransportState::Error;
        publishState(diagnostic_.state); publishError(diagnostic_.detail); return false;
    }
    bool foundOut = false, foundIn = false;
    for (UCHAR i = 0; i < descriptor.bNumEndpoints; ++i) {
        WINUSB_PIPE_INFORMATION pipe{};
        if (!WinUsb_QueryPipe(usb, 0, i, &pipe)) continue;
        if (pipe.PipeType == UsbdPipeTypeBulk && pipe.PipeId == FpgaUsbTransportInfo::BulkOutEndpoint) foundOut = true;
        if (pipe.PipeType == UsbdPipeTypeBulk && pipe.PipeId == FpgaUsbTransportInfo::BulkInEndpoint) foundIn = true;
    }
    if (!foundOut || !foundIn) {
        diagnostic_.detail = QStringLiteral("GP01 bulk endpoints 0x02 OUT and 0x86 IN were not found");
        WinUsb_Free(usb); CloseHandle(device); diagnostic_.state = FpgaUsbTransportState::Error;
        publishState(diagnostic_.state); publishError(diagnostic_.detail); return false;
    }
    ULONG readTimeoutMs = 50;
    if (!WinUsb_SetPipePolicy(usb, FpgaUsbTransportInfo::BulkInEndpoint,
                              PIPE_TRANSFER_TIMEOUT, sizeof(readTimeoutMs), &readTimeoutMs) ||
        !WinUsb_SetPipePolicy(usb, FpgaUsbTransportInfo::BulkOutEndpoint,
                              PIPE_TRANSFER_TIMEOUT, sizeof(readTimeoutMs), &readTimeoutMs)) {
        diagnostic_.detail = winError(QStringLiteral("WinUsb_SetPipePolicy(timeout) failed"));
        WinUsb_Free(usb); CloseHandle(device); diagnostic_.state = FpgaUsbTransportState::Error;
        publishState(diagnostic_.state); publishError(diagnostic_.detail); return false;
    }
    deviceHandle_ = device;
    interfaceHandle_ = usb;
    QByteArray b1, b0;
    if (!readDiagnostic(FpgaUsbTransportInfo::DiagnosticB1, b1) ||
        !readDiagnostic(FpgaUsbTransportInfo::DiagnosticB0, b0)) {
        close(); diagnostic_.state = FpgaUsbTransportState::Error;
        publishState(diagnostic_.state); return false;
    }
    diagnostic_.fields.insert(QStringLiteral("diagnostic_b1"), QString::fromLatin1(b1.toHex()));
    diagnostic_.fields.insert(QStringLiteral("diagnostic_b0"), QString::fromLatin1(b0.toHex()));
    diagnostic_.fields.insert(QStringLiteral("backend"), QStringLiteral("winusb"));
    diagnostic_.fields.insert(QStringLiteral("locator"), path);
    diagnostic_.state = FpgaUsbTransportState::Open;
    diagnostic_.detail = QStringLiteral("GP01 FX3 WinUSB open");
    publishState(diagnostic_.state);
    return true;
#else
    Q_UNUSED(locator);
    diagnostic_.state = FpgaUsbTransportState::Error;
    diagnostic_.detail = QStringLiteral(
        "FPGA USB hardware backend unavailable: WinUSB is implemented on Windows only");
    publishState(diagnostic_.state);
    publishError(diagnostic_.detail);
    return false;
#endif
}

void FpgaUsbHardwareTransport::close()
{
#ifdef Q_OS_WIN
    if (interfaceHandle_)
        WinUsb_Free(static_cast<WINUSB_INTERFACE_HANDLE>(interfaceHandle_));
    if (deviceHandle_)
        CloseHandle(static_cast<HANDLE>(deviceHandle_));
    interfaceHandle_ = nullptr;
    deviceHandle_ = nullptr;
#endif
    diagnostic_.fields.clear();
    diagnostic_.state = FpgaUsbTransportState::Offline;
    publishState(diagnostic_.state);
}

bool FpgaUsbHardwareTransport::isOpen() const
{
#ifdef Q_OS_WIN
    return interfaceHandle_ != nullptr;
#else
    return false;
#endif
}

qint64 FpgaUsbHardwareTransport::read(QByteArray &destination, qint64 maxBytes)
{
    destination.clear();
#ifdef Q_OS_WIN
    if (!isOpen() || maxBytes <= 0 || maxBytes > std::numeric_limits<ULONG>::max()) return -1;
    destination.resize(static_cast<qsizetype>(maxBytes));
    ULONG transferred = 0;
    if (!WinUsb_ReadPipe(static_cast<WINUSB_INTERFACE_HANDLE>(interfaceHandle_), FpgaUsbTransportInfo::BulkInEndpoint,
                         reinterpret_cast<PUCHAR>(destination.data()), static_cast<ULONG>(maxBytes), &transferred, nullptr)) {
        const DWORD error = GetLastError();
        if (error == ERROR_SEM_TIMEOUT) {
            destination.clear();
            return 0;
        }
        const QString message = QStringLiteral("WinUsb_ReadPipe failed (%1)").arg(error);
        close(); diagnostic_.state = FpgaUsbTransportState::Error; diagnostic_.detail = message;
        publishState(diagnostic_.state); publishError(message);
        destination.clear(); return -1;
    }
    destination.resize(static_cast<qsizetype>(transferred));
    diagnostic_.bytesRead += transferred;
    if (transferred) emit bytesReceived(destination);
    return transferred;
#else
    Q_UNUSED(destination); Q_UNUSED(maxBytes);
    publishError(QStringLiteral("FPGA USB hardware backend unavailable: read rejected"));
    return -1;
#endif
}

qint64 FpgaUsbHardwareTransport::write(const QByteArray &source)
{
#ifdef Q_OS_WIN
    if (!isOpen() || source.isEmpty() || source.size() % 4 != 0 || quint64(source.size()) > quint64(std::numeric_limits<ULONG>::max())) return -1;
    ULONG transferred = 0;
    if (!WinUsb_WritePipe(static_cast<WINUSB_INTERFACE_HANDLE>(interfaceHandle_), FpgaUsbTransportInfo::BulkOutEndpoint,
                          reinterpret_cast<PUCHAR>(const_cast<char *>(source.constData())), static_cast<ULONG>(source.size()),
                          &transferred, nullptr)) {
        const QString message = QStringLiteral("WinUsb_WritePipe failed (%1)").arg(GetLastError());
        close(); diagnostic_.state = FpgaUsbTransportState::Error; diagnostic_.detail = message;
        publishState(diagnostic_.state); publishError(message); return -1;
    }
    diagnostic_.bytesWritten += transferred;
    if (transferred != quint64(source.size())) {
        close(); diagnostic_.state = FpgaUsbTransportState::Error;
        diagnostic_.detail = QStringLiteral("WinUSB partial bulk OUT write");
        publishState(diagnostic_.state); publishError(diagnostic_.detail); return -1;
    }
    return transferred;
#else
    Q_UNUSED(source);
    publishError(QStringLiteral("FPGA USB hardware backend unavailable: write rejected"));
    return -1;
#endif
}

FpgaUsbDiagnostic FpgaUsbHardwareTransport::diagnostic() const
{
    return diagnostic_;
}

bool FpgaUsbHardwareTransport::readDiagnostic(quint8 request, QByteArray &response)
{
#ifdef Q_OS_WIN
    if (!isOpen() || (request != FpgaUsbTransportInfo::DiagnosticB0 && request != FpgaUsbTransportInfo::DiagnosticB1))
        return false;
    response.resize(32);
    WINUSB_SETUP_PACKET setup{0xC0, request, 0, 0, 32};
    ULONG transferred = 0;
    if (!WinUsb_ControlTransfer(static_cast<WINUSB_INTERFACE_HANDLE>(interfaceHandle_), setup,
                                reinterpret_cast<PUCHAR>(response.data()), 32, &transferred, nullptr) || transferred != 32) {
        response.clear(); publishError(QStringLiteral("GP01 diagnostic 0x%1 control read failed (%2)")
                                           .arg(request, 2, 16, QLatin1Char('0')).arg(GetLastError()));
        return false;
    }
    QByteArray expectedMarker = QByteArrayLiteral("GP01");
    expectedMarker.append(char(0xAC));
    expectedMarker.append(char(0x10));
    expectedMarker.append(char(0x00));
    expectedMarker.append(char(0x00));
    if (request == FpgaUsbTransportInfo::DiagnosticB1 && response.left(expectedMarker.size()) != expectedMarker) {
        publishError(QStringLiteral("USB device does not report the GP01 B1 marker")); response.clear(); return false;
    }
    return true;
#else
    Q_UNUSED(request); Q_UNUSED(response); return false;
#endif
}

FpgaUsbReplayTransport::FpgaUsbReplayTransport(QByteArray incoming, QObject *parent)
    : FpgaUsbTransport(parent), incoming_(std::move(incoming))
{
    diagnostic_.detail = QStringLiteral("offline replay");
    diagnostic_.fields.insert(QStringLiteral("mode"), QStringLiteral("replay"));
}

bool FpgaUsbReplayTransport::open(const QString &locator)
{
    Q_UNUSED(locator);
    open_ = true;
    diagnostic_.state = FpgaUsbTransportState::Open;
    diagnostic_.detail = QStringLiteral("offline replay open");
    publishState(diagnostic_.state);
    return true;
}

void FpgaUsbReplayTransport::close()
{
    open_ = false;
    diagnostic_.state = FpgaUsbTransportState::Offline;
    publishState(diagnostic_.state);
}

bool FpgaUsbReplayTransport::isOpen() const
{
    return open_;
}

qint64 FpgaUsbReplayTransport::read(QByteArray &destination, qint64 maxBytes)
{
    if (!open_ || maxBytes <= 0)
        return open_ ? 0 : -1;
    const qint64 count = std::min<qint64>(maxBytes, incoming_.size());
    destination = incoming_.left(count);
    incoming_.remove(0, count);
    diagnostic_.bytesRead += static_cast<quint64>(count);
    if (count > 0)
        emit bytesReceived(destination);
    return count;
}

qint64 FpgaUsbReplayTransport::write(const QByteArray &source)
{
    if (!open_)
        return -1;
    if (source.size() % 4 != 0) {
        const QString message = QStringLiteral("USB bulk write must be 4-byte aligned (got %1 bytes)")
                                     .arg(source.size());
        publishError(message);
        return -1;
    }
    written_.append(source);
    diagnostic_.bytesWritten += static_cast<quint64>(source.size());
    return source.size();
}

FpgaUsbDiagnostic FpgaUsbReplayTransport::diagnostic() const
{
    return diagnostic_;
}

void FpgaUsbReplayTransport::appendIncoming(const QByteArray &bytes)
{
    incoming_.append(bytes);
}

QByteArray FpgaUsbReplayTransport::writtenBytes() const
{
    return written_;
}

std::unique_ptr<FpgaUsbTransport> makeFpgaUsbHardwareTransport(QObject *parent)
{
    return makeFpgaUsbHardwareTransport(QStringLiteral("auto"), parent);
}

namespace
{
class AutoUsbTransport final : public FpgaUsbTransport
{
public:
    explicit AutoUsbTransport(QObject *parent) : FpgaUsbTransport(parent) {}
    bool open(const QString &locator) override
    {
        close();
        auto cypress = std::make_unique<FpgaCyApiTransport>();
        if (cypress->diagnostic().hardwareBackendAvailable)
        {
            if (cypress->open(locator)) { adopt(std::move(cypress)); return true; }
            const auto d = cypress->diagnostic();
            // A matched/ambiguous Cypress device must not silently fall through
            // to another driver or device after identity/endpoint failure.
            if (!d.fields.value(QStringLiteral("locators")).toStringList().isEmpty()
                || d.detail.contains(QStringLiteral("diagnostic")) || d.detail.contains(QStringLiteral("endpoint")))
            { adopt(std::move(cypress)); publishError(d.detail); return false; }
        }
        const QString cypressDetail = cypress->diagnostic().detail;
        auto winusb = std::make_unique<FpgaUsbHardwareTransport>();
        const bool opened = winusb->open(locator);
        adopt(std::move(winusb));
        cypressDetail_ = cypressDetail;
        if (!opened) publishError(diagnostic().detail);
        return opened;
    }
    void close() override
    {
        if (active_) active_->close();
        active_.reset();
        cypressDetail_.clear();
        publishState(FpgaUsbTransportState::Offline);
    }
    bool isOpen() const override { return active_ && active_->isOpen(); }
    qint64 read(QByteArray &destination, qint64 maxBytes) override
    { if (!active_) { destination.clear(); return -1; } return active_->read(destination,maxBytes); }
    qint64 write(const QByteArray &source) override { return active_ ? active_->write(source) : -1; }
    bool readDiagnostic(quint8 request, QByteArray &response) override
    { if (!active_) { response.clear(); return false; } return active_->readDiagnostic(request,response); }
    FpgaUsbDiagnostic diagnostic() const override
    {
        auto d = active_ ? active_->diagnostic() : FpgaUsbDiagnostic{};
        if (!cypressDetail_.isEmpty()) d.fields.insert(QStringLiteral("cypress_status"),cypressDetail_);
        return d;
    }
private:
    void adopt(std::unique_ptr<FpgaUsbTransport> transport)
    {
        active_ = std::move(transport);
        connect(active_.get(), &FpgaUsbTransport::stateChanged, this, &FpgaUsbTransport::stateChanged);
        connect(active_.get(), &FpgaUsbTransport::errorOccurred, this, &FpgaUsbTransport::errorOccurred);
        connect(active_.get(), &FpgaUsbTransport::bytesReceived, this, &FpgaUsbTransport::bytesReceived);
        publishState(active_->diagnostic().state);
    }
    std::unique_ptr<FpgaUsbTransport> active_;
    QString cypressDetail_;
};
class UnavailableUsbTransport final : public FpgaUsbTransport
{
public:
    UnavailableUsbTransport(QString name, QObject *parent) : FpgaUsbTransport(parent), name_(std::move(name)) {}
    bool open(const QString &) override { publishError(diagnostic().detail); publishState(FpgaUsbTransportState::Error); return false; }
    void close() override {}
    bool isOpen() const override { return false; }
    qint64 read(QByteArray &b,qint64) override { b.clear(); return -1; }
    qint64 write(const QByteArray &) override { return -1; }
    FpgaUsbDiagnostic diagnostic() const override
    { FpgaUsbDiagnostic d; d.state=FpgaUsbTransportState::Error; d.detail=QStringLiteral("Unknown FPGA USB backend: %1 (expected auto/cypress/winusb)").arg(name_); return d; }
private:
    QString name_;
};
}
std::unique_ptr<FpgaUsbTransport> makeFpgaUsbHardwareTransport(const QString &backend, QObject *parent)
{
    const QString name = backend.trimmed().toLower();
    if (name.isEmpty() || name == QStringLiteral("auto")) return std::make_unique<AutoUsbTransport>(parent);
    if (name == QStringLiteral("cypress")) return std::make_unique<FpgaCyApiTransport>(parent);
    if (name == QStringLiteral("winusb")) return std::make_unique<FpgaUsbHardwareTransport>(parent);
    return std::make_unique<UnavailableUsbTransport>(backend,parent);
}

std::unique_ptr<FpgaUsbTransport> makeFpgaUsbReplayTransport(const QByteArray &incoming,
                                                              QObject *parent)
{
    return std::make_unique<FpgaUsbReplayTransport>(incoming, parent);
}

}  // namespace VaporView::Ground::Devices
