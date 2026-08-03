#include "ground/widgets/Ai8TemperatureControllerPanel.h"
#include "ground/widgets/TemperatureTrendPlotWidget.h"
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
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace VaporView::Ground::Widgets
{
namespace
{
constexpr int kPageColumnCount = 4;
constexpr int kEditorMinimumWidth = 118;
constexpr int kParameterFieldMinimumHeight = 66;
constexpr int kAi8TemperatureHistoryLimit = 240;
constexpr int kAi8NavigationButtonHeight = 30;
constexpr int kAi8NavigationHorizontalMargin = 4;
constexpr int kAi8NavigationVerticalMargin = 3;
constexpr int kAi8NavigationSpacing = 4;

QString runStateHexDigits(quint16 rawValue)
{
    return QString::number(static_cast<uint>(rawValue), 16)
        .rightJustified(4, QLatin1Char('0'))
        .toUpper();
}

QString unknownRunStateText(quint16 rawValue, bool english)
{
    const QString decimalText = QString::number(static_cast<uint>(rawValue));
    const QString hexText = runStateHexDigits(rawValue);
    return english
        ? QStringLiteral("Keep Device Value (Unknown: %1 / 0x%2)").arg(decimalText, hexText)
        : QStringLiteral("保持设备原值（未识别：%1 / 0x%2）").arg(decimalText, hexText);
}

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
    navigationBar->setFrameShape(QFrame::NoFrame);
    navigationBar->setAttribute(Qt::WA_StyledBackground, true);
    navigationBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *navigationLayout = new QHBoxLayout(navigationBar);
    navigationLayout->setContentsMargins(kAi8NavigationHorizontalMargin,
                                         kAi8NavigationVerticalMargin,
                                         kAi8NavigationHorizontalMargin,
                                         kAi8NavigationVerticalMargin);
    navigationLayout->setSpacing(kAi8NavigationSpacing);

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
        button->setText(pageTexts.at(index).first);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->ensurePolished();
        const int buttonWidth = std::max({
            88,
            button->fontMetrics().horizontalAdvance(pageTexts.at(index).first) + 40,
            button->fontMetrics().horizontalAdvance(pageTexts.at(index).second) + 40});
        button->setFixedSize(buttonWidth, kAi8NavigationButtonHeight);
        page_button_group_->addButton(button, index);
        navigationLayout->addWidget(button);
        button_bindings_.append({button, pageTexts.at(index).first, pageTexts.at(index).second});
    }
    navigationBar->setMinimumWidth(navigationBar->sizeHint().width());
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

    temperature_plot_ = new ::TemperatureTrendPlotWidget(this);
    temperature_plot_->setObjectName(QStringLiteral("ai8TemperatureTrendPlot"));
    temperature_plot_->setProperty("ai8TemperaturePlot", true);
    temperature_plot_->setCompactMode(true);
    temperature_plot_->setMinimumHeight(180);
    temperature_plot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rootLayout->addWidget(temperature_plot_, 1);

    auto *statusRow = new QWidget(this);
    statusRow->setObjectName(QStringLiteral("ai8ProtocolStatusRow"));
    auto *statusLayout = new QHBoxLayout(statusRow);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(8);

    protocol_status_label_ = new QLabel(statusRow);
    protocol_status_label_->setObjectName(QStringLiteral("ai8ProtocolStatus"));
    protocol_status_label_->setProperty("protocolReady", false);
    protocol_status_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
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
    connect(readButton, &QPushButton::clicked, this,
            &Ai8TemperatureControllerPanel::readPageRequested);

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
    connect(writeButton, &QPushButton::clicked, this,
            &Ai8TemperatureControllerPanel::writePageRequested);
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

    auto *channelSpin = createSpinBox(page, QStringLiteral("ai8ChannelSpin"), 1, 8, 1);
    connect(channelSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this]() {
                updateMeasuredValue();
                updateTemperaturePlot();
                emit outputStatusChanged();
            });
    auto *setpointSpin = createDoubleSpinBox(page,
                                              QStringLiteral("ai8SetpointSpin"),
                                              -999.0,
                                              3200.0,
                                              0.0,
                                              1,
                                              QStringLiteral(" °C"));
    connect(setpointSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this]() { updateTemperaturePlot(); });
    auto *pvEdit = new QLineEdit(QStringLiteral("---"), page);
    pvEdit->setObjectName(QStringLiteral("ai8MeasuredTemperatureEdit"));
    pvEdit->setReadOnly(true);
    pvEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pvEdit->setMinimumWidth(kEditorMinimumWidth);
    auto *pSpin = createDoubleSpinBox(page,
                                      QStringLiteral("ai8ProportionalBandSpin"),
                                      0.0,
                                      3200.0,
                                      0.0,
                                      1);
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
    auto *channelInputSpin = createSpinBox(page, QStringLiteral("ai8ChannelInputConfigSpin"), 0, 4, 1);
    auto *offsetSpin = createDoubleSpinBox(page, QStringLiteral("ai8MeasurementOffsetSpin"), -999.0, 3200.0, 0.0, 1);
    auto *correctionEntrySpin = createSpinBox(page, QStringLiteral("ai8CorrectionEntrySpin"), 0, 999, 0);

    addFieldsToPage(layout,
                    {createParameterField(QStringLiteral("输入参数组"), QStringLiteral("Input Group"), groupSpin, page),
                     createParameterField(QStringLiteral("输入规格 InP"), QStringLiteral("Input Type InP"), inputSpecCombo, page),
                     createParameterField(QStringLiteral("定标下限 ScL"), QStringLiteral("Scale Low ScL"), scaleLowSpin, page),
                     createParameterField(QStringLiteral("定标上限 ScH"), QStringLiteral("Scale High ScH"), scaleHighSpin, page),
                     createParameterField(QStringLiteral("数字滤波 FIL"), QStringLiteral("Digital Filter FIL"), filterSpin, page),
                     createParameterField(QStringLiteral("通道输入组 In"), QStringLiteral("Channel Input Group In"), channelInputSpin, page),
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
    auto *alarmResetSpin = createSpinBox(page, QStringLiteral("ai8AlarmResetSpin"), 0, 31, 0);

    addFieldsToPage(layout,
                    {createParameterField(QStringLiteral("输出参数组"), QStringLiteral("Output Group"), groupSpin, page),
                     createParameterField(QStringLiteral("控制方向 Act"), QStringLiteral("Control Action Act"), actionCombo, page),
                     createParameterField(QStringLiteral("控制回差 HYS"), QStringLiteral("Hysteresis HYS"), hysteresisSpin, page),
                     createParameterField(QStringLiteral("输出下限 OPL"), QStringLiteral("Output Low OPL"), outputLowSpin, page),
                     createParameterField(QStringLiteral("输出上限 OPH"), QStringLiteral("Output High OPH"), outputHighSpin, page),
                     createParameterField(QStringLiteral("升温斜率 Srh"), QStringLiteral("Rise Slope Srh"), riseSlopeSpin, page),
                     createParameterField(QStringLiteral("降温斜率 SrL"), QStringLiteral("Fall Slope SrL"), fallSlopeSpin, page),
                     createParameterField(QStringLiteral("报警锁定掩码 AAF"), QStringLiteral("Alarm Latch Mask AAF"), alarmResetSpin, page)});
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

    auto *addressSpin = createSpinBox(page, QStringLiteral("ai8DeviceAddressSpin"), 1, 88, 1);
    QComboBox *baudCombo = createFixedChoiceCombo(page);
    baudCombo->setObjectName(QStringLiteral("ai8BaudCombo"));
    for (int baudRate : {4800, 9600, 19200, 38400, 57600, 115200})
    {
        baudCombo->addItem(QString::number(baudRate), baudRate);
    }
    baudCombo->setCurrentIndex(2);
    auto *controlChannelsSpin = createSpinBox(page, QStringLiteral("ai8ControlChannelCountSpin"), 1, 8, 8);
    auto *controlCycleSpin = createDoubleSpinBox(page,
                                                 QStringLiteral("ai8ControlCycleSpin"),
                                                 0.0,
                                                 50.0,
                                                 0.0,
                                                 1,
                                                 QStringLiteral(" s"));
    QComboBox *runCombo = createFixedChoiceCombo(page);
    run_state_combo_ = runCombo;
    runCombo->setObjectName(QStringLiteral("ai8RunModeCombo"));
    connect(runCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index) {
                if (!run_state_combo_ || index < 0 || index >= run_state_combo_->count())
                {
                    return;
                }
                bool ok = false;
                const uint rawValue = run_state_combo_->itemData(index).toUInt(&ok);
                if (!ok || rawValue > std::numeric_limits<quint16>::max() ||
                    !Ai8TemperatureControllerProtocol::isDocumentedRunState(
                        static_cast<quint16>(rawValue)))
                {
                    run_state_write_requested_ = false;
                    run_state_write_value_ = 0;
                    return;
                }
                run_state_write_requested_ = true;
                run_state_write_value_ = static_cast<quint16>(rawValue);
            });
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
    combo->addItem(english_ ? english : chinese, userData);
    combo_item_bindings_.append({combo, index, chinese, english});
}

