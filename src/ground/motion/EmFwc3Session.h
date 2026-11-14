#pragma once

#include <QObject>
#include <QThread>
#include <QStringList>
#include <array>
class QTimer;

namespace VaporView::Ground::Motion
{
struct EmFwc3State
{
    bool connected = false;
    bool busy = false;
    bool continuousMotion = false;
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
    bool isUiTestMode() const { return ui_test_mode_; }
    void setUiTestMode(bool enabled);
    void setUiTestScenario(bool failed, bool stalled);
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
    bool ui_test_mode_ = false, ui_test_failed_ = false, ui_test_stalled_ = false;
    std::array<int, 3> ui_test_directions_{};
    QTimer *ui_test_timer_ = nullptr;
};
}
Q_DECLARE_METATYPE(VaporView::Ground::Motion::EmFwc3State)
