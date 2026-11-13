#include "SkyDeviceManager.h"
#include "TelemetryCodec.h"
#include "LogContext.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QDateTime>
#include <QEvent>
#include <QPointer>
#include <QThread>
#include <QTcpServer>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <atomic>

namespace VaporView {
class SkyRtcmWriterTestAccess {
public:
    static void setIo(SkyDeviceManager& manager, std::function<bool()> open,
                      std::function<qint64(const QByteArray&)> write,
                      std::function<QString()> error, std::function<void()> close) {
        manager.rtcm_test_io_ = std::make_shared<SkyDeviceManager::RtcmTestIo>(
            SkyDeviceManager::RtcmTestIo{std::move(open), std::move(write), std::move(error), std::move(close)});
    }
    static int pending(SkyDeviceManager& manager) {
        std::lock_guard<std::mutex> lock(manager.rtcm_diagnostic_mutex_);
        return int(manager.rtcm_first_failure_.has_value()) + int(manager.rtcm_latest_diagnostic_.has_value());
    }
};
}

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

void generateSimulatedData(VaporView::SkyDeviceManager& manager)
{
    require(QMetaObject::invokeMethod(&manager, "generateSimulatedData", Qt::DirectConnection),
            "invoke simulated data generation");
}

bool hasStatus(const QVector<VaporView::DeviceStatusItem>& statuses, VaporView::SkyDeviceId id)
{
    for (const VaporView::DeviceStatusItem& status : statuses)
    {
        if (status.device_id == id)
        {
            return true;
        }
    }
    return false;
}