void Ai8TemperatureControllerPanel::updateRunStateCombo(quint16 rawValue)
{
    run_state_raw_ = rawValue;
    run_state_write_requested_ = false;
    run_state_write_value_ = 0;
    if (!run_state_combo_)
    {
        return;
    }

    if (run_state_unknown_item_index_ >= 0 &&
        run_state_unknown_item_index_ < run_state_combo_->count())
    {
        const int removedIndex = run_state_unknown_item_index_;
        run_state_combo_->removeItem(removedIndex);
        for (int index = combo_item_bindings_.size() - 1; index >= 0; --index)
        {
            ComboItemBinding& binding = combo_item_bindings_[index];
            if (binding.combo != run_state_combo_)
            {
                continue;
            }
            if (binding.index == removedIndex)
            {
                combo_item_bindings_.removeAt(index);
            }
            else if (binding.index > removedIndex)
            {
                --binding.index;
            }
        }
    }
    run_state_unknown_item_index_ = -1;

    if (!Ai8TemperatureControllerProtocol::isDocumentedRunState(rawValue))
    {
        addComboItem(run_state_combo_,
                     unknownRunStateText(rawValue, false),
                     unknownRunStateText(rawValue, true),
                     QVariant::fromValue(static_cast<uint>(rawValue)));
        run_state_unknown_item_index_ = run_state_combo_->count() - 1;
    }

    int selectedIndex = -1;
    for (int index = 0; index < run_state_combo_->count(); ++index)
    {
        bool ok = false;
        const uint value = run_state_combo_->itemData(index).toUInt(&ok);
        if (ok && value == static_cast<uint>(rawValue))
        {
            selectedIndex = index;
            break;
        }
    }
    if (selectedIndex >= 0)
    {
        const QSignalBlocker blocker(run_state_combo_);
        run_state_combo_->setCurrentIndex(selectedIndex);
    }
    updateRunStateAccessibility();
}

