#include "SkyFpgaBackend.h"
#include "FpgaTelemetry.h"
#include <QPointer>
#include <QSet>
#include <cmath>

namespace VaporView {
bool SkyFpgaBackend::validateOperation(const QJsonObject& o, QString *error)
{
    return FpgaRemote::validateControl(o, error);
}

SkyFpgaBackend::SkyFpgaBackend(std::unique_ptr<Ground::Devices::FpgaUsbTransport> transport, QObject *parent)
    : QObject(parent), controller_(new Ground::Devices::FpgaDeviceController(std::move(transport)))
{
    qRegisterMetaType<FpgaWave::CompletedStream>();
    controller_->moveToThread(&worker_);
    connect(&worker_, &QThread::finished, controller_, &QObject::deleteLater);
    connect(controller_, &Ground::Devices::FpgaDeviceController::transportConnectionChanged, this, [this](bool connected) {
        status_["connected"] = connected; emit statusChanged(status_);
    });
    connect(controller_, &Ground::Devices::FpgaDeviceController::connectionChanged, this, [this](bool ready, bool busy, const QString& detail) {
        status_["ready"] = ready; status_["busy"] = busy; status_["detail"] = detail; emit statusChanged(status_);
    });
    connect(controller_, &Ground::Devices::FpgaDeviceController::hardwareValuesChanged, this, [this](const QMap<quint32, quint32>& values) {
        QJsonObject registers; for (auto i = values.begin(); i != values.end(); ++i) registers[QString::number(i.key())] = double(i.value());
        status_["registers"] = registers; emit statusChanged(status_);
    });
    connect(controller_, &Ground::Devices::FpgaDeviceController::measurementUpdated, this, &SkyFpgaBackend::measurementUpdated);
    auto previewClock = std::make_shared<QElapsedTimer>(); previewClock->start();
    auto lastPreview = std::make_shared<QHash<quint16,qint64>>();
    connect(controller_, &Ground::Devices::FpgaDeviceController::waveformUpdated, controller_,
        [this, previewClock, lastPreview](const FpgaWave::CompletedStream& source) {
            const qint64 now=previewClock->elapsed();
            if (lastPreview->contains(source.source) && now-lastPreview->value(source.source)<500) return;
            lastPreview->insert(source.source,now);
            FpgaWave::CompletedStream preview;
            preview.source=source.source;preview.message=source.message;preview.flags=source.flags;preview.cycleId=source.cycleId;
            preview.timestamp=source.timestamp;preview.schema=source.schema;preview.format=source.format;preview.rate=source.rate;
            preview.totalPoints=source.totalPoints;preview.bytesPerPoint=source.bytesPerPoint;preview.adcBits=source.adcBits;
            preview.complete=source.complete;preview.partial=source.partial;preview.overflow=source.overflow;preview.continuityError=source.continuityError;
            preview.preview=true;
            const qsizetype count=qMax(source.signed32Samples.size(),qMax(source.unsigned32Samples.size(),source.dliaPoints.size()));
            preview.previewStride=quint32(qMax<qsizetype>(1,(count+63)/64));
            for (qsizetype i=0;i<count;i+=preview.previewStride) {
                if (i<source.signed32Samples.size()) preview.signed32Samples.push_back(source.signed32Samples[i]);
                if (i<source.unsigned32Samples.size()) preview.unsigned32Samples.push_back(source.unsigned32Samples[i]);
                if (i<source.dliaPoints.size()) preview.dliaPoints.push_back(source.dliaPoints[i]);
            }
            QMetaObject::invokeMethod(this,[this,preview=std::move(preview)] { emit waveformUpdated(preview); },Qt::QueuedConnection);
        });
    // Raw capture remains on the USB worker; no unbounded queued copies of full USB traffic.
    connect(controller_, &Ground::Devices::FpgaDeviceController::rawFrame, this, &SkyFpgaBackend::rawFrame, Qt::DirectConnection);
    connect(controller_, &Ground::Devices::FpgaDeviceController::rawCommand, this, &SkyFpgaBackend::rawCommand, Qt::DirectConnection);
    connect(controller_, &Ground::Devices::FpgaDeviceController::rawUsbBytes, this, &SkyFpgaBackend::rawUsbBytes, Qt::DirectConnection);
    connect(controller_, &Ground::Devices::FpgaDeviceController::snapshot, this, &SkyFpgaBackend::snapshot, Qt::DirectConnection);
    connect(controller_, &Ground::Devices::FpgaDeviceController::logRecordGenerated, this, [this](LogRecord record) {
        record.source=QStringLiteral("SkyCore");emit logRecord(record);
    });
    worker_.setObjectName(QStringLiteral("skyFpgaUsbWorker")); worker_.start();
}
SkyFpgaBackend::~SkyFpgaBackend() { shutdown(); }
void SkyFpgaBackend::setConfiguration(const FpgaControlConfig& configuration)
{
    status_["configuration"] = configuration.toJson();
    if (worker_.isRunning()) QMetaObject::invokeMethod(controller_, [controller = controller_, configuration] {
        controller->setConfiguration(configuration);
    }, Qt::QueuedConnection);
}
void SkyFpgaBackend::shutdown()
{
    if (!worker_.isRunning()) return;
    QMetaObject::invokeMethod(controller_, &Ground::Devices::FpgaDeviceController::disconnectDevice, Qt::BlockingQueuedConnection);
    worker_.quit(); worker_.wait(); controller_ = nullptr;
    if (completion_) { auto callback = std::move(completion_); activeRequest_ = 0;
        callback(CommandErrorCode::DeviceDisconnectFailed, QStringLiteral("SkyCore stopped; execution state must be read back."), status_); }
}
void SkyFpgaBackend::submit(const QJsonObject& operation, Completion completion)
{
    QString error;
    if (!validateOperation(operation, &error)) { completion(CommandErrorCode::InvalidPayload, error, status_); return; }
    if (activeRequest_) { completion(CommandErrorCode::DeviceOperationBusy, QStringLiteral("FPGA operation is in progress."), status_); return; }
    if (!worker_.isRunning()) { completion(CommandErrorCode::DeviceNotConnected, QStringLiteral("FPGA worker stopped."), status_); return; }
    const quint64 identity = nextRequest_++;
    activeRequest_ = identity; completion_ = std::move(completion);
    QTimer::singleShot(120000,this,[this,identity] {
        if (activeRequest_!=identity) return;
        // Cancel the receive/command loop; a timeout never resubmits a physical write.
        QMetaObject::invokeMethod(controller_, &Ground::Devices::FpgaDeviceController::disconnectDevice,Qt::QueuedConnection);
        auto callback=std::move(completion_);activeRequest_=0;
        if(callback) callback(CommandErrorCode::ConfigApplyFailed,QStringLiteral("FPGA operation timed out; physical outcome is unknown. Reconnect and read back before retrying."),status_);
    });
    QPointer<SkyFpgaBackend> self(this);
    QMetaObject::invokeMethod(controller_, [self, controller = controller_, identity, operation] {
        auto finish = [self, identity, operation, controller](CommandErrorCode code, const QString& detail) {
            if (!self) return;
            QJsonObject registers;
            const auto& values=controller->hardwareValues();
            for(auto i=values.begin();i!=values.end();++i) registers[QString::number(i.key())]=double(i.value());
            QMetaObject::invokeMethod(self, [self, identity, code, detail, operation, registers] {
                if (!self || self->activeRequest_ != identity) return;
                self->status_["registers"]=registers;
                if(code!=CommandErrorCode::Ok) self->status_["detail"]=detail;
                if(code==CommandErrorCode::Ok && operation.value("op").toString()=="configure") {
                    self->status_["configuration"]=operation.value("configuration").toObject();
                }
                emit self->statusChanged(self->status_);
                auto callback = std::move(self->completion_); self->activeRequest_ = 0;
                if (callback) callback(code, detail, self->status_);
            }, Qt::QueuedConnection);
        };
        const QString op = operation.value("op").toString();
        if (controller->busy()) { finish(CommandErrorCode::DeviceOperationBusy, QStringLiteral("FPGA is busy.")); return; }
        if (op != "connect" && op != "disconnect" && !controller->ready()) {
            finish(CommandErrorCode::DeviceNotConnected, QStringLiteral("FPGA handshake/readback not ready.")); return;
        }
        // This observer exists only for this worker invocation; periodic refreshes cannot complete another request.
        auto observer = std::make_shared<QMetaObject::Connection>();
        *observer = QObject::connect(controller, &Ground::Devices::FpgaDeviceController::snapshot, controller,
            [finish, observer](quint64, const QJsonObject& document) {
                const QString event = document.value("event").toString();
                if (event != "operation_completed" && event != "operation_failed") return;
                QObject::disconnect(*observer);
                finish(event == "operation_completed" ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed,
                       document.value("detail").toString());
            });
        if (op == "connect") controller->connectDevice(operation.value("locator").toString(), operation.value("backend").toString("auto"));
        else if (op == "disconnect") { controller->disconnectDevice(); QObject::disconnect(*observer); finish(CommandErrorCode::Ok, {}); }
        else if (op == "refresh") controller->refresh();
        else if (op == "configure") {
            const auto config = FpgaControlConfig::fromJson(operation.value("configuration").toObject());
            controller->applyConfiguration(config);
        }
        else if (op == "acquisition") controller->setAcquisition(operation.value("enabled").toBool());
        else if (op == "wms") controller->setWaveform(operation.value("channel").toInt(), operation.value("enabled").toBool());
        else if (op == "dac") controller->setDac(operation.value("channel").toInt(), operation.value("enabled").toBool());
        else if (op == "sensor") controller->setSensorEnabled(quint16(operation.value("source").toInt()), operation.value("enabled").toBool());
        else if (op == "raw") controller->setRawEnabled(operation.value("enabled").toBool());
        else if (op == "temperature") controller->setTemperature(operation.value("celsius").toDouble());
        if (!controller->busy()) { QObject::disconnect(*observer); finish(CommandErrorCode::ConfigApplyFailed, QStringLiteral("Operation was not started; verify FPGA state.")); }
    }, Qt::QueuedConnection);
}
}
