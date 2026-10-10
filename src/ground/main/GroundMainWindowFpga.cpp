#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/devices/UiTestDataModel.h"
#include <QFileDialog>
#include <QJsonDocument>
#include "FpgaTelemetry.h"

#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
VaporView::Ground::Devices::RemoteSkyController *MainWindow::testRemoteSkyController()
{
    return state_->remote_sky_controller_.get();
}
#endif

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
    controller->moveToThread(state_->fpga_thread_);
    connect(state_->fpga_thread_, &QThread::finished, controller, &QObject::deleteLater);
    const auto dispatch = [this, controller](const QJsonObject& request, std::function<void()> local) {
        if (isRemoteSkyMode()) sendRemoteFpgaControl(request);
        else QMetaObject::invokeMethod(controller, std::move(local), Qt::QueuedConnection);
    };
    const auto blockUiTest = [this]() {
        if (!isUiTestMode()) return false;
        state_->fpga_page_->appendDiagnostic(state_->is_english_
            ? QStringLiteral("[UI test] FPGA command ignored; hardware writes are disabled.")
            : QStringLiteral("[界面测试] FPGA 操作已忽略；界面测试不会写入硬件。"));
        return true;
    };

    connect(page, &FpgaControlPage::connectRequested, this, [this, controller](const QString &locator, const QString &backend) {
        if (isUiTestMode())
        {
            state_->fpga_page_->appendDiagnostic(state_->is_english_
                ? QStringLiteral("[UI test] FPGA connect ignored; hardware writes are disabled.")
                : QStringLiteral("[界面测试] FPGA 连接已忽略；界面测试不会访问硬件。"));
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
    connect(page, &FpgaControlPage::disconnectRequested, this, [dispatch, controller, blockUiTest] {
        if (blockUiTest()) return;
        dispatch({{"op", "disconnect"}}, [controller] { controller->disconnectDevice(); });
    });
    connect(page, &FpgaControlPage::applyRequested, this, [dispatch, controller, blockUiTest](const FpgaControlConfig& config) {
        if (blockUiTest()) return;
        dispatch({{"op", "configure"}, {"configuration", config.toJson()}}, [controller, config] { controller->applyConfiguration(config); });
    });
    connect(page, &FpgaControlPage::configurationChanged, this, [this, controller](const FpgaControlConfig &config) {
        if (isUiTestMode()) return;
        if (isRemoteSkyMode())
        {
            state_->fpga_remote_config_ = config;
            QSettings settings = VaporView::applicationConfigSettings();
            VaporView::setPersistentSetting(settings, QStringLiteral("Fpga/remoteDesiredConfiguration"), QJsonDocument(config.toJson()).toJson(QJsonDocument::Compact));
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
    });
    connect(page, &FpgaControlPage::refreshRequested, this, [dispatch, controller, blockUiTest] {
        if (blockUiTest()) return;
        dispatch({{"op", "refresh"}}, [controller] { controller->refresh(); });
    });
    connect(page, &FpgaControlPage::acquisitionRequested, this, [dispatch, controller, blockUiTest](bool enabled) {
        if (blockUiTest()) return;
        dispatch({{"op", "acquisition"}, {"enabled", enabled}}, [controller, enabled] { controller->setAcquisition(enabled); });
    });
    connect(page, &FpgaControlPage::waveformRequested, this, [dispatch, controller, blockUiTest](int channel, bool enabled) {
        if (blockUiTest()) return;
        dispatch({{"op", "wms"}, {"channel", channel}, {"enabled", enabled}}, [controller, channel, enabled] { controller->setWaveform(channel, enabled); });
    });
    connect(page, &FpgaControlPage::dacRequested, this, [dispatch, controller, blockUiTest](int channel, bool enabled) {
        if (blockUiTest()) return;
        dispatch({{"op", "dac"}, {"channel", channel}, {"enabled", enabled}}, [controller, channel, enabled] { controller->setDac(channel, enabled); });
    });
    connect(page, &FpgaControlPage::sensorEnableRequested, this, [dispatch, controller, blockUiTest](quint16 source, bool enabled) {
        if (blockUiTest()) return;
        dispatch({{"op", "sensor"}, {"source", source}, {"enabled", enabled}}, [controller, source, enabled] { controller->setSensorEnabled(source, enabled); });
    });
    connect(page, &FpgaControlPage::rawRequested, this, [dispatch, controller, blockUiTest](bool enabled) {
        if (blockUiTest()) return;
        dispatch({{"op", "raw"}, {"enabled", enabled}}, [controller, enabled] { controller->setRawEnabled(enabled); });
    });
    connect(page, &FpgaControlPage::setTemperatureRequested, this, [dispatch, controller, blockUiTest](double celsius) {
        if (blockUiTest()) return;
        dispatch({{"op", "temperature"}, {"celsius", celsius}}, [controller, celsius] { controller->setTemperature(celsius); });
    });
    connect(page, &FpgaControlPage::recordingRequested, this, [this](bool enable) {
        if (isUiTestMode())
        {
            state_->fpga_page_->appendDiagnostic(state_->is_english_
                ? QStringLiteral("[UI test] FPGA recording command ignored; simulated recording remains active.")
                : QStringLiteral("[界面测试] FPGA 记录操作已忽略；模拟记录保持运行。"));
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
        if (!isRemoteSkyMode()) page->setHardwareValues(values);
    }, Qt::QueuedConnection);
    connect(controller, &Controller::logRecordGenerated, this, [page](const VaporView::LogRecord &record) {
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
        state_->fpga_remote_request_id_ = 0;
        QJsonObject status;
        if (VaporView::FpgaRemote::parseStatus(response.payload, status)) receiveRemoteFpgaStatus(status);
        page->appendDiagnostic(response.error_code == VaporView::CommandErrorCode::Ok
            ? (state_->is_english_ ? QStringLiteral("Sky FPGA operation confirmed by final readback.") : QStringLiteral("天空端 FPGA 操作已由最终读回确认。"))
            : (state_->is_english_ ? QStringLiteral("Sky FPGA operation failed: ") : QStringLiteral("天空端 FPGA 操作失败：")) + response.error_message);
        updateFpgaPageBackend();
    });
    connect(remote, &Remote::deviceOperationRejected, this, [this, page](quint32 requestId, const VaporView::CommandAck& ack) {
        if (requestId != state_->fpga_remote_request_id_) return;
        state_->fpga_remote_request_id_ = 0;
        page->appendDiagnostic((state_->is_english_ ? QStringLiteral("Sky rejected FPGA command: ") : QStringLiteral("天空端拒绝 FPGA 命令：")) + VaporView::commandErrorCodeText(ack.error_code, state_->is_english_));
        updateFpgaPageBackend();
    });
    connect(remote, &Remote::deviceOperationTimedOut, this, [this, page](quint32 requestId) {
        if (requestId != state_->fpga_remote_request_id_) return;
        state_->fpga_remote_request_id_ = 0;
        page->appendDiagnostic(state_->is_english_ ? QStringLiteral("FPGA command timed out; execution is unknown. Read status before issuing another write; no automatic retry.")
            : QStringLiteral("FPGA 命令超时，执行结果未知。再次写入前请读取状态；系统不会自动重发。"));
        updateFpgaPageBackend();
    });
    connect(remote, &Remote::linkOpenChanged, this, [this](bool open) {
        if (!open) {
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
    QString error;
    if (!remote || !remote->isOpen()) {
        state_->fpga_page_->appendDiagnostic(state_->is_english_ ? QStringLiteral("Connect to SkyCore via local IPC or the telemetry link first.")
            : QStringLiteral("请先通过本机 IPC 或数传链路连接 SkyCore。"));
        updateFpgaPageBackend();
        return;
    }
    if (state_->fpga_remote_request_id_) return;
    const auto op = request.value("op").toString();
    if (op != "connect" && op != "disconnect" && op != "refresh"
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
    state_->fpga_remote_request_id_ = remote->sendFpgaControl(request);
    updateFpgaPageBackend();
}

void MainWindow::receiveRemoteFpgaStatus(const QJsonObject& status)
{
    const auto configuration = status.value("configuration").toObject();
    const bool configurationChanged = configuration != state_->fpga_remote_status_.value("configuration").toObject();
    state_->fpga_remote_status_ = status;
    state_->fpga_remote_status_time_ = std::chrono::steady_clock::now();
    if (configurationChanged) {
        state_->fpga_remote_config_ = FpgaControlConfig::fromJson(configuration);
        if (isRemoteSkyMode()) state_->fpga_page_->setConfiguration(state_->fpga_remote_config_);
    }
    if (isRemoteSkyMode()) {
        QMap<quint32, quint32> values;
        const auto registers = status.value("registers").toObject();
        for (auto it = registers.begin(); it != registers.end(); ++it) values.insert(it.key().toUInt(), quint32(it.value().toDouble()));
        state_->fpga_page_->setHardwareValues(values);
        updateFpgaPageBackend();
    }
}

void MainWindow::updateFpgaPageBackend()
{
    if (!state_->fpga_page_) return;
    if (isUiTestMode() && state_->ui_test_model_)
    {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - state_->ui_test_started_ms_;
        const auto scenario = state_->ui_test_model_->scenario();
        const bool stalled = scenario == VaporView::Ground::Devices::UiTestScenario::DataStalled;
        state_->fpga_page_->setUiTestState(
            true, stalled,
            scenario == VaporView::Ground::Devices::UiTestScenario::PartialFailure, elapsed);
        return;
    }
    if (!isRemoteSkyMode()) {
        state_->fpga_page_->setTransportConnected(state_->fpga_connected_);
        state_->fpga_page_->setConnectionState(state_->fpga_ready_, state_->fpga_busy_, state_->is_english_
            ? QStringLiteral("Local USB (bench mode)") : QStringLiteral("本机 USB（调试模式）"));
        return;
    }
    const bool link = state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen();
    const bool fresh = link && !state_->fpga_remote_status_.isEmpty()
        && std::chrono::steady_clock::now() - state_->fpga_remote_status_time_ < std::chrono::seconds(4);
    const auto& status = state_->fpga_remote_status_;
    const QString detail = fresh ? QStringLiteral("SkyCore USB · ") + status.value("detail").toString()
        : state_->is_english_ ? QStringLiteral("Connect local IPC or telemetry; enable fpga.enabled in Sky config. Link closure leaves Sky acquisition running.")
                             : QStringLiteral("请连接本机 IPC 或数传，并在天空配置启用 fpga.enabled。此链路断开不停止天空端采集。");
    state_->fpga_page_->setTransportConnected(fresh && status.value("connected").toBool());
    state_->fpga_page_->setConnectionState(fresh && status.value("ready").toBool(),
        state_->fpga_remote_request_id_ != 0 || (fresh && status.value("busy").toBool()), detail);
    state_->fpga_page_->setRecordingState(fresh && status.value("recording_state").toInt() == 1,
        fresh ? status.value("session_directory").toString() + (state_->is_english_ ? QStringLiteral("\nSky FPGA archived: ") : QStringLiteral("\n天空端 FPGA 归档：")) + status.value("archived_records").toString()
              : (state_->is_english_ ? QStringLiteral("Sky recording status unavailable") : QStringLiteral("天空端记录状态未知")));
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