void Ai8TemperatureControllerPanel::updateRunStateAccessibility()
{
    if (!run_state_combo_)
    {
        return;
    }
    run_state_combo_->setAccessibleName(
        english_ ? QStringLiteral("Run State Srun") : QStringLiteral("运行状态 Srun"));
    run_state_combo_->setAccessibleDescription(
        english_
            ? QStringLiteral("Select a documented Srun value to request a change. The unknown device value is preserved; saving other global parameters does not write Srun.")
            : QStringLiteral("选择说明书支持的 Srun 值才会请求修改。未知设备原值会被保留；保存其他全局参数不会写入 Srun。"));
    if (run_state_unknown_item_index_ >= 0 &&
        run_state_unknown_item_index_ < run_state_combo_->count())
    {
        run_state_combo_->setItemData(
            run_state_unknown_item_index_,
            english_
                ? QStringLiteral("Saving other global parameters preserves this device value and does not write Srun.")
                : QStringLiteral("保存其他全局参数时会保留设备原始值，不会写入 Srun。"),
            Qt::ToolTipRole);
    }
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
    english_ = english;
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
    updateRunStateAccessibility();

    updateStatusText();
    const QString backendToolTip = backend_connected_
        ? (english
               ? QStringLiteral("Read and write use Modbus-RTU; every write is confirmed by read-back.")
               : QStringLiteral("读写使用 Modbus-RTU；每次写入后都会回读确认。"))
        : (english
               ? QStringLiteral("Select the AI-8288 serial port and connect first.")
               : QStringLiteral("请先选择 AI-8288 串口并执行连接。"));
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
    if (temperature_plot_)
    {
        temperature_plot_->setEnglish(english);
    }
    updateGeometry();
    emit outputStatusChanged();
}

void Ai8TemperatureControllerPanel::setBackendConnected(bool connected, const QString& detail)
{
    backend_connected_ = connected;
    backend_detail_ = detail;
    if (!connected)
    {
        operation_status_.clear();
    }
    if (read_button_) read_button_->setEnabled(connected);
    if (write_button_) write_button_->setEnabled(connected);
    updateStatusText();
    emit outputStatusChanged();
}

