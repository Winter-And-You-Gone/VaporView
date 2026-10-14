#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/devices/UiTestDataModel.h"
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QGridLayout>
#include <QPointer>
#include <QDoubleValidator>
#include <QIntValidator>
#include "FpgaTelemetry.h"
#include "ground/widgets/DeviceEnabledCheckBox.h"

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
        ? QStringLiteral("Disconnect this device to edit. Changes are staged and verified when connecting; Direct presets are separate.")
        : QStringLiteral("断开此设备后可修改；参数暂存，连接时下发并核对读回。直连预设独立保留。");
    const bool busy = state_->fpga_home_action_device_ != VaporView::SkyDeviceId::All || (isUiTestMode()
        ? state_->ui_test_connection_in_progress_ : isRemoteSkyMode()
        ? state_->fpga_remote_request_id_ || state_->fpga_remote_status_.value("busy").toBool() || state_->remote_sky_config_applying_
        : state_->fpga_busy_ || state_->fpga_replaying_);
    const bool usbConnected = isUiTestMode() ? state_->fpga_sim_ready_ : isRemoteSkyMode()
        ? state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen() && state_->fpga_remote_status_.value("connected").toBool()
        : state_->fpga_connected_;
    const auto field = [&](QComboBox *preset, quint16 source, const QString &key, quint32 address, const QString &fixed = {}) {
        if (!preset) return;
        auto *parent=preset->parentWidget();
        auto *grid=qobject_cast<QGridLayout*>(parent->layout());if(!grid)return;
        int row,column,rowSpan,columnSpan;
        grid->getItemPosition(grid->indexOf(preset),&row,&column,&rowSpan,&columnSpan);
        auto *label=parent->findChild<QLabel*>(preset->objectName()+QStringLiteral("FpgaReadback"));
        if(!label) {
            label=new QLabel(parent);label->setObjectName(preset->objectName()+QStringLiteral("FpgaReadback"));
            label->setAlignment(Qt::AlignCenter);grid->addWidget(label,row,column,rowSpan,columnSpan);
        }
        const bool supported = !key.isEmpty();
        auto *editor=parent->findChild<QComboBox*>(preset->objectName()+QStringLiteral("FpgaEditor"));
        if(supported && !editor) {
            editor=new QComboBox(parent);editor->setObjectName(preset->objectName()+QStringLiteral("FpgaEditor"));
            editor->setEditable(true);editor->setInsertPolicy(QComboBox::NoInsert);editor->setMaxVisibleItems(15);
            configureComboPopup(editor);grid->addWidget(editor,row,column,rowSpan,columnSpan,Qt::AlignVCenter);
            connect(editor,&QComboBox::currentTextChanged,this,[this,editor](const QString &text) {
                if(!isFpgaRelayPath() || !editor->isEnabled())return;
                const auto source=quint16(editor->property("source").toUInt());
                const auto key=editor->property("parameter").toString();
                fpgaSensorDrafts()[source][key]=text;
                editor->setToolTip(state_->is_english_?QStringLiteral("Staged; applied and verified when connecting.")
                    :QStringLiteral("已暂存，连接时下发并核对读回。"));
            });
        }
        preset->setVisible(!relay);
        if(relay)preset->setEnabled(false);
        label->setText(fixed);label->setToolTip(hint);label->setVisible(relay&&!supported);
        if(!editor)return;
        editor->setVisible(relay&&supported);
        if(!supported)return;
        const QSignalBlocker blocker(editor);
        if(editor->property("source").toUInt()!=source) {
            editor->clear();editor->setProperty("source",source);editor->setProperty("parameter",key);
            const auto *oldValidator=editor->validator();editor->setValidator(nullptr);delete oldValidator;
            if(key=="baud") {
                const int minimum=source==0x40?1200:source==0x42?9600:115200;
                const int maximum=source==0x40?19200:6250000;
                editor->setValidator(new QIntValidator(minimum,maximum,editor));
                for(int baud:{1200,2400,4800,9600,19200,38400,57600,115200,230400,460800,500000,921600,1000000,2000000,6250000})
                    if(baud>=minimum&&baud<=maximum)editor->addItem(QString::number(baud));
            } else {
                auto *validator=new QDoubleValidator(1000.0/3600000,1000,9,editor);validator->setLocale(QLocale::c());editor->setValidator(validator);
                editor->addItems({"0.1","0.2","0.5","1","2","5","10","20","50","100","1000"});
            }
        }
        editor->setFixedSize(preset->size());editor->setFont(preset->font());
        const auto draft=fpgaSensorDrafts().value(source);
        const auto value=draft.value(key);
        const auto numberText=[&](double value) {return QString::number(key=="baud"?value:1000.0/value,'g',9);};
        const QString text=draft.contains(key)?value.toString()
            :registers.value(address)?numberText(registers.value(address)):QString();
        if(editor->currentText()!=text)editor->setCurrentText(text);
        editor->lineEdit()->setPlaceholderText(state_->is_english_?(key=="baud"?"Baud":"Hz"):(key=="baud"?"波特率":"Hz"));
        const quint32 control=0x6004+(source-0x40)*0x100;
        editor->setEnabled(relay&&!busy&&(!usbConnected||(registers.contains(control)&&!(registers.value(control)&1))));
        editor->setToolTip(hint+(key=="poll_ms"?(state_->is_english_?QStringLiteral(" Frequency in Hz; polling cadence, not measured sample rate.")
            :QStringLiteral(" 频率单位 Hz，表示轮询节奏，不是实测数据频率。")):QString()));
    };
    for(auto it=state_->device_config_.internal_sensor_rows.begin();it!=state_->device_config_.internal_sensor_rows.end();++it) {
        const auto &widgets=it.value();
        field(qobject_cast<QComboBox*>(widgets[4]),it.key(),"poll_ms",it.key()==0x43?0x6314:0x6414);
        field(qobject_cast<QComboBox*>(widgets[2]),it.key(),{},0,QStringLiteral("I²C"));
        const bool connected=registers.value(0x6004+(it.key()-0x40)*0x100)&1;
        auto *check=qobject_cast<QCheckBox*>(widgets[5]);
        const auto id=it.key()==0x43?VaporView::SkyDeviceId::Bmp390:VaporView::SkyDeviceId::Sht45;
        if(relay){const QSignalBlocker blocker(check);check->setChecked(connected);}
        check->setEnabled(!relay&&homeDeviceActionState(id)!=VaporView::DeviceState::Connected
            &&homeDeviceActionState(id)!=VaporView::DeviceState::Connecting
            &&(!isRemoteSkyMode()||state_->remote_sky_controller_->isOpen()));
        qobject_cast<QLabel*>(widgets[3])->setText(relay?(state_->is_english_?QStringLiteral("Rate:"):QStringLiteral("频率："))
            :(state_->is_english_?QStringLiteral("Receive:"):QStringLiteral("接收：")));
        auto *button=qobject_cast<QToolButton*>(widgets[6]);
        updateDeviceConfigRemoteActionButton(id);
        widgets[0]->show();widgets[3]->show();widgets[5]->show();button->parentWidget()->show();
        if(!relay) {
            const auto action=homeDeviceActionState(id);
            const bool editable=action!=VaporView::DeviceState::Connected&&action!=VaporView::DeviceState::Connecting
                &&(!isRemoteSkyMode()||(!state_->remote_sky_config_loading_&&!state_->remote_sky_config_applying_));
            for(int index:{1,2,4}){widgets[index]->show();widgets[index]->setEnabled(editable);}
            widgets[4]->setToolTip(state_->is_english_?QStringLiteral("Receive update limit in Hz. The USB serial bridge controls physical sampling; no polling command is sent.")
                :QStringLiteral("接收更新频率上限，单位 Hz。物理采样由 USB 串口桥控制；不发送轮询命令。"));
        }
    }
    refreshInternalEnvironment();
    const quint16 pressure = homeFpgaSensorSource(VaporView::SkyDeviceId::Ptb);
    field(state_->device_config_.epsilon_baud_combo,0x42,"baud",0x6210);
    field(state_->device_config_.ptb_baud_combo,pressure,pressure==0x40?"baud":"",0x6010,QStringLiteral("I²C"));
    field(state_->device_config_.hmp_baud_combo,0,{},0,state_->is_english_?QStringLiteral("Not implemented"):QStringLiteral("下位机未接入"));
    field(state_->device_config_.lidar_baud_combo,0x45,"baud",0x6510);
    field(state_->device_config_.ai8_temperature_baud_combo,0x46,{},0,registers.contains(0x6610)?QString::number(registers.value(0x6610)):QStringLiteral("19200"));
    field(state_->device_config_.temperature_baud_combo,0,{},0,state_->is_english_?QStringLiteral("Unsupported"):QStringLiteral("不支持"));
    field(state_->device_config_.ptb_rate_combo,pressure,"poll_ms",pressure==0x43?0x6314:0x6018);
    field(state_->device_config_.hmp_rate_combo,0,{},0,QStringLiteral("—"));
    field(state_->device_config_.ai8_temperature_rate_combo,0x46,"poll_ms",0x662c);
    field(state_->device_config_.lidar_rate_combo,0x45,{},0,state_->is_english_?QStringLiteral("Device output"):QStringLiteral("设备输出"));
    field(state_->device_config_.temperature_rate_combo,0,{},0,QStringLiteral("—"));
    if (auto *check=state_->device_config_.fpga_wave_enabled_check) {
        bool known=true,enabled=false;
        for(quint32 control:{0x4004u,0x4104u,0x5004u,0x5104u}) {
            known &= registers.contains(control);
            enabled |= (registers.value(control)&1)!=0;
        }
        const auto action=homeFpgaDeviceActionState(VaporView::SkyDeviceId::WaveTcp);
        const QSignalBlocker blocker(check);
        check->setVisible(relay);
        check->setChecked(known&&enabled);
        check->setEnabled(relay&&known&&!busy&&action!=VaporView::DeviceState::Disabled
            &&action!=VaporView::DeviceState::Connecting);
        const QString tooltip=state_->is_english_
            ? QStringLiteral("ADC/DLIA acquisition readback. Toggle to start both acquisition channels or stop and drain them; RAW upload is controlled separately.")
            : QStringLiteral("ADC/DLIA 实际采集状态。勾选启动两路采集，取消则停止并排空；RAW 上传单独控制。");
        check->setToolTip(tooltip);check->setAccessibleName(tooltip);
    }
    if (relay && state_->device_config_.tcp_wave_transport_hint_lbl) {
        const auto rate=[&](quint32 address) {return registers.value(address)?QString::number(registers.value(address)):QStringLiteral("—");};
        auto *label=state_->device_config_.tcp_wave_transport_hint_lbl;
        label->setText((state_->is_english_?QStringLiteral("Sampling: %1 Hz"):QStringLiteral("采样：%1 Hz")).arg(rate(0x4014)));
        label->setToolTip((state_->is_english_
            ? QStringLiteral("ADC0/1 share this sampling rate. DLIA0: %1 Hz; DLIA1: %2 Hz. Edit sampling and demodulation rates on the FPGA parameter page. RAW upload and received waveform frame rates are separate.")
            : QStringLiteral("ADC0/1 共用此采样率。DLIA0：%1 Hz；DLIA1：%2 Hz。采样率与解调输出率在 FPGA 参数页修改；RAW 上传和收到的波形帧频另计。")).arg(rate(0x504c),rate(0x514c)));
    }
    if (!relay) {
        for (auto *check : {state_->device_config_.epsilon_enabled_check, state_->device_config_.ptb_enabled_check,
                 state_->device_config_.hmp_enabled_check, state_->device_config_.lidar_enabled_check,
                 state_->device_config_.temperature_enabled_check, state_->device_config_.ai8_temperature_enabled_check,
                 state_->device_config_.tcp_wave_enabled_check})
            if (check) check->setToolTip(QString());
        return;
    }
    if (state_->device_config_.ptb_lbl) state_->device_config_.ptb_lbl->setText(pressure == 0x43 ? QStringLiteral("BMP390") : QStringLiteral("PTB210"));

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

}

