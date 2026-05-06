#ifndef VaporView_SKY_TUI_CONTROLLER_H_
#define VaporView_SKY_TUI_CONTROLLER_H_

#include "SkyRuntime.h"

#include <QObject>
#include <QStringList>
#include <atomic>
#include <memory>
#include <thread>

namespace VaporView
{

class SkyTuiController : public QObject
{
    Q_OBJECT

public:
    SkyTuiController(SkyRuntime *runtime, const SkyRuntimeOptions& options, QObject *parent = nullptr);
    ~SkyTuiController() override;

    void start();

public slots:
    void appendLog(const QString& message);

private:
    void handleCommand(const QString& line);
    void printBanner();
    void printHelp();
    void printStatus();
    void printDevices();
    void printConfig();
    void printPrompt();
    bool parseDeviceName(const QString& name, SkyDeviceId& id) const;
    void handleDeviceCommand(const QString& action, const QString& deviceName);
    void handleRecordCommand(const QString& action);
    void handleWaveformCommand(const QString& action);
    QString recordingStateText(quint8 state) const;
    QString commandErrorText(CommandErrorCode error) const;

    SkyRuntime *runtime_ = nullptr;
    SkyRuntimeOptions options_;
    QStringList pending_logs_;
    std::shared_ptr<std::atomic_bool> input_running_;
    std::thread input_thread_;
    bool started_ = false;
};

}  // namespace VaporView

#endif
