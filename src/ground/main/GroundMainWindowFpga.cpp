#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/devices/UiTestDataModel.h"
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QGridLayout>
#include <QPointer>
#include <QSpinBox>
#include "FpgaTelemetry.h"

#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
VaporView::Ground::Devices::RemoteSkyController *MainWindow::testRemoteSkyController()
{
    return state_->remote_sky_controller_.get();
}
#endif

void MainWindow::syncFpgaDeviceConfigUi()
{
    const bool relay = isFpgaRelayPath();
    const QString ai8Unsupported = state_->is_english_
        ? QStringLiteral("VLP1 does not expose AI8 parameter pages. Use the FPGA page for polling and confirmed temperature setpoint control.")
        : QStringLiteral("当前 VLP1 不支持 AI8 完整参数页；轮询配置与设温回读请使用 FPGA 页面。");
    const QString epsilonUnsupported = state_->is_english_
        ? QStringLiteral("VLP1 does not expose EPSILON parameter commands or RTCM forwarding.")
        : QStringLiteral("当前 VLP1 不支持 EPSILON 参数命令或 RTCM 转发。");
    if (state_->ai8_device_session_) state_->ai8_device_session_->setTransportSupported(!relay, relay ? ai8Unsupported : QString());
    if (state_->epsilon_device_session_) state_->epsilon_device_session_->setTransportSupported(!relay, relay ? epsilonUnsupported : QString());
    if (relay && !isUiTestMode() && state_->ai8_temperature_controller_panel_) {
        state_->ai8_temperature_controller_panel_->setBackendConnected(
            homeFpgaDeviceState(VaporView::SkyDeviceId::Ai8TemperatureController) == VaporView::DeviceState::Connected,
            state_->is_english_ ? QStringLiteral("FPGA relay") : QStringLiteral("FPGA 中转"));
        state_->ai8_temperature_controller_panel_->setPageCommandsEnabled(false, ai8Unsupported);
    }
    const auto registers = homeFpgaRegisters();
    const QString hint = state_->is_english_
        ? QStringLiteral("Confirmed FPGA register readback. Use Set to apply supported UART / polling parameters. Direct presets are retained separately.")
        : QStringLiteral("FPGA 实际寄存器读回。点击设置提交支持的串口/轮询参数；直连预设独立保留。");
    const auto readback = [&](QComboBox *editor, const QString &value) {
        if (!editor) return;
        auto *parent = editor->parentWidget();
        const QString name = editor->objectName() + QStringLiteral("FpgaReadback");
        auto *label = parent->findChild<QLabel *>(name);
        if (!label) {
            auto *grid = qobject_cast<QGridLayout *>(parent->layout());
            if (!grid) return;
            int row, column, rowSpan, columnSpan;
            grid->getItemPosition(grid->indexOf(editor), &row, &column, &rowSpan, &columnSpan);
            label = new QLabel(parent); label->setObjectName(name);
            label->setAlignment(Qt::AlignCenter);
            grid->addWidget(label, row, column, rowSpan, columnSpan);
        }
        label->setText(value); label->setToolTip(hint);
        label->setVisible(relay); editor->setVisible(!relay);
        if (relay) editor->setEnabled(false);
    };
    const auto word = [&](quint32 address) { return registers.contains(address) ? QString::number(registers.value(address)) : QStringLiteral("—"); };
    const auto rate = [&](quint32 address) { return registers.value(address) ? QString::number(1000.0/registers.value(address),'g',4) : QStringLiteral("—"); };
    const quint16 pressure = homeFpgaSensorSource(VaporView::SkyDeviceId::Ptb);
    readback(state_->device_config_.epsilon_baud_combo, word(0x6210));
    readback(state_->device_config_.ptb_baud_combo, pressure == 0x43 ? QStringLiteral("I²C") : word(0x6010));
    readback(state_->device_config_.hmp_baud_combo, QStringLiteral("I²C"));
    readback(state_->device_config_.lidar_baud_combo, word(0x6510));
    readback(state_->device_config_.ai8_temperature_baud_combo, word(0x6610));
    readback(state_->device_config_.temperature_baud_combo, state_->is_english_ ? QStringLiteral("Unsupported") : QStringLiteral("不支持"));
    readback(state_->device_config_.ptb_rate_combo, rate(pressure == 0x43 ? 0x6314 : 0x6018));
    readback(state_->device_config_.hmp_rate_combo, rate(0x6414));
    readback(state_->device_config_.ai8_temperature_rate_combo, rate(0x662c));
    readback(state_->device_config_.lidar_rate_combo, state_->is_english_ ? QStringLiteral("Device output") : QStringLiteral("设备输出"));
    readback(state_->device_config_.temperature_rate_combo, QStringLiteral("—"));
    const bool writable = !registers.isEmpty() && (isUiTestMode() || (isRemoteSkyMode()
        ? !state_->fpga_remote_request_id_ && !state_->fpga_remote_status_.value("busy").toBool() && !state_->remote_sky_config_applying_
        : !state_->fpga_busy_));
    const auto editable = [&](QComboBox *preset, quint16 source, bool supported) {
        if (!preset) return;
        auto *parent=preset->parentWidget();
        auto *button=parent->findChild<QPushButton*>(preset->objectName()+QStringLiteral("FpgaEdit"));
        if(!button) {
            auto *grid=qobject_cast<QGridLayout*>(parent->layout());if(!grid)return;
            auto *value=parent->findChild<QLabel*>(preset->objectName()+QStringLiteral("FpgaReadback"));
            int row,column,rowSpan,columnSpan;grid->getItemPosition(grid->indexOf(value),&row,&column,&rowSpan,&columnSpan);
            grid->removeWidget(value);
            auto *cell=new QWidget(parent);auto *layout=new QHBoxLayout(cell);layout->setContentsMargins(0,0,0,0);layout->setSpacing(6);
            value->setParent(cell);layout->addWidget(value,1);
            button=new QPushButton(cell);button->setObjectName(preset->objectName()+QStringLiteral("FpgaEdit"));
            layout->addWidget(button);grid->addWidget(cell,row,column,rowSpan,columnSpan);
            connect(button,&QPushButton::clicked,this,[this,button]{editFpgaSensorConfiguration(quint16(button->property("source").toUInt()));});
        }
        button->setProperty("source",source);button->setText(state_->is_english_?QStringLiteral("Set"):QStringLiteral("设置"));
        button->parentWidget()->setVisible(relay);button->setVisible(relay&&supported);
        const auto *value=parent->findChild<QLabel*>(preset->objectName()+QStringLiteral("FpgaReadback"));
        button->setEnabled(writable && value && value->text()!=QStringLiteral("—"));
        button->setToolTip(state_->is_english_?QStringLiteral("Configure FPGA UART / polling with verified readback."):QStringLiteral("设置 FPGA 串口/轮询参数，提交后核对读回。"));
    };
    editable(state_->device_config_.epsilon_baud_combo,0x42,true);
    editable(state_->device_config_.ptb_baud_combo,pressure,pressure==0x40);
    editable(state_->device_config_.lidar_baud_combo,0x45,true);
    editable(state_->device_config_.ptb_rate_combo,pressure,true);
    editable(state_->device_config_.hmp_rate_combo,0x44,true);
    editable(state_->device_config_.ai8_temperature_rate_combo,0x46,true);
    if (!relay) {
        for (auto *check : {state_->device_config_.epsilon_enabled_check, state_->device_config_.ptb_enabled_check,
                 state_->device_config_.hmp_enabled_check, state_->device_config_.lidar_enabled_check,
                 state_->device_config_.temperature_enabled_check, state_->device_config_.ai8_temperature_enabled_check,
                 state_->device_config_.tcp_wave_enabled_check})
            if (check) check->setToolTip(QString());
        return;
    }
    if (state_->device_config_.ptb_lbl) state_->device_config_.ptb_lbl->setText(pressure == 0x43 ? QStringLiteral("BMP390") : QStringLiteral("PTB210"));
    if (state_->device_config_.hmp_lbl) state_->device_config_.hmp_lbl->setText(QStringLiteral("SHT45"));
    if (state_->device_config_.epsilon_packet_rates_btn) {
        state_->device_config_.epsilon_packet_rates_btn->setEnabled(false);
        state_->device_config_.epsilon_packet_rates_btn->setToolTip(epsilonUnsupported);
    }
    for (auto *check : {state_->device_config_.epsilon_enabled_check, state_->device_config_.ptb_enabled_check,
             state_->device_config_.hmp_enabled_check, state_->device_config_.lidar_enabled_check,
             state_->device_config_.temperature_enabled_check, state_->device_config_.ai8_temperature_enabled_check,
             state_->device_config_.tcp_wave_enabled_check}) {
        if (!check) continue;
        check->setEnabled(false);
        check->setToolTip(state_->is_english_ ? QStringLiteral("Direct startup preset. Use the device action to enable or disable the FPGA channel.")
            : QStringLiteral("直连启动预设。FPGA 通道启停请使用设备操作按钮。"));
    }
    for (const auto &entry : {qMakePair(state_->device_config_.ptb_source_combo, pressure == 0x43 ? QStringLiteral("bmp390") : QStringLiteral("ptb210")),
                             qMakePair(state_->device_config_.hmp_source_combo, QStringLiteral("sht45"))}) {
        if (!entry.first) continue;
        const QSignalBlocker blocker(entry.first);
        entry.first->setCurrentIndex(entry.first->findData(entry.second));
        entry.first->setToolTip(state_->is_english_ ? QStringLiteral("FPGA sensor source; shared with the FPGA page.") : QStringLiteral("FPGA 传感器来源，与 FPGA 页面同步。"));
    }
    if (state_->device_config_.hmp_source_combo) state_->device_config_.hmp_source_combo->setEnabled(false);
    if (state_->device_config_.ptb_source_combo) state_->device_config_.ptb_source_combo->setEnabled(
        isUiTestMode() || (isRemoteSkyMode()
            ? state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen() && !state_->remote_sky_config_applying_ && !state_->fpga_remote_request_id_
            : !state_->fpga_busy_ && !state_->fpga_replaying_));
}