void Ai8TemperatureControllerPanel::setOperationStatus(const QString& text, bool success)
{
    operation_status_ = text;
    operation_succeeded_ = success;
    updateStatusText();
}

QString Ai8TemperatureControllerPanel::currentOutputStatusText() const
{
    const auto unknownText = english_
        ? QStringLiteral("Output: --")
        : QStringLiteral("输出：--");
    auto *channelSpin = findChild<QSpinBox *>(QStringLiteral("ai8ChannelSpin"));
    if (!channelSpin || !backend_connected_ || !latest_live_data_.controlStatesValid)
    {
        return unknownText;
    }

    const int channel = std::clamp(channelSpin->value(),
                                   1,
                                   Ai8TemperatureControllerProtocol::kChannelCount);
    const auto state = latest_live_data_.controlStates[static_cast<size_t>(channel - 1)];
    const QString stateText = Ai8TemperatureControllerProtocol::channelControlStateName(state, english_);
    return english_
        ? QStringLiteral("CH%1: %2").arg(channel).arg(stateText)
        : QStringLiteral("通道%1：%2").arg(channel).arg(stateText);
}

void Ai8TemperatureControllerPanel::updateStatusText()
{
    if (!protocol_status_label_)
    {
        return;
    }
    protocol_status_label_->setProperty("protocolReady", backend_connected_);
    protocol_status_label_->setProperty("operationFailed", backend_connected_ && !operation_succeeded_);
    QString text;
    if (!operation_status_.isEmpty())
    {
        text = operation_status_;
    }
    else if (backend_connected_)
    {
        text = english_ ? QStringLiteral("Modbus backend connected")
                        : QStringLiteral("Modbus 通信后端已连接");
        if (!backend_detail_.isEmpty())
        {
            text += QStringLiteral(" · ") + backend_detail_;
        }
    }
    else
    {
        text = english_ ? QStringLiteral("Communication backend not connected")
                        : QStringLiteral("通信后端未连接");
    }
    protocol_status_label_->setText(text);
    protocol_status_label_->style()->unpolish(protocol_status_label_);
    protocol_status_label_->style()->polish(protocol_status_label_);
}

Ai8TemperatureControllerProtocol::PageData Ai8TemperatureControllerPanel::currentPageData() const
{
    using namespace Ai8TemperatureControllerProtocol;
    PageData pageData;
    pageData.page = static_cast<Page>(page_stack_ ? page_stack_->currentIndex() : 0);
    auto spin = [this](const char *name) {
        return findChild<QSpinBox *>(QString::fromLatin1(name));
    };
    auto doubleSpin = [this](const char *name) {
        return findChild<QDoubleSpinBox *>(QString::fromLatin1(name));
    };
    auto combo = [this](const char *name) {
        return findChild<QComboBox *>(QString::fromLatin1(name));
    };
    pageData.selection.channel = spin("ai8ChannelSpin")->value();
    pageData.selection.inputGroup = spin("ai8InputGroupSpin")->value();
    pageData.selection.outputGroup = spin("ai8OutputGroupSpin")->value();

    pageData.channel.setpointC = doubleSpin("ai8SetpointSpin")->value();
    pageData.channel.proportionalBand = doubleSpin("ai8ProportionalBandSpin")->value();
    pageData.channel.integralTimeS = doubleSpin("ai8IntegralTimeSpin")->value();
    pageData.channel.derivativeTimeS = doubleSpin("ai8DerivativeTimeSpin")->value();
    pageData.channel.workMode = combo("ai8WorkModeCombo")->currentData().toInt();
    pageData.channel.manualOutputPercent = doubleSpin("ai8ManualOutputSpin")->value();

    pageData.input.inputType = combo("ai8InputSpecCombo")->currentData().toInt();
    pageData.input.scaleLow = doubleSpin("ai8ScaleLowSpin")->value();
    pageData.input.scaleHigh = doubleSpin("ai8ScaleHighSpin")->value();
    pageData.input.filter = spin("ai8FilterSpin")->value();
    pageData.input.channelInputGroup = spin("ai8ChannelInputConfigSpin")->value();
    pageData.input.measurementOffset = doubleSpin("ai8MeasurementOffsetSpin")->value();
    pageData.input.correctionEntry = spin("ai8CorrectionEntrySpin")->value();

    pageData.output.controlAction = combo("ai8ControlActionCombo")->currentData().toInt();
    pageData.output.hysteresis = doubleSpin("ai8HysteresisSpin")->value();
    pageData.output.outputLowPercent = spin("ai8OutputLowSpin")->value();
    pageData.output.outputHighPercent = spin("ai8OutputHighSpin")->value();
    pageData.output.riseSlope = doubleSpin("ai8RiseSlopeSpin")->value();
    pageData.output.fallSlope = doubleSpin("ai8FallSlopeSpin")->value();
    pageData.output.alarmResetFlags = spin("ai8AlarmResetSpin")->value();

    pageData.global.address = spin("ai8DeviceAddressSpin")->value();
    pageData.global.baudRate = combo("ai8BaudCombo")->currentData().toInt();
    pageData.global.controlChannelCount = spin("ai8ControlChannelCountSpin")->value();
    pageData.global.controlCycleS = doubleSpin("ai8ControlCycleSpin")->value();
    pageData.global.runStateRaw = run_state_raw_;
    pageData.global.runStateIsDocumented =
        Ai8TemperatureControllerProtocol::isDocumentedRunState(pageData.global.runStateRaw);
    pageData.global.runStateWriteRequested = run_state_write_requested_;
    pageData.global.runStateWriteValue = run_state_write_value_;
    pageData.global.parameterLock = combo("ai8ParameterLockCombo")->currentData().toInt();
    pageData.global.sampleMode = spin("ai8SampleModeSpin")->value();
    pageData.global.decimalPoint = spin("ai8DecimalPointSpin")->value();
    return pageData;
}