void requireConnectedWithData(const VaporView::SkyDeviceManager& manager, VaporView::SkyDeviceId id)
{
    const VaporView::DeviceStatusItem status = manager.status(id);
    require(status.state == VaporView::DeviceState::Connected, "simulated device is connected");
    require(status.rx_count > 0, "simulated device produced data");
    require(status.last_data_time_us > 0, "simulated device last data time updated");
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    {
        using namespace VaporView;
        std::atomic<bool> canOpen{false};
        std::atomic<bool> shortWrite{false};
        std::atomic<int> writes{0};
        std::atomic<quint64> workerThread{0};
        SkyDeviceManager manager;
        SkyRtcmWriterTestAccess::setIo(manager, [&] { return canOpen.load(); },
            [&](const QByteArray& payload) -> qint64 {
                workerThread.store(static_cast<quint64>(reinterpret_cast<quintptr>(QThread::currentThreadId())));
                const bool fail = shortWrite.load();
                ++writes;
                return fail ? 1 : payload.size();
            }, [] { return QStringLiteral("test serial error"); }, [] {});
        QVector<LogRecord> events;
        QObject::connect(&manager, &SkyDeviceManager::logRecord, &manager, [&](const LogRecord& record) {
            if (record.category == QStringLiteral("device.rtcm.output")) events.push_back(record);
        });
        auto config = SkyConfig::defaults();
        config.epsilon_rtcm.enabled = true;
        config.epsilon_rtcm.forward_port = QStringLiteral("test-rtcm");
        config.epsilon_rtcm.baud_rate = 115200;
        {
            ScopedLogContext context({QStringLiteral("old-config-id"), QStringLiteral("old-session")});
            manager.loadConfig(config);
        }
        const auto wait = [](const std::function<bool()>& predicate, bool pump) {
            QElapsedTimer timer; timer.start();
            while (!predicate() && timer.elapsed() < 3000) {
                if (pump) QCoreApplication::processEvents();
                QThread::msleep(1);
            }
            return predicate();
        };
        for (int i = 0; i < 3; ++i) require(manager.receiveRtcmCorrectionData("abcd"), "enqueue RTCM failure sample");
        require(wait([&] { return manager.rtcmCorrectionStats().dropped_chunks == 3; }, false), "failed RTCM chunks counted");
        require(wait([&] { return events.size() == 1; }, true), "serial open failure reported once");
        require(events.first().correlation_id.isEmpty() && events.first().session_id.isEmpty() &&
                events.first().fields.value(QStringLiteral("reason_code")).toString() == QStringLiteral("SERIAL_OPEN_FAILED") &&
                events.first().fields.value(QStringLiteral("system_error")).toString() == QStringLiteral("test serial error"),
                "background failure does not inherit old operation context");
        canOpen = true;
        shortWrite = true;
        require(manager.receiveRtcmCorrectionData("abcd"), "enqueue short RTCM write");
        require(wait([&] { return writes.load() == 1; }, false), "short RTCM write attempted");
        QCoreApplication::processEvents();
        require(events.size() == 1, "continued failure does not emit a recovery");
        shortWrite = false;
        require(manager.receiveRtcmCorrectionData("abcd"), "enqueue recovered RTCM write");
        require(wait([&] { return events.size() == 2; }, true), "successful write reports recovery");
        require(events.last().fields.value(QStringLiteral("event")).toString() == QStringLiteral("rtcm_serial_output_recovered") &&
                events.last().thread_id == workerThread.load(), "recovery retains worker thread identity");
        shortWrite = true;
        require(manager.receiveRtcmCorrectionData("abcd"), "enqueue new short write");
        require(wait([&] { return events.size() == 3; }, true), "new failure reported");
        require(events.last().fields.value(QStringLiteral("reason_code")).toString() == QStringLiteral("SERIAL_WRITE_FAILED") &&
                events.last().fields.value(QStringLiteral("requested_bytes")).toLongLong() == 4 &&
                events.last().fields.value(QStringLiteral("written_bytes")).toLongLong() == 1 &&
                events.last().thread_id == workerThread.load(), "short write reports actual worker and byte counts");
        for (int i = 0; i < 30; ++i) {
            shortWrite = (i % 2) != 0;
            const int expectedWrites = writes.load() + 1;
            require(manager.receiveRtcmCorrectionData("abcd"), "enqueue transition while owner is busy");
            require(wait([&] { return writes.load() >= expectedWrites; }, false), "worker processes transition");
            QCoreApplication::processEvents();
            require(SkyRtcmWriterTestAccess::pending(manager) <= 2, "coalescing remains bounded");
        }
        require(wait([&] { return SkyRtcmWriterTestAccess::pending(manager) > 0; }, false), "final failure is pending");
        QElapsedTimer stable; stable.start();
        while (stable.elapsed() < 1200) { QCoreApplication::processEvents(); QThread::msleep(1); }
        require(events.size() == 3, "rapid recovered and failed transitions do not report a false recovery");
        shortWrite = false;
        require(manager.receiveRtcmCorrectionData("abcd"), "enqueue final recovery");
        require(wait([&] { return events.size() == 4; }, true) &&
                events.last().fields.value(QStringLiteral("event")).toString() == QStringLiteral("rtcm_serial_output_recovered"),
                "final recovery is delivered without another packet");
        shortWrite = true;
        require(manager.receiveRtcmCorrectionData("abcd"), "enqueue failure in next interval");
        require(wait([&] { return events.size() == 5; }, true) &&
                events.last().fields.value(QStringLiteral("event")).toString() == QStringLiteral("rtcm_serial_output_failed"),
                "a new failure is reported in the next interval");
        shortWrite = false;
        require(manager.receiveRtcmCorrectionData("abcd"), "enqueue recovery in following interval");
        require(wait([&] { return events.size() == 6; }, true) &&
                events.last().fields.value(QStringLiteral("event")).toString() == QStringLiteral("rtcm_serial_output_recovered"),
                "a later real recovery remains observable");
        shortWrite = true;
        require(manager.receiveRtcmCorrectionData("abcd"), "enqueue old-generation failure");
        require(wait([&] { return SkyRtcmWriterTestAccess::pending(manager) > 0; }, false), "old-generation failure is pending");
        config.epsilon_rtcm.enabled = false;
        manager.loadConfig(config);
        QElapsedTimer afterStop; afterStop.start();
        while (afterStop.elapsed() < 1200) { QCoreApplication::processEvents(); QThread::msleep(1); }
        require(events.size() == 6, "reconfiguration discards queued old-generation diagnostics and timers");
    }

    {
        using namespace VaporView;
        std::atomic<bool> canOpen{false};
        std::atomic<int> writes{0};
        std::atomic<quint64> workerThread{0};
        QVector<LogRecord> events;
        SkyDeviceManager manager;
        SkyRtcmWriterTestAccess::setIo(manager, [&] { return canOpen.load(); },
            [&](const QByteArray& payload) -> qint64 {
                workerThread.store(static_cast<quint64>(reinterpret_cast<quintptr>(QThread::currentThreadId())));
                ++writes;
                return payload.size();
            },
            [] { return QStringLiteral("offline"); }, [] {});
        QObject::connect(&manager, &SkyDeviceManager::logRecord, &manager, [&](const LogRecord& record) {
            if (record.category == QStringLiteral("device.rtcm.output")) events.push_back(record);
        });
        auto config = SkyConfig::defaults();
        config.epsilon_rtcm.enabled = true;
        config.epsilon_rtcm.forward_port = QStringLiteral("test-rtcm");
        config.epsilon_rtcm.baud_rate = 115200;
        manager.loadConfig(config);
        require(manager.receiveRtcmCorrectionData("abcd"), "enqueue first failure before owner runs");
        QElapsedTimer timer; timer.start();
        while (manager.rtcmCorrectionStats().dropped_chunks == 0 && timer.elapsed() < 3000) QThread::msleep(1);
        require(manager.rtcmCorrectionStats().dropped_chunks == 1, "first failure processed");
        canOpen = true;
        const quint64 recoveryBeforeUs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000;
        require(manager.receiveRtcmCorrectionData("abcd"), "enqueue immediate recovery before owner runs");
        timer.restart();
        while (SkyRtcmWriterTestAccess::pending(manager) < 2 && timer.elapsed() < 3000) QThread::msleep(1);
        require(writes.load() == 1 && SkyRtcmWriterTestAccess::pending(manager) == 2,
                "first failure and latest recovery are both bounded before delivery");
        const quint64 recoveryQueuedUs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000;
        timer.restart();
        while (events.size() < 2 && timer.elapsed() < 3000) { QCoreApplication::processEvents(); QThread::msleep(1); }
        require(events.size() == 2 &&
                events.first().fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_serial_output_failed") &&
                events.last().fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_serial_output_recovered"),
                "first failure and final recovery survive an initially busy owner");
        const auto& recovery = events.last();
        const quint64 deliveryUs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000;
        require(recovery.timestamp_us >= recoveryBeforeUs && recovery.timestamp_us <= recoveryQueuedUs &&
                deliveryUs >= recovery.timestamp_us + 800000 && recovery.thread_id == workerThread.load() &&
                recovery.thread_id != static_cast<quint64>(reinterpret_cast<quintptr>(QThread::currentThreadId())) &&
                recovery.timestamp_utc == QDateTime::fromMSecsSinceEpoch(
                    static_cast<qint64>(recovery.timestamp_us / 1000), Qt::UTC).toString(Qt::ISODateWithMs),
                "delayed recovery retains occurrence time and worker thread");
        manager.shutdown(false);
    }

    {
        using namespace VaporView;
        int failures = 0;
        SkyDeviceManager manager;
        SkyRtcmWriterTestAccess::setIo(manager, [] { return false; },
            [](const QByteArray&) -> qint64 { return -1; }, [] { return QStringLiteral("offline"); }, [] {});
        QObject::connect(&manager, &SkyDeviceManager::logRecord, &manager, [&](const LogRecord& record) {
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("rtcm_serial_output_failed")) {
                ++failures;
                manager.shutdown(false);
            }
        }, Qt::DirectConnection);
        auto config = SkyConfig::defaults();
        config.epsilon_rtcm.enabled = true;
        config.epsilon_rtcm.forward_port = QStringLiteral("test-rtcm");
        config.epsilon_rtcm.baud_rate = 115200;
        manager.loadConfig(config);
        require(manager.receiveRtcmCorrectionData("abcd"), "enqueue failure for reentrant shutdown");
        QElapsedTimer timer; timer.start();
        while (!failures && timer.elapsed() < 3000) { QCoreApplication::processEvents(); QThread::msleep(1); }
        require(failures == 1, "direct log listener can stop RTCM worker without deadlock");
    }

    {
        using namespace VaporView;
        std::atomic<bool> canOpen{false};
        int events = 0;
        auto *manager = new SkyDeviceManager;
        QPointer<SkyDeviceManager> weak(manager);
        SkyRtcmWriterTestAccess::setIo(*manager, [&] { return canOpen.load(); },
            [](const QByteArray& payload) -> qint64 { return payload.size(); },
            [] { return QStringLiteral("offline"); }, [] {});
        QObject::connect(manager, &SkyDeviceManager::logRecord, &app, [&](const LogRecord& record) {
            if (record.category == QStringLiteral("device.rtcm.output")) {
                ++events;
                if (weak) weak->deleteLater();
            }
        }, Qt::DirectConnection);
        auto config = SkyConfig::defaults();
        config.epsilon_rtcm.enabled = true;
        config.epsilon_rtcm.forward_port = QStringLiteral("test-rtcm");
        config.epsilon_rtcm.baud_rate = 115200;
        manager->loadConfig(config);
        require(manager->receiveRtcmCorrectionData("abcd"), "enqueue failure before deferred destruction");
        QElapsedTimer timer; timer.start();
        while (manager->rtcmCorrectionStats().dropped_chunks == 0 && timer.elapsed() < 3000) QThread::msleep(1);
        require(manager->rtcmCorrectionStats().dropped_chunks == 1, "failure processed before deferred destruction");
        canOpen = true;
        require(manager->receiveRtcmCorrectionData("abcd"), "enqueue recovery before deferred destruction");
        timer.restart();
        while (SkyRtcmWriterTestAccess::pending(*manager) < 2 && timer.elapsed() < 3000) QThread::msleep(1);
        require(SkyRtcmWriterTestAccess::pending(*manager) == 2, "recovery pending before deferred destruction");
        timer.restart();
        while (!events && timer.elapsed() < 3000) { QCoreApplication::processEvents(); QThread::msleep(1); }
        require(events == 1, "first failure reaches direct destruction listener");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        require(!weak, "manager is destroyed after direct listener returns");
        timer.restart();
        while (timer.elapsed() < 1200) { QCoreApplication::processEvents(); QThread::msleep(1); }
        require(events == 1, "pending recovery timer cannot outlive manager");
    }

    {
        using namespace VaporView;
        QTcpServer server;
        require(server.listen(QHostAddress::LocalHost, 0), "reentrant wave listener starts");
        SkyDeviceManager manager;
        auto config = SkyConfig::defaults();
        config.wave_tcp.enabled = true;
        config.wave_tcp.host = QStringLiteral("127.0.0.1");
        config.wave_tcp.port = server.serverPort();
        manager.loadConfig(config);
        bool restarted = false;
        QString completed;
        QObject::connect(&manager, &SkyDeviceManager::logRecord, &manager, [&](const LogRecord& record) {
            const auto event = record.fields.value(QStringLiteral("event")).toString();
            if (event == QStringLiteral("wave_tcp_connect_cancelled") && !restarted) {
                restarted = true;
                require(manager.connectDevice(SkyDeviceId::WaveTcp, nullptr, QStringLiteral("wave-b")),
                        "cancellation listener starts a new wave connection");
            } else if (event == QStringLiteral("wave_tcp_connect_succeeded")) {
                completed = record.correlation_id;
            }
        });
        require(manager.connectDevice(SkyDeviceId::WaveTcp, nullptr, QStringLiteral("wave-a")), "first wave connect starts");
        manager.disconnectDevice(SkyDeviceId::WaveTcp);
        QElapsedTimer timer; timer.start();
        while ((completed.isEmpty() || !server.hasPendingConnections()) && timer.elapsed() < 3000)
            QCoreApplication::processEvents();
        require(restarted && completed == QStringLiteral("wave-b") && server.hasPendingConnections() &&
                manager.status(SkyDeviceId::WaveTcp).state == DeviceState::Connected,
                "old cancellation cannot close or overwrite the replacement connection");
        QObject::disconnect(&manager, nullptr, &manager, nullptr);
    }
    {
        using namespace VaporView;
        QTcpServer server;
        require(server.listen(QHostAddress::LocalHost, 0), "wave completion listener starts");
        SkyDeviceManager manager;
        auto config = SkyConfig::defaults();
        config.wave_tcp.enabled = true;
        config.wave_tcp.host = QStringLiteral("127.0.0.1");
        config.wave_tcp.port = server.serverPort();
        manager.loadConfig(config);
        bool disconnected = false;
        QObject::connect(&manager, &SkyDeviceManager::logRecord, &manager, [&](const LogRecord& record) {
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("wave_tcp_connect_succeeded")) {
                disconnected = true;
                manager.disconnectDevice(SkyDeviceId::WaveTcp);
            }
        });
        require(manager.connectDevice(SkyDeviceId::WaveTcp, nullptr, QStringLiteral("wave-success")), "wave connect starts");
        QElapsedTimer timer; timer.start();
        while (!disconnected && timer.elapsed() < 3000) QCoreApplication::processEvents();
        require(disconnected && manager.status(SkyDeviceId::WaveTcp).state == DeviceState::Disconnected,
                "completion listener can disconnect without a stale state update");
        QObject::disconnect(&manager, nullptr, &manager, nullptr);
    }
    {
        using namespace VaporView;
        auto manager = std::make_unique<SkyDeviceManager>();
        auto config = SkyConfig::defaults();
        config.wave_tcp.enabled = true;
        config.wave_tcp.host = QStringLiteral("127.0.0.1");
        config.wave_tcp.port = 9;
        manager->loadConfig(config);
        QObject::connect(manager.get(), &SkyDeviceManager::logRecord, &app, [&](const LogRecord& record) {
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("wave_tcp_connect_cancelled")) manager.reset();
        });
        require(manager->connectDevice(SkyDeviceId::WaveTcp, nullptr, QStringLiteral("wave-destroy")), "destroy test connection starts");
        manager->disconnectDevice(SkyDeviceId::WaveTcp);
        require(!manager, "cancellation listener may destroy manager safely");
    }

    {
        using namespace VaporView;
        QTcpServer server;
        require(server.listen(QHostAddress::LocalHost, 0), "state reentry wave listener starts");
        SkyDeviceManager manager;
        auto config = SkyConfig::defaults();
        config.wave_tcp.enabled = true;
        config.wave_tcp.host = QStringLiteral("127.0.0.1");
        config.wave_tcp.port = server.serverPort();
        manager.loadConfig(config);
        require(manager.connectDevice(SkyDeviceId::WaveTcp, nullptr, QStringLiteral("state-old")),
                "state reentry initial connection starts");
        bool restarted = false;
        QVector<DeviceState> observed;
        QObject::connect(&manager, &SkyDeviceManager::deviceStatusChanged, &manager,
                         [&](SkyDeviceId id, const DeviceStatusItem& status) {
            if (id == SkyDeviceId::WaveTcp && status.state == DeviceState::Disconnected && !restarted) {
                restarted = true;
                require(manager.connectDevice(SkyDeviceId::WaveTcp, nullptr, QStringLiteral("state-new")),
                        "state listener starts a replacement connection");
            }
        });
        QObject::connect(&manager, &SkyDeviceManager::deviceStatusChanged, &manager,
                         [&](SkyDeviceId id, const DeviceStatusItem& status) {
            if (id == SkyDeviceId::WaveTcp) observed.push_back(status.state);
        });
        manager.disconnectDevice(SkyDeviceId::WaveTcp);
        require(restarted && observed.size() >= 2 && observed.at(0) == DeviceState::Connecting &&
                observed.at(1) == DeviceState::Disconnected,
                "each listener receives the original status snapshot during reentry");
        QElapsedTimer timer; timer.start();
        while (manager.status(SkyDeviceId::WaveTcp).state != DeviceState::Connected && timer.elapsed() < 3000)
            QCoreApplication::processEvents();
        require(manager.status(SkyDeviceId::WaveTcp).state == DeviceState::Connected,
                "old disconnect does not overwrite the replacement state");
        QObject::disconnect(&manager, nullptr, &manager, nullptr);
    }
    {
        using namespace VaporView;
        auto manager = std::make_unique<SkyDeviceManager>();
        auto config = SkyConfig::defaults();
        config.wave_tcp.enabled = true;
        config.wave_tcp.host = QStringLiteral("127.0.0.1");
        config.wave_tcp.port = 9;
        manager->loadConfig(config);
        require(manager->connectDevice(SkyDeviceId::WaveTcp, nullptr, QStringLiteral("state-destroy")),
                "state destruction test connection starts");
        QObject::connect(manager.get(), &SkyDeviceManager::deviceStatusChanged, &app,
                         [&](SkyDeviceId id, const DeviceStatusItem& status) {
            if (id == SkyDeviceId::WaveTcp && status.state == DeviceState::Disconnected) manager.reset();
        });
        manager->disconnectDevice(SkyDeviceId::WaveTcp);
        require(!manager, "disconnect state listener may destroy manager safely");
    }

    {
        QTcpServer server;
        require(server.listen(QHostAddress::LocalHost, 0), "isolated wave listener starts");
        VaporView::SkyDeviceManager manager;
        auto baseline = VaporView::SkyConfig::defaults();
        baseline.wave_tcp.enabled = false;
        manager.loadConfig(baseline);
        QVector<VaporView::LogRecord> records;
        QObject::connect(&manager, &VaporView::SkyDeviceManager::logRecord, &manager,
                         [&records](const VaporView::LogRecord& record) { records.push_back(record); });
        auto desired = baseline;
        desired.wave_tcp.enabled = true;
        desired.wave_tcp.host = QStringLiteral("127.0.0.1");
        desired.wave_tcp.port = server.serverPort();
        require(manager.applyConfig(desired, QStringLiteral("config-wave-id"), QStringLiteral("session-a")).success,
                "config schedules isolated wave connection");
        QElapsedTimer timer; timer.start();
        while ((!server.hasPendingConnections() || manager.status(VaporView::SkyDeviceId::WaveTcp).state != VaporView::DeviceState::Connected) && timer.elapsed() < 3000)
            QCoreApplication::processEvents();
        require(server.hasPendingConnections(), "wave connection reaches listener");
        QTcpSocket *peer = server.nextPendingConnection();
        bool found = false;
        for (const auto& record : records)
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("wave_tcp_connect_succeeded"))
                found = record.correlation_id == QStringLiteral("config-wave-id") && record.session_id == QStringLiteral("session-a") &&
                        !record.fields.contains(QStringLiteral("socket_error")) && !record.fields.contains(QStringLiteral("system_error"));
        require(found, "configuration connection completion retains operation context");
        records.clear();
        peer->disconnectFromHost();
        timer.restart();
        while (manager.status(VaporView::SkyDeviceId::WaveTcp).state == VaporView::DeviceState::Connected && timer.elapsed() < 3000)
            QCoreApplication::processEvents();
        require(manager.status(VaporView::SkyDeviceId::WaveTcp).state == VaporView::DeviceState::Disconnected,
                "independent remote disconnect changes state");
        for (const auto& record : records)
            require(record.correlation_id != QStringLiteral("config-wave-id"), "independent disconnect does not inherit old operation");
        peer->deleteLater();
        server.close();
        QTcpServer closedPort;
        require(closedPort.listen(QHostAddress::LocalHost, 0), "reserve a separate wave port");
        const quint16 unavailablePort = closedPort.serverPort();
        closedPort.close();
        records.clear();
        desired.wave_tcp.port = unavailablePort;
        require(manager.applyConfig(desired, QStringLiteral("failed-wave-id")).success,
                "second wave configuration schedules");
        QTcpSocket *pendingSocket = nullptr;
        for (auto *socket : manager.findChildren<QTcpSocket*>())
            if (socket->state() == QAbstractSocket::ConnectingState) pendingSocket = socket;
        require(pendingSocket, "second wave socket is pending");
        require(QMetaObject::invokeMethod(pendingSocket, "errorOccurred", Qt::DirectConnection,
                                          Q_ARG(QAbstractSocket::SocketError, QAbstractSocket::ConnectionRefusedError)),
                "inject deterministic socket refusal");
        timer.restart();
        auto hasFailure = [&records] {
            for (const auto& record : records)
                if (record.fields.value(QStringLiteral("event")) == QStringLiteral("wave_tcp_connect_failed")) return true;
            return false;
        };
        while (!hasFailure() && timer.elapsed() < 3000)
            QCoreApplication::processEvents();
        found = false;
        for (const auto& record : records)
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("wave_tcp_connect_failed"))
                found = record.correlation_id == QStringLiteral("failed-wave-id") && record.session_id.isEmpty();
        if (!found) {
            std::cerr << "wave status=" << static_cast<int>(manager.status(VaporView::SkyDeviceId::WaveTcp).state)
                      << " first-port=" << server.serverPort() << " second-port=" << unavailablePort
                      << " config-port=" << manager.config().wave_tcp.port << '\n';
            for (const auto *socket : manager.findChildren<QTcpSocket*>())
                std::cerr << "socket-state=" << static_cast<int>(socket->state())
                          << " error=" << socket->errorString().toStdString()
                          << " peer-port=" << socket->peerPort() << '\n';
            for (const auto& record : records)
                std::cerr << record.toJsonLine().constData();
        }
        require(found, "failed connection uses new context without stale session");
        QObject::disconnect(&manager, nullptr, &manager, nullptr);
    }

    {
        QTcpServer reserved;
        require(reserved.listen(QHostAddress::LocalHost, 0), "reserve refused wave port");
        const quint16 port = reserved.serverPort();
        reserved.close();
        VaporView::SkyDeviceManager manager;
        auto config = VaporView::SkyConfig::defaults();
        config.wave_tcp.enabled = false;
        manager.loadConfig(config);
        QVector<VaporView::LogRecord> records;
        QObject::connect(&manager, &VaporView::SkyDeviceManager::logRecord, &manager,
                         [&records](const VaporView::LogRecord& record) { records.push_back(record); });
        config.wave_tcp.enabled = true;
        config.wave_tcp.host = QStringLiteral("127.0.0.1");
        config.wave_tcp.port = port;
        require(manager.applyConfig(config, QStringLiteral("wave-refused-id"), QStringLiteral("wave-refused-session")).success,
                "schedule refused wave connection");
        QElapsedTimer timer; timer.start();
        auto findFailure = [&records]() -> const VaporView::LogRecord* {
            for (const auto& record : records)
                if (record.fields.value(QStringLiteral("event")) == QStringLiteral("wave_tcp_connect_failed")) return &record;
            return nullptr;
        };
        while (!findFailure() && timer.elapsed() < 5000) QCoreApplication::processEvents();
        const auto *failure = findFailure();
        require(failure && failure->correlation_id == QStringLiteral("wave-refused-id") &&
                failure->session_id == QStringLiteral("wave-refused-session") &&
                failure->fields.value(QStringLiteral("host")).toString() == config.wave_tcp.host &&
                failure->fields.value(QStringLiteral("port")).toUInt() == port &&
                failure->fields.contains(QStringLiteral("socket_error")) &&
                failure->fields.value(QStringLiteral("socket_error")).toInt() == int(QAbstractSocket::ConnectionRefusedError) &&
                !failure->fields.value(QStringLiteral("system_error")).toString().isEmpty(),
                "actual wave connection refusal retains context and socket diagnostics");
    }

    VaporView::SkyConfig config = VaporView::SkyConfig::defaults();
    config.epsilon.enabled = true;
    config.ptb = {true, QStringLiteral("/dev/test-bmp390"), 115200, 20.0};
    config.ptb.source = QStringLiteral("bmp390");
    config.hc2a = {true, QStringLiteral("/dev/test-sht45"), 115200, 20.0};
    config.hc2a.source = QStringLiteral("sht45");
    config.lidar.enabled = true;
    config.temperature_controller = {true, QStringLiteral("/dev/test-rd105"), 38400, 5.0, 3};
    config.ai8_temperature_controller = {true, QStringLiteral("/dev/test-ai8"), 19200, 5.0, 5};
    config.wave_tcp.enabled = true;
    config.bmp390={true,QStringLiteral("/dev/internal-bmp"),115200,1.0,{}};
    config.sht45={true,QStringLiteral("/dev/internal-sht"),115200,1.0,{}};
    {
        VaporView::SkyDeviceManager real;
        auto unavailable=config;unavailable.bmp390.port="COM_VAPORVIEW_TEST_DOES_NOT_EXIST";
        real.loadConfig(unavailable);bool diagnosed=false;
        QObject::connect(&real,&VaporView::SkyDeviceManager::logRecord,&real,[&](const VaporView::LogRecord& record) {
            if(record.fields.value("event").toString()=="internal_serial_error")diagnosed=true;
        });
        require(real.connectDevice(VaporView::SkyDeviceId::Bmp390),"Sky schedules an independent real bridge open");
        QElapsedTimer wait;wait.start();
        while(!diagnosed&&wait.elapsed()<3000){QCoreApplication::processEvents();QThread::msleep(1);}
        require(diagnosed&&real.status(VaporView::SkyDeviceId::Bmp390).state==VaporView::DeviceState::Error
            &&real.status(VaporView::SkyDeviceId::Bmp390).error_code!=0,"Sky serial failure publishes both status and diagnostic reason");
    }

    VaporView::SkyDeviceManager manager;
    manager.loadConfig(config);
    manager.setSimulateData(true);
    int internalSamples=0;
    QObject::connect(&manager,&VaporView::SkyDeviceManager::internalEnvironmentUpdated,&manager,
        [&](const VaporView::FpgaSensor::Reading& reading){
            require(reading.source==0x43||reading.source==0x44,"internal device source remains independent");
            require(reading.validity.measurement,"simulated internal measurement valid");++internalSamples;
        });
    manager.connectAll();
    generateSimulatedData(manager);

    const QVector<VaporView::DeviceStatusItem> statuses = manager.allStatuses();
    require(statuses.size() == 9 && internalSamples==2, "allStatuses covers independent internal devices and both produce readings");
    requireConnectedWithData(manager,VaporView::SkyDeviceId::Bmp390);
    requireConnectedWithData(manager,VaporView::SkyDeviceId::Sht45);
    for (VaporView::SkyDeviceId id : {VaporView::SkyDeviceId::Epsilon,
                                      VaporView::SkyDeviceId::Ptb,
                                      VaporView::SkyDeviceId::Hc2a,
                                      VaporView::SkyDeviceId::Lidar,
                                      VaporView::SkyDeviceId::TemperatureController,
                                      VaporView::SkyDeviceId::Ai8TemperatureController,
                                      VaporView::SkyDeviceId::WaveTcp})
    {
        require(hasStatus(statuses, id), "allStatuses contains formal device");
        requireConnectedWithData(manager, id);
    }

    require(manager.latestEpsilon().valid, "simulated EPSILON data is valid");
    require(std::fabs(manager.latestEpsilon().imu_packet_rate_hz - 250.0) < 0.000001 &&
                std::fabs(manager.latestEpsilon().sys_state_packet_rate_hz - 100.0) < 0.000001,
            "simulated EPSILON starts with the SkyConfig packet-rate profile");
    require(manager.latestPtb().valid && std::fabs(manager.latestPtb().pressure_hpa) > 1.0,
            "simulated pressure data is valid");
    require(manager.latestHc2a().valid && std::fabs(manager.latestHc2a().humidity) > 1.0,
            "simulated humidity data is valid");
    require(manager.latestLidar().valid && manager.latestLidar().distance_m > 0.0,
            "simulated lidar data is valid");
    require(manager.latestTemperatureController().valid,
            "simulated RD105 data is valid");
    require(manager.latestAi8TemperatureController().valid &&
                manager.latestAi8TemperatureController().controlStatesValid,
            "simulated AI-8 data is valid");
    require(!manager.latestWaveform().isEmpty() && !manager.latestRawWaveform().isEmpty(),
            "simulated Wave TCP data is valid");

    VaporView::CommandErrorCode error = VaporView::CommandErrorCode::Ok;
    require(manager.setTemperatureTarget(1, 31.0, &error) && error == VaporView::CommandErrorCode::Ok,
            "RD105 simulated set target succeeds");
    require(manager.setTemperatureOutputEnabled(1, true, &error) && error == VaporView::CommandErrorCode::Ok,
            "RD105 simulated output enable succeeds");
    require(!manager.setTemperatureTarget(3, 31.0, &error) &&
                error == VaporView::CommandErrorCode::InvalidPayload,
            "RD105 simulated command rejects invalid channel");
    generateSimulatedData(manager);
    const VaporView::TemperatureControllerData rd105 = manager.latestTemperatureController();
    require(std::fabs(rd105.channels[0].target_temperature_c - 31.0) < 0.000001 &&
                rd105.channels[0].output_enabled,
            "RD105 simulated telemetry reflects commands");

    VaporView::EpsilonPacketRatesOperation epsilonPacketRates;
    epsilonPacketRates.output_rate_hz = 100;
    epsilonPacketRates.callback_rate_hz = 250;
    epsilonPacketRates.packet_rates = {{0x40, 250}, {0x50, 100}, {0x5C, 10}};
    epsilonPacketRates.packet_rates[VaporView::Ppk::kMsgRawSatellite] = VaporView::Ppk::kDefaultObservationRateHz;
    epsilonPacketRates.packet_rate_signature = QStringLiteral("40=250;50=100;5C=10");
    QString operationMessage;
    require(manager.configureEpsilonPacketRates(epsilonPacketRates, &error, &operationMessage) &&
                error == VaporView::CommandErrorCode::Ok &&
                !manager.config().toJson().value(QStringLiteral("epsilon")).toObject()
                    .contains(QStringLiteral("frequency_hz")),
            "EPSILON simulated packet-rate operation succeeds");
    generateSimulatedData(manager);
    require(std::fabs(manager.latestEpsilon().imu_packet_rate_hz - 250.0) < 0.000001 &&
                std::fabs(manager.latestEpsilon().sys_state_packet_rate_hz - 100.0) < 0.000001,
            "EPSILON simulated packet-rate operation updates packet profile rates");
    require(manager.latestEpsilon().raw_satellite_epoch_rate_hz==VaporView::Ppk::kDefaultObservationRateHz,
            "remote ConfigureEpsilonPacketRates sets independent 0x77=5Hz");
    auto legacyJson=manager.config().toJson();
    auto legacyEpsilon=legacyJson.value("epsilon").toObject();legacyEpsilon.remove("packet_rates");
    legacyEpsilon.remove("imu_to_main_antenna_body_m");legacyJson.insert("epsilon",legacyEpsilon);
    VaporView::SkyConfig legacyConfig;QString legacyError;
    require(VaporView::SkyConfig::fromJson(legacyJson,legacyConfig,&legacyError) &&
        legacyConfig.epsilon.packet_rates.at(VaporView::Ppk::kMsgRawSatellite)==VaporView::Ppk::kDefaultObservationRateHz,
        "old SkyConfig defaults PPK observations to 5Hz");
    auto invalidPacketRates = epsilonPacketRates;
    invalidPacketRates.packet_rates[0xFF] = 100;
    require(!manager.configureEpsilonPacketRates(invalidPacketRates, &error, &operationMessage) &&
                error == VaporView::CommandErrorCode::ConfigInvalid,
            "EPSILON simulated packet-rate operation rejects unsupported packet id");

    VaporView::SkyConfig packetProfileConfig = manager.config();
    packetProfileConfig.epsilon.packet_rates[0x40] = 200;
    const VaporView::ApplyConfigResult packetProfileResult = manager.applyConfig(packetProfileConfig);
    const QJsonObject packetProfileApply = packetProfileResult.json.value(
        QStringLiteral("epsilon_packet_rates")).toObject();
    require(packetProfileResult.success && packetProfileApply.value(QStringLiteral("changed")).toBool() &&
                packetProfileApply.value(QStringLiteral("applied")).toBool() &&
                !packetProfileApply.value(QStringLiteral("pending")).toBool(),
            "SkyConfig packet-rate profile applies to a connected simulated EPSILON");
    generateSimulatedData(manager);
    require(std::fabs(manager.latestEpsilon().imu_packet_rate_hz - 200.0) < 0.000001,
            "SkyConfig packet-rate profile updates simulated EPSILON output");

    require(manager.disconnectDevice(VaporView::SkyDeviceId::Epsilon, &error),
            "simulated EPSILON disconnects before deferred profile apply");
    packetProfileConfig.epsilon.packet_rates[0x40] = 100;
    const VaporView::ApplyConfigResult pendingPacketProfileResult =
        manager.applyConfig(packetProfileConfig);
    const QJsonObject pendingPacketProfile = pendingPacketProfileResult.json.value(
        QStringLiteral("epsilon_packet_rates")).toObject();
    require(pendingPacketProfileResult.success &&
                pendingPacketProfile.value(QStringLiteral("pending")).toBool() &&
                !pendingPacketProfile.value(QStringLiteral("applied")).toBool(),
            "SkyConfig packet-rate profile is marked pending while EPSILON is disconnected");
    require(manager.connectDevice(VaporView::SkyDeviceId::Epsilon, &error),
            "simulated EPSILON reconnects with deferred packet profile");
    generateSimulatedData(manager);
    require(std::fabs(manager.latestEpsilon().imu_packet_rate_hz - 100.0) < 0.000001,
            "deferred SkyConfig packet-rate profile applies on reconnect");

    VaporView::EpsilonMainAntennaLeverArmOperation leverArm;
    leverArm.x_m = 1.25;
    leverArm.y_m = -0.5;
    leverArm.z_m = 0.75;
    require(manager.configureEpsilonMainAntennaLeverArm(leverArm, &error, &operationMessage) &&
                error == VaporView::CommandErrorCode::Ok,
            "EPSILON simulated lever-arm operation succeeds");
    leverArm.x_m = 101.0;
    require(!manager.configureEpsilonMainAntennaLeverArm(leverArm, &error, &operationMessage) &&
                error == VaporView::CommandErrorCode::InvalidPayload,
            "EPSILON simulated lever-arm operation rejects invalid range");

    VaporView::EpsilonRtcmInputOperation rtcmInput;
    rtcmInput.device_port_index = 3;
    rtcmInput.forward_port = QStringLiteral("/dev/test-rtcm");
    rtcmInput.forward_baud = 230400;
    require(manager.configureEpsilonRtcmInput(rtcmInput, &error, &operationMessage) &&
                error == VaporView::CommandErrorCode::Ok &&
                manager.config().epsilon_rtcm.enabled &&
                manager.config().epsilon_rtcm.device_port_index == 3 &&
                manager.config().epsilon_rtcm.forward_port == QStringLiteral("/dev/test-rtcm") &&
                manager.config().epsilon_rtcm.baud_rate == 230400,
            "EPSILON simulated RTCM input operation updates Sky config");
    const QByteArray rtcmBytes = QByteArray::fromHex("D30000123456");
    require(manager.receiveRtcmCorrectionData(rtcmBytes, &error) &&
                error == VaporView::CommandErrorCode::Ok,
            "EPSILON simulated RTCM correction receive succeeds");
    const VaporView::RtcmCorrectionStats rtcmStats = manager.rtcmCorrectionStats();
    require(rtcmStats.bytes_received >= static_cast<quint64>(rtcmBytes.size()) &&
                rtcmStats.chunks_received > 0 &&
                rtcmStats.last_receive_time_us > 0,
            "EPSILON simulated RTCM correction counters update");

    VaporView::Ai8TemperatureControllerProtocol::Selection ai8Selection;
    ai8Selection.channel = 2;
    ai8Selection.inputGroup = 2;
    ai8Selection.outputGroup = 2;
    VaporView::Ai8TemperatureControllerProtocol::PageData ai8Page;
    require(manager.readAi8Page(VaporView::Ai8TemperatureControllerProtocol::Page::Channel,
                                ai8Selection,
                                ai8Page,
                                &error,
                                &operationMessage) &&
                error == VaporView::CommandErrorCode::Ok &&
                std::fabs(ai8Page.channel.setpointC - 25.0) < 0.000001,
            "AI-8 simulated page read succeeds");
    ai8Page.channel.setpointC = 37.5;
    VaporView::Ai8TemperatureControllerProtocol::PageData confirmedAi8Page;
    require(manager.writeAi8Page(ai8Page, confirmedAi8Page, &error, &operationMessage) &&
                error == VaporView::CommandErrorCode::Ok &&
                std::fabs(confirmedAi8Page.channel.setpointC - 37.5) < 0.000001,
            "AI-8 simulated page write and read-back succeeds");
    VaporView::Ai8TemperatureControllerProtocol::PageData readBackAi8Page;
    require(manager.readAi8Page(VaporView::Ai8TemperatureControllerProtocol::Page::Channel,
                                ai8Selection,
                                readBackAi8Page,
                                &error,
                                &operationMessage) &&
                std::fabs(readBackAi8Page.channel.setpointC - 37.5) < 0.000001,
            "AI-8 simulated read reflects written value");
    VaporView::Ai8TemperatureControllerProtocol::Selection otherChannel = ai8Selection;
    otherChannel.channel = 3;
    require(manager.readAi8Page(VaporView::Ai8TemperatureControllerProtocol::Page::Channel,
                                otherChannel,
                                readBackAi8Page,
                                &error,
                                &operationMessage) &&
                std::fabs(readBackAi8Page.channel.setpointC - 25.0) < 0.000001,
            "AI-8 simulated channel state is isolated by selection");

    VaporView::Ai8TemperatureControllerProtocol::PageData inputPage;
    require(manager.readAi8Page(VaporView::Ai8TemperatureControllerProtocol::Page::InputGroup,
                                ai8Selection,
                                inputPage,
                                &error,
                                &operationMessage),
            "AI-8 simulated input-group page read succeeds");
    inputPage.input.filter = 7;
    require(manager.writeAi8Page(inputPage, confirmedAi8Page, &error, &operationMessage) &&
                confirmedAi8Page.input.filter == 7,
            "AI-8 simulated input-group write and read-back succeeds");

    VaporView::Ai8TemperatureControllerProtocol::PageData outputPage;
    require(manager.readAi8Page(VaporView::Ai8TemperatureControllerProtocol::Page::OutputGroup,
                                ai8Selection,
                                outputPage,
                                &error,
                                &operationMessage),
            "AI-8 simulated output-group page read succeeds");
    outputPage.output.outputHighPercent = 101;
    require(manager.writeAi8Page(outputPage, confirmedAi8Page, &error, &operationMessage) &&
                confirmedAi8Page.output.outputHighPercent == 101,
            "AI-8 simulated output-group write and read-back succeeds");

    VaporView::Ai8TemperatureControllerProtocol::PageData globalPage;
    require(manager.readAi8Page(VaporView::Ai8TemperatureControllerProtocol::Page::Global,
                                ai8Selection,
                                globalPage,
                                &error,
                                &operationMessage),
            "AI-8 simulated global page read succeeds");
    globalPage.global.address = 7;
    require(manager.writeAi8Page(globalPage, confirmedAi8Page, &error, &operationMessage) &&
                confirmedAi8Page.global.address == 7,
            "AI-8 simulated global write and read-back succeeds");

    auto invalidAi8Page = ai8Page;
    invalidAi8Page.output.outputHighPercent = 106;
    require(!manager.writeAi8Page(invalidAi8Page, confirmedAi8Page, &error, &operationMessage) &&
                error == VaporView::CommandErrorCode::InvalidPayload,
            "AI-8 simulated write rejects invalid values");
    require(manager.restoreAi8FactoryDefaults(
                VaporView::Ai8TemperatureControllerProtocol::Page::Channel,
                ai8Selection,
                confirmedAi8Page,
                &error,
                &operationMessage) &&
                error == VaporView::CommandErrorCode::Ok &&
                std::fabs(confirmedAi8Page.channel.setpointC - 25.0) < 0.000001,
            "AI-8 simulated factory reset restores defaults");

    int ai8DisconnectLogCount = 0;
    QObject::connect(&manager, &VaporView::SkyDeviceManager::logRecord,
                     [&](const VaporView::LogRecord& record) {
                         if (record.fields.value(QStringLiteral("event")).toString() ==
                                 QStringLiteral("device_disconnected") &&
                             record.fields.value(QStringLiteral("device_id")).toString() ==
                                 VaporView::skyDeviceIdName(VaporView::SkyDeviceId::Ai8TemperatureController))
                         {
                             ++ai8DisconnectLogCount;
                         }
                     });

    require(manager.disconnectDevice(VaporView::SkyDeviceId::Ai8TemperatureController, &error) &&
                error == VaporView::CommandErrorCode::Ok,
            "AI-8 simulated disconnect succeeds");
    require(ai8DisconnectLogCount == 1,
            "AI-8 simulated disconnect publishes one disconnect log");
    require(manager.disconnectDevice(VaporView::SkyDeviceId::Ai8TemperatureController, &error) &&
                error == VaporView::CommandErrorCode::Ok,
            "AI-8 simulated repeated disconnect succeeds");
    require(ai8DisconnectLogCount == 1,
            "AI-8 simulated repeated disconnect is log-idempotent");
    require(manager.status(VaporView::SkyDeviceId::Ai8TemperatureController).state ==
                VaporView::DeviceState::Disconnected,
            "AI-8 simulated status is disconnected");
    require(!manager.latestAi8TemperatureController().valid,
            "AI-8 simulated data invalidates on disconnect");
    generateSimulatedData(manager);
    require(!manager.latestAi8TemperatureController().valid,
            "AI-8 simulated data stays invalid while disconnected");

    require(manager.reconnectDevice(VaporView::SkyDeviceId::Ai8TemperatureController, &error) &&
                error == VaporView::CommandErrorCode::Ok,
            "AI-8 simulated reconnect succeeds");
    generateSimulatedData(manager);
    requireConnectedWithData(manager, VaporView::SkyDeviceId::Ai8TemperatureController);
    require(manager.latestAi8TemperatureController().valid,
            "AI-8 simulated data resumes after reconnect");
    VaporView::DeviceOperationRequest settingsRequest;
    settingsRequest.device_id = VaporView::SkyDeviceId::Epsilon;
    settingsRequest.operation = VaporView::DeviceOperation::ReadEpsilonSettings;
    settingsRequest.payload = VaporView::TelemetryCodec::serializeEpsilonSettingsRead(VaporView::EpsilonSettingsGroup::Installation);
    auto settingsSnapshot = std::make_shared<VaporView::EpsilonSettingsSnapshot>();
    require(manager.prepareEpsilonOperation(settingsRequest, settingsSnapshot)() == VaporView::CommandErrorCode::UnknownCommand &&
            settingsSnapshot->values.empty(), "simulation does not invent EPSILON hardware settings");
    settingsRequest.payload = "{}";
    require(manager.prepareEpsilonOperation(settingsRequest)() == VaporView::CommandErrorCode::InvalidPayload,
            "Sky rejects malformed EPSILON settings read before hardware access");
    settingsRequest.operation = VaporView::DeviceOperation::RestartEpsilonDevice;
    require(manager.prepareEpsilonOperation(settingsRequest)() == VaporView::CommandErrorCode::InvalidPayload,
            "Sky restart accepts only empty payload");
    settingsRequest.operation = VaporView::DeviceOperation::CalibrateEpsilonLevel;
    settingsRequest.payload = VaporView::TelemetryCodec::serializeEpsilonMaintenanceAction(VaporView::EpsilonMaintenanceAction::Level);
    require(manager.prepareEpsilonOperation(settingsRequest)() == VaporView::CommandErrorCode::UnknownCommand,
            "simulation rejects calibration without a hardware maintenance workflow");
    settingsRequest.operation = VaporView::DeviceOperation::CalibrateEpsilonGyroscope;
    settingsRequest.payload = VaporView::TelemetryCodec::serializeEpsilonMaintenanceAction(VaporView::EpsilonMaintenanceAction::Gyroscope);
    auto maintenance = std::make_shared<VaporView::EpsilonMaintenanceResult>();
    require(manager.prepareEpsilonOperation(settingsRequest, {}, {}, maintenance)() == VaporView::CommandErrorCode::UnknownCommand &&
            maintenance->action == VaporView::EpsilonMaintenanceAction::Gyroscope,
            "simulation rejection retains the requested maintenance action");
    settingsRequest.payload = "{}";
    require(manager.prepareEpsilonOperation(settingsRequest, {}, {}, maintenance)() == VaporView::CommandErrorCode::InvalidPayload,
            "simulation still rejects malformed maintenance payloads");
    settingsRequest.operation = VaporView::DeviceOperation::CalibrateEpsilonMagnetic2D;
    settingsRequest.payload = VaporView::TelemetryCodec::serializeEpsilonMaintenanceAction(VaporView::EpsilonMaintenanceAction::Magnetic2D);
    require(manager.prepareEpsilonOperation(settingsRequest, {}, {}, maintenance)() == VaporView::CommandErrorCode::UnknownCommand &&
            maintenance->action == VaporView::EpsilonMaintenanceAction::Magnetic2D,
            "simulation never invents magnetic progress or completion");
    settingsRequest.operation = VaporView::DeviceOperation::ReadEpsilonDgnss;
    settingsRequest.payload.clear();
    auto dgnss = std::make_shared<VaporView::EpsilonDgnssSnapshot>();
    require(manager.prepareEpsilonOperation(settingsRequest, {}, {}, {}, dgnss)() == VaporView::CommandErrorCode::UnknownCommand && dgnss->values.empty(),
            "simulation refuses fabricated D4G account and modem fields");

    const int logCountBeforeShutdown = ai8DisconnectLogCount;
    manager.setSimulateData(false);
    manager.shutdown(false);
    require(ai8DisconnectLogCount == logCountBeforeShutdown,
            "silent shutdown suppresses per-device disconnect logs");

    VaporView::SkyConfig disabledAi8 = manager.config();
    disabledAi8.ai8_temperature_controller.enabled = false;
    const VaporView::ApplyConfigResult result = manager.applyConfig(disabledAi8);
    require(result.success, "simulated apply config succeeds");
    require(manager.status(VaporView::SkyDeviceId::Ai8TemperatureController).state ==
                VaporView::DeviceState::Disabled,
            "AI-8 simulated apply disable updates status");
    require(!manager.latestAi8TemperatureController().valid,
            "AI-8 simulated apply disable invalidates data");

    manager.shutdown(false);
    std::cout << "sky_device_manager_simulation_test passed\n";
    return 0;
}
