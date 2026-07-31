#include "ground/widgets/Ai8TemperatureControllerPanel.h"

#include "ground/widgets/TemperatureControllerWidgets.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace VaporView::Ground::Widgets
{
namespace
{
constexpr int kPageColumnCount = 4;
constexpr int kEditorMinimumWidth = 118;
constexpr int kParameterFieldMinimumHeight = 66;

QSpinBox *createSpinBox(QWidget *parent,
                        const QString& objectName,
                        int minimum,
                        int maximum,
                        int value)
{
    auto *spin = new QSpinBox(parent);
    spin->setObjectName(objectName);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    spin->setMinimumWidth(kEditorMinimumWidth);
    return spin;
}

QDoubleSpinBox *createDoubleSpinBox(QWidget *parent,
                                    const QString& objectName,
                                    double minimum,
                                    double maximum,
                                    double value,
                                    int decimals,
                                    const QString& suffix = QString())
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setObjectName(objectName);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    spin->setDecimals(decimals);
    spin->setMinimumWidth(kEditorMinimumWidth);
    spin->setSuffix(suffix);
    return spin;
}

void addFieldsToPage(QGridLayout *layout, const QList<QWidget *>& fields)
{
    for (qsizetype index = 0; index < fields.size(); ++index)
    {
        const int row = static_cast<int>(index) / kPageColumnCount;
        const int column = static_cast<int>(index) % kPageColumnCount;
        layout->addWidget(fields.at(index), row, column);
    }
    for (int column = 0; column < kPageColumnCount; ++column)
    {
        layout->setColumnStretch(column, 1);
    }
}

} // namespace