void MainWindow::editFpgaSensorConfiguration(quint16 source)
{
    const auto registers=homeFpgaRegisters();
    if(!isFpgaRelayPath() || registers.isEmpty())return;
    const bool remote=isRemoteSkyMode(), simulation=isUiTestMode();
    const quint32 base=0x6000+(source-0x40)*0x100;
    const bool hasBaud=source==0x40 || source==0x42 || source==0x45;
    const bool hasPoll=source==0x40 || source==0x43 || source==0x44 || source==0x46;
    const quint32 pollAddress=base+(source==0x40?0x18:source==0x46?0x2c:0x14);
    if((hasBaud&&!registers.contains(base+0x10)) || (hasPoll&&!registers.contains(pollAddress)))return;
    auto *dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName(QStringLiteral("fpgaSensorConfigDialog"));
    dialog->setStyleSheet(VaporView::applyAppThemeTokens(QStringLiteral(
        "QDialog#fpgaSensorConfigDialog { background: @vv-surface; color: @vv-text; }"
        "QDialog#fpgaSensorConfigDialog QLabel { background: transparent; color: @vv-text; }"),state_->dark_theme_enabled_));
    const QString name=source==0x40?QStringLiteral("PTB210"):source==0x42?QStringLiteral("EPSILON"):
        source==0x43?QStringLiteral("BMP390"):source==0x44?QStringLiteral("SHT45"):source==0x45?QStringLiteral("TFA1500-L"):QStringLiteral("AI8");
    dialog->setWindowTitle(name+(state_->is_english_?QStringLiteral(" · FPGA settings"):QStringLiteral(" · FPGA 参数")));
    auto *layout=new QVBoxLayout(dialog);auto *form=new QFormLayout;layout->addLayout(form);
    QSpinBox *baud=nullptr,*poll=nullptr;
    if(hasBaud) {
        baud=new QSpinBox(dialog);baud->setObjectName(QStringLiteral("fpgaSensorBaud"));
        baud->setRange(source==0x40?1200:source==0x42?9600:115200,source==0x40?19200:6250000);
        baud->setValue(int(registers.value(base+0x10)));baud->setSuffix(QStringLiteral(" bps"));
        form->addRow(state_->is_english_?QStringLiteral("FPGA UART baud"):QStringLiteral("FPGA 串口波特率"),baud);
    }
    if(hasPoll) {
        poll=new QSpinBox(dialog);poll->setObjectName(QStringLiteral("fpgaSensorPollMs"));poll->setRange(1,3600000);
        poll->setValue(int(registers.value(pollAddress)));poll->setSuffix(QStringLiteral(" ms"));
        form->addRow(state_->is_english_?QStringLiteral("Polling interval"):QStringLiteral("轮询间隔"),poll);
    }
    auto *hint=new QLabel(state_->is_english_
        ? QStringLiteral("The target channel pauses for writing and verified readback, then resumes its previous state. FPGA UART baud must match the sensor. Polling interval is not measured sample rate.")
        : QStringLiteral("提交时暂停目标通道，写入并核对读回后恢复原启用状态。FPGA 波特率需与传感器一致；轮询间隔不等于实际数据频率。"),dialog);
    hint->setWordWrap(true);hint->setMinimumWidth(400);layout->addWidget(hint);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel,dialog);layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Apply)->setObjectName(QStringLiteral("fpgaSensorConfigApply"));
    buttons->button(QDialogButtonBox::Apply)->setText(state_->is_english_?QStringLiteral("Apply and verify"):QStringLiteral("提交并读回"));
    buttons->button(QDialogButtonBox::Cancel)->setText(state_->is_english_?QStringLiteral("Cancel"):QStringLiteral("取消"));
    connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,dialog,[this,dialog,hint,source,remote,simulation,registers,base,pollAddress,baud,poll] {
        if(!isFpgaRelayPath() || remote!=isRemoteSkyMode() || simulation!=isUiTestMode() || homeFpgaRegisters().isEmpty()
            || (!simulation && (remote?state_->fpga_remote_request_id_ || state_->remote_sky_config_applying_ || state_->fpga_remote_status_.value("busy").toBool():state_->fpga_busy_))
            || ((source==0x40 || source==0x43) && homeFpgaSensorSource(VaporView::SkyDeviceId::Ptb)!=source)) {
            hint->setText(state_->is_english_?QStringLiteral("Connection, source or operation state changed. Close and reopen settings after readback."):QStringLiteral("连接、来源或事务状态已改变，请在读回就绪后重新打开参数。"));return;
        }
        QJsonObject request{{"op","sensor_config"},{"source",source}};
        if(baud && quint32(baud->value())!=registers.value(base+0x10))request["baud"]=baud->value();
        if(poll && quint32(poll->value())!=registers.value(pollAddress))request["poll_ms"]=poll->value();
        if(request.size()==2){dialog->accept();return;}
        if(simulation)state_->fpga_page_->simulateOperation(request);
        else if(remote)sendRemoteFpgaControl(request);
        else {
            state_->fpga_busy_=true;syncFpgaDeviceConfigUi();
            auto *controller=state_->fpga_controller_;QMetaObject::invokeMethod(controller,[controller,request]{controller->configureSensor(request);},Qt::QueuedConnection);
        }
        dialog->accept();
    });
    dialog->open();
}

quint16 MainWindow::homeFpgaSensorSource(VaporView::SkyDeviceId device) const
{
    using VaporView::SkyDeviceId;
    switch (device) {
    case SkyDeviceId::Epsilon: return 0x42;
    case SkyDeviceId::Ptb: return isUiTestMode() ? state_->fpga_sim_pressure_source_
        : isRemoteSkyMode() ? state_->fpga_remote_config_.pressureSource : state_->fpga_config_.pressureSource;
    case SkyDeviceId::Hmp: return 0x44;
    case SkyDeviceId::Lidar: return 0x45;
    case SkyDeviceId::Ai8TemperatureController: return 0x46;
    default: return 0;
    }
}

double MainWindow::homeFpgaSensorRate(VaporView::SkyDeviceId device) const
{
    if (homeFpgaDeviceState(device) != VaporView::DeviceState::Connected) return 0.0;
    const auto source = homeFpgaSensorSource(device);
    const double hz = isRemoteSkyMode()
        ? state_->fpga_remote_status_.value("sensor_rates_hz").toObject().value(QString::number(source)).toDouble()
        : state_->fpga_sensor_rates_.value(source);
    return std::isfinite(hz) && hz > 0.0 ? hz : 0.0;
}

