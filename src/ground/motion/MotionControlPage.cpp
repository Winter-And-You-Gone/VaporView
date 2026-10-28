#include "MotionControlPage.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/TopLevelCardStyle.h"
#include "ground/widgets/LabelTextSelection.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QGridLayout>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
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
    b->setFocusPolicy(Qt::TabFocus); b->setAccessibleName(QString::fromUtf8(cn));
    return b;
}
QLabel *label(QWidget *parent, const char *cn, const char *en)
{
    auto *l = new QLabel(parent); wording(l, cn, en); l->setWordWrap(true); return l;
}
struct Card
{
    QFrame *frame;
    QLabel *title;
    QWidget *body;
};
Card createCard(QWidget *parent, const QString& name)
{
    auto *frame = new QFrame(parent); frame->setObjectName(name);
    frame->setProperty("emFwc3Card", true);
    configureTopLevelCard(frame);
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(1, 1, 1, 1); layout->setSpacing(0);
    auto *header = new QWidget(frame); header->setObjectName(QStringLiteral("sectionTitleBar"));
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *headerLayout = new QHBoxLayout(header); headerLayout->setContentsMargins(12, 6, 12, 6);
    auto *title = new VisualTextLabel(header); title->setObjectName(QStringLiteral("sectionTitleLabel"));
    configureSelectableCardTitle(title); headerLayout->addWidget(title, 1);
    layout->addWidget(header);
    auto *body = new QWidget(frame); body->setObjectName(QStringLiteral("emFwc3CardBody"));
    body->setAttribute(Qt::WA_StyledBackground, true); layout->addWidget(body);
    return {frame, title, body};
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
    QPointer<QWidget> config;
    QScrollArea *scroll = nullptr;
    QGridLayout *grid = nullptr;
    std::array<QFrame*, 3> cards{};
    std::array<QLabel*, 3> channelTitles{};
    std::array<QLabel*, 3> positions{};
    std::array<QDoubleSpinBox*, 3> targets{}, steps{};
    std::array<QComboBox*, 3> types{};
    QComboBox *port = nullptr;
    QCheckBox *confirmTypes = nullptr;
    QPushButton *connect = nullptr, *disconnect = nullptr, *refreshPorts = nullptr, *stopAll = nullptr;
    QLabel *connection = nullptr, *configConnection = nullptr, *status = nullptr, *speedReadback = nullptr;
    QLabel *configStatus = nullptr;
    QSpinBox *speed = nullptr;
    QList<QWidget*> normalActions;
    QList<QWidget*> stopActions;
    bool english = false, dark = false;
    int scale = 100;
    int columns = 0;
    bool wasConnected = false;
};

