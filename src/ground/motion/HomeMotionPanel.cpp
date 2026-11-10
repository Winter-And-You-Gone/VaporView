#include "HomeMotionPanel.h"
#include "EmFwc3Session.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "shared/theme/AppTheme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace VaporView::Ground::Motion
{
HomeMotionPanel::HomeMotionPanel(EmFwc3Session *session, QComboBox *channelCombo, QWidget *parent)
    : QWidget(parent), session_(session), channel_combo_(channelCombo)
{
    setObjectName(QStringLiteral("homeMotionPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 10);
    layout->setSpacing(6);

    connection_ = new QLabel(this);
    connection_->setObjectName(QStringLiteral("homeMotionConnectionState"));
    layout->addWidget(connection_);

    position_ = new QLabel(this);
    position_->setObjectName(QStringLiteral("homeMotionPosition"));
    position_->setAlignment(Qt::AlignCenter);
    layout->addWidget(position_);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    stop_ = new QPushButton(this);
    stop_->setObjectName(QStringLiteral("homeMotionStopAll"));
    configure_ = new QPushButton(this);
    configure_->setObjectName(QStringLiteral("homeMotionConfigure"));
    actions->addWidget(stop_);
    actions->addWidget(configure_);
    actions->addStretch();
    layout->addLayout(actions);
    layout->addStretch();

    connect(stop_, &QPushButton::clicked, this, [this] {
        if (!VaporView::settingsWritesSuspended())
            session_->execute(EmFwc3Session::Command::StopAll);
    });
    connect(configure_, &QPushButton::clicked, this, &HomeMotionPanel::openConfigurationRequested);
    connect(session_, &EmFwc3Session::stateChanged, this, [this] { refresh(); });
    connect(channel_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refresh(); });
    setLanguage(false);
}

void HomeMotionPanel::setLanguage(bool english)
{
    english_ = english;
    for (int i = 0; i < 3; ++i)
        channel_combo_->setItemText(i, english ? QStringLiteral("Channel %1").arg(i + 1) : QStringLiteral("通道 %1").arg(i + 1));
    stop_->setText(english ? QStringLiteral("Stop all") : QStringLiteral("全部停止"));
    configure_->setText(english ? QStringLiteral("Device configuration") : QStringLiteral("设备配置"));
    refresh();
}

void HomeMotionPanel::refresh()
{
    const auto state = session_->state();
    connection_->setText(state.busy
        ? (english_ ? QStringLiteral("Operation in progress…") : QStringLiteral("操作进行中…"))
        : state.connected ? (english_ ? QStringLiteral("Connected · local USB") : QStringLiteral("已连接 · 本机 USB 直连"))
                          : (english_ ? QStringLiteral("Disconnected") : QStringLiteral("未连接")));
    const int channel = channel_combo_->currentIndex();
    position_->setText(channel >= 0 && channel < 3 && state.connected && state.valid[channel]
        ? QString::number(state.positions[channel], 'f', 3) + QStringLiteral("°") : QStringLiteral("—"));
    stop_->setEnabled(state.connected);
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