VaporView::DeviceState MainWindow::homeFpgaDeviceActionState(VaporView::SkyDeviceId device) const
{
    using VaporView::DeviceState;
    using VaporView::SkyDeviceId;
    if (!homeFpgaSensorSource(device) && device != SkyDeviceId::WaveTcp) return DeviceState::Disabled;
    if (state_->fpga_home_action_device_ != SkyDeviceId::All)
        return state_->fpga_home_action_device_ == device ? DeviceState::Connecting : DeviceState::Disabled;
    if (isUiTestMode()) {
        if (state_->ui_test_connection_in_progress_) return DeviceState::Connecting;
    } else if (isRemoteSkyMode()) {
        if (state_->fpga_remote_request_id_ || state_->fpga_remote_status_.value("busy").toBool()) return DeviceState::Disabled;
    } else if (state_->fpga_busy_ || state_->fpga_replaying_ || anyCollectorRunning()
        || state_->connection_attempt_in_progress_) return DeviceState::Disabled;
    return homeFpgaDeviceState(device);
}

QMap<quint32, quint32> MainWindow::homeFpgaRegisters() const
{
    if (isUiTestMode()) return state_->fpga_sim_ready_ ? state_->fpga_sim_registers_ : QMap<quint32, quint32>{};
    if (!isRemoteSkyMode()) return state_->fpga_ready_ && !state_->fpga_replaying_
        ? state_->fpga_registers_ : QMap<quint32, quint32>{};
    if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen() || !state_->fpga_remote_status_.value("ready").toBool()
        || std::chrono::steady_clock::now() - state_->fpga_remote_status_time_ >= std::chrono::seconds(4)) return {};
    QMap<quint32, quint32> registers;
    const auto values = state_->fpga_remote_status_.value("registers").toObject();
    for (auto it = values.begin(); it != values.end(); ++it) registers.insert(it.key().toUInt(), quint32(it.value().toDouble()));
    return registers;
}

void MainWindow::updateHomeFpgaWaveActions()
{
    if (!state_->tcp_wave_panel_) return;
    const auto registers = homeFpgaRegisters();
    bool acquiring = true;
    for (quint32 control : {0x4004u, 0x4104u, 0x5004u, 0x5104u}) acquiring &= (registers.value(control) & 1) != 0;
    bool raw = false;
    for (int channel = 0; channel < 2; ++channel)
        raw |= (registers.value(0x403c + channel * 0x100) & 1) && (registers.value(0x50) & (1u << channel));
    const auto action = homeFpgaDeviceActionState(VaporView::SkyDeviceId::WaveTcp);
    state_->tcp_wave_panel_->setFpgaAcquisitionState(isFpgaRelayPath() && registers.contains(0x4004),
        action == VaporView::DeviceState::Disabled || action == VaporView::DeviceState::Connecting
            || state_->fpga_home_raw_start_pending_, acquiring, raw);
}

void MainWindow::requestHomeFpgaWaveAction(bool raw, bool enabled)
{
    const auto action = homeFpgaDeviceActionState(VaporView::SkyDeviceId::WaveTcp);
    if (!isFpgaRelayPath() || !state_->fpga_page_ || homeFpgaRegisters().isEmpty()
        || action == VaporView::DeviceState::Disabled || action == VaporView::DeviceState::Connecting) return;
    if (!raw) { setHomeFpgaDeviceEnabled(VaporView::SkyDeviceId::WaveTcp, enabled); return; }
    const auto registers = homeFpgaRegisters();
    bool acquiring = true;
    for (quint32 control : {0x4004u, 0x4104u, 0x5004u, 0x5104u}) acquiring &= (registers.value(control) & 1) != 0;
    if (enabled && !acquiring) {
        state_->fpga_home_raw_start_pending_ = true;
        setHomeFpgaDeviceEnabled(VaporView::SkyDeviceId::WaveTcp, true);
        return;
    }
    state_->fpga_home_action_device_ = VaporView::SkyDeviceId::WaveTcp;
    state_->fpga_home_action_started_ = false;
    emit state_->fpga_page_->rawRequested(enabled);
    if (isUiTestMode() || (isRemoteSkyMode() && !state_->fpga_remote_request_id_))
        state_->fpga_home_action_device_ = VaporView::SkyDeviceId::All;
    updateHomeDeviceStatusCapsules();
}

void MainWindow::finishHomeFpgaRawStart()
{
    if (!state_->fpga_home_raw_start_pending_) return;
    state_->fpga_home_raw_start_pending_ = false;
    const auto registers = homeFpgaRegisters();
    for (quint32 control : {0x4004u, 0x4104u, 0x5004u, 0x5104u})
        if (!(registers.value(control) & 1)) return;
    requestHomeFpgaWaveAction(true, true);
}

VaporView::DeviceState MainWindow::homeFpgaDeviceState(VaporView::SkyDeviceId device) const
{
    using VaporView::DeviceState;
    const auto source = homeFpgaSensorSource(device);
    if (!source && device != VaporView::SkyDeviceId::WaveTcp) return DeviceState::Disabled;
    // Command serialization locks actions, but does not disconnect other devices.
    if (isUiTestMode()) {
        if (!state_->fpga_sim_ready_) return DeviceState::Disconnected;
    } else if (!isRemoteSkyMode()) {
        if (state_->fpga_replaying_) return DeviceState::Disabled;
        if (!state_->fpga_ready_) return DeviceState::Disconnected;
    } else {
        if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen()) return DeviceState::Disabled;
        const bool fresh = std::chrono::steady_clock::now() - state_->fpga_remote_status_time_ < std::chrono::seconds(4);
        if (state_->fpga_remote_status_.isEmpty() || (fresh && !state_->fpga_remote_status_.value("connected").toBool()
            && !state_->fpga_remote_status_.value("busy").toBool())) return DeviceState::Disconnected;
    }
    const auto registers = homeFpgaRegisters();
    if (source) {
        const quint32 control = 0x6004 + (source - 0x40) * 0x100;
        if (!registers.contains(control)) return DeviceState::Disabled;
        return registers.value(control) & 1 ? DeviceState::Connected : DeviceState::Disconnected;
    }
    for (quint32 control : {0x4004u,0x4104u,0x5004u,0x5104u})
        if (registers.value(control) & 1) return DeviceState::Connected;
    return registers.contains(0x4004) ? DeviceState::Disconnected : DeviceState::Disabled;
}

void MainWindow::setHomeFpgaDeviceEnabled(VaporView::SkyDeviceId device, bool enabled)
{
    using VaporView::SkyDeviceId;
    const auto source = homeFpgaSensorSource(device);
    if (!source && device != SkyDeviceId::WaveTcp) return;
    state_->fpga_home_action_device_ = device;
    state_->fpga_home_action_started_ = false;
    if (enabled) startHomeDeviceActionSpinner(device);
    if (isUiTestMode()) {
        const auto apply = [this, device, source, enabled] {
            if (!isUiTestMode() || state_->fpga_home_action_device_!=device) return;
            if (!isFpgaRelayPath()) {state_->fpga_home_action_device_=SkyDeviceId::All;return;}
            if (enabled && !state_->fpga_sim_ready_) state_->fpga_page_->simulateOperation({{"op","connect"}});
            state_->fpga_page_->simulateOperation(device == SkyDeviceId::WaveTcp
                ? QJsonObject{{"op","acquisition"},{"enabled",enabled}}
                : QJsonObject{{"op","sensor"},{"source",source},{"enabled",enabled}});
            state_->ui_test_model_->setDeviceState(device, enabled ? VaporView::DeviceState::Connected : VaporView::DeviceState::Disconnected);
            state_->fpga_home_action_device_ = SkyDeviceId::All;
            finishHomeFpgaRawStart();
            updateConnectionStatus(false);
        };
        if (enabled) QTimer::singleShot(350,this,apply); else apply();
    } else if (isRemoteSkyMode() && enabled && !state_->fpga_remote_status_.value("ready").toBool()) {
        state_->fpga_home_connect_device_ = device;
        sendRemoteFpgaControl({{"op", "connect"}, {"locator", state_->remote_sky_config_.fpga.locator},
            {"backend", state_->remote_sky_config_.fpga.configuration.backend}});
        if (!state_->fpga_remote_request_id_) {
            state_->fpga_home_connect_device_ = SkyDeviceId::All;
            state_->fpga_home_action_device_ = SkyDeviceId::All;
        }
    } else if (!isRemoteSkyMode() && !state_->fpga_ready_) {
        // Open the configured USB backend, then enable only the requested device.
        QPointer<MainWindow> window(this);
        auto *controller = state_->fpga_controller_;
        const auto backend = state_->fpga_config_.backend;
        QMetaObject::invokeMethod(controller,[controller,window,device,backend] {
            controller->connectDevice({},backend);
            const auto enableDevice = [window,device] {
                if (window) QMetaObject::invokeMethod(window,[window,device] {
                    if (!window || window->state_->fpga_home_action_device_ != device) return;
                    window->state_->fpga_home_action_device_ = SkyDeviceId::All;
                    if (window->isFpgaRelayPath() && !window->isRemoteSkyMode()
                        && window->homeFpgaDeviceActionState(device) != VaporView::DeviceState::Connected)
                        window->setHomeFpgaDeviceEnabled(device,true);
                    else {
                        window->updateHomeDeviceStatusCapsules();
                        window->updateTemperatureTitleButtonsState();
                    }
                },Qt::QueuedConnection);
            };
            if (controller->ready() && !controller->busy()) enableDevice();
            else if (controller->busy()) {
                auto completion = std::make_shared<QMetaObject::Connection>();
                *completion = QObject::connect(controller,&VaporView::Ground::Devices::FpgaDeviceController::connectionChanged,
                    controller,[completion,enableDevice](bool ready,bool busy,const QString&) {
                        if (busy) return;
                        QObject::disconnect(*completion);
                        if (ready) enableDevice();
                    });
            }
        },Qt::QueuedConnection);
    } else {
        if (device == SkyDeviceId::WaveTcp) emit state_->fpga_page_->acquisitionRequested(enabled);
        else emit state_->fpga_page_->sensorEnableRequested(source,enabled);
        if (isRemoteSkyMode() && !state_->fpga_remote_request_id_) state_->fpga_home_action_device_ = SkyDeviceId::All;
    }
    updateHomeDeviceStatusCapsules();
    updateTemperatureTitleButtonsState();
}