Ai8TemperatureControllerPanel::Ai8TemperatureControllerPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void Ai8TemperatureControllerPanel::setupUi()
{
    setObjectName(QStringLiteral("ai8TemperatureControllerPanel"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    auto *navigationBar = new QFrame(this);
    navigationBar->setObjectName(QStringLiteral("ai8NavigationBar"));
    navigationBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *navigationLayout = new QHBoxLayout(navigationBar);
    navigationLayout->setContentsMargins(4, 4, 4, 4);
    navigationLayout->setSpacing(4);

    page_button_group_ = new QButtonGroup(this);
    page_button_group_->setExclusive(true);
    const QList<QPair<QString, QString>> pageTexts = {
        {QStringLiteral("通道参数"), QStringLiteral("Channel")},
        {QStringLiteral("输入参数组"), QStringLiteral("Input Group")},
        {QStringLiteral("输出参数组"), QStringLiteral("Output Group")},
        {QStringLiteral("全局参数"), QStringLiteral("Global")},
    };
    for (int index = 0; index < pageTexts.size(); ++index)
    {
        auto *button = new QPushButton(navigationBar);
        button->setObjectName(QStringLiteral("ai8PageSelectorButton%1").arg(index + 1));
        button->setProperty("ai8PageSelector", true);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::TabFocus);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setMinimumWidth(104);
        page_button_group_->addButton(button, index);
        navigationLayout->addWidget(button);
        button_bindings_.append({button, pageTexts.at(index).first, pageTexts.at(index).second});
    }
    navigationLayout->addStretch(1);
    connect(page_button_group_, &QButtonGroup::idClicked, this, [this](int index) {
        selectPage(index);
    });
    rootLayout->addWidget(navigationBar);

    page_stack_ = new QStackedWidget(this);
    page_stack_->setObjectName(QStringLiteral("ai8ParameterStack"));
    page_stack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    page_stack_->addWidget(createChannelPage());
    page_stack_->addWidget(createInputPage());
    page_stack_->addWidget(createOutputPage());
    page_stack_->addWidget(createGlobalPage());
    rootLayout->addWidget(page_stack_);

    auto *statusRow = new QWidget(this);
    statusRow->setObjectName(QStringLiteral("ai8ProtocolStatusRow"));
    auto *statusLayout = new QHBoxLayout(statusRow);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(8);

    protocol_status_label_ = new QLabel(statusRow);
    protocol_status_label_->setObjectName(QStringLiteral("ai8ProtocolStatus"));
    protocol_status_label_->setProperty("protocolReady", false);
    protocol_status_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label_bindings_.append({protocol_status_label_,
                            QStringLiteral("通信后端未接入"),
                            QStringLiteral("Communication backend not connected")});
    statusLayout->addWidget(protocol_status_label_);
    statusLayout->addStretch(1);

    auto *readButton = new QPushButton(statusRow);
    read_button_ = readButton;
    readButton->setObjectName(QStringLiteral("ai8ReadParametersButton"));
    readButton->setEnabled(false);
    readButton->setFocusPolicy(Qt::TabFocus);
    readButton->setMinimumWidth(112);
    button_bindings_.append({readButton,
                             QStringLiteral("读取当前页"),
                             QStringLiteral("Read Page")});
    statusLayout->addWidget(readButton);

    auto *writeButton = new QPushButton(statusRow);
    write_button_ = writeButton;
    writeButton->setObjectName(QStringLiteral("ai8WriteParametersButton"));
    writeButton->setProperty("primaryAction", true);
    writeButton->setEnabled(false);
    writeButton->setFocusPolicy(Qt::TabFocus);
    writeButton->setMinimumWidth(112);
    button_bindings_.append({writeButton,
                             QStringLiteral("写入当前页"),
                             QStringLiteral("Write Page")});
    statusLayout->addWidget(writeButton);
    rootLayout->addWidget(statusRow);

    setEnglish(false);
    selectPage(0);
}

QWidget *Ai8TemperatureControllerPanel::createChannelPage()
{
    auto *page = new QWidget(page_stack_);
    page->setObjectName(QStringLiteral("ai8ChannelParametersPage"));
    auto *layout = new QGridLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(8);

    auto *channelSpin = createSpinBox(page, QStringLiteral("ai8ChannelSpin"), 1, 96, 1);
    auto *setpointSpin = createDoubleSpinBox(page,
                                              QStringLiteral("ai8SetpointSpin"),
                                              -999.0,
                                              3200.0,
                                              0.0,
                                              1,
                                              QStringLiteral(" °C"));
    auto *pvEdit = new QLineEdit(QStringLiteral("---"), page);
    pvEdit->setObjectName(QStringLiteral("ai8MeasuredTemperatureEdit"));
    pvEdit->setReadOnly(true);
    pvEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pvEdit->setMinimumWidth(kEditorMinimumWidth);
    auto *pSpin = createSpinBox(page, QStringLiteral("ai8ProportionalBandSpin"), 0, 32000, 0);
    auto *iSpin = createDoubleSpinBox(page,
                                     QStringLiteral("ai8IntegralTimeSpin"),
                                     0.0,
                                     3200.0,
                                     0.0,
                                     1,
                                     QStringLiteral(" s"));
    auto *dSpin = createDoubleSpinBox(page,
                                     QStringLiteral("ai8DerivativeTimeSpin"),
                                     -327.6,
                                     327.6,
                                     0.0,
                                     2,
                                     QStringLiteral(" s"));
    QComboBox *modeCombo = createFixedChoiceCombo(page);
    modeCombo->setObjectName(QStringLiteral("ai8WorkModeCombo"));
    addComboItem(modeCombo, QStringLiteral("APID 调节"), QStringLiteral("APID Control"), 0);
    addComboItem(modeCombo, QStringLiteral("自整定"), QStringLiteral("Auto Tune"), 1);
    addComboItem(modeCombo, QStringLiteral("位式控制"), QStringLiteral("ON/OFF Control"), 2);
    addComboItem(modeCombo, QStringLiteral("手动输出"), QStringLiteral("Manual Output"), 3);
    addComboItem(modeCombo, QStringLiteral("停止控制"), QStringLiteral("Stop"), 4);
    addComboItem(modeCombo, QStringLiteral("PV 变送"), QStringLiteral("PV Transmit"), 5);
    auto *outputSpin = createDoubleSpinBox(page,
                                           QStringLiteral("ai8ManualOutputSpin"),
                                           0.0,
                                           100.0,
                                           0.0,
                                           1,
                                           QStringLiteral(" %"));

    addFieldsToPage(layout,
                    {createParameterField(QStringLiteral("通道"), QStringLiteral("Channel"), channelSpin, page),
                     createParameterField(QStringLiteral("给定值 SP"), QStringLiteral("Setpoint SP"), setpointSpin, page),
                     createParameterField(QStringLiteral("测量值 PV"), QStringLiteral("Measured PV"), pvEdit, page),
                     createParameterField(QStringLiteral("比例带 P"), QStringLiteral("Proportional Band P"), pSpin, page),
                     createParameterField(QStringLiteral("积分时间 I"), QStringLiteral("Integral Time I"), iSpin, page),
                     createParameterField(QStringLiteral("微分时间 d"), QStringLiteral("Derivative Time d"), dSpin, page),
                     createParameterField(QStringLiteral("工作模式 At"), QStringLiteral("Work Mode At"), modeCombo, page),
                     createParameterField(QStringLiteral("手动输出 OP"), QStringLiteral("Manual Output OP"), outputSpin, page)});
    return page;
}

QWidget *Ai8TemperatureControllerPanel::createInputPage()
{
    auto *page = new QWidget(page_stack_);
    page->setObjectName(QStringLiteral("ai8InputParametersPage"));
    auto *layout = new QGridLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(8);

    auto *groupSpin = createSpinBox(page, QStringLiteral("ai8InputGroupSpin"), 1, 4, 1);
    QComboBox *inputSpecCombo = createFixedChoiceCombo(page);
    inputSpecCombo->setObjectName(QStringLiteral("ai8InputSpecCombo"));
    addComboItem(inputSpecCombo, QStringLiteral("K 热电偶"), QStringLiteral("K Thermocouple"), 0);
    addComboItem(inputSpecCombo, QStringLiteral("S 热电偶"), QStringLiteral("S Thermocouple"), 1);
    addComboItem(inputSpecCombo, QStringLiteral("T 热电偶"), QStringLiteral("T Thermocouple"), 3);
    addComboItem(inputSpecCombo, QStringLiteral("J 热电偶"), QStringLiteral("J Thermocouple"), 5);
    addComboItem(inputSpecCombo, QStringLiteral("Pt100"), QStringLiteral("Pt100"), 21);
    addComboItem(inputSpecCombo, QStringLiteral("Pt1000"), QStringLiteral("Pt1000"), 23);
    addComboItem(inputSpecCombo, QStringLiteral("4–20 mA"), QStringLiteral("4–20 mA"), 51);
    auto *scaleLowSpin = createDoubleSpinBox(page, QStringLiteral("ai8ScaleLowSpin"), -999.0, 3200.0, 0.0, 1);
    auto *scaleHighSpin = createDoubleSpinBox(page, QStringLiteral("ai8ScaleHighSpin"), -999.0, 3200.0, 100.0, 1);
    auto *filterSpin = createSpinBox(page, QStringLiteral("ai8FilterSpin"), 0, 999, 0);
    auto *channelInputSpin = createSpinBox(page, QStringLiteral("ai8ChannelInputConfigSpin"), 0, 9999, 1);
    auto *offsetSpin = createDoubleSpinBox(page, QStringLiteral("ai8MeasurementOffsetSpin"), -999.0, 3200.0, 0.0, 1);
    auto *correctionEntrySpin = createSpinBox(page, QStringLiteral("ai8CorrectionEntrySpin"), 0, 99, 0);

    addFieldsToPage(layout,
                    {createParameterField(QStringLiteral("输入参数组"), QStringLiteral("Input Group"), groupSpin, page),
                     createParameterField(QStringLiteral("输入规格 InP"), QStringLiteral("Input Type InP"), inputSpecCombo, page),
                     createParameterField(QStringLiteral("定标下限 ScL"), QStringLiteral("Scale Low ScL"), scaleLowSpin, page),
                     createParameterField(QStringLiteral("定标上限 ScH"), QStringLiteral("Scale High ScH"), scaleHighSpin, page),
                     createParameterField(QStringLiteral("数字滤波 FIL"), QStringLiteral("Digital Filter FIL"), filterSpin, page),
                     createParameterField(QStringLiteral("通道配置 In"), QStringLiteral("Channel Config In"), channelInputSpin, page),
                     createParameterField(QStringLiteral("测量平移 Sc"), QStringLiteral("Measurement Offset Sc"), offsetSpin, page),
                     createParameterField(QStringLiteral("校正表入口"), QStringLiteral("Correction Entry"), correctionEntrySpin, page)});
    return page;
}

QWidget *Ai8TemperatureControllerPanel::createOutputPage()
{
    auto *page = new QWidget(page_stack_);
    page->setObjectName(QStringLiteral("ai8OutputParametersPage"));
    auto *layout = new QGridLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(8);

    auto *groupSpin = createSpinBox(page, QStringLiteral("ai8OutputGroupSpin"), 1, 4, 1);
    QComboBox *actionCombo = createFixedChoiceCombo(page);
    actionCombo->setObjectName(QStringLiteral("ai8ControlActionCombo"));
    addComboItem(actionCombo, QStringLiteral("加热（反作用）"), QStringLiteral("Heating (Reverse)"), 0);
    addComboItem(actionCombo, QStringLiteral("制冷（正作用）"), QStringLiteral("Cooling (Direct)"), 1);
    auto *hysteresisSpin = createDoubleSpinBox(page, QStringLiteral("ai8HysteresisSpin"), -999.0, 3200.0, 0.0, 1);
    auto *outputLowSpin = createSpinBox(page, QStringLiteral("ai8OutputLowSpin"), 0, 100, 0);
    outputLowSpin->setSuffix(QStringLiteral(" %"));
    auto *outputHighSpin = createSpinBox(page, QStringLiteral("ai8OutputHighSpin"), 0, 105, 100);
    outputHighSpin->setSuffix(QStringLiteral(" %"));
    auto *riseSlopeSpin = createDoubleSpinBox(page, QStringLiteral("ai8RiseSlopeSpin"), 0.0, 3200.0, 0.0, 1);
    auto *fallSlopeSpin = createDoubleSpinBox(page, QStringLiteral("ai8FallSlopeSpin"), 0.0, 3200.0, 0.0, 1);
    QComboBox *alarmResetCombo = createFixedChoiceCombo(page);
    alarmResetCombo->setObjectName(QStringLiteral("ai8AlarmResetCombo"));
    addComboItem(alarmResetCombo, QStringLiteral("自动复位"), QStringLiteral("Auto Reset"), 0);
    addComboItem(alarmResetCombo, QStringLiteral("锁定，手动复位"), QStringLiteral("Latched / Manual Reset"), 1);

    addFieldsToPage(layout,
                    {createParameterField(QStringLiteral("输出参数组"), QStringLiteral("Output Group"), groupSpin, page),
                     createParameterField(QStringLiteral("控制方向 Act"), QStringLiteral("Control Action Act"), actionCombo, page),
                     createParameterField(QStringLiteral("控制回差 HYS"), QStringLiteral("Hysteresis HYS"), hysteresisSpin, page),
                     createParameterField(QStringLiteral("输出下限 OPL"), QStringLiteral("Output Low OPL"), outputLowSpin, page),
                     createParameterField(QStringLiteral("输出上限 OPH"), QStringLiteral("Output High OPH"), outputHighSpin, page),
                     createParameterField(QStringLiteral("升温斜率 Srh"), QStringLiteral("Rise Slope Srh"), riseSlopeSpin, page),
                     createParameterField(QStringLiteral("降温斜率 SrL"), QStringLiteral("Fall Slope SrL"), fallSlopeSpin, page),
                     createParameterField(QStringLiteral("报警复位 AAF"), QStringLiteral("Alarm Reset AAF"), alarmResetCombo, page)});
    return page;
}

QWidget *Ai8TemperatureControllerPanel::createGlobalPage()
{
    auto *page = new QWidget(page_stack_);
    page->setObjectName(QStringLiteral("ai8GlobalParametersPage"));
    auto *layout = new QGridLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(8);

    auto *addressSpin = createSpinBox(page, QStringLiteral("ai8DeviceAddressSpin"), 0, 88, 1);
    QComboBox *baudCombo = createFixedChoiceCombo(page);
    baudCombo->setObjectName(QStringLiteral("ai8BaudCombo"));
    for (int baudRate : {4800, 9600, 19200, 38400, 57600, 115200})
    {
        baudCombo->addItem(QString::number(baudRate), baudRate);
    }
    baudCombo->setCurrentIndex(2);
    auto *controlChannelsSpin = createSpinBox(page, QStringLiteral("ai8ControlChannelCountSpin"), 1, 96, 8);
    auto *controlCycleSpin = createDoubleSpinBox(page,
                                                 QStringLiteral("ai8ControlCycleSpin"),
                                                 0.0,
                                                 50.0,
                                                 0.0,
                                                 1,
                                                 QStringLiteral(" s"));
    QComboBox *runCombo = createFixedChoiceCombo(page);
    runCombo->setObjectName(QStringLiteral("ai8RunModeCombo"));
    addComboItem(runCombo, QStringLiteral("自动运行"), QStringLiteral("Auto Run"), 0);
    addComboItem(runCombo, QStringLiteral("上电后停止"), QStringLiteral("Stop After Power Cycle"), 15);
    addComboItem(runCombo, QStringLiteral("全部停止"), QStringLiteral("Stop All"), 9655);
    QComboBox *lockCombo = createFixedChoiceCombo(page);
    lockCombo->setObjectName(QStringLiteral("ai8ParameterLockCombo"));
    addComboItem(lockCombo, QStringLiteral("允许写入"), QStringLiteral("Writable"), 0);
    addComboItem(lockCombo, QStringLiteral("锁定组参数"), QStringLiteral("Lock Group Parameters"), 32);
    auto *sampleModeSpin = createSpinBox(page, QStringLiteral("ai8SampleModeSpin"), 0, 3, 0);
    auto *decimalPointSpin = createSpinBox(page, QStringLiteral("ai8DecimalPointSpin"), 0, 3, 1);

    addFieldsToPage(layout,
                    {createParameterField(QStringLiteral("通讯地址 Addr"), QStringLiteral("Address Addr"), addressSpin, page),
                     createParameterField(QStringLiteral("波特率 bAud"), QStringLiteral("Baud Rate bAud"), baudCombo, page),
                     createParameterField(QStringLiteral("控制回路数 Ctn"), QStringLiteral("Control Channels Ctn"), controlChannelsSpin, page),
                     createParameterField(QStringLiteral("控制周期 CtI"), QStringLiteral("Control Cycle CtI"), controlCycleSpin, page),
                     createParameterField(QStringLiteral("运行状态 Srun"), QStringLiteral("Run State Srun"), runCombo, page),
                     createParameterField(QStringLiteral("参数锁 Loc"), QStringLiteral("Parameter Lock Loc"), lockCombo, page),
                     createParameterField(QStringLiteral("采样模式 EAF"), QStringLiteral("Sample Mode EAF"), sampleModeSpin, page),
                     createParameterField(QStringLiteral("显示小数点 dPt"), QStringLiteral("Display Decimal dPt"), decimalPointSpin, page)});
    return page;
}

QWidget *Ai8TemperatureControllerPanel::createParameterField(const QString& chinese,
                                                              const QString& english,
                                                              QWidget *editor,
                                                              QWidget *parent)
{
    auto *field = new QFrame(parent);
    field->setObjectName(QStringLiteral("ai8ParameterField"));
    field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    field->setMinimumHeight(kParameterFieldMinimumHeight);
    auto *layout = new QVBoxLayout(field);
    layout->setContentsMargins(10, 7, 10, 7);
    layout->setSpacing(4);

    auto *label = new QLabel(field);
    label->setObjectName(QStringLiteral("fieldLabel"));
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label_bindings_.append({label, chinese, english});
    layout->addWidget(label);
    editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(editor);
    return field;
}

QComboBox *Ai8TemperatureControllerPanel::createFixedChoiceCombo(QWidget *parent)
{
    QComboBox *combo = createSingleLevelPopupComboBox(parent, true, false);
    combo->setMinimumWidth(kEditorMinimumWidth);
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return combo;
}

void Ai8TemperatureControllerPanel::addComboItem(QComboBox *combo,
                                                  const QString& chinese,
                                                  const QString& english,
                                                  const QVariant& userData)
{
    if (!combo)
    {
        return;
    }
    const int index = combo->count();
    combo->addItem(chinese, userData);
    combo_item_bindings_.append({combo, index, chinese, english});
}

void Ai8TemperatureControllerPanel::selectPage(int index)
{
    if (!page_stack_ || !page_button_group_ || page_stack_->count() == 0)
    {
        return;
    }
    const int pageIndex = std::clamp(index, 0, page_stack_->count() - 1);
    page_stack_->setCurrentIndex(pageIndex);
    if (QAbstractButton *button = page_button_group_->button(pageIndex))
    {
        button->setChecked(true);
    }
}

void Ai8TemperatureControllerPanel::setEnglish(bool english)
{
    for (const LabelBinding& binding : label_bindings_)
    {
        if (binding.label)
        {
            binding.label->setText(english ? binding.english : binding.chinese);
        }
    }
    for (const ButtonBinding& binding : button_bindings_)
    {
        if (binding.button)
        {
            binding.button->setText(english ? binding.english : binding.chinese);
        }
    }
    for (const ComboItemBinding& binding : combo_item_bindings_)
    {
        if (binding.combo && binding.index >= 0 && binding.index < binding.combo->count())
        {
            binding.combo->setItemText(binding.index, english ? binding.english : binding.chinese);
        }
    }

    const QString backendToolTip = english
        ? QStringLiteral("The AI-8 Modbus backend is not connected yet.")
        : QStringLiteral("AI-8 Modbus 通信后端尚未接入。");
    if (protocol_status_label_)
    {
        protocol_status_label_->setToolTip(backendToolTip);
    }
    if (read_button_)
    {
        read_button_->setToolTip(backendToolTip);
    }
    if (write_button_)
    {
        write_button_->setToolTip(backendToolTip);
    }
    updateGeometry();
}

} // namespace VaporView::Ground::Widgets