void Ai8TemperatureControllerPanel::applyPageData(
    const Ai8TemperatureControllerProtocol::PageData& pageData)
{
    auto setSpin = [this](const char *name, int value) {
        if (auto *widget = findChild<QSpinBox *>(QString::fromLatin1(name)))
        {
            const QSignalBlocker blocker(widget);
            widget->setValue(value);
        }
    };
    auto setDouble = [this](const char *name, double value) {
        if (auto *widget = findChild<QDoubleSpinBox *>(QString::fromLatin1(name)))
        {
            const QSignalBlocker blocker(widget);
            widget->setValue(value);
        }
    };
    auto setCombo = [this](const char *name, int value) {
        if (auto *widget = findChild<QComboBox *>(QString::fromLatin1(name)))
        {
            const int index = widget->findData(value);
            if (index >= 0)
            {
                const QSignalBlocker blocker(widget);
                widget->setCurrentIndex(index);
            }
        }
    };

    setSpin("ai8ChannelSpin", pageData.selection.channel);
    setSpin("ai8InputGroupSpin", pageData.selection.inputGroup);
    setSpin("ai8OutputGroupSpin", pageData.selection.outputGroup);
    switch (pageData.page)
    {
    case Ai8TemperatureControllerProtocol::Page::Channel:
        setDouble("ai8SetpointSpin", pageData.channel.setpointC);
        setDouble("ai8ProportionalBandSpin", pageData.channel.proportionalBand);
        setDouble("ai8IntegralTimeSpin", pageData.channel.integralTimeS);
        setDouble("ai8DerivativeTimeSpin", pageData.channel.derivativeTimeS);
        setCombo("ai8WorkModeCombo", pageData.channel.workMode);
        setDouble("ai8ManualOutputSpin", pageData.channel.manualOutputPercent);
        if (auto *pv = findChild<QLineEdit *>(QStringLiteral("ai8MeasuredTemperatureEdit")))
        {
            pv->setText(QString::number(pageData.channel.measuredC, 'f', 1) + QStringLiteral(" °C"));
        }
        break;
    case Ai8TemperatureControllerProtocol::Page::InputGroup:
        setCombo("ai8InputSpecCombo", pageData.input.inputType);
        setDouble("ai8ScaleLowSpin", pageData.input.scaleLow);
        setDouble("ai8ScaleHighSpin", pageData.input.scaleHigh);
        setSpin("ai8FilterSpin", pageData.input.filter);
        setSpin("ai8ChannelInputConfigSpin", pageData.input.channelInputGroup);
        setDouble("ai8MeasurementOffsetSpin", pageData.input.measurementOffset);
        setSpin("ai8CorrectionEntrySpin", pageData.input.correctionEntry);
        break;
    case Ai8TemperatureControllerProtocol::Page::OutputGroup:
        setCombo("ai8ControlActionCombo", pageData.output.controlAction);
        setDouble("ai8HysteresisSpin", pageData.output.hysteresis);
        setSpin("ai8OutputLowSpin", pageData.output.outputLowPercent);
        setSpin("ai8OutputHighSpin", pageData.output.outputHighPercent);
        setDouble("ai8RiseSlopeSpin", pageData.output.riseSlope);
        setDouble("ai8FallSlopeSpin", pageData.output.fallSlope);
        setSpin("ai8AlarmResetSpin", pageData.output.alarmResetFlags);
        break;
    case Ai8TemperatureControllerProtocol::Page::Global:
        setSpin("ai8DeviceAddressSpin", pageData.global.address);
        setCombo("ai8BaudCombo", pageData.global.baudRate);
        setSpin("ai8ControlChannelCountSpin", pageData.global.controlChannelCount);
        setDouble("ai8ControlCycleSpin", pageData.global.controlCycleS);
        updateRunStateCombo(pageData.global.runStateRaw);
        setCombo("ai8ParameterLockCombo", (pageData.global.parameterLock & 0x0020) != 0 ? 32 : 0);
        setSpin("ai8SampleModeSpin", pageData.global.sampleMode);
        setSpin("ai8DecimalPointSpin", pageData.global.decimalPoint);
        break;
    }
    if (pageData.page == Ai8TemperatureControllerProtocol::Page::Channel)
    {
        const int channelIndex = std::clamp(
            pageData.selection.channel, 1, Ai8TemperatureControllerProtocol::kChannelCount) - 1;
        if (std::isfinite(pageData.channel.measuredC))
        {
            auto& history = measured_temperature_history_[static_cast<size_t>(channelIndex)];
            history.append(pageData.channel.measuredC);
            while (history.size() > kAi8TemperatureHistoryLimit)
            {
                history.removeFirst();
            }
        }
    }
    updateTemperaturePlot();
}