MotionControlPage::MotionControlPage(QWidget *parent, const QString& sdkDirectory)
    : QWidget(parent), state_(std::make_unique<State>())
{
    setObjectName(QStringLiteral("motionControlPage"));
    auto& s = *state_;
    s.session = new EmFwc3Session(this, sdkDirectory);
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    s.scroll = new QScrollArea(this); s.scroll->setWidgetResizable(true); s.scroll->setFrameShape(QFrame::NoFrame);
    s.scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    s.scroll->viewport()->installEventFilter(this);
    auto *content = new QWidget(s.scroll);
    content->setObjectName(QStringLiteral("emFwc3MotionContent"));
    auto *layout = new QVBoxLayout(content); layout->setContentsMargins(12, 12, 12, 12); layout->setSpacing(12);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    auto *heading = new QHBoxLayout;
    auto *title = label(content, "运动控制 · EM-FWC3", "Motion control · EM-FWC3");
    title->setObjectName(QStringLiteral("sectionTitleLabel")); heading->addWidget(title); heading->addStretch();
    title->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    auto *settings = button(content, "emFwc3ConnectionSettings", "连接设置", "Connection settings");
    connect(settings, &QPushButton::clicked, this, &MotionControlPage::showConnectionSettingsRequested);
    heading->addWidget(settings);
    s.stopAll = button(content, "emFwc3StopAll", "全部停止", "Stop all"); heading->addWidget(s.stopAll);
    s.stopActions << s.stopAll;
    connect(s.stopAll, &QPushButton::clicked, this, [this] { run(EmFwc3Session::Command::StopAll); });
    layout->addLayout(heading);
    layout->addWidget(label(content, "本机 USB 直连（虚拟串口）· 独立于传感器直连 / FPGA 路径", "Local USB (virtual COM) · independent of Direct / FPGA sensor transport"));
    s.connection = new QLabel(content); s.connection->setObjectName(QStringLiteral("emFwc3ConnectionState")); layout->addWidget(s.connection);
    s.grid = new QGridLayout; s.grid->setSpacing(12);
    for (int i = 0; i < 3; ++i) {
        const auto suffix = QString::number(i + 1);
        const auto section = createCard(content, QStringLiteral("emFwc3Channel") + suffix);
        s.cards[i] = section.frame; s.channelTitles[i] = section.title;
        auto *card = section.body;
        auto *v = new QVBoxLayout(card); v->setContentsMargins(12, 10, 12, 12); v->setSpacing(8);
        v->addWidget(label(card, "当前角度（设备读回）", "Current angle (device readback)"));
        s.positions[i] = new QLabel(QStringLiteral("—"), card);
        s.positions[i]->setObjectName(QStringLiteral("emFwc3Position") + suffix); v->addWidget(s.positions[i]);
        v->addWidget(label(card, "目标角度", "Target angle"));
        s.targets[i] = angle(card, QStringLiteral("emFwc3Target") + suffix, false); v->addWidget(s.targets[i]);
        auto *go = button(card, qPrintable(QStringLiteral("emFwc3Go") + suffix), "定位", "Move to target");
        v->addWidget(go); s.normalActions << go << s.targets[i];
        connect(go, &QPushButton::clicked, this, [this, i] { run(EmFwc3Session::Command::Absolute, i, state_->targets[i]->value()); });
        v->addWidget(label(card, "相对步长", "Relative step"));
        s.steps[i] = angle(card, QStringLiteral("emFwc3Step") + suffix, true); v->addWidget(s.steps[i]);
        auto *row = new QHBoxLayout;
        auto *minus = button(card, qPrintable(QStringLiteral("emFwc3Minus") + suffix), "反向步进", "Step −");
        auto *plus = button(card, qPrintable(QStringLiteral("emFwc3Plus") + suffix), "正向步进", "Step +");
        row->addWidget(minus); row->addWidget(plus); v->addLayout(row);
        s.normalActions << minus << plus << s.steps[i];
        connect(minus, &QPushButton::clicked, this, [this, i] { run(EmFwc3Session::Command::Relative, i, -state_->steps[i]->value()); });
        connect(plus, &QPushButton::clicked, this, [this, i] { run(EmFwc3Session::Command::Relative, i, state_->steps[i]->value()); });
        auto *actions = new QHBoxLayout;
        auto *home = button(card, qPrintable(QStringLiteral("emFwc3Home") + suffix), "回零", "Home");
        auto *stop = button(card, qPrintable(QStringLiteral("emFwc3Stop") + suffix), "停止", "Stop");
        actions->addWidget(home); actions->addWidget(stop); v->addLayout(actions);
        s.normalActions << home; s.stopActions << stop;
        connect(home, &QPushButton::clicked, this, [this, i] {
            if (QMessageBox::question(this, text("回零", "Home"),
                text("将驱动通道 %1 回零，确认周围运动空间已清空？", "Home channel %1? Check the mechanism's clearance first.").arg(i + 1)) == QMessageBox::Yes)
                run(EmFwc3Session::Command::Home, i);
        });
        connect(stop, &QPushButton::clicked, this, [this, i] { run(EmFwc3Session::Command::Continuous, i, 0); });
        auto *advanced = new QWidget(card);
        advanced->setObjectName(QStringLiteral("emFwc3ContinuousArea") + suffix);
        auto *a = new QVBoxLayout(advanced); a->setContentsMargins(0, 4, 0, 0); a->setSpacing(8);
        auto *toggle = new QCheckBox(advanced);
        toggle->setFocusPolicy(Qt::TabFocus);
        toggle->setObjectName(QStringLiteral("emFwc3ContinuousToggle") + suffix);
        wording(toggle, "连续运动", "Continuous motion"); a->addWidget(toggle);
        auto *continuousBody = new QWidget(advanced);
        continuousBody->setObjectName(QStringLiteral("emFwc3ContinuousBody") + suffix);
        auto *continuousLayout = new QVBoxLayout(continuousBody); continuousLayout->setContentsMargins(0, 0, 0, 0);
        auto *reverse = button(continuousBody, qPrintable(QStringLiteral("emFwc3Reverse") + suffix), "反向连续运行", "Run reverse");
        auto *forward = button(continuousBody, qPrintable(QStringLiteral("emFwc3Forward") + suffix), "正向连续运行", "Run forward");
        continuousLayout->addWidget(reverse); continuousLayout->addWidget(forward);
        a->addWidget(continuousBody); continuousBody->hide(); v->addWidget(advanced);
        advanced->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        connect(toggle, &QCheckBox::toggled, continuousBody, &QWidget::setVisible);
        s.normalActions << advanced;
        for (auto *b : {reverse, forward}) connect(b, &QPushButton::clicked, this, [this, i, b, forward] {
            if (QMessageBox::question(this, text("连续运动", "Continuous motion"),
                text("连续运动将持续到下发停止指令，确认开始？", "Continuous motion runs until a stop command. Start?")) == QMessageBox::Yes)
                run(EmFwc3Session::Command::Continuous, i, b == forward ? 1 : -1);
        });
        s.grid->addWidget(section.frame, 0, i);
    }
    layout->addLayout(s.grid);
    const auto parameterCard = createCard(content, QStringLiteral("emFwc3Parameters"));
    wording(parameterCard.title, "设备公共参数", "Device-wide parameters");
    auto *parameters = parameterCard.body;
    auto *p = new QVBoxLayout(parameters); p->setContentsMargins(12, 10, 12, 12);
    s.speedReadback = new QLabel(parameters); s.speedReadback->setObjectName(QStringLiteral("emFwc3SpeedReadback")); p->addWidget(s.speedReadback);
    s.speed = new QSpinBox(parameters); s.speed->setObjectName(QStringLiteral("emFwc3Speed"));
    s.speed->setRange(1, std::numeric_limits<int>::max()); s.speed->setValue(50); s.speed->setKeyboardTracking(false);
    p->addWidget(label(parameters, "速度参数（单位与有效范围按实际电机确认）", "Speed parameter (verify units and valid range for the fitted motor)"));
    p->addWidget(s.speed);
    auto *apply = button(parameters, "emFwc3ApplySpeed", "应用速度并读回", "Apply speed and read back"); p->addWidget(apply);
    auto *save = button(parameters, "emFwc3SavePosition", "保存位置到设备", "Save positions to device"); p->addWidget(save);
    s.normalActions << s.speed << apply << save;
    connect(apply, &QPushButton::clicked, this, [this] { run(EmFwc3Session::Command::Speed, 0, state_->speed->value()); });
    connect(save, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, text("保存位置", "Save positions"),
            text("将当前位置写入设备 EEPROM？此操作不应频繁执行。", "Write the current positions to device EEPROM? Avoid frequent writes.")) == QMessageBox::Yes)
            run(EmFwc3Session::Command::SavePosition);
    });
    layout->addWidget(parameterCard.frame);
    s.status = new QLabel(content); s.status->setObjectName(QStringLiteral("emFwc3Status")); s.status->setWordWrap(true); layout->addWidget(s.status);
    layout->addStretch(); s.scroll->setWidget(content); outer->addWidget(s.scroll);
    connect(s.session, &EmFwc3Session::stateChanged, this, [this] { refresh(); });
    connect(s.session, &EmFwc3Session::operationFinished, this, [this](const QString& operation, int code, const QString& detail) {
        const bool motion = operation == "relative" || operation == "absolute" || operation == "home" || operation == "continuous";
        const QString result = code == 0
            ? (motion ? text("指令已发送，请观察角度读回；不代表已到位。", "Command sent; observe the angle readback. Arrival is not confirmed.")
                      : text("操作成功。", "Operation succeeded."))
            : text("操作失败，错误码 %1。", "Operation failed, code %1.").arg(code);
        setStatus(operation + QStringLiteral(": ") + result + (detail.isEmpty() ? QString() : QStringLiteral("\n") + detail), code != 0);
    });
    setLanguage(false); setTheme(false); refresh();
}

