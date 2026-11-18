#pragma once

#include "EmFwc3Session.h"
#include <QFrame>
#include <memory>

namespace VaporView::Ground::Motion
{
class MotionControlPage final : public QFrame
{
    Q_OBJECT
public:
    explicit MotionControlPage(QWidget *parent = nullptr, const QString& sdkDirectory = {});
    ~MotionControlPage() override;
    EmFwc3Session *session() const;
    double targetAngle(int channel) const;
    double stepAngle(int channel) const;
    void setTargetAngle(int channel, double value);
    void setStepAngle(int channel, double value);
    void moveToTarget(int channel);
    void moveStep(int channel, int direction);
    QString portName() const;
    QStringList portNames() const;
    void setPortName(const QString& port);
    void connectDevice();
    void homeChannel(int channel);
    void runContinuous(int channel, int direction);
    void setUiTestMode(bool enabled);
    void setTheme(bool dark, int fontScalePercent = 100);
    void setLanguage(bool english);
    void shutdown();
signals:
    void channelParametersChanged(int channel);
    void portChanged(const QString& port);
    void portsChanged();
protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    struct State;
    std::unique_ptr<State> state_;
    QString text(const char *cn, const char *en) const;
    void refresh();
    void refreshPorts();
    void reflow();
    void saveSettings();
    void setStatus(const QString& message, bool error);
    void run(EmFwc3Session::Command command, int channel = 0, double value = 0);
};
}
