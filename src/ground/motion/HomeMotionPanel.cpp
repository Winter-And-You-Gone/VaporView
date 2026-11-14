#include "HomeMotionPanel.h"
#include "EmFwc3Session.h"
#include "MotionControlPage.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "shared/theme/AppTheme.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace VaporView::Ground::Motion
{
HomeMotionPanel::HomeMotionPanel(MotionControlPage *motionPage, QComboBox *channelCombo, QComboBox *portCombo,
                                 QLabel *connectionLabel, QWidget *parent)
    : QWidget(parent), session_(motionPage->session()), motion_page_(motionPage), connection_(connectionLabel),
      channel_combo_(channelCombo), port_combo_(portCombo)
{
    setObjectName(QStringLiteral("homeMotionPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 10);
    layout->setSpacing(6);

    auto *fields = new QGridLayout;
    fields->setHorizontalSpacing(6);
    fields->setVerticalSpacing(6);
    position_label_ = new QLabel(this);
    fields->addWidget(position_label_, 0, 0);
    position_ = new QLabel(this);
    position_->setObjectName(QStringLiteral("homeMotionPosition"));
    fields->addWidget(position_, 0, 1);
    target_label_ = new QLabel(this);
    fields->addWidget(target_label_, 0, 2);
    target_ = new QDoubleSpinBox(this);
    target_->setObjectName(QStringLiteral("homeMotionTarget"));
    target_->setRange(-1000000, 1000000); target_->setDecimals(3);
    target_->setSuffix(QStringLiteral("°")); target_->setKeyboardTracking(false);
    fields->addWidget(target_, 0, 3);
    go_ = new QPushButton(this); go_->setObjectName(QStringLiteral("homeMotionGo"));
    fields->addWidget(go_, 0, 4);
    step_label_ = new QLabel(this);
    fields->addWidget(step_label_, 1, 0);
    step_ = new QDoubleSpinBox(this);
    step_->setObjectName(QStringLiteral("homeMotionStep"));
    step_->setRange(0.001, 1000000); step_->setDecimals(3);
    step_->setSuffix(QStringLiteral("°")); step_->setKeyboardTracking(false);
    fields->addWidget(step_, 1, 1, 1, 2);
    minus_ = new QPushButton(QStringLiteral("−"), this); minus_->setObjectName(QStringLiteral("homeMotionMinus"));
    plus_ = new QPushButton(QStringLiteral("+"), this); plus_->setObjectName(QStringLiteral("homeMotionPlus"));
    fields->addWidget(minus_, 1, 3); fields->addWidget(plus_, 1, 4);
    fields->setColumnStretch(3, 1);
    layout->addLayout(fields);

    reverse_ = new QPushButton(this); reverse_->setObjectName(QStringLiteral("homeMotionReverse"));
    forward_ = new QPushButton(this); forward_->setObjectName(QStringLiteral("homeMotionForward"));

    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    stop_ = new QPushButton(this);
    stop_->setObjectName(QStringLiteral("homeMotionStopAll"));
    home_ = new QPushButton(this); home_->setObjectName(QStringLiteral("homeMotionHome"));
    configure_ = new QPushButton(this);
    configure_->setObjectName(QStringLiteral("homeMotionConfigure"));
    actions->addWidget(reverse_); actions->addWidget(forward_);
    actions->addWidget(home_); actions->addWidget(stop_);
    actions->addWidget(configure_);
    layout->addLayout(actions);
    layout->addStretch();

    connect(stop_, &QPushButton::clicked, this, [this] {
        if (!VaporView::settingsWritesSuspended())
            session_->execute(EmFwc3Session::Command::StopAll);
    });
    connect(configure_, &QPushButton::clicked, this, &HomeMotionPanel::openConfigurationRequested);
    connect(port_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) motion_page_->setPortName(port_combo_->itemData(index).toString());
    });
    connect(motion_page_, &MotionControlPage::portChanged, this, &HomeMotionPanel::refreshPorts);
    connect(motion_page_, &MotionControlPage::portsChanged, this, &HomeMotionPanel::refreshPorts);
    connect(home_, &QPushButton::clicked, this, [this] { motion_page_->homeChannel(channel_combo_->currentIndex()); });
    for (auto *button : {reverse_, forward_}) connect(button, &QPushButton::clicked, this, [this, button] {
        motion_page_->runContinuous(channel_combo_->currentIndex(), button == forward_ ? 1 : -1);
    });
    connect(session_, &EmFwc3Session::stateChanged, this, [this] { refresh(); });
    connect(channel_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshParameters(); refresh(); });
    connect(motion_page_, &MotionControlPage::channelParametersChanged, this, [this](int channel) {
        if (channel == channel_combo_->currentIndex()) refreshParameters();
    });
    connect(target_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        const int channel = channel_combo_->currentIndex();
        if (channel >= 0 && channel < 3) motion_page_->setTargetAngle(channel, value);
    });
    connect(step_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        const int channel = channel_combo_->currentIndex();
        if (channel >= 0 && channel < 3) motion_page_->setStepAngle(channel, value);
    });
    connect(go_, &QPushButton::clicked, this, [this] {
        const int channel = channel_combo_->currentIndex();
        target_->interpretText();
        if (channel >= 0 && channel < 3) motion_page_->moveToTarget(channel);
    });
    for (auto *button : {minus_, plus_}) connect(button, &QPushButton::clicked, this, [this, button] {
        const int channel = channel_combo_->currentIndex();
        step_->interpretText();
        if (channel >= 0 && channel < 3) motion_page_->moveStep(channel, button == plus_ ? 1 : -1);
    });
    refreshParameters();
    refreshPorts();
    setLanguage(false);
}

