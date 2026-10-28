#pragma once

#include "EmFwc3Session.h"
#include <QWidget>
#include <memory>

namespace VaporView::Ground::Motion
{
class MotionControlPage final : public QWidget
{
    Q_OBJECT
public:
    explicit MotionControlPage(QWidget *parent = nullptr, const QString& sdkDirectory = {});
    ~MotionControlPage() override;
    QWidget *createConnectionPanel(QWidget *parent);
    EmFwc3Session *session() const;
    void setTheme(bool dark, int fontScalePercent = 100);
    void setLanguage(bool english);
    void shutdown();
signals:
    void showConnectionSettingsRequested();
    void showControlsRequested();
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
