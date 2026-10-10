#pragma once

#include "FpgaDeviceController.h"
#include "TelemetryTypes.h"
#include <QThread>
#include <functional>

namespace VaporView {
// One controller and one USB receive loop, owned exclusively by SkyCore.
class SkyFpgaBackend final : public QObject {
    Q_OBJECT
public:
    using Completion = std::function<void(CommandErrorCode, QString, QJsonObject)>;
    explicit SkyFpgaBackend(std::unique_ptr<Ground::Devices::FpgaUsbTransport> transport = {}, QObject *parent = nullptr);
    ~SkyFpgaBackend() override;
    void submit(const QJsonObject& operation, Completion completion);
    void shutdown();
    void setConfiguration(const FpgaControlConfig& configuration);
    QJsonObject statusDocument() const { return status_; }
    static bool validateOperation(const QJsonObject& operation, QString *error = nullptr);
signals:
    void statusChanged(QJsonObject status);
    void measurementUpdated(const VaporView::FpgaSensor::AdaptedMeasurements& measurement);
    void waveformUpdated(const VaporView::FpgaWave::CompletedStream& waveform);
    void rawFrame(quint64 time, QByteArray bytes);
    void rawCommand(quint64 time, QByteArray bytes);
    void rawUsbBytes(quint64 time, QByteArray bytes);
    void snapshot(quint64 time, QJsonObject document);
    void logRecord(const VaporView::LogRecord& record);
private:
    QThread worker_;
    Ground::Devices::FpgaDeviceController *controller_ = nullptr;
    QJsonObject status_{{"version",1},{"connected",false},{"ready",false},{"busy",false},{"registers",QJsonObject{}},
        {"configuration",FpgaControlConfig{}.toJson()},{"detail",QStringLiteral("FPGA disconnected")},{"archived_records",QStringLiteral("0")}};
    quint64 nextRequest_ = 1, activeRequest_ = 0;
    Completion completion_;
};
}
