#pragma once

#include <QObject>
#include <QThread>
#include <QStringList>
#include <array>

namespace VaporView::Ground::Motion
{
struct EmFwc3State
{
    bool connected = false;
    bool busy = false;
    std::array<double, 3> positions{};
    std::array<bool, 3> valid{};
    int speed = 0;
    bool speedValid = false;
};

class EmFwc3Worker;

// A local USB/serial session, deliberately independent of sensor/FPGA transport.
class EmFwc3Session final : public QObject
{
    Q_OBJECT
public:
    enum class Command { Relative, Absolute, Home, Continuous, StopAll, Speed, SavePosition };
    explicit EmFwc3Session(QObject *parent = nullptr, const QString& sdkDirectory = {});
    ~EmFwc3Session() override;
    EmFwc3State state() const { return state_; }
    void connectDevice(const QString& port, const QStringList& motorTypes);
    void disconnectDevice();
    void execute(Command command, int channel = 0, double value = 0);
    void shutdown();
signals:
    void stateChanged(const VaporView::Ground::Motion::EmFwc3State& state);
    void operationFinished(const QString& operation, int code, const QString& detail);
private:
    QThread thread_;
    EmFwc3Worker *worker_ = nullptr;
    EmFwc3State state_;
    bool closing_ = false;
    int pending_ = 0;
};
}
Q_DECLARE_METATYPE(VaporView::Ground::Motion::EmFwc3State)
