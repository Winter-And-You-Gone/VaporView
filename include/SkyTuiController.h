#ifndef VaporView_SKY_TUI_CONTROLLER_H_
#define VaporView_SKY_TUI_CONTROLLER_H_

#include "SkyLocalIpcClient.h"
#include "SkyTuiOptions.h"

#include <QObject>
#include <QStringList>

namespace VaporView
{

struct SkyTuiCommandItem
{
    QString command;
    QString description;
};

struct SkyTuiCommandResult
{
    enum class Type
    {
        None,
        ClearLogs,
        Quit,
    };

    Type type = Type::None;
    QStringList messages;
};

class SkyTuiController : public QObject
{
    Q_OBJECT

public:
    SkyTuiController(SkyLocalIpcClient *client, const SkyTuiOptions& options, QObject *parent = nullptr);

    SkyTuiCommandResult executeCommand(const QString& line);
    QList<SkyTuiCommandItem> commandPalette() const;
    QStringList helpLines() const;

private:
    bool parseDeviceName(const QString& name, SkyDeviceId& id) const;
    SkyTuiCommandResult handleDeviceCommand(const QString& action, const QString& deviceName);
    SkyTuiCommandResult handleRecordCommand(const QString& action);
    SkyTuiCommandResult handleWaveformCommand(const QString& action);
    QStringList statusLines() const;
    QStringList deviceLines() const;
    QStringList configLines() const;
    QString recordingStateText(quint8 state) const;
    QString commandErrorText(CommandErrorCode error) const;

    SkyLocalIpcClient *client_ = nullptr;
    SkyTuiOptions options_;
};

}  // namespace VaporView

#endif