QMap<quint16, QJsonObject>& MainWindow::fpgaSensorDrafts()
{
    return state_->fpga_sensor_drafts_[(isRemoteSkyMode()?1:0)+(isUiTestMode()?2:0)];
}

QJsonObject MainWindow::fpgaSensorConnectRequest(quint16 source)
{
    auto request=fpgaSensorDrafts().value(source);
    // Keep the typed text intact during refresh; convert only for the transaction.
    for(auto it=request.begin();it!=request.end();++it) {
        bool ok=false;const double number=it.value().toString().toDouble(&ok);
        const double value=it.key()=="baud"?number:(number>0?std::round(1000.0/number):0);
        if(ok&&std::isfinite(value))it.value()=value;
    }
    request["op"]="sensor";request["source"]=source;request["enabled"]=true;
    return request;
}

void MainWindow::confirmFpgaSensorDraft(quint16 source, const QMap<quint32, quint32>& registers)
{
    auto &drafts=fpgaSensorDrafts();
    const auto draft=drafts.value(source);
    const auto request=fpgaSensorConnectRequest(source);
    const quint32 base=0x6000+(source-0x40)*0x100;
    for(const auto &key:draft.keys()) {
        const quint32 address=base+(key=="baud"?0x10:source==0x40?0x18:source==0x46?0x2c:0x14);
        if(request.value(key).isDouble()&&registers.contains(address)&&registers.value(address)==request.value(key).toDouble())
            drafts[source].remove(key);
    }
    if(drafts.value(source).isEmpty())drafts.remove(source);
}

