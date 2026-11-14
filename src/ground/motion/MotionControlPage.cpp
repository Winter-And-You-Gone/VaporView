#include "MotionControlPage.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/TopLevelCardStyle.h"
#include "ground/widgets/LabelTextSelection.h"
#include "ground/widgets/DeviceEnabledCheckBox.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QGridLayout>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <limits>

namespace VaporView::Ground::Motion
{
namespace
{
void wording(QObject *object, const char *cn, const char *en)
{
    object->setProperty("emFwc3Cn", QString::fromUtf8(cn));
    object->setProperty("emFwc3En", QString::fromUtf8(en));
}
QPushButton *button(QWidget *parent, const char *name, const char *cn, const char *en)
{
    auto *b = new QPushButton(QString::fromUtf8(cn), parent);
    b->setObjectName(QString::fromLatin1(name)); wording(b, cn, en);
    b->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    b->setFocusPolicy(Qt::TabFocus); b->setAccessibleName(QString::fromUtf8(cn));
    return b;
}
QLabel *label(QWidget *parent, const char *cn, const char *en)
{
    auto *l = new QLabel(parent); wording(l, cn, en); l->setWordWrap(true); return l;
}
QDoubleSpinBox *angle(QWidget *parent, const QString& name, bool step)
{
    auto *spin = new QDoubleSpinBox(parent); spin->setObjectName(name);
    // These are numeric input limits, not claims about the connected mechanism's travel.
    spin->setRange(step ? 0.001 : -1000000, 1000000); spin->setDecimals(3);
    spin->setSuffix(QStringLiteral("°")); spin->setValue(step ? 1 : 0);
    spin->setKeyboardTracking(false); return spin;
}
}
struct MotionControlPage::State
{
    EmFwc3Session *session = nullptr;
    QWidget *header = nullptr, *connectionControls = nullptr;
    QGridLayout *headerLayout = nullptr;
    std::array<QGridLayout*, 3> motionLayouts{};
    std::array<std::array<QWidget*, 4>, 3> motionGroups{};
    QGridLayout *deviceLayout = nullptr;
    std::array<QWidget*, 2> deviceGroups{};
    QLabel *title = nullptr;
    QComboBox *channel = nullptr;
    QStackedWidget *channels = nullptr;
    std::array<QLabel*, 3> positions{};
    std::array<QDoubleSpinBox*, 3> targets{}, steps{};
    QComboBox *port = nullptr;
    QPushButton *connect = nullptr, *disconnect = nullptr, *refreshPorts = nullptr, *stopAll = nullptr;
    QLabel *connection = nullptr, *status = nullptr, *speedReadback = nullptr;
    QSpinBox *speed = nullptr;
    QList<QWidget*> normalActions;
    QList<QWidget*> stopActions;
    bool english = false;
    bool wasConnected = false;
};

MotionControlPage::MotionControlPage(QWidget *parent, const QString& sdkDirectory)
    : QFrame(parent), state_(std::make_unique<State>())
{
    setObjectName(QStringLiteral("emFwc3ConnectionPanel"));
    setProperty("emFwc3Card", true);
    configureTopLevelCard(this);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto& s = *state_;
    s.session = new EmFwc3Session(this, sdkDirectory);
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(1, 1, 1, 1); outer->setSpacing(0);
    s.header = new QWidget(this); s.header->setObjectName(QStringLiteral("sectionTitleBar"));
    s.headerLayout = new QGridLayout(s.header); s.headerLayout->setContentsMargins(10, 4, 10, 4);
    s.headerLayout->setHorizontalSpacing(12); s.headerLayout->setVerticalSpacing(4);
    s.title = new VisualTextLabel(s.header); s.title->setObjectName(QStringLiteral("sectionTitleLabel"));
    configureSelectableCardTitle(s.title); wording(s.title, "运动控制 · EM-FWC3", "Motion control · EM-FWC3");
    s.headerLayout->addWidget(s.title, 0, 0);
    s.connectionControls = new QWidget(s.header);
    auto *connectionLayout = new QHBoxLayout(s.connectionControls);
    connectionLayout->setContentsMargins(0, 0, 0, 0); connectionLayout->setSpacing(6);
    connectionLayout->addStretch();
    s.port = new QComboBox(s.connectionControls); s.port->setEditable(true); s.port->setInsertPolicy(QComboBox::NoInsert);
    s.port->setObjectName(QStringLiteral("emFwc3Port")); s.port->setFixedWidth(110);
    s.port->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    s.port->setMinimumContentsLength(8); connectionLayout->addWidget(s.port);
    s.refreshPorts = button(s.connectionControls, "emFwc3RefreshPorts", "刷新串口", "Refresh ports"); connectionLayout->addWidget(s.refreshPorts);
    s.connect = button(s.connectionControls, "emFwc3Connect", "连接", "Connect"); connectionLayout->addWidget(s.connect);
    s.disconnect = button(s.connectionControls, "emFwc3Disconnect", "断开", "Disconnect"); connectionLayout->addWidget(s.disconnect);
    s.headerLayout->addWidget(s.connectionControls, 0, 1);
    s.header->installEventFilter(this);
    s.headerLayout->setColumnStretch(1, 1); outer->addWidget(s.header);

    auto *content = new QWidget(this); content->setObjectName(QStringLiteral("emFwc3CardBody"));
    content->setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(content); layout->setContentsMargins(12, 8, 12, 10); layout->setSpacing(8);
    s.connection = new QLabel(content); s.connection->setObjectName(QStringLiteral("emFwc3ConnectionState")); layout->addWidget(s.connection);
    layout->addWidget(label(content, "本机 USB 虚拟串口直连，独立于直连 / FPGA 与本地 / 远端采集。",
        "Local USB virtual COM, independent of Direct / FPGA and Local / Remote acquisition."));
    s.channel = new QComboBox(content); s.channel->setObjectName(QStringLiteral("emFwc3ChannelSelector"));
    s.channel->addItems({QString(), QString(), QString()});
    s.channel->hide(); // Keep multi-channel routing; this installation exposes channel 1 only.
    s.channels = new QStackedWidget(content); s.channels->setObjectName(QStringLiteral("emFwc3Channels"));
    connect(s.channel, &QComboBox::currentIndexChanged, s.channels, &QStackedWidget::setCurrentIndex);
    QSettings settings("VaporView", "EmFwc3");
    for (int i = 0; i < 3; ++i) {
        const auto suffix = QString::number(i + 1);
        auto *card = new QWidget(s.channels); card->setObjectName(QStringLiteral("emFwc3Channel") + suffix);
        auto *v = new QVBoxLayout(card); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(8);
        auto *readback = new QHBoxLayout;
        readback->addWidget(label(card, "电机型号", "Motor type"));
        auto *motorType = new QLabel(QStringLiteral("CEM-FPM"), card);
        motorType->setObjectName(QStringLiteral("emFwc3MotorType") + suffix);
        readback->addWidget(motorType); readback->addSpacing(12);
        readback->addWidget(label(card, "当前角度", "Current angle"));
        s.positions[i] = new QLabel(QStringLiteral("—"), card);
        s.positions[i]->setObjectName(QStringLiteral("emFwc3Position") + suffix); readback->addWidget(s.positions[i]); readback->addStretch(); v->addLayout(readback);
        auto *moves = new QGridLayout; moves->setHorizontalSpacing(12); moves->setVerticalSpacing(6);
        s.motionLayouts[i] = moves;
        auto *targetGroup = new QWidget(card); auto *targetRow = new QHBoxLayout(targetGroup);
        targetRow->setContentsMargins(0, 0, 0, 0); targetRow->setSpacing(6);
        targetRow->addWidget(label(card, "目标角度", "Target angle"));
        s.targets[i] = angle(card, QStringLiteral("emFwc3Target") + suffix, false); targetRow->addWidget(s.targets[i]);
        auto *go = button(card, qPrintable(QStringLiteral("emFwc3Go") + suffix), "定位", "Move to target");
        targetRow->addWidget(go); s.normalActions << go << s.targets[i];
        connect(go, &QPushButton::clicked, this, [this, i] { run(EmFwc3Session::Command::Absolute, i, state_->targets[i]->value()); });
        auto *stepGroup = new QWidget(card); auto *stepRow = new QHBoxLayout(stepGroup);
        stepRow->setContentsMargins(0, 0, 0, 0); stepRow->setSpacing(6);
        stepRow->addWidget(label(card, "相对步长", "Relative step"));
        s.steps[i] = angle(card, QStringLiteral("emFwc3Step") + suffix, true); stepRow->addWidget(s.steps[i]);
        connect(s.targets[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, i] { emit channelParametersChanged(i); });
        connect(s.steps[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, i] { emit channelParametersChanged(i); });
        auto *minus = button(card, qPrintable(QStringLiteral("emFwc3Minus") + suffix), "反向步进", "Step −");
        auto *plus = button(card, qPrintable(QStringLiteral("emFwc3Plus") + suffix), "正向步进", "Step +");
        stepRow->addWidget(minus); stepRow->addWidget(plus); v->addLayout(moves);
        s.normalActions << minus << plus << s.steps[i];
        connect(minus, &QPushButton::clicked, this, [this, i] { run(EmFwc3Session::Command::Relative, i, -state_->steps[i]->value()); });
        connect(plus, &QPushButton::clicked, this, [this, i] { run(EmFwc3Session::Command::Relative, i, state_->steps[i]->value()); });
        auto *actionGroup = new QWidget(card); auto *actions = new QHBoxLayout(actionGroup);
        actions->setContentsMargins(0, 0, 0, 0); actions->setSpacing(6);
        auto *home = button(card, qPrintable(QStringLiteral("emFwc3Home") + suffix), "回零", "Home");
        auto *stop = button(card, qPrintable(QStringLiteral("emFwc3Stop") + suffix), "停止", "Stop");
        actions->addWidget(home); actions->addWidget(stop); actions->addStretch();
        s.normalActions << home; s.stopActions << stop;
        connect(home, &QPushButton::clicked, this, [this, i] {
            if (QMessageBox::question(this, text("回零", "Home"),
                text("将驱动通道 %1 回零，确认周围运动空间已清空？", "Home channel %1? Check the mechanism's clearance first.").arg(i + 1)) == QMessageBox::Yes)
                run(EmFwc3Session::Command::Home, i);
        });
        connect(stop, &QPushButton::clicked, this, [this, i] { run(EmFwc3Session::Command::Continuous, i, 0); });
        auto *advanced = new QWidget(card); advanced->setObjectName(QStringLiteral("emFwc3ContinuousArea") + suffix);
        auto *a = new QHBoxLayout(advanced); a->setContentsMargins(0, 0, 0, 0); a->setSpacing(8);
        auto *toggle = new DeviceEnabledCheckBox(advanced); toggle->setFocusPolicy(Qt::TabFocus);
        toggle->setObjectName(QStringLiteral("emFwc3ContinuousToggle") + suffix); wording(toggle, "连续运动", "Continuous motion"); a->addWidget(toggle);
        auto *continuousBody = new QWidget(advanced); continuousBody->setObjectName(QStringLiteral("emFwc3ContinuousBody") + suffix);
        auto *continuousLayout = new QHBoxLayout(continuousBody); continuousLayout->setContentsMargins(0, 0, 0, 0); continuousLayout->setSpacing(6);
        auto *reverse = button(continuousBody, qPrintable(QStringLiteral("emFwc3Reverse") + suffix), "反向连续运行", "Run reverse");
        auto *forward = button(continuousBody, qPrintable(QStringLiteral("emFwc3Forward") + suffix), "正向连续运行", "Run forward");
        continuousLayout->addWidget(reverse); continuousLayout->addWidget(forward);
        a->addWidget(continuousBody); a->addStretch(); continuousBody->hide();
        advanced->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        s.motionGroups[i] = {targetGroup, stepGroup, actionGroup, advanced};
        for (int group = 0; group < 4; ++group) moves->addWidget(s.motionGroups[i][group], group, 0);
        moves->setColumnStretch(0, 1);
        connect(toggle, &QCheckBox::toggled, continuousBody, &QWidget::setVisible);
        connect(toggle, &QCheckBox::toggled, this, [this] { QTimer::singleShot(0, this, &MotionControlPage::reflow); });
        s.normalActions << advanced;
        for (auto *b : {reverse, forward}) connect(b, &QPushButton::clicked, this, [this, i, b, forward] {
            if (QMessageBox::question(this, text("连续运动", "Continuous motion"),
                text("连续运动将持续到下发停止指令，确认开始？", "Continuous motion runs until a stop command. Start?")) == QMessageBox::Yes)
                run(EmFwc3Session::Command::Continuous, i, b == forward ? 1 : -1);
        });
        s.channels->addWidget(card);
    }
    layout->addWidget(s.channels);
    auto *separator = new QFrame(content); separator->setFrameShape(QFrame::HLine); layout->addWidget(separator);
    layout->addWidget(label(content, "设备公共参数 · 速度单位及有效范围请按实际电机确认", "Device-wide parameters · verify speed units and range for the fitted motor"));
    s.deviceLayout = new QGridLayout; s.deviceLayout->setHorizontalSpacing(12); s.deviceLayout->setVerticalSpacing(6);
    auto *speedGroup = new QWidget(content); auto *speedRow = new QHBoxLayout(speedGroup);
    speedRow->setContentsMargins(0, 0, 0, 0); speedRow->setSpacing(6);
    speedRow->addWidget(label(content, "速度", "Speed"));
    s.speed = new QSpinBox(content); s.speed->setObjectName(QStringLiteral("emFwc3Speed"));
    s.speed->setRange(1, std::numeric_limits<int>::max()); s.speed->setValue(50); s.speed->setKeyboardTracking(false); speedRow->addWidget(s.speed);
    auto *apply = button(content, "emFwc3ApplySpeed", "应用速度并读回", "Apply speed and read back"); speedRow->addWidget(apply);
    s.speedReadback = new QLabel(content); s.speedReadback->setObjectName(QStringLiteral("emFwc3SpeedReadback")); speedRow->addStretch();
    auto *deviceGroup = new QWidget(content); auto *deviceActions = new QHBoxLayout(deviceGroup);
    deviceActions->setContentsMargins(0, 0, 0, 0); deviceActions->setSpacing(6);
    auto *save = button(content, "emFwc3SavePosition", "保存位置到设备", "Save positions to device"); deviceActions->addWidget(save);
    s.stopAll = button(content, "emFwc3StopAll", "全部停止", "Stop all"); deviceActions->addWidget(s.stopAll); deviceActions->addWidget(s.speedReadback); deviceActions->addStretch();
    s.deviceGroups = {speedGroup, deviceGroup};
    s.deviceLayout->addWidget(speedGroup, 0, 0); s.deviceLayout->addWidget(deviceGroup, 1, 0);
    s.deviceLayout->setColumnStretch(0, 1); layout->addLayout(s.deviceLayout);
    s.normalActions << s.speed << apply << save; s.stopActions << s.stopAll;
    connect(s.stopAll, &QPushButton::clicked, this, [this] { run(EmFwc3Session::Command::StopAll); });
    connect(apply, &QPushButton::clicked, this, [this] { run(EmFwc3Session::Command::Speed, 0, state_->speed->value()); });
    connect(save, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, text("保存位置", "Save positions"),
            text("将当前位置写入设备 EEPROM？此操作不应频繁执行。", "Write the current positions to device EEPROM? Avoid frequent writes.")) == QMessageBox::Yes)
            run(EmFwc3Session::Command::SavePosition);
    });
    s.status = new QLabel(content); s.status->setObjectName(QStringLiteral("emFwc3Status")); s.status->setWordWrap(true); s.status->hide(); layout->addWidget(s.status);
    outer->addWidget(content);
    connect(s.refreshPorts, &QPushButton::clicked, this, &MotionControlPage::refreshPorts);
    connect(s.connect, &QPushButton::clicked, this, [this] {
        if (VaporView::settingsWritesSuspended()) return;
        const QStringList types{QStringLiteral("CEM-FPM"), QStringLiteral("CEM-FPM"), QStringLiteral("CEM-FPM")};
        saveSettings(); state_->session->connectDevice(state_->port->currentData().isValid()
            && state_->port->currentText() == state_->port->itemText(state_->port->currentIndex())
                ? state_->port->currentData().toString() : state_->port->currentText().trimmed(), types);
    });
    connect(s.disconnect, &QPushButton::clicked, s.session, &EmFwc3Session::disconnectDevice);
    refreshPorts();
    const auto savedPort = settings.value(QStringLiteral("port")).toString();
    if (!savedPort.isEmpty()) {
        const int index = s.port->findData(savedPort);
        if (index >= 0) s.port->setCurrentIndex(index); else s.port->setEditText(savedPort);
    }
    connect(s.port, &QComboBox::currentTextChanged, this, [this] { saveSettings(); refresh(); });
    connect(s.session, &EmFwc3Session::stateChanged, this, [this] { refresh(); });
    connect(s.session, &EmFwc3Session::operationFinished, this, [this](const QString& operation, int code, const QString& detail) {
        const bool motion = operation == "relative" || operation == "absolute" || operation == "home" || operation == "continuous";
        const QString result = code == 0
            ? (state_->session->state().continuousMotion
                ? text("连续运行已请求；角度读取已暂停，停止后恢复。", "Continuous motion requested; angle readings resume after stopping.")
                : motion ? text("指令已发送，请观察角度读回；不代表已到位。", "Command sent; observe the angle readback. Arrival is not confirmed.")
                      : text("操作成功。", "Operation succeeded."))
            : text("操作失败，错误码 %1。", "Operation failed, code %1.").arg(code);
        setStatus(operation + QStringLiteral(": ") + result + (detail.isEmpty() ? QString() : QStringLiteral("\n") + detail), code != 0);
    });
    setLanguage(false); setTheme(false); refresh();
}

