#include "HomeMotionPanel.h"
#include "EmFwc3Session.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "shared/theme/AppTheme.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace VaporView::Ground::Motion
{
HomeMotionPanel::HomeMotionPanel(EmFwc3Session *session, QWidget *parent)
    : QWidget(parent), session_(session)
{
    setObjectName(QStringLiteral("homeMotionPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 10);
    layout->setSpacing(6);

    connection_ = new QLabel(this);
    connection_->setObjectName(QStringLiteral("homeMotionConnectionState"));
    layout->addWidget(connection_);

    auto *axes = new QGridLayout;
    axes->setHorizontalSpacing(8);
    axes->setVerticalSpacing(2);
    for (int i = 0; i < 3; ++i) {
        channels_[i] = new QLabel(this);
        channels_[i]->setAlignment(Qt::AlignCenter);
        positions_[i] = new QLabel(this);
        positions_[i]->setObjectName(QStringLiteral("homeMotionPosition%1").arg(i + 1));
        positions_[i]->setAlignment(Qt::AlignCenter);
        axes->addWidget(channels_[i], 0, i);
        axes->addWidget(positions_[i], 1, i);
        axes->setColumnStretch(i, 1);
    }
    layout->addLayout(axes);

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
    setLanguage(false);
}

void HomeMotionPanel::setLanguage(bool english)
{
    english_ = english;
    for (int i = 0; i < 3; ++i)
        channels_[i]->setText(english ? tr("Channel %1").arg(i + 1) : QStringLiteral("通道 %1").arg(i + 1));
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
    for (int i = 0; i < 3; ++i)
        positions_[i]->setText(state.connected && state.valid[i]
            ? QString::number(state.positions[i], 'f', 3) + QStringLiteral("°") : QStringLiteral("—"));
    stop_->setEnabled(state.connected);
}

void HomeMotionPanel::setTheme(bool dark, int fontScalePercent)
{
    setStyleSheet(applyAppThemeTokens(QStringLiteral(
        "QWidget#homeMotionPanel { background: transparent; }"
        "QWidget#homeMotionPanel QLabel { background: transparent; border: none; color: @vv-text; }"
        "QLabel#homeMotionPosition1, QLabel#homeMotionPosition2, QLabel#homeMotionPosition3 { font-size: %1px; font-weight: 600; }"
        "QPushButton#homeMotionStopAll:enabled { background-color: @vv-danger; color: @vv-text-inverse; }"
    ).arg(qRound(18 * fontScalePercent / 100.0)), dark));
}
}