void MainWindow::setupFpgaControlPage()
{
    using Controller = VaporView::Ground::Devices::FpgaDeviceController;
    qRegisterMetaType<VaporView::FpgaWave::CompletedStream>();
    QSettings settings = VaporView::applicationConfigSettings();
    const auto saved = QJsonDocument::fromJson(settings.value(QStringLiteral("Fpga/desiredConfiguration")).toByteArray());
    state_->fpga_config_ = FpgaControlConfig::fromJson(saved.object());
    state_->fpga_remote_config_ = FpgaControlConfig::fromJson(
        QJsonDocument::fromJson(settings.value(QStringLiteral("Fpga/remoteDesiredConfiguration")).toByteArray()).object());
    state_->fpga_page_ = new FpgaControlPage(state_->main_page_stack_);
    state_->fpga_page_->setObjectName(QStringLiteral("fpgaControlPage"));
    state_->fpga_page_->setConfiguration(isRemoteSkyMode() ? state_->fpga_remote_config_ : state_->fpga_config_);
    state_->fpga_page_->setLanguage(state_->is_english_);
    state_->fpga_page_->setTheme(state_->dark_theme_enabled_, state_->font_scale_percent_);
    state_->main_page_stack_->addWidget(state_->fpga_page_);
    state_->fpga_ai8_live_.measuredC.fill(std::numeric_limits<double>::quiet_NaN());
    state_->fpga_thread_ = new QThread(this);
    state_->fpga_thread_->setObjectName(QStringLiteral("fpgaUsbWorkerThread"));
    state_->fpga_controller_ = new Controller;
    auto *controller = state_->fpga_controller_;
    auto *page = state_->fpga_page_;
    connect(page,&FpgaControlPage::simulationStateChanged,this,[this](bool connected,const QMap<quint32,quint32>& values,quint16 pressure) {
        if (!isUiTestMode()) return;
        state_->fpga_sim_ready_=connected;state_->fpga_sim_registers_=values;state_->fpga_sim_pressure_source_=pressure;
        updateHomeDeviceStatusCapsules();updateTemperatureTitleButtonsState();syncFpgaDeviceConfigUi();
    });
    connect(page, &FpgaControlPage::waveformDisplayUpdated, this,
        [this](quint16 source, const QVector<QVector<double>> &samples,
               const QStringList &components, const QString &detail, quint32 totalPoints, bool preview) {
            if (state_->tcp_wave_panel_)
                state_->tcp_wave_panel_->setFpgaWaveform(source, samples, components, detail, totalPoints, preview);
        });
    connect(page, &FpgaControlPage::secondHarmonicPeakReady, this,
        [this](quint16 source, quint32 cycleId, quint64 timestamp, double peak, bool preview) {
            if (state_->tcp_wave_panel_)
                state_->tcp_wave_panel_->setFpgaSecondHarmonicPeak(source, cycleId, timestamp, peak, preview);
        });
    controller->moveToThread(state_->fpga_thread_);
    connect(state_->fpga_thread_, &QThread::finished, controller, &QObject::deleteLater);
    const auto dispatch = [this, controller](const QJsonObject& request, std::function<void()> local) {
        if (isUiTestMode()) state_->fpga_page_->simulateOperation(request);
        else if (isRemoteSkyMode()) sendRemoteFpgaControl(request);
        else QMetaObject::invokeMethod(controller, std::move(local), Qt::QueuedConnection);
    };
    connect(page, &FpgaControlPage::discoverDevicesRequested, this, [this, controller, page] {
        if (isUiTestMode()) {
            page->simulateOperation({{"op","discover"}});
            return;
        }
        if (isRemoteSkyMode()) { sendRemoteFpgaControl({{"op", "discover"}}); return; }
        const auto generation = page->usbDiscoveryGeneration();
        const auto backend = page->configuration().backend;
        QPointer<FpgaControlPage> target(page);
        QMetaObject::invokeMethod(controller, [controller, target, generation, backend] {
            const auto result = controller->discoverDevices(backend);
            if (target) QMetaObject::invokeMethod(target, [target, generation, result] {
                if (target && !target->property("fpgaUiTestMode").toBool() && target->usbDiscoveryGeneration() == generation) target->setUsbDiscoveryResult(result);
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    });

    connect(page, &FpgaControlPage::connectRequested, this, [this, controller](const QString &locator, const QString &backend) {
        if (isUiTestMode())
        {
            state_->fpga_page_->simulateOperation({{"op","connect"}});
            return;
        }
        if (isRemoteSkyMode())
        {
            sendRemoteFpgaControl({{"op", "connect"}, {"locator", locator}, {"backend", backend}});
            return;
        }
        if (state_->fpga_connected_ || state_->fpga_busy_ || state_->fpga_replaying_ || isUiTestMode() || isRemoteSkyMode() || anyCollectorRunning() || state_->connection_attempt_in_progress_
            || (state_->tcp_wave_panel_ && (state_->tcp_wave_panel_->isConnected() || state_->tcp_wave_panel_->isConnecting())))
        {
            state_->fpga_page_->setConnectionState(state_->fpga_ready_, state_->fpga_busy_, state_->is_english_
                ? QStringLiteral("Switch to Local and disconnect serial/TCP sources before connecting FPGA.")
                : QStringLiteral("请切换到本地模式并断开串口/TCP数据源后再连接 FPGA。"));
            return;
        }
        QMetaObject::invokeMethod(controller, [controller, locator, backend] { controller->connectDevice(locator,backend); }, Qt::QueuedConnection);
    });
    connect(page, &FpgaControlPage::disconnectRequested, this, [dispatch, controller] {
        dispatch({{"op", "disconnect"}}, [controller] { controller->disconnectDevice(); });
    });
    connect(page, &FpgaControlPage::applyRequested, this, [dispatch, controller](const FpgaControlConfig& config) {
        dispatch({{"op", "configure"}, {"section", "acquisition"}, {"configuration", config.toJson()}}, [controller, config] { controller->applyConfiguration(config,"acquisition"); });
    });
    connect(page, &FpgaControlPage::applyWmsRequested, this, [dispatch, controller](const FpgaControlConfig& config) {
        dispatch({{"op", "configure"}, {"section", "wms"}, {"configuration", config.toJson()}}, [controller, config] { controller->applyConfiguration(config,"wms"); });
    });
    connect(page, &FpgaControlPage::applyDliaRequested, this, [dispatch, controller](const FpgaControlConfig& config) {
        dispatch({{"op", "configure"}, {"section", "dlia"}, {"configuration", config.toJson()}}, [controller, config] { controller->applyConfiguration(config,"dlia"); });
    });
    connect(page, &FpgaControlPage::configurationChanged, this, [this, controller](const FpgaControlConfig &config) {
        if (isUiTestMode()) { state_->fpga_sim_pressure_source_ = config.pressureSource; syncFpgaDeviceConfigUi(); return; }
        if (isRemoteSkyMode())
        {
            const bool pressureChanged = state_->fpga_remote_config_.pressureSource != config.pressureSource;
            state_->fpga_remote_config_ = config;
            QSettings settings = VaporView::applicationConfigSettings();
            VaporView::setPersistentSetting(settings, QStringLiteral("Fpga/remoteDesiredConfiguration"), QJsonDocument(config.toJson()).toJson(QJsonDocument::Compact));
            if (pressureChanged && state_->remote_sky_config_loaded_) {
                state_->remote_sky_config_.fpga.configuration.pressureSource = config.pressureSource;
                markRemoteSkyConfigDirty();
                onRemoteSkyConfigApplyClicked();
            }
            syncFpgaDeviceConfigUi();
            return;
        }
        const bool pressureChanged = state_->fpga_config_.pressureSource != config.pressureSource;
        state_->fpga_config_ = config;
        QSettings settings = VaporView::applicationConfigSettings();
        VaporView::setPersistentSetting(settings, QStringLiteral("Fpga/desiredConfiguration"), QJsonDocument(config.toJson()).toJson(QJsonDocument::Compact));
        QMetaObject::invokeMethod(controller, [controller, config] { controller->setConfiguration(config); }, Qt::QueuedConnection);
        if (pressureChanged && (state_->fpga_ready_ || state_->fpga_replaying_))
        {
            state_->current_ptb_ = VaporView::PtbData{};
            state_->fpga_sources_.remove(0x40); state_->fpga_sources_.remove(0x43);
            { const std::lock_guard<std::mutex> lock(state_->fpga_snapshot_mutex_); state_->fpga_recording_snapshot_.hasPtb=false; }
            onRefreshTimer();
        }
        syncFpgaDeviceConfigUi();
    });
    connect(page, &FpgaControlPage::refreshRequested, this, [dispatch, controller] {
        dispatch({{"op", "refresh"}}, [controller] { controller->refresh(); });
    });
    connect(page, &FpgaControlPage::acquisitionRequested, this, [dispatch, controller](bool enabled) {
        dispatch({{"op", "acquisition"}, {"enabled", enabled}}, [controller, enabled] { controller->setAcquisition(enabled); });
    });
    connect(page, &FpgaControlPage::waveformRequested, this, [dispatch, controller](int channel, bool enabled) {
        dispatch({{"op", "wms"}, {"channel", channel}, {"enabled", enabled}}, [controller, channel, enabled] { controller->setWaveform(channel, enabled); });
    });
    connect(page, &FpgaControlPage::dacRequested, this, [dispatch, controller](int channel, bool enabled) {
        dispatch({{"op", "dac"}, {"channel", channel}, {"enabled", enabled}}, [controller, channel, enabled] { controller->setDac(channel, enabled); });
    });
    connect(page, &FpgaControlPage::sensorEnableRequested, this, [dispatch, controller](quint16 source, bool enabled) {
        dispatch({{"op", "sensor"}, {"source", source}, {"enabled", enabled}}, [controller, source, enabled] { controller->setSensorEnabled(source, enabled); });
    });
    connect(page, &FpgaControlPage::rawRequested, this, [dispatch, controller](bool enabled) {
        dispatch({{"op", "raw"}, {"enabled", enabled}}, [controller, enabled] { controller->setRawEnabled(enabled); });
    });
    connect(page, &FpgaControlPage::setTemperatureRequested, this, [dispatch, controller](double celsius) {
        dispatch({{"op", "temperature"}, {"celsius", celsius}}, [controller, celsius] { controller->setTemperature(celsius); });
    });
    connect(page, &FpgaControlPage::recordingRequested, this, [this](bool enable) {
        if (isUiTestMode())
        {
            state_->fpga_page_->simulateOperation({{"op","record"},{"enabled",enable}});
            return;
        }
        if (enable) onStartRecordingClicked(); else onStopRecordingClicked();
        updateRecordingStatusLabel();
    });
    connect(page, &FpgaControlPage::replayRequested, this, [this, controller](const QString &requested) {
        if (isUiTestMode())
        {
            state_->fpga_page_->appendDiagnostic(state_->is_english_
                ? QStringLiteral("[UI test] FPGA replay ignored.")
                : QStringLiteral("[界面测试] FPGA 回放操作已忽略。"));
            return;
        }
        if (state_->fpga_connected_ || state_->fpga_busy_ || state_->fpga_replaying_ || anyLocalDeviceConnected() || isRemoteSkyMode())
        { state_->fpga_page_->appendDiagnostic(state_->is_english_ ? QStringLiteral("Disconnect live sources before offline replay.") : QStringLiteral("离线回放前请断开所有实时数据源。")); return; }
        QString directory = requested;
        if (directory.isEmpty()) directory = QFileDialog::getExistingDirectory(this, state_->is_english_ ? QStringLiteral("Select complete session directory") : QStringLiteral("选择完整会话目录"), state_->recording_directory_);
        if (directory.isEmpty()) return;
        state_->fpga_replaying_ = true;
        QMetaObject::invokeMethod(controller, [controller, directory] { controller->replaySession(directory); }, Qt::QueuedConnection);
    });
    connect(page, &FpgaControlPage::exportRequested, this, [this, controller](const QString &requested) {
        if (isUiTestMode())
        {
            state_->fpga_page_->appendDiagnostic(state_->is_english_
                ? QStringLiteral("[UI test] FPGA export ignored.")
                : QStringLiteral("[界面测试] FPGA 导出操作已忽略。"));
            return;
        }
        QString directory = requested;
        if (directory.isEmpty()) directory = QFileDialog::getExistingDirectory(this, state_->is_english_ ? QStringLiteral("Select complete session directory") : QStringLiteral("选择完整会话目录"), state_->recording_directory_);
        if (directory.isEmpty()) return;
        const QString output = QFileDialog::getSaveFileName(this, state_->is_english_ ? QStringLiteral("Export FPGA session") : QStringLiteral("导出 FPGA 会话"), QDir(directory).filePath(QStringLiteral("fpga_decoded.csv")), QStringLiteral("CSV (*.csv);;JSON (*.json);;BIN (*.bin)"));
        if (!output.isEmpty()) QMetaObject::invokeMethod(controller, [controller, directory, output] { controller->exportSession(directory,output); }, Qt::QueuedConnection);
    });
    connect(controller, &Controller::transportConnectionChanged, this, [this](bool connected) {
        const bool wasConnected = state_->fpga_connected_;
        state_->fpga_connected_ = connected;
        if (!isRemoteSkyMode()) state_->fpga_page_->setTransportConnected(connected);
        if (!connected)
        {
            state_->fpga_ready_ = false;
            invalidateFpgaMeasurements();
            if (wasConnected && !anyCollectorRunning() && !(state_->tcp_wave_panel_ && state_->tcp_wave_panel_->isConnected())) stopRecording(true);
        }
        updateConnectionStatus(anyLocalDeviceConnected());
    }, Qt::QueuedConnection);
    connect(controller, &Controller::connectionChanged, this, [this](bool ready, bool busy, const QString &detail) {
        const bool wasReady = state_->fpga_ready_;
        state_->fpga_ready_ = ready; state_->fpga_busy_ = busy;
        if (!isRemoteSkyMode()) {
            if (ready && busy && state_->fpga_home_action_device_!=VaporView::SkyDeviceId::All) state_->fpga_home_action_started_ = true;
            if (!busy && state_->fpga_home_action_started_) {
                state_->fpga_home_action_device_=VaporView::SkyDeviceId::All;
                state_->fpga_home_action_started_=false;
                finishHomeFpgaRawStart();
            }
        }
        if (!isRemoteSkyMode()) state_->fpga_page_->setConnectionState(ready,busy,detail);
        if (wasReady && !ready)
        {
            invalidateFpgaMeasurements();
            if (!anyCollectorRunning() && !(state_->tcp_wave_panel_ && state_->tcp_wave_panel_->isConnected())) stopRecording(true);
        }
        updateConnectionStatus(anyLocalDeviceConnected());
        if (state_->ai8_temperature_controller_panel_ && ready)
            state_->ai8_temperature_controller_panel_->setPageCommandsEnabled(false, state_->is_english_
                ? QStringLiteral("FPGA AI8 setpoint commands are available on the FPGA page.")
                : QStringLiteral("FPGA AI8 设温请使用 FPGA 页面；此处串口参数命令不可用。"));
        updateRecordingActionStates();
    }, Qt::QueuedConnection);
    connect(controller, &Controller::hardwareValuesChanged, this, [this, page](const QMap<quint32, quint32>& values) {
        state_->fpga_registers_=values;
        if (!isRemoteSkyMode()) {page->setHardwareValues(values);updateHomeDeviceStatusCapsules();syncFpgaDeviceConfigUi();}
    }, Qt::QueuedConnection);
    connect(controller,&Controller::sensorConfigurationApplied,this,[this,page](quint16 source,const QMap<quint32,quint32>& values) {
        if(source!=0x46 || isRemoteSkyMode() || isUiTestMode())return;
        state_->fpga_config_.ai8.pollIntervalMs=values.value(0x662c);
        auto desired=page->configuration();desired.ai8.pollIntervalMs=values.value(0x662c);page->setConfiguration(desired);
    },Qt::QueuedConnection);
    connect(controller, &Controller::logRecordGenerated, this, [this,page](const VaporView::LogRecord &record) {
        if (record.level>=VaporView::LogLevel::Error && !isRemoteSkyMode()) {
            state_->fpga_home_raw_start_pending_=false;
            state_->fpga_home_action_device_=VaporView::SkyDeviceId::All;
            state_->fpga_home_action_started_=false;
        }
        page->appendDiagnostic(record.message);
        VaporView::LogService::withCurrentInstance([&record](VaporView::LogService &service) { service.publish(record); });
    }, Qt::QueuedConnection);
    connect(controller, &Controller::waveformUpdated, this, [this, page](const VaporView::FpgaWave::CompletedStream& stream) {
        if (!isRemoteSkyMode()) page->updateWaveform(stream);
    }, Qt::QueuedConnection);
    connect(controller, &Controller::replayFinished, this, [this](bool, const QString &detail) {
        state_->fpga_replaying_ = false;
        state_->fpga_page_->appendDiagnostic(detail);
        state_->fpga_page_->setConnectionState(false,false,state_->is_english_ ? QStringLiteral("Offline replay finished") : QStringLiteral("离线回放完成"));
        updateConnectionStatus(anyLocalDeviceConnected());
    }, Qt::QueuedConnection);
    connect(controller, &Controller::measurementUpdated, this, [this](const VaporView::FpgaSensor::AdaptedMeasurements &m) {
        if (isRemoteSkyMode()) return;
        const auto &r = m.reading;
        state_->fpga_sensor_rates_[r.source] = m.sampleRateHz;
        state_->fpga_page_->updateSensor(r);
        const bool valid = r.validity.structure && r.validity.crc && r.validity.deviceOnline && r.validity.measurement;
        if (valid) state_->fpga_sources_.insert(r.source); else state_->fpga_sources_.remove(r.source);
        const auto now = std::chrono::steady_clock::now();
        if ((r.kind == VaporView::FpgaSensor::SensorKind::Ptb210 || r.kind == VaporView::FpgaSensor::SensorKind::Bmp390)
            && r.source == state_->fpga_config_.pressureSource)
        {
            state_->current_ptb_ = VaporView::PtbData{};
            if (valid && r.pressurePa) { state_->current_ptb_.pressure_hpa = *r.pressurePa/100.; state_->current_ptb_.valid=true; state_->current_ptb_.timestamp=now; }
        }
        else if (r.kind == VaporView::FpgaSensor::SensorKind::Sht45)
        {
            state_->current_hmp_ = VaporView::HmpData{};
            if (valid && r.temperatureC && r.humidityPct) { state_->current_hmp_.temperature=*r.temperatureC; state_->current_hmp_.humidity=*r.humidityPct; state_->current_hmp_.valid=true; state_->current_hmp_.timestamp=now; }
        }
        else if (r.kind == VaporView::FpgaSensor::SensorKind::Tfa1500)
        {
            state_->current_lidar_ = VaporView::LidarData{};
            if (valid && r.distanceMm) { state_->current_lidar_.distance_m=*r.distanceMm/1000.; state_->current_lidar_.valid=true; state_->current_lidar_.timestamp=now; }
        }
        else if (r.kind == VaporView::FpgaSensor::SensorKind::Epsilon)
        {
            if (m.epsilon)
            {
                state_->current_epsilon_ = *m.epsilon;
                state_->current_epsilon_.timestamp = now;
                state_->fpga_epsilon_status_fresh_ = m.epsilonStatusFresh;
                const int id = r.epsilonMessageId.value_or(0);
                const bool positionPacket = id == 0x50 || id == 0x5c || id == 0x59;
                const bool velocityPacket = id == 0x42 || id == 0x50 || id == 0x5f || id == 0x59;
                if (positionPacket) { state_->fpga_position_valid_ = m.navigationValid; state_->fpga_position_time_ = now; }
                if (velocityPacket) { state_->fpga_velocity_valid_ = m.navigationValid && (id != 0x59 || m.epsilon->gnss_velocity_valid); state_->fpga_velocity_time_=now; }
                if (!m.epsilonStatusFresh && id != 0x59) { state_->fpga_position_valid_=false; state_->fpga_velocity_valid_=false; }
                state_->current_epsilon_.valid = valid && m.epsilon->valid && (m.attitudeValid || m.navigationValid);
                if (!m.epsilonStatusFresh) state_->current_epsilon_.filter_status_bits &= ~quint16(0xf);
                if (!state_->fpga_position_valid_) { state_->current_epsilon_.latitude_deg=std::numeric_limits<double>::quiet_NaN(); state_->current_epsilon_.longitude_deg=std::numeric_limits<double>::quiet_NaN(); state_->current_epsilon_.height_m=std::numeric_limits<double>::quiet_NaN(); }
#ifdef VAPORVIEW_HAS_OSGEARTH
                if (positionPacket && m.navigationValid) maybeForwardMap3DSample(state_->current_epsilon_, VaporView::Ground::Session::GroundRecordingService::currentTimestampUs());
#endif
            }
            else if (!valid) state_->current_epsilon_ = VaporView::EpsilonData{};
        }
        else if (r.kind == VaporView::FpgaSensor::SensorKind::Ai8)
        {
            const int channel = m.ai8 ? m.ai8->channel-1 : -1;
            if (channel >= 0 && channel < VaporView::Ai8TemperatureControllerProtocol::kChannelCount)
            {
                state_->fpga_ai8_live_.measuredC[channel] = m.ai8 && m.ai8->measurementValid ? m.ai8->measuredC : std::numeric_limits<double>::quiet_NaN();
                state_->fpga_ai8_live_.valid = std::any_of(state_->fpga_ai8_live_.measuredC.begin(),state_->fpga_ai8_live_.measuredC.end(),[](double v){return std::isfinite(v);});
                if (m.ai8) { state_->fpga_ai8_live_.mainStatusRaw=m.ai8->host; state_->fpga_ai8_live_.mainStatusValid=m.ai8->measurementValid; }
                if (state_->ai8_temperature_controller_panel_) state_->ai8_temperature_controller_panel_->applyLiveData(state_->fpga_ai8_live_);
                if (state_->ai8_temperature_overview_panel_) state_->ai8_temperature_overview_panel_->applyLiveData(state_->fpga_ai8_live_);
            }
        }
        {
            const std::lock_guard<std::mutex> lock(state_->fpga_snapshot_mutex_);
            auto &snapshot = state_->fpga_recording_snapshot_;
            snapshot = VaporView::Ground::Session::GroundSensorSnapshot{};
            if (state_->fpga_ready_ && state_->fpga_config_.recordSensors)
            {
                snapshot.epsilon=state_->current_epsilon_; snapshot.ptb=state_->current_ptb_;
                snapshot.hmp=state_->current_hmp_; snapshot.lidar=state_->current_lidar_;
                snapshot.hasEpsilon=state_->fpga_sources_.contains(0x42) && snapshot.epsilon.valid;
                snapshot.hasPtb=state_->fpga_sources_.contains(state_->fpga_config_.pressureSource) && snapshot.ptb.valid;
                snapshot.hasHmp=state_->fpga_sources_.contains(0x44) && snapshot.hmp.valid;
                snapshot.hasLidar=state_->fpga_sources_.contains(0x45) && snapshot.lidar.valid;
            }
        }
        updateHomeDeviceStatusCapsules();
        if (state_->combination_navigation_page_) state_->combination_navigation_page_->refreshStatus();
        onRefreshTimer();
    }, Qt::QueuedConnection);

    // These APIs enqueue under the recording service's lock, independent of GUI delivery.
    auto *recorder = state_->recording_service_.get();
    connect(controller,&Controller::rawFrame,this,[recorder](quint64 t,const QByteArray &b){recorder->recordFpgaFrame(t,b);},Qt::DirectConnection);
    connect(controller,&Controller::rawCommand,this,[recorder](quint64 t,const QByteArray &b){recorder->recordFpgaCommand(t,b);},Qt::DirectConnection);
    connect(controller,&Controller::rawUsbBytes,this,[recorder](quint64 t,const QByteArray &b){recorder->recordFpgaUsbBytes(t,b);},Qt::DirectConnection);
    connect(controller,&Controller::snapshot,this,[recorder](quint64 t,const QJsonObject &o){recorder->recordFpgaSnapshot(t,o);},Qt::DirectConnection);
    state_->fpga_thread_->start();
    const auto config = state_->fpga_config_;
    QMetaObject::invokeMethod(controller,[controller,config]{controller->setConfiguration(config);},Qt::QueuedConnection);

    using Remote = VaporView::Ground::Devices::RemoteSkyController;
    auto *remote = state_->remote_sky_controller_.get();
    connect(remote, &Remote::fpgaStatusUpdated, this, &MainWindow::receiveRemoteFpgaStatus);
    connect(remote, &Remote::fpgaSensorUpdated, this, [this, page](const VaporView::FpgaSensor::Reading& reading) {
        if (isRemoteSkyMode() && state_->remote_sky_controller_->isOpen()
            && state_->fpga_remote_status_.value("ready").toBool()
            && std::chrono::steady_clock::now() - state_->fpga_remote_status_time_ < std::chrono::seconds(4))
            page->updateSensor(reading);
    });
    connect(remote, &Remote::fpgaWaveformUpdated, this, [this, page](const VaporView::FpgaWave::CompletedStream& stream) {
        if (isRemoteSkyMode() && state_->remote_sky_controller_->isOpen()
            && state_->fpga_remote_status_.value("ready").toBool()
            && std::chrono::steady_clock::now() - state_->fpga_remote_status_time_ < std::chrono::seconds(4))
            page->updateWaveform(stream);
    });
    connect(remote, &Remote::deviceOperationResponseReceived, this, [this, page](const VaporView::DeviceOperationResponse& response) {
        if (response.device_id != VaporView::SkyDeviceId::Fpga
            || response.operation != VaporView::DeviceOperation::FpgaControl
            || response.request_id != state_->fpga_remote_request_id_) return;
        const auto connectDevice = state_->fpga_home_connect_device_;
        state_->fpga_home_connect_device_ = VaporView::SkyDeviceId::All;
        state_->fpga_remote_request_id_ = 0;
        if (response.error_code != VaporView::CommandErrorCode::Ok) state_->fpga_home_raw_start_pending_ = false;
        const bool discovery = state_->fpga_remote_operation_ == "discover";
        QJsonObject status;
        const bool statusParsed = VaporView::FpgaRemote::parseStatus(response.payload, status);
        if (statusParsed) {
            receiveRemoteFpgaStatus(status);
            if (discovery && isRemoteSkyMode() && page->usbDiscoveryGeneration() == state_->fpga_remote_discovery_generation_)
                page->setUsbDiscoveryResult(status.contains("usb_discovery") ? status.value("usb_discovery").toObject()
                    : QJsonObject{{"devices",QJsonArray{}},{"error",response.error_message.isEmpty()?QStringLiteral("SkyCore did not return USB discovery; update SkyCore."):response.error_message}});
        } else if (discovery && isRemoteSkyMode() && page->usbDiscoveryGeneration() == state_->fpga_remote_discovery_generation_) {
            page->setUsbDiscoveryResult({{"devices",QJsonArray{}},{"error",QStringLiteral("Invalid Sky USB discovery response")}});
        }
        page->appendDiagnostic(response.error_code == VaporView::CommandErrorCode::Ok
            ? (discovery ? (state_->is_english_ ? QStringLiteral("Sky USB discovery completed.") : QStringLiteral("天空端 USB 枚举完成。"))
                : (state_->is_english_ ? QStringLiteral("Sky FPGA operation confirmed by final readback.") : QStringLiteral("天空端 FPGA 操作已由最终读回确认。")))
            : (state_->is_english_ ? QStringLiteral("Sky FPGA operation failed: ") : QStringLiteral("天空端 FPGA 操作失败：")) + response.error_message);
        updateFpgaPageBackend();
        if (connectDevice != VaporView::SkyDeviceId::All && statusParsed && response.error_code == VaporView::CommandErrorCode::Ok
            && state_->fpga_remote_status_.value("ready").toBool() && isRemoteSkyMode() && isFpgaRelayPath())
            setHomeFpgaDeviceEnabled(connectDevice, true);
        finishHomeFpgaRawStart();
    });
    connect(remote, &Remote::deviceOperationRejected, this, [this, page](quint32 requestId, const VaporView::CommandAck& ack) {
        if (requestId != state_->fpga_remote_request_id_) return;
        state_->fpga_remote_request_id_ = 0;
        state_->fpga_home_raw_start_pending_ = false;
        state_->fpga_home_connect_device_ = VaporView::SkyDeviceId::All;
        if (state_->fpga_remote_operation_ == "discover" && isRemoteSkyMode() && page->usbDiscoveryGeneration() == state_->fpga_remote_discovery_generation_)
            page->setUsbDiscoveryResult({{"devices",QJsonArray{}},{"error",VaporView::commandErrorCodeText(ack.error_code,state_->is_english_)}});
        page->appendDiagnostic((state_->is_english_ ? QStringLiteral("Sky rejected FPGA command: ") : QStringLiteral("天空端拒绝 FPGA 命令：")) + VaporView::commandErrorCodeText(ack.error_code, state_->is_english_));
        updateFpgaPageBackend();
    });
    connect(remote, &Remote::deviceOperationTimedOut, this, [this, page](quint32 requestId) {
        if (requestId != state_->fpga_remote_request_id_) return;
        state_->fpga_remote_request_id_ = 0;
        state_->fpga_home_raw_start_pending_ = false;
        state_->fpga_home_connect_device_ = VaporView::SkyDeviceId::All;
        if (state_->fpga_remote_operation_ == "discover" && isRemoteSkyMode() && page->usbDiscoveryGeneration() == state_->fpga_remote_discovery_generation_)
            page->setUsbDiscoveryResult({{"devices",QJsonArray{}},{"error",QStringLiteral("Sky USB discovery timed out")}});
        page->appendDiagnostic(state_->fpga_remote_operation_ == "discover"
            ? (state_->is_english_ ? QStringLiteral("Sky USB discovery timed out.") : QStringLiteral("天空端 USB 枚举超时。"))
            : (state_->is_english_ ? QStringLiteral("FPGA command timed out; execution is unknown. Read status before issuing another write; no automatic retry.")
                : QStringLiteral("FPGA 命令超时，执行结果未知。再次写入前请读取状态；系统不会自动重发。")));
        updateFpgaPageBackend();
    });
    connect(remote, &Remote::linkOpenChanged, this, [this](bool open) {
        if (!open) {
            state_->fpga_home_connect_device_ = VaporView::SkyDeviceId::All;
            state_->fpga_home_raw_start_pending_ = false;
            state_->fpga_remote_status_ = {};
            state_->fpga_remote_status_time_ = {};
            state_->fpga_remote_request_id_ = 0;
        }
        if (isRemoteSkyMode()) updateFpgaPageBackend();
    });
    auto *freshnessTimer = new QTimer(this);
    freshnessTimer->setInterval(1000);
    connect(freshnessTimer, &QTimer::timeout, this, [this] { if (isRemoteSkyMode()) updateFpgaPageBackend(); });
    freshnessTimer->start();
    updateFpgaPageBackend();
}

void MainWindow::sendRemoteFpgaControl(const QJsonObject& request)
{
    auto *remote = state_->remote_sky_controller_.get();
    const auto op = request.value("op").toString();
    QString error;
    if (!remote || !remote->isOpen()) {
        if (op == "discover") state_->fpga_page_->setUsbDiscoveryResult({{"devices",QJsonArray{}},{"error",QStringLiteral("Connect to SkyCore first")}});
        state_->fpga_page_->appendDiagnostic(state_->is_english_ ? QStringLiteral("Connect to SkyCore through the radio TCP link first.")
            : QStringLiteral("请先通过数传 TCP 链路连接 SkyCore。"));
        updateFpgaPageBackend();
        return;
    }
    if (state_->fpga_remote_request_id_) {
        if (op == "discover") state_->fpga_page_->setUsbDiscoveryResult({{"devices",QJsonArray{}},{"error",QStringLiteral("Sky FPGA operation in progress; refresh devices when it completes")}});
        return;
    }
    if (op != "connect" && op != "disconnect" && op != "refresh" && op != "discover"
        && (!state_->fpga_remote_status_.value("ready").toBool()
            || std::chrono::steady_clock::now() - state_->fpga_remote_status_time_ >= std::chrono::seconds(4))) {
        state_->fpga_page_->appendDiagnostic(state_->is_english_ ? QStringLiteral("Fresh Sky FPGA ready status is required before writing.")
            : QStringLiteral("写入前必须收到新鲜且就绪的天空端 FPGA 状态。"));
        updateFpgaPageBackend();
        return;
    }
    if (!VaporView::FpgaRemote::validateControl(request, &error)) {
        state_->fpga_page_->appendDiagnostic(error);
        return;
    }
    state_->fpga_remote_operation_ = op;
    state_->fpga_remote_discovery_generation_ = state_->fpga_page_->usbDiscoveryGeneration();
    state_->fpga_remote_request_id_ = remote->sendFpgaControl(request);
    updateFpgaPageBackend();
}

void MainWindow::receiveRemoteFpgaStatus(const QJsonObject& status)
{
    const auto configuration = status.value("configuration").toObject();
    const bool configurationChanged = configuration != state_->fpga_remote_status_.value("configuration").toObject();
    state_->fpga_remote_status_ = status;
    state_->fpga_remote_status_time_ = std::chrono::steady_clock::now();
    if (configurationChanged && !state_->remote_sky_config_applying_) {
        state_->fpga_remote_config_ = FpgaControlConfig::fromJson(configuration);
        state_->remote_sky_config_.fpga.configuration.ai8.pollIntervalMs=state_->fpga_remote_config_.ai8.pollIntervalMs;
        if (isRemoteSkyMode()) state_->fpga_page_->setConfiguration(state_->fpga_remote_config_);
    }
    if (isRemoteSkyMode()) {
        QMap<quint32, quint32> values;
        const auto registers = status.value("registers").toObject();
        for (auto it = registers.begin(); it != registers.end(); ++it) values.insert(it.key().toUInt(), quint32(it.value().toDouble()));
        state_->fpga_page_->setHardwareValues(values);
        updateFpgaPageBackend();
        syncFpgaDeviceConfigUi();
    }
}

void MainWindow::updateFpgaPageBackend()
{
    if (!state_->fpga_page_) return;
    syncFpgaDeviceConfigUi();
    if (isUiTestMode() && state_->ui_test_model_)
    {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - state_->ui_test_started_ms_;
        const auto scenario = state_->ui_test_model_->scenario();
        const bool stalled = scenario == VaporView::Ground::Devices::UiTestScenario::DataStalled;
        state_->fpga_page_->setUiTestState(
            true, stalled,
            scenario == VaporView::Ground::Devices::UiTestScenario::PartialFailure, elapsed,
            VaporView::SensorTransportPath::FpgaRelay);
        return;
    }
    state_->fpga_page_->setUsbDiscoveryHost(isRemoteSkyMode(), !isRemoteSkyMode()
        || (state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen()));
    state_->fpga_page_->setSensorTransportPath(currentSensorTransportPath());
    if (!isRemoteSkyMode()) {
        state_->fpga_page_->setTransportConnected(state_->fpga_connected_);
        const QString routeDetail = currentSensorTransportPath() == VaporView::SensorTransportPath::FpgaRelay
            ? (state_->is_english_ ? QStringLiteral("Sensors -> FPGA relay -> VaporView · ")
                                   : QStringLiteral("传感器 -> FPGA 中转 -> VaporView · "))
            : (state_->is_english_ ? QStringLiteral("FPGA control page · direct sensors · ")
                                   : QStringLiteral("FPGA 控制页 · 传感器直连 · "));
        state_->fpga_page_->setConnectionState(state_->fpga_ready_, state_->fpga_busy_, state_->is_english_
            ? routeDetail + QStringLiteral("Local USB (bench mode)")
            : routeDetail + QStringLiteral("本机 USB（调试模式）"));
        return;
    }
    const bool link = state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen();
    const bool fresh = link && !state_->fpga_remote_status_.isEmpty()
        && std::chrono::steady_clock::now() - state_->fpga_remote_status_time_ < std::chrono::seconds(4);
    const auto& status = state_->fpga_remote_status_;
    const QString routeDetail = currentSensorTransportPath() == VaporView::SensorTransportPath::FpgaRelay
        ? (state_->is_english_ ? QStringLiteral("Sensors -> FPGA relay -> SkyCore · ")
                               : QStringLiteral("传感器 -> FPGA 中转 -> SkyCore · "))
        : (state_->is_english_ ? QStringLiteral("SkyCore direct sensors · ")
                               : QStringLiteral("SkyCore 直连传感器 · "));
    const QString detail = fresh ? routeDetail + status.value("detail").toString()
        : state_->is_english_ ? QStringLiteral("Connect the radio TCP link; enable fpga.enabled in Sky config. Link closure leaves Sky acquisition running.")
                             : QStringLiteral("请连接数传 TCP 链路，并在天空配置启用 fpga.enabled。数传断开不停止天空端采集。");
    state_->fpga_page_->setTransportConnected(fresh && status.value("connected").toBool());
    state_->fpga_page_->setConnectionState(fresh && status.value("ready").toBool(),
        state_->remote_sky_config_applying_ || state_->fpga_remote_request_id_ != 0 || (fresh && status.value("busy").toBool()), detail);
    if (auto *pressure = state_->fpga_page_->findChild<QComboBox *>(QStringLiteral("fpgaPressureSource")))
        pressure->setEnabled(link && !state_->remote_sky_config_applying_ && !state_->fpga_remote_request_id_);
    state_->fpga_page_->setRecordingState(fresh && status.value("recording_state").toInt() == 1,
        fresh ? status.value("session_directory").toString() + (state_->is_english_ ? QStringLiteral("\nSky FPGA archived: ") : QStringLiteral("\n天空端 FPGA 归档：")) + status.value("archived_records").toString()
              : (state_->is_english_ ? QStringLiteral("Sky recording status unavailable") : QStringLiteral("天空端记录状态未知")));
    if (!state_->fpga_remote_request_id_) state_->fpga_home_action_device_=VaporView::SkyDeviceId::All;
    updateHomeDeviceStatusCapsules();updateTemperatureTitleButtonsState();
}

void MainWindow::shutdownFpgaWorker()
{
    if (!state_->fpga_thread_) return;
    state_->fpga_thread_->requestInterruption();
    if (state_->fpga_thread_->isRunning() && state_->fpga_controller_)
        QMetaObject::invokeMethod(state_->fpga_controller_, &VaporView::Ground::Devices::FpgaDeviceController::disconnectDevice, Qt::BlockingQueuedConnection);
    state_->fpga_thread_->quit();
    state_->fpga_thread_->wait();
    state_->fpga_controller_ = nullptr;
    delete state_->fpga_thread_;
    state_->fpga_thread_ = nullptr;
}

void MainWindow::invalidateFpgaMeasurements()
{
    state_->fpga_home_raw_start_pending_ = false;
    state_->fpga_sensor_rates_.clear();
    { const std::lock_guard<std::mutex> lock(state_->fpga_snapshot_mutex_); state_->fpga_recording_snapshot_ = VaporView::Ground::Session::GroundSensorSnapshot{}; }
    state_->fpga_epsilon_status_fresh_=false;
    state_->fpga_position_valid_=false;
    state_->fpga_velocity_valid_=false;
    if (state_->fpga_sources_.contains(0x42)) state_->current_epsilon_ = VaporView::EpsilonData{};
    if (state_->fpga_sources_.contains(state_->fpga_config_.pressureSource)) state_->current_ptb_ = VaporView::PtbData{};
    if (state_->fpga_sources_.contains(0x44)) state_->current_hmp_ = VaporView::HmpData{};
    if (state_->fpga_sources_.contains(0x45)) state_->current_lidar_ = VaporView::LidarData{};
    if (state_->fpga_sources_.contains(0x46))
    {
        state_->fpga_ai8_live_ = VaporView::Ai8TemperatureControllerProtocol::LiveData{};
        state_->fpga_ai8_live_.measuredC.fill(std::numeric_limits<double>::quiet_NaN());
        if (state_->ai8_temperature_controller_panel_) state_->ai8_temperature_controller_panel_->applyLiveData(state_->fpga_ai8_live_);
        if (state_->ai8_temperature_overview_panel_) state_->ai8_temperature_overview_panel_->applyLiveData(state_->fpga_ai8_live_);
    }
    state_->fpga_sources_.clear();
    onRefreshTimer();
}