MotionControlPage::~MotionControlPage() = default;
EmFwc3Session *MotionControlPage::session() const { return state_->session; }
void MotionControlPage::shutdown() { state_->session->shutdown(); }
QString MotionControlPage::text(const char *cn, const char *en) const
{
    return QString::fromUtf8(state_->english ? en : cn);
}
QWidget *MotionControlPage::createConnectionPanel(QWidget *parent)
{
    auto& s = *state_;
    if (s.config) return s.config;
    const auto section = createCard(parent, QStringLiteral("emFwc3ConnectionPanel"));
    s.config = section.frame;
    wording(section.title, "本机直连设备 · EM-FWC3", "Local direct devices · EM-FWC3");
    auto *panel = section.body;
    auto *v = new QVBoxLayout(panel); v->setContentsMargins(12, 10, 12, 12); v->setSpacing(8);
    v->addWidget(label(panel, "USB 虚拟串口直连当前电脑，保持独立连接。切换直连 / FPGA 或本地 / 远端采集不会断开本设备。",
        "USB virtual COM connected to this computer. Its connection is independent of Direct / FPGA and Local / Remote acquisition."));
    s.configConnection = new QLabel(panel); s.configConnection->setObjectName(QStringLiteral("emFwc3ConfigState")); v->addWidget(s.configConnection);
    s.configStatus = new QLabel(panel); s.configStatus->setObjectName(QStringLiteral("emFwc3ConfigStatus"));
    s.configStatus->setWordWrap(true); v->addWidget(s.configStatus);
    v->addWidget(label(panel, "串口", "COM port"));
    s.port = new QComboBox(panel); s.port->setEditable(true); s.port->setInsertPolicy(QComboBox::NoInsert);
    s.port->setObjectName(QStringLiteral("emFwc3Port")); v->addWidget(s.port);
    s.refreshPorts = button(panel, "emFwc3RefreshPorts", "刷新串口", "Refresh ports"); v->addWidget(s.refreshPorts);
    connect(s.refreshPorts, &QPushButton::clicked, this, &MotionControlPage::refreshPorts);
    QSettings settings("VaporView", "EmFwc3");
    for (int i = 0; i < 3; ++i) {
        auto *caption = new QLabel(panel); caption->setObjectName(QStringLiteral("emFwc3TypeLabel%1").arg(i + 1)); v->addWidget(caption);
        s.types[i] = new QComboBox(panel); s.types[i]->setObjectName(QStringLiteral("emFwc3MotorType%1").arg(i + 1));
        s.types[i]->addItems({QStringLiteral("EM-FPM"), QStringLiteral("CEM-FPM")});
        s.types[i]->setCurrentText(settings.value(QStringLiteral("motor_type_%1").arg(i), QStringLiteral("CEM-FPM")).toString());
        v->addWidget(s.types[i]);
        connect(s.types[i], &QComboBox::currentTextChanged, this, [this] { state_->confirmTypes->setChecked(false); saveSettings(); });
    }
    s.confirmTypes = new QCheckBox(panel); s.confirmTypes->setObjectName(QStringLiteral("emFwc3ConfirmTypes"));
    wording(s.confirmTypes, "已核对三路电机型号（影响角度换算）", "I have checked all three motor types (used for angle conversion)");
    v->addWidget(s.confirmTypes);
    connect(s.confirmTypes, &QCheckBox::toggled, this, [this] { refresh(); });
    s.connect = button(panel, "emFwc3Connect", "连接", "Connect");
    s.disconnect = button(panel, "emFwc3Disconnect", "断开", "Disconnect");
    v->addWidget(s.connect); v->addWidget(s.disconnect);
    auto *open = button(panel, "emFwc3OpenControls", "打开运动控制", "Open motion control"); v->addWidget(open);
    connect(open, &QPushButton::clicked, this, &MotionControlPage::showControlsRequested);
    connect(s.connect, &QPushButton::clicked, this, [this] {
        if (VaporView::settingsWritesSuspended()) return;
        QStringList types;
        for (auto *combo : state_->types) types << combo->currentText();
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
    setLanguage(s.english); setTheme(s.dark, s.scale); refresh(); return section.frame;
}
void MotionControlPage::refreshPorts()
{
    auto *port = state_->port;
    if (!port) return;
    QString previous = port->currentData().isValid() && port->currentText() == port->itemText(port->currentIndex())
        ? port->currentData().toString() : port->currentText();
    port->blockSignals(true); port->clear();
    for (const auto& info : QSerialPortInfo::availablePorts())
        port->addItem(info.portName() + QStringLiteral(" — ") + info.description(), info.portName());
    int index = port->findData(previous);
    if (index >= 0) port->setCurrentIndex(index);
    else port->setEditText(previous);
    port->blockSignals(false); refresh();
}
void MotionControlPage::saveSettings()
{
    const auto& s = *state_;
    if (!s.config) return;
    QSettings settings("VaporView", "EmFwc3");
    const QString port = s.port->currentIndex() >= 0 && s.port->currentText() == s.port->itemText(s.port->currentIndex())
        ? s.port->currentData().toString() : s.port->currentText().trimmed();
    VaporView::setPersistentSetting(settings, QStringLiteral("port"), port);
    for (int i = 0; i < 3; ++i)
        VaporView::setPersistentSetting(settings, QStringLiteral("motor_type_%1").arg(i), s.types[i]->currentText());
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
    if (s.configConnection) s.configConnection->setText(connection);
    for (int i = 0; i < 3; ++i) s.positions[i]->setText(snapshot.connected && snapshot.valid[i]
        ? QString::number(snapshot.positions[i], 'f', 3) + QStringLiteral("°") : QStringLiteral("—"));
    s.speedReadback->setText(text("速度读回：%1", "Speed readback: %1").arg(snapshot.connected && snapshot.speedValid ? QString::number(snapshot.speed) : QStringLiteral("—")));
    for (auto *w : s.normalActions) w->setEnabled(snapshot.connected && !snapshot.busy);
    for (auto *w : s.stopActions) w->setEnabled(snapshot.connected);
    if (s.config) {
        const bool editable = !snapshot.connected && !snapshot.busy;
        s.port->setEnabled(editable); s.refreshPorts->setEnabled(editable); s.confirmTypes->setEnabled(editable);
        for (auto *combo : s.types) combo->setEnabled(editable);
        s.connect->setEnabled(editable && s.confirmTypes->isChecked() && !s.port->currentText().trimmed().isEmpty());
        s.disconnect->setEnabled(snapshot.connected && !snapshot.busy);
    }
}
void MotionControlPage::setStatus(const QString& message, bool error)
{
    state_->status->setText(message); state_->status->setProperty("emFwc3Error", error);
    state_->status->style()->unpolish(state_->status); state_->status->style()->polish(state_->status);
    if (state_->config) {
        state_->configStatus->setText(message); state_->configStatus->setProperty("emFwc3Error", error);
        state_->configStatus->style()->unpolish(state_->configStatus); state_->configStatus->style()->polish(state_->configStatus);
    }
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
    translate(this); translate(s.config);
    for (int i = 0; i < 3; ++i) {
        s.channelTitles[i]->setText(text("通道 %1", "Channel %1").arg(i + 1));
        if (s.config) s.config->findChild<QLabel*>(QStringLiteral("emFwc3TypeLabel%1").arg(i + 1))->setText(text("通道 %1 电机型号", "Channel %1 motor type").arg(i + 1));
    }
    refresh(); reflow();
}
void MotionControlPage::setTheme(bool dark, int fontScalePercent)
{
    auto& s = *state_; s.dark = dark; s.scale = fontScalePercent;
    // Leave ample room for wrapped titles without scaling Qt's maximum-size sentinel.
    const QString style = applyAppThemeTokens(QStringLiteral(
        "QWidget#motionControlPage, QWidget#emFwc3MotionContent { background-color: @vv-window; color: @vv-text; }"
        "QWidget#motionControlPage QScrollArea { border: none; background: @vv-window; }"
        "QFrame[emFwc3Card=true] { background-color: @vv-surface-raised; color: @vv-text; border: 1px solid @vv-border; border-radius: 12px; }"
        "QFrame[emFwc3Card=true] > QWidget#sectionTitleBar { background-color: @vv-surface-raised; border: none; border-bottom: 1px solid @vv-border; border-top-left-radius: 11px; border-top-right-radius: 11px; min-height: %2px; max-height: 4096px; }"
        "QWidget#emFwc3CardBody { background-color: @vv-surface-raised; border: none; border-bottom-left-radius: 11px; border-bottom-right-radius: 11px; }"
        "QLabel, QLabel#sectionTitleLabel { background-color: transparent; color: @vv-text; border: none; }"
        "QCheckBox { color: @vv-text; background-color: transparent; }"
        "QCheckBox:disabled { color: @vv-text-disabled; }"
        "QWidget#sectionTitleBar QLabel#sectionTitleLabel { font-size: %3px; font-weight: 700; padding: 0px; margin: 0px; }"
        "QLabel[emFwc3Error=true] { color: @vv-danger; }"
        "QLabel#emFwc3Position1, QLabel#emFwc3Position2, QLabel#emFwc3Position3 { font-size: %1px; font-weight: 600; }"
        "QPushButton#emFwc3StopAll:enabled, QPushButton#emFwc3Stop1:enabled, QPushButton#emFwc3Stop2:enabled, QPushButton#emFwc3Stop3:enabled { background-color: @vv-danger; color: @vv-text-inverse; }"
    ).arg(qRound(24 * fontScalePercent / 100.0)).arg(qRound(40 * fontScalePercent / 100.0)).arg(qRound(16 * fontScalePercent / 100.0)), dark);
    setStyleSheet(style); if (s.config) s.config->setStyleSheet(style);
    setPalette(appThemePalette(dark));
    if (s.config) s.config->setPalette(appThemePalette(dark));
    reflow();
}
void MotionControlPage::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); reflow(); }
bool MotionControlPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == state_->scroll->viewport() && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &MotionControlPage::reflow);
    return QWidget::eventFilter(watched, event);
}
void MotionControlPage::reflow()
{
    auto& s = *state_;
    if (!s.grid) return;
    int minimum = 240;
    for (auto *card : s.cards) minimum = std::max(minimum, card->minimumSizeHint().width());
    const int columns = std::clamp((s.scroll->viewport()->width() - 24 + 12) / (minimum + 12), 1, 3);
    if (columns == s.columns) return;
    s.columns = columns;
    for (int i = 0; i < 3; ++i) {
        s.grid->removeWidget(s.cards[i]); s.grid->addWidget(s.cards[i], i / columns, i % columns);
        s.grid->setColumnStretch(i, i < columns ? 1 : 0);
    }
}
}
