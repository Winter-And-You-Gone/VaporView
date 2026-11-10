#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QComboBox;

namespace VaporView::Ground::Motion
{
class EmFwc3Session;

class HomeMotionPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit HomeMotionPanel(EmFwc3Session *session, QComboBox *channelCombo, QWidget *parent = nullptr);
    void setTheme(bool dark, int fontScalePercent = 100);
    void setLanguage(bool english);

signals:
    void openConfigurationRequested();

private:
    void refresh();

    EmFwc3Session *session_;
    QLabel *connection_;
    QComboBox *channel_combo_;
    QLabel *position_;
    QPushButton *stop_;
    QPushButton *configure_;
    bool english_ = false;
};
}