void Ai8TemperatureControllerPanel::applyLiveData(
    const Ai8TemperatureControllerProtocol::LiveData& liveData)
{
    latest_live_data_ = liveData;
    if (liveData.valid)
    {
        for (int index = 0; index < Ai8TemperatureControllerProtocol::kChannelCount; ++index)
        {
            const double measured = liveData.measuredC[static_cast<size_t>(index)];
            if (!std::isfinite(measured))
            {
                continue;
            }
            auto& history = measured_temperature_history_[static_cast<size_t>(index)];
            history.append(measured);
            while (history.size() > kAi8TemperatureHistoryLimit)
            {
                history.removeFirst();
            }
        }
    }
    updateMeasuredValue();
    updateTemperaturePlot();
    emit outputStatusChanged();
}

void Ai8TemperatureControllerPanel::updateMeasuredValue()
{
    auto *edit = findChild<QLineEdit *>(QStringLiteral("ai8MeasuredTemperatureEdit"));
    auto *channelSpin = findChild<QSpinBox *>(QStringLiteral("ai8ChannelSpin"));
    if (!edit || !channelSpin)
    {
        return;
    }
    const int index = std::clamp(channelSpin->value(), 1,
                                 Ai8TemperatureControllerProtocol::kChannelCount) - 1;
    edit->setText(latest_live_data_.valid
        ? QString::number(latest_live_data_.measuredC[static_cast<size_t>(index)], 'f', 1) +
              QStringLiteral(" °C")
        : QStringLiteral("---"));
}

void Ai8TemperatureControllerPanel::updateTemperaturePlot()
{
    if (!temperature_plot_)
    {
        return;
    }
    auto *channelSpin = findChild<QSpinBox *>(QStringLiteral("ai8ChannelSpin"));
    auto *setpointSpin = findChild<QDoubleSpinBox *>(QStringLiteral("ai8SetpointSpin"));
    if (!channelSpin)
    {
        return;
    }
    const int channelIndex = std::clamp(
        channelSpin->value(), 1, Ai8TemperatureControllerProtocol::kChannelCount) - 1;
    temperature_plot_->setChannelIndex(channelIndex);
    temperature_plot_->setTargetTemperature(
        setpointSpin ? setpointSpin->value() : std::numeric_limits<double>::quiet_NaN());
    temperature_plot_->setSamples(measured_temperature_history_[static_cast<size_t>(channelIndex)]);
}

} // namespace VaporView::Ground::Widgets