void HomeMotionPanel::setLanguage(bool english)
{
    english_ = english;
    for (int i = 0; i < 3; ++i)
        channel_combo_->setItemText(i, english ? QStringLiteral("Channel %1").arg(i + 1) : QStringLiteral("通道 %1").arg(i + 1));
    stop_->setText(english ? QStringLiteral("Stop") : QStringLiteral("停止"));
    home_->setText(english ? QStringLiteral("Home") : QStringLiteral("回零"));
    reverse_->setText(english ? QStringLiteral("Run reverse") : QStringLiteral("反向连续运行"));
    forward_->setText(english ? QStringLiteral("Run forward") : QStringLiteral("正向连续运行"));
    port_combo_->setAccessibleName(english ? QStringLiteral("Motion COM port") : QStringLiteral("电机串口"));
    position_label_->setText(english ? QStringLiteral("Current angle") : QStringLiteral("当前角度"));
    target_label_->setText(english ? QStringLiteral("Target angle") : QStringLiteral("目标角度"));
    step_label_->setText(english ? QStringLiteral("Step") : QStringLiteral("步长"));
    go_->setText(english ? QStringLiteral("Move") : QStringLiteral("定位"));
    minus_->setAccessibleName(english ? QStringLiteral("Step reverse") : QStringLiteral("反向步进"));
    plus_->setAccessibleName(english ? QStringLiteral("Step forward") : QStringLiteral("正向步进"));
    configure_->setText(english ? QStringLiteral("Settings") : QStringLiteral("设备配置"));
    refreshPorts();
}

void HomeMotionPanel::refresh()
{
    const auto state = session_->state();
    connection_->setText(state.busy
        ? (english_ ? QStringLiteral("Operation in progress…") : QStringLiteral("操作进行中…"))
        : state.connected ? (english_ ? QStringLiteral("Connected · local USB") : QStringLiteral("已连接 · 本机 USB 直连"))
                          : (english_ ? QStringLiteral("Disconnected") : QStringLiteral("未连接")));
    const int channel = channel_combo_->currentIndex();
    position_->setText(state.continuousMotion
        ? (english_ ? QStringLiteral("Reading paused") : QStringLiteral("读取已暂停"))
        : channel >= 0 && channel < 3 && state.connected && state.valid[channel]
        ? QString::number(state.positions[channel], 'f', 3) + QStringLiteral("°") : QStringLiteral("—"));
    stop_->setEnabled(state.connected);
    const bool editable = !state.connected && !state.busy;
    port_combo_->setEnabled(editable);
    for (auto *widget : {static_cast<QWidget *>(target_), static_cast<QWidget *>(step_),
                         static_cast<QWidget *>(go_), static_cast<QWidget *>(minus_), static_cast<QWidget *>(plus_),
                         static_cast<QWidget *>(home_), static_cast<QWidget *>(reverse_), static_cast<QWidget *>(forward_)})
        widget->setEnabled(state.connected && !state.busy && !state.continuousMotion && channel >= 0 && channel < 3);
}

void HomeMotionPanel::refreshPorts()
{
    const QSignalBlocker blocker(port_combo_);
    const QString selectedPort = motion_page_->portName().trimmed();
    port_combo_->clear();
    port_combo_->addItem(english_ ? QStringLiteral("Select port") : QStringLiteral("选择串口"), QString());
    for (const QString& port : motion_page_->portNames())
    {
        const QString name = port.trimmed();
        if (!name.isEmpty() && port_combo_->findData(name) < 0) port_combo_->addItem(name, name);
    }
    if (!selectedPort.isEmpty() && port_combo_->findData(selectedPort) < 0)
        port_combo_->addItem(selectedPort, selectedPort);
    port_combo_->setCurrentIndex(port_combo_->findData(selectedPort));
    refresh();
}

void HomeMotionPanel::refreshParameters()
{
    const int channel = channel_combo_->currentIndex();
    if (channel < 0 || channel >= 3) return;
    const QSignalBlocker targetBlocker(target_), stepBlocker(step_);
    target_->setValue(motion_page_->targetAngle(channel));
    step_->setValue(motion_page_->stepAngle(channel));
}

void HomeMotionPanel::setTheme(bool dark, int fontScalePercent)
{
    setStyleSheet(applyAppThemeTokens(QStringLiteral(
        "QWidget#homeMotionPanel { background: transparent; }"
        "QWidget#homeMotionPanel QLabel { background: transparent; border: none; color: @vv-text; }"
        "QLabel#homeMotionPosition { font-size: %1px; font-weight: 600; }"
        "QPushButton#homeMotionStopAll:enabled { background-color: @vv-danger; color: @vv-text-inverse; }"
    ).arg(qRound(18 * fontScalePercent / 100.0)), dark));
}
}
