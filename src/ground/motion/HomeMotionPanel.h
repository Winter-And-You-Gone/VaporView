#pragma once

#include <QWidget>
#include <array>

class QLabel;
class QPushButton;

namespace VaporView::Ground::Motion
{
class EmFwc3Session;

class HomeMotionPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit HomeMotionPanel(EmFwc3Session *session, QWidget *parent = nullptr);
    void setTheme(bool dark, int fontScalePercent = 100);
    void setLanguage(bool english);

signals:
    void openConfigurationRequested();

private:
    void refresh();

    EmFwc3Session *session_;
    QLabel *connection_;
    std::array<QLabel *, 3> channels_{};
    std::array<QLabel *, 3> positions_{};
    QPushButton *stop_;
    QPushButton *configure_;
    bool english_ = false;
};
}