MotionControlPage::~MotionControlPage() = default;
EmFwc3Session *MotionControlPage::session() const { return state_->session; }
double MotionControlPage::targetAngle(int channel) const { return state_->targets.at(channel)->value(); }
double MotionControlPage::stepAngle(int channel) const { return state_->steps.at(channel)->value(); }
void MotionControlPage::setTargetAngle(int channel, double value) { state_->targets.at(channel)->setValue(value); }
void MotionControlPage::setStepAngle(int channel, double value) { state_->steps.at(channel)->setValue(value); }
void MotionControlPage::moveToTarget(int channel) { run(EmFwc3Session::Command::Absolute, channel, targetAngle(channel)); }
void MotionControlPage::moveStep(int channel, int direction) { run(EmFwc3Session::Command::Relative, channel, direction * stepAngle(channel)); }
void MotionControlPage::shutdown() { state_->session->shutdown(); }
QString MotionControlPage::text(const char *cn, const char *en) const
{
    return QString::fromUtf8(state_->english ? en : cn);
}
void MotionControlPage::refreshPorts()
{
    auto *port = state_->port;
    if (!port) return;
    QString previous = port->currentData().isValid() && port->currentText() == port->itemText(port->currentIndex())
        ? port->currentData().toString() : port->currentText();
    port->blockSignals(true); port->clear();
    for (const auto& info : QSerialPortInfo::availablePorts()) {
        port->addItem(info.portName(), info.portName());
        port->setItemData(port->count() - 1, info.description(), Qt::ToolTipRole);
    }
    int index = port->findData(previous);
    if (index >= 0) port->setCurrentIndex(index);
    else port->setEditText(previous);
    port->blockSignals(false); refresh();
}
void MotionControlPage::saveSettings()
{
    const auto& s = *state_;
    QSettings settings("VaporView", "EmFwc3");
    const QString port = s.port->currentIndex() >= 0 && s.port->currentText() == s.port->itemText(s.port->currentIndex())
        ? s.port->currentData().toString() : s.port->currentText().trimmed();
    VaporView::setPersistentSetting(settings, QStringLiteral("port"), port);
}
void MotionControlPage::run(EmFwc3Session::Command command, int channel, double value)
{
    if (!VaporView::settingsWritesSuspended()) state_->session->execute(command, channel, value);
}
void MotionControlPage::refresh()
{
    auto& s = *state_; const auto snapshot = s.session->state();
    if (snapshot.connected && !s.wasConnected && snapshot.speedValid) s.speed->setValue(snapshot.speed);
    s.wasConnected = snapshot.connected;
    const QString connection = snapshot.busy ? text("操作进行中…", "Operation in progress…")
        : snapshot.connected ? text("已连接 · 本机 USB 直连", "Connected · local USB") : text("未连接", "Disconnected");
    s.connection->setText(connection);
    for (int i = 0; i < 3; ++i) s.positions[i]->setText(snapshot.continuousMotion ? text("读取已暂停", "Reading paused") : snapshot.connected && snapshot.valid[i]
        ? QString::number(snapshot.positions[i], 'f', 3) + QStringLiteral("°") : QStringLiteral("—"));
    const QString speedReadback = text("速度读回：%1", "Speed readback: %1").arg(snapshot.connected && snapshot.speedValid ? QString::number(snapshot.speed) : QStringLiteral("—"));
    if (s.speedReadback->text() != speedReadback) {
        s.speedReadback->setText(speedReadback);
        QTimer::singleShot(0, this, &MotionControlPage::reflow);
    }
    for (auto *w : s.normalActions) w->setEnabled(snapshot.connected && !snapshot.busy && !snapshot.continuousMotion);
    for (auto *w : s.stopActions) w->setEnabled(snapshot.connected);
    {
        const bool editable = !snapshot.connected && !snapshot.busy;
        s.port->setEnabled(editable); s.refreshPorts->setEnabled(editable);
        s.connect->setEnabled(editable && !s.port->currentText().trimmed().isEmpty());
        s.disconnect->setEnabled(snapshot.connected && !snapshot.busy);
    }
}
void MotionControlPage::setStatus(const QString& message, bool error)
{
    state_->status->setText(message); state_->status->setProperty("emFwc3Error", error);
    state_->status->style()->unpolish(state_->status); state_->status->style()->polish(state_->status);
    state_->status->setVisible(!message.isEmpty());
}
void MotionControlPage::setLanguage(bool english)
{
    auto& s = *state_; s.english = english;
    const auto translate = [english](QWidget *root) {
        if (!root) return;
        auto objects = root->findChildren<QWidget*>(); objects.prepend(root);
        for (auto *object : objects) {
            const auto value = object->property(english ? "emFwc3En" : "emFwc3Cn");
            if (!value.isValid()) continue;
            if (auto *l = qobject_cast<QLabel*>(object)) l->setText(value.toString());
            else if (auto *b = qobject_cast<QAbstractButton*>(object)) { b->setText(value.toString()); b->setAccessibleName(value.toString()); }
        }
    };
    translate(this);
    s.port->setAccessibleName(text("串口", "COM port"));
    s.channel->setAccessibleName(text("通道", "Channel"));
    for (int i = 0; i < 3; ++i) {
        s.channel->setItemText(i, text("通道 %1", "Channel %1").arg(i + 1));
    }
    refresh(); reflow();
}
void MotionControlPage::setTheme(bool dark, int fontScalePercent)
{
    state_->port->setFixedWidth(qRound(110 * fontScalePercent / 100.0));
    // Leave ample room for wrapped titles without scaling Qt's maximum-size sentinel.
    const QString style = applyAppThemeTokens(QStringLiteral(
        "QFrame[emFwc3Card=true] { background-color: @vv-surface-raised; color: @vv-text; border: 1px solid @vv-border; border-radius: 12px; }"
        "QFrame[emFwc3Card=true] > QWidget#sectionTitleBar { background-color: @vv-surface-raised; border: none; border-bottom: 1px solid @vv-border; border-top-left-radius: 11px; border-top-right-radius: 11px; min-height: %2px; max-height: 4096px; }"
        "QWidget#emFwc3CardBody { background-color: @vv-surface-raised; border: none; border-bottom-left-radius: 11px; border-bottom-right-radius: 11px; }"
        "QLabel, QLabel#sectionTitleLabel { background-color: transparent; color: @vv-text; border: none; }"
        "QStackedWidget, QStackedWidget > QWidget { background-color: transparent; border: none; }"
        "QPushButton { min-width: 0px; padding: 3px 10px; min-height: %4px; max-height: %4px; }"
        "QComboBox, QSpinBox, QDoubleSpinBox { min-height: %4px; max-height: %4px; }"
        "QCheckBox { color: @vv-text; background-color: transparent; }"
        "QCheckBox:disabled { color: @vv-text-disabled; }"
        "QWidget#sectionTitleBar QLabel#sectionTitleLabel { font-size: %3px; font-weight: 700; padding: 0px; margin: 0px; }"
        "QLabel[emFwc3Error=true] { color: @vv-danger; }"
        "QLabel#emFwc3Position1, QLabel#emFwc3Position2, QLabel#emFwc3Position3 { font-size: %1px; font-weight: 600; }"
        "QPushButton#emFwc3StopAll:enabled, QPushButton#emFwc3Stop1:enabled, QPushButton#emFwc3Stop2:enabled, QPushButton#emFwc3Stop3:enabled { background-color: @vv-danger; color: @vv-text-inverse; }"
    ).arg(qRound(24 * fontScalePercent / 100.0)).arg(qRound(40 * fontScalePercent / 100.0)).arg(qRound(16 * fontScalePercent / 100.0)).arg(qRound(24 * fontScalePercent / 100.0)), dark);
    setStyleSheet(style);
    setPalette(appThemePalette(dark));
    reflow();
}
void MotionControlPage::resizeEvent(QResizeEvent *event) { QFrame::resizeEvent(event); reflow(); }
bool MotionControlPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == state_->header && event->type() == QEvent::LayoutRequest)
        QTimer::singleShot(0, this, &MotionControlPage::reflow);
    return QFrame::eventFilter(watched, event);
}
void MotionControlPage::reflow()
{
    auto& s = *state_;
    const bool wrap = width() - 34 < s.title->sizeHint().width() + s.connectionControls->layout()->sizeHint().width();
    int row, column, rowSpan, columnSpan;
    s.headerLayout->getItemPosition(s.headerLayout->indexOf(s.connectionControls), &row, &column, &rowSpan, &columnSpan);
    if (row != (wrap ? 1 : 0)) {
        s.headerLayout->removeWidget(s.connectionControls);
        s.headerLayout->addWidget(s.connectionControls, wrap ? 1 : 0, wrap ? 0 : 1, 1, wrap ? 2 : 1);
    }
    const int available = width() - 28;
    const auto arrange = [available](QGridLayout *grid, const auto& groups) {
        if (!grid || !groups[0] || !groups[1]) return;
        int firstColumn = 0, secondColumn = 0;
        for (size_t i = 0; i + 1 < groups.size(); i += 2)
        {
            firstColumn = qMax(firstColumn, groups[i]->sizeHint().width());
            secondColumn = qMax(secondColumn, groups[i + 1]->sizeHint().width());
        }
        const int needed = firstColumn + secondColumn + grid->horizontalSpacing();
        const bool twoColumns = available >= needed;
        int currentRow, currentColumn, rowSpan, columnSpan;
        grid->getItemPosition(grid->indexOf(groups[1]), &currentRow, &currentColumn, &rowSpan, &columnSpan);
        if (currentColumn == (twoColumns ? 1 : 0)) return;
        for (auto *group : groups) grid->removeWidget(group);
        grid->setColumnStretch(1, twoColumns ? 1 : 0);
        for (size_t i = 0; i < groups.size(); ++i)
            grid->addWidget(groups[i], twoColumns ? int(i / 2) : int(i), twoColumns ? int(i % 2) : 0);
    };
    for (int i = 0; i < 3; ++i) arrange(s.motionLayouts[i], s.motionGroups[i]);
    arrange(s.deviceLayout, s.deviceGroups);
}
}
