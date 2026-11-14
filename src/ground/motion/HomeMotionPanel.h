#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QComboBox;
class QDoubleSpinBox;

namespace VaporView::Ground::Motion
{
class EmFwc3Session;
class MotionControlPage;

class HomeMotionPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit HomeMotionPanel(MotionControlPage *motionPage, QComboBox *channelCombo, QComboBox *portCombo,
                             QLabel *connectionLabel, QWidget *parent = nullptr);
    void setTheme(bool dark, int fontScalePercent = 100);
    void setLanguage(bool english);

signals:
    void openConfigurationRequested();

private:
    void refresh();
    void refreshParameters();
    void refreshPorts();

    EmFwc3Session *session_;
    MotionControlPage *motion_page_;
    QLabel *connection_;
    QComboBox *channel_combo_;
    QComboBox *port_combo_;
    QLabel *position_;
    QLabel *position_label_, *target_label_;
    QDoubleSpinBox *target_;
    QPushButton *go_;
    QPushButton *stop_;
    QPushButton *home_;
    QPushButton *configure_;
    bool english_ = false;
};
}