quint16 MainWindow::homeFpgaSensorSource(VaporView::SkyDeviceId device) const
{
    using VaporView::SkyDeviceId;
    switch (device) {
    case SkyDeviceId::Epsilon: return 0x42;
    case SkyDeviceId::Ptb: return 0x40;
    case SkyDeviceId::Bmp390: return 0x43;
    case SkyDeviceId::Sht45: return 0x44;
    // HMP has no implemented measurement/control channel in the current firmware.
    case SkyDeviceId::Hmp: return 0;
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
            if(device==SkyDeviceId::WaveTcp)state_->fpga_page_->simulateOperation({{"op","acquisition"},{"enabled",enabled}});
            else emit state_->fpga_page_->sensorEnableRequested(source,enabled);
            state_->ui_test_model_->setDeviceState(device, homeFpgaDeviceState(device));
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
                        && (window->homeFpgaDeviceActionState(device) != VaporView::DeviceState::Connected
                            || !window->fpgaSensorDrafts().value(window->homeFpgaSensorSource(device)).isEmpty()))
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
    connect(page, &FpgaControlPage::simulationSensorUpdated, this, [this](const VaporView::FpgaSensor::Reading &reading) {
        if (isUiTestMode()) updateInternalEnvironment(reading);
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
    connect(page, &FpgaControlPage::sensorEnableRequested, this, [this, dispatch, controller](quint16 source, bool enabled) {
        auto request=enabled?fpgaSensorConnectRequest(source):QJsonObject{};
        request["op"]="sensor";request["source"]=source;request["enabled"]=enabled;
        if(!VaporView::FpgaRemote::validateControl(request)) {
            state_->fpga_home_action_device_=VaporView::SkyDeviceId::All;
            state_->fpga_home_action_started_=false;
            state_->fpga_page_->appendDiagnostic(state_->is_english_?QStringLiteral("Invalid staged sensor settings; connection was not started.")
                :QStringLiteral("暂存的波特率或频率无效，未启动设备连接。"));
            updateFpgaPageBackend();return;
        }
        dispatch(request, [controller, request, source, enabled] {
            if(enabled)controller->connectSensor(request);else controller->setSensorEnabled(source,false);
        });
        if(isUiTestMode()&&enabled)confirmFpgaSensorDraft(source,homeFpgaRegisters());
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
        if(isRemoteSkyMode() || isUiTestMode())return;
        confirmFpgaSensorDraft(source,values);
        if(source==0x46) {
            state_->fpga_config_.ai8.pollIntervalMs=values.value(0x662c);
            auto desired=page->configuration();desired.ai8.pollIntervalMs=values.value(0x662c);page->setConfiguration(desired);
        }
        syncFpgaDeviceConfigUi();
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
        updateInternalEnvironment(r);
        if (r.kind == VaporView::FpgaSensor::SensorKind::Ptb210 && r.source == 0x40)
        {
            state_->current_ptb_ = VaporView::PtbData{};
            if (valid && r.pressurePa) { state_->current_ptb_.pressure_hpa = *r.pressurePa/100.; state_->current_ptb_.valid=true; state_->current_ptb_.timestamp=now; }
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
                snapshot.hasPtb=state_->fpga_sources_.contains(0x40) && snapshot.ptb.valid;
                snapshot.hasHmp=false; // Reserved HMP source 0x41 is not implemented by the firmware.
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
    connect(remote,&Remote::internalEnvironmentUpdated,this,[this](const VaporView::FpgaSensor::Reading& reading) {
        if(isRemoteSkyMode()&&!isFpgaRelayPath()&&state_->remote_sky_controller_->isOpen())updateInternalEnvironment(reading);
    });
    connect(remote, &Remote::fpgaSensorUpdated, this, [this, page](const VaporView::FpgaSensor::Reading& reading) {
        if (isRemoteSkyMode() && state_->remote_sky_controller_->isOpen()
            && state_->fpga_remote_status_.value("ready").toBool()
            && std::chrono::steady_clock::now() - state_->fpga_remote_status_time_ < std::chrono::seconds(4))
        {
            page->updateSensor(reading);
            updateInternalEnvironment(reading);
        }
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
        const auto request=state_->fpga_remote_control_;
        state_->fpga_home_connect_device_ = VaporView::SkyDeviceId::All;
        state_->fpga_remote_request_id_ = 0;
        if (response.error_code != VaporView::CommandErrorCode::Ok) state_->fpga_home_raw_start_pending_ = false;
        const bool discovery = state_->fpga_remote_operation_ == "discover";
        QJsonObject status;
        const bool statusParsed = VaporView::FpgaRemote::parseStatus(response.payload, status);
        if (statusParsed) {
            receiveRemoteFpgaStatus(status);
            if(response.error_code==VaporView::CommandErrorCode::Ok && request.value("op")=="sensor"
                && request.value("enabled").toBool() && isRemoteSkyMode())
                confirmFpgaSensorDraft(quint16(request.value("source").toInt()),homeFpgaRegisters());
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
    state_->fpga_remote_control_ = request;
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
    if (state_->fpga_sources_.contains(0x40)) state_->current_ptb_ = VaporView::PtbData{};
    if (isFpgaRelayPath()) state_->current_hmp_ = VaporView::HmpData{};
    state_->internal_environment_.clear();state_->internal_environment_times_.clear();
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

void MainWindow::updateInternalEnvironment(const VaporView::FpgaSensor::Reading &reading)
{
    refreshInternalEnvironment();
    if ((reading.source == 0x43 && reading.kind == VaporView::FpgaSensor::SensorKind::Bmp390)
        || (reading.source == 0x44 && reading.kind == VaporView::FpgaSensor::SensorKind::Sht45)) {
        state_->internal_environment_[reading.source] = reading;
        state_->internal_environment_times_[reading.source] = std::chrono::steady_clock::now();
        refreshInternalEnvironment();
    }
}

void MainWindow::refreshInternalEnvironment()
{
    if (!state_->internal_environment_lbl_) return;
    const int context=(isRemoteSkyMode()?1:0)+(isUiTestMode()?2:0)+(isFpgaRelayPath()?0:4);
    if (context!=state_->internal_environment_context_) {
        state_->internal_environment_.clear();state_->internal_environment_times_.clear();
        state_->internal_environment_context_=context;
    }
    const auto registers = homeFpgaRegisters();
    const auto enabled=[&](quint16 source) {
        if(isFpgaRelayPath())return !registers.isEmpty()&&(registers.value(0x6004+(source-0x40)*0x100)&1)!=0;
        const auto id=source==0x43?VaporView::SkyDeviceId::Bmp390:VaporView::SkyDeviceId::Sht45;
        return homeDeviceActionState(id)==VaporView::DeviceState::Connected;
    };
    if(isUiTestMode()&&!isFpgaRelayPath()&&state_->ui_test_model_->scenario()!=VaporView::Ground::Devices::UiTestScenario::DataStalled) {
        const double phase=(QDateTime::currentMSecsSinceEpoch()-state_->ui_test_started_ms_)/1000.0;
        for(quint16 source:{0x43,0x44})if(enabled(source)) {
            VaporView::FpgaSensor::Reading sample;sample.source=source;
            sample.kind=source==0x43?VaporView::FpgaSensor::SensorKind::Bmp390:VaporView::FpgaSensor::SensorKind::Sht45;
            sample.validity={true,true,true,true};
            if(source==0x43)sample.pressurePa=101500+20*std::sin(phase);
            else{sample.temperatureC=24+std::sin(phase);sample.humidityPct=45+std::sin(phase);}
            state_->internal_environment_[source]=sample;state_->internal_environment_times_[source]=std::chrono::steady_clock::now();
        }
    }
    for(quint16 source:{0x43,0x44})if(!enabled(source)) {
        state_->internal_environment_.remove(source);state_->internal_environment_times_.remove(source);
    }
    const auto value = [&](quint16 source, std::optional<double> VaporView::FpgaSensor::Reading::*field, double scale) {
        const auto reading = state_->internal_environment_.value(source);
        const quint32 base = 0x6000 + (source - 0x40) * 0x100;
        const auto age = std::chrono::steady_clock::now() - state_->internal_environment_times_.value(source);
        const auto expiry = std::chrono::milliseconds(isFpgaRelayPath()
            ?qMax<quint64>(4000,quint64(registers.value(base+0x14,1000))*3):4000);
        const auto number = reading.*field;
        if (!enabled(source) || age >= expiry || !reading.validity.structure
            || !reading.validity.crc || !reading.validity.deviceOnline || !reading.validity.measurement
            || !number || !std::isfinite(*number)) return QStringLiteral("—");
        return QString::number(*number*scale, 'f', 2);
    };
    const auto pressure = value(0x43,&VaporView::FpgaSensor::Reading::pressurePa,0.01);
    const auto temperature = value(0x44,&VaporView::FpgaSensor::Reading::temperatureC,1);
    const auto humidity = value(0x44,&VaporView::FpgaSensor::Reading::humidityPct,1);
    state_->internal_environment_lbl_->setText((state_->is_english_
        ? QStringLiteral("Internal pressure: %1 hPa    Internal temperature: %2 °C    Internal humidity: %3 %RH")
        : QStringLiteral("内部气压：%1 hPa    内部温度：%2 °C    内部湿度：%3 %RH")).arg(pressure,temperature,humidity));
    state_->internal_environment_lbl_->setToolTip(state_->is_english_
        ? QStringLiteral("BMP390 pressure; SHT45 temperature/humidity. Environmental PTB210/HMP data and session fields are separate.")
        : QStringLiteral("BMP390 内部气压；SHT45 内部温湿度。环境数据及 session 字段继续使用 PTB210/HMP。"));
}
