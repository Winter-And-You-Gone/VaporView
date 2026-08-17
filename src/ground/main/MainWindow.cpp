#include "ground/main/GroundMainWindowImplementation.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , state_(std::make_unique<MainWindowState>())
{
    VaporView::LogService::withCurrentInstance([this](VaporView::LogService& logService) {
        connect(&logService, &VaporView::LogService::recordPublished, this,
                [this](const VaporView::LogRecord& record) {
                    if (!state_->recording_service_->isSessionOpen() ||
                        record.category == QStringLiteral("ui.progress") ||
                        record.fields.value(QStringLiteral("session_sink_failure")).toBool())
                    {
                        return;
                    }
                    const bool errorRecord = record.level >= VaporView::LogLevel::Error;
                    if (!state_->recording_service_->appendEvent(
                            VaporView::logLevelName(record.level).toLower(), record.message))
                    {
                        VaporView::LogService::withCurrentInstance([&](VaporView::LogService& logService) {
                            logService.publish(VaporView::LogLevel::Error,
                                               QStringLiteral("Ground"),
                                               QStringLiteral("session.write"),
                                               QStringLiteral("无法从会话日志接收器写入 event_log.csv。"),
                                               {{QStringLiteral("session_sink_failure"), true},
                                                {QStringLiteral("event"), QStringLiteral("session_event_log_append_failed")},
                                                {QStringLiteral("error_code"), QStringLiteral("SESSION_EVENT_LOG_APPEND_FAILED")},
                                                {QStringLiteral("source"), record.source},
                                                {QStringLiteral("category"), record.category}});
                        });
                    }
                    if (errorRecord && !state_->recording_service_->appendError(record.message))
                    {
                        VaporView::LogService::withCurrentInstance([&](VaporView::LogService& logService) {
                            logService.publish(VaporView::LogLevel::Error,
                                               QStringLiteral("Ground"),
                                               QStringLiteral("session.write"),
                                               QStringLiteral("无法从会话日志接收器写入 error_log.txt。"),
                                               {{QStringLiteral("session_sink_failure"), true},
                                                {QStringLiteral("event"), QStringLiteral("session_error_log_append_failed")},
                                                {QStringLiteral("error_code"), QStringLiteral("SESSION_ERROR_LOG_APPEND_FAILED")},
                                                {QStringLiteral("source"), record.source},
                                                {QStringLiteral("category"), record.category}});
                        });
                    }
                },
                Qt::DirectConnection);
        connect(&logService, &VaporView::LogService::recordPublished, this,
                [this](const VaporView::LogRecord& record) {
                    if (record.category == QStringLiteral("ui.progress"))
                    {
                        return;
                    }
                    enqueueUiLogRecord(record);
                    state_->has_inline_progress_log_ = false;
                });
    });
    setWindowFlags(Qt::Window |
                   Qt::FramelessWindowHint |
                   Qt::WindowMinimizeButtonHint |
                   Qt::WindowMaximizeButtonHint |
                   Qt::WindowCloseButtonHint);
    setProperty(kMainWindowProperty, true);

    const double currentPointSize = qApp->font().pointSizeF();
    state_->base_font_point_size_ = currentPointSize > 0.0 ? currentPointSize : 10.0;

    VaporView::migrateLegacyApplicationConfig();
    QSettings userSettings("VaporView", "MainWindow");
    QSettings applicationSettings = VaporView::applicationConfigSettings();
    applicationSettings.beginGroup(QStringLiteral("MainWindow"));
    state_->font_scale_percent_ = userSettings.value(
        "font_scale_percent",
        VaporView::defaultFontScalePercentForScreen(this)).toInt();
    if (state_->font_scale_percent_ < 60 || state_->font_scale_percent_ > 180)
    {
        state_->font_scale_percent_ = 100;
    }
    state_->dark_theme_enabled_ = userSettings.value("dark_theme_enabled", false).toBool();
    state_->log_view_mode_ = VaporView::Ground::Main::uiLogViewModeFromSetting(
        userSettings.value(QStringLiteral("log_view_mode"), QStringLiteral("attention")).toString());
    state_->log_auto_follow_enabled_ = userSettings.value(QStringLiteral("log_auto_follow"), true).toBool();
    state_->log_hide_source_category_enabled_ =
        userSettings.value(QStringLiteral("log_hide_source_category"), false).toBool();
    if (qApp)
    {
        qApp->setProperty(kAppDarkThemeProperty, state_->dark_theme_enabled_);
    }
    state_->recording_directory_ = configuredRecordingDirectory();
    state_->recording_export_rate_hz_ = applicationSettings.value("recording_export_rate_hz", 20).toInt();
    if (state_->recording_export_rate_hz_ < 1 || state_->recording_export_rate_hz_ > 200)
    {
        state_->recording_export_rate_hz_ = 20;
    }
    state_->imu_recording_rate_hz_ = applicationSettings.value("imu_recording_rate_hz", 0).toInt();
    if (state_->imu_recording_rate_hz_ < 0 || state_->imu_recording_rate_hz_ > 1000)
    {
        state_->imu_recording_rate_hz_ = 0;
    }
    state_->waveform_recording_rate_hz_ = 0;

    state_->recording_service_->setSensorSnapshotProvider([this]() {
        VaporView::Ground::Session::GroundSensorSnapshot snapshot;
        const CollectorSnapshot collectors = snapshotCollectors();
        const auto now = std::chrono::steady_clock::now();
        auto isFresh = [now](auto *collector, const auto& sample) {
            if (!collector || sample.timestamp == std::chrono::steady_clock::time_point{})
            {
                return false;
            }
            const int rate = std::max(1, collector->getSampleRate());
            const int timeoutMs = std::max(250, static_cast<int>(std::ceil(3000.0 / rate)));
            const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - sample.timestamp).count();
            return ageMs >= 0 && ageMs <= timeoutMs;
        };

        if (collectors.epsilon)
        {
            snapshot.epsilon = collectors.epsilon->getLatestData();
            snapshot.hasEpsilon = isFresh(collectors.epsilon.get(), snapshot.epsilon);
        }
        if (collectors.ptb)
        {
            snapshot.ptb = collectors.ptb->getLatestData();
            snapshot.hasPtb = isFresh(collectors.ptb.get(), snapshot.ptb);
        }
        if (collectors.hmp)
        {
            snapshot.hmp = collectors.hmp->getLatestData();
            snapshot.hasHmp = isFresh(collectors.hmp.get(), snapshot.hmp);
        }
        if (collectors.lidar)
        {
            snapshot.lidar = collectors.lidar->getLatestData();
            snapshot.hasLidar = isFresh(collectors.lidar.get(), snapshot.lidar);
        }
        return snapshot;
    });
    state_->recording_service_->setStatusCallback([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            updateRecordingStatusLabel();
        }, Qt::QueuedConnection);
    });
    state_->recording_service_->setWarningCallback([this](
        VaporView::Ground::Session::GroundRecordingWarning warning,
        quint64 value) {
        QMetaObject::invokeMethod(this, [this, warning, value]() {
            using Warning = VaporView::Ground::Session::GroundRecordingWarning;
            switch (warning)
            {
            case Warning::RawFormatDocumentCopyFailed:
                publishGroundLog(VaporView::LogLevel::Warning,
                                 QStringLiteral("session.write"),
                                 QStringLiteral("raw_format_document_copy_failed"),
                                 QStringLiteral("未能将统一 raw DAT 格式说明复制到当前会话目录。"),
                                 {{QStringLiteral("error_code"), QStringLiteral("RAW_FORMAT_DOCUMENT_COPY_FAILED")},
                                  {QStringLiteral("ui_dedupe_key"), QStringLiteral("session:raw_format_document_copy_failed")}});
                break;
            case Warning::DeviceConfigSnapshotFailed:
                publishGroundLog(VaporView::LogLevel::Warning,
                                 QStringLiteral("session.write"),
                                 QStringLiteral("device_config_snapshot_failed"),
                                 QStringLiteral("保存设备配置快照失败。"),
                                 {{QStringLiteral("error_code"), QStringLiteral("DEVICE_CONFIG_SNAPSHOT_FAILED")},
                                  {QStringLiteral("ui_dedupe_key"), QStringLiteral("session:device_config_snapshot_failed")}});
                break;
            case Warning::MetadataUpdateFailed:
                publishGroundLog(VaporView::LogLevel::Warning,
                                 QStringLiteral("session.write"),
                                 QStringLiteral("session_metadata_update_failed"),
                                 QStringLiteral("更新会话元数据失败。"),
                                 {{QStringLiteral("error_code"), QStringLiteral("SESSION_METADATA_UPDATE_FAILED")},
                                  {QStringLiteral("ui_dedupe_key"), QStringLiteral("session:metadata_update_failed")}});
                break;
            case Warning::TcpQueueBacklog:
                publishGroundLog(VaporView::LogLevel::Warning,
                                 QStringLiteral("session.write"),
                                 QStringLiteral("tcp_raw_recording_queue_backlog"),
                                 QStringLiteral("TCP 原始记录队列出现积压，磁盘写入可能慢于数据流。"),
                                 {{QStringLiteral("backlog_mib"), static_cast<qulonglong>(value)},
                                  {QStringLiteral("reason_code"), QStringLiteral("BACKPRESSURE")},
                                  {QStringLiteral("ui_dedupe_key"), QStringLiteral("session:tcp_raw_queue_backlog")}});
                break;
            case Warning::TcpQueueFull:
                publishGroundLog(VaporView::LogLevel::Warning,
                                 QStringLiteral("session.write"),
                                 QStringLiteral("tcp_raw_recording_queue_full"),
                                 QStringLiteral("TCP 原始记录队列已满，正在丢弃新到原始帧以保持链路响应。"),
                                 {{QStringLiteral("queue_mib"), static_cast<qulonglong>(value)},
                                  {QStringLiteral("error_code"), QStringLiteral("TCP_RAW_QUEUE_FULL")},
                                  {QStringLiteral("ui_dedupe_key"), QStringLiteral("session:tcp_raw_queue_full")}});
                break;
            case Warning::TcpFramesDropped:
                publishGroundLog(VaporView::LogLevel::Warning,
                                 QStringLiteral("session.write"),
                                 QStringLiteral("tcp_raw_frames_dropped"),
                                 QStringLiteral("TCP 原始记录队列已满，已丢弃部分帧。"),
                                 {{QStringLiteral("dropped_frames"), static_cast<qulonglong>(value)},
                                  {QStringLiteral("error_code"), QStringLiteral("TCP_RAW_FRAMES_DROPPED")},
                                  {QStringLiteral("ui_dedupe_key"), QStringLiteral("session:tcp_raw_frames_dropped")}});
                break;
            case Warning::DeviceRawQueueBacklog:
                publishGroundLog(VaporView::LogLevel::Warning,
                                 QStringLiteral("session.write"),
                                 QStringLiteral("device_raw_recording_queue_backlog"),
                                 QStringLiteral("设备原始记录队列出现积压，磁盘写入可能慢于串口数据流。"),
                                 {{QStringLiteral("backlog_mib"), static_cast<qulonglong>(value)},
                                  {QStringLiteral("reason_code"), QStringLiteral("BACKPRESSURE")},
                                  {QStringLiteral("ui_dedupe_key"), QStringLiteral("session:device_raw_queue_backlog")}});
                break;
            case Warning::DeviceRawQueueFull:
                publishGroundLog(VaporView::LogLevel::Warning,
                                 QStringLiteral("session.write"),
                                 QStringLiteral("device_raw_recording_queue_full"),
                                 QStringLiteral("设备原始记录队列已满，正在丢弃新到原始帧以保持采集线程响应。"),
                                 {{QStringLiteral("queue_mib"), static_cast<qulonglong>(value)},
                                  {QStringLiteral("error_code"), QStringLiteral("DEVICE_RAW_QUEUE_FULL")},
                                  {QStringLiteral("ui_dedupe_key"), QStringLiteral("session:device_raw_queue_full")}});
                break;
            case Warning::DeviceRawFramesDropped:
                publishGroundLog(VaporView::LogLevel::Warning,
                                 QStringLiteral("session.write"),
                                 QStringLiteral("device_raw_frames_dropped"),
                                 QStringLiteral("设备原始记录队列已满，已丢弃部分帧。"),
                                 {{QStringLiteral("dropped_frames"), static_cast<qulonglong>(value)},
                                  {QStringLiteral("error_code"), QStringLiteral("DEVICE_RAW_FRAMES_DROPPED")},
                                  {QStringLiteral("ui_dedupe_key"), QStringLiteral("session:device_raw_frames_dropped")}});
                break;
            }
        }, Qt::QueuedConnection);
    });

    VaporView::Ground::Devices::LocalConnectionCallbacks connectionCallbacks;
    connectionCallbacks.log = [this](const VaporView::Ground::Devices::LocalConnectionLogEntry& entry) {
        QMetaObject::invokeMethod(this, [this, entry]() {
            publishGroundLog(entry.level,
                             entry.category,
                             entry.event,
                             entry.message,
                             entry.fields);
        }, Qt::QueuedConnection);
    };
    connectionCallbacks.finished = [this](bool connected) {
        QMetaObject::invokeMethod(this, [this, connected]() {
            if (state_->local_connection_coordinator_)
            {
                state_->local_connection_coordinator_->serialFinished(connected);
            }
        }, Qt::QueuedConnection);
    };
    connectionCallbacks.dataReady = [this](VaporView::Ground::Devices::LocalDeviceKind device) {
        const quint32 bit = 1u << static_cast<unsigned>(device);
        if ((local_data_update_pending_mask_.fetch_or(bit, std::memory_order_acq_rel) & bit) != 0)
        {
            return;
        }
        QMetaObject::invokeMethod(this, [this, device, bit]() {
            local_data_update_pending_mask_.fetch_and(~bit, std::memory_order_acq_rel);
            using Device = VaporView::Ground::Devices::LocalDeviceKind;
            switch (device)
            {
            case Device::Epsilon: onEpsilonDataReady(); break;
            case Device::Ptb: onPtbDataReady(); break;
            case Device::Hmp: onHmpDataReady(); break;
            case Device::Lidar: onLidarDataReady(); break;
            case Device::TemperatureController: onTemperatureControllerDataReady(); break;
            case Device::Ai8TemperatureController: onAi8TemperatureControllerDataReady(); break;
            }
        }, Qt::QueuedConnection);
    };
    connectionCallbacks.rawEpsilonFrame = [this](quint64 timestampUs,
                                                   quint8 packetId,
                                                   quint8 serialNumber,
                                                   const void *payload,
                                                   size_t size) {
        state_->recording_service_->recordRawEpsilonFrame(timestampUs, packetId, serialNumber, payload, size);
    };
    connectionCallbacks.rawPtbResponse = [this](quint64 timestampUs, const void *payload, size_t size) {
        state_->recording_service_->recordRawPtbResponse(timestampUs, payload, size);
    };
    connectionCallbacks.rawHmpResponse = [this](quint64 timestampUs, const void *payload, size_t size) {
        state_->recording_service_->recordRawHmpResponse(timestampUs, payload, size);
    };
    connectionCallbacks.rawLidarFrame = [this](quint64 timestampUs,
                                                quint16 protocol,
                                                const void *payload,
                                                size_t size) {
        state_->recording_service_->recordRawLidarFrame(timestampUs, protocol, payload, size);
    };
    state_->local_connection_controller_->setCallbacks(std::move(connectionCallbacks));

    RecordingScheduleController::Hooks scheduleHooks;
    scheduleHooks.startRecording = [this]() {
        QString failureReason;
        const bool started = tryStartScheduledRecording(&failureReason);
        return RecordingScheduleController::StartResult{started, failureReason};
    };
    scheduleHooks.stopRecording = [this]() {
        return tryStopScheduledRecording();
    };
    scheduleHooks.sessionOpen = [this]() {
        return scheduledRecordingSessionOpen();
    };
    scheduleHooks.log = [this](const RecordingScheduleController::LogEntry& entry) {
        publishGroundLog(entry.level,
                         QStringLiteral("session.recording"),
                         entry.event,
                         entry.message,
                         entry.fields);
    };
    scheduleHooks.stateChanged = [this]() {
        if (state_->scheduled_recording_timer_)
        {
            state_->recording_schedule_controller_->isActive()
                ? state_->scheduled_recording_timer_->start()
                : state_->scheduled_recording_timer_->stop();
        }
        updateScheduledRecordingAction();
        updateRecordingStatusLabel();
    };
    state_->recording_schedule_controller_->setHooks(std::move(scheduleHooks));

    state_->restoring_persistent_settings_ = true;

    loadModernStyleSheet();

    setupMenuBar();
    setupToolBar();
    state_->remote_sky_controller_ = std::make_unique<VaporView::Ground::Devices::RemoteSkyController>();
    state_->remote_sky_controller_->setObjectName(QStringLiteral("remoteSkyController"));
    setProperty("remoteSkyController",
                QVariant::fromValue(static_cast<QObject *>(state_->remote_sky_controller_.get())));
    setupCentralWidget();
    VaporView::Ground::Devices::Ai8DeviceSession::LocalAdapter ai8LocalAdapter;
    ai8LocalAdapter.readPage = [controller = state_->local_connection_controller_.get()](
                                   VaporView::Ai8TemperatureControllerProtocol::Page page,
                                   const VaporView::Ai8TemperatureControllerProtocol::Selection& selection) {
        return controller->readAi8Page(page, selection);
    };
    ai8LocalAdapter.writePage = [controller = state_->local_connection_controller_.get()](
                                    const VaporView::Ai8TemperatureControllerProtocol::PageData& pageData) {
        return controller->writeAi8Page(pageData);
    };
    state_->ai8_device_session_ =
        std::make_unique<VaporView::Ground::Devices::Ai8DeviceSession>(
            std::move(ai8LocalAdapter), state_->remote_sky_controller_.get());
    state_->ai8_device_session_->setEnglish(state_->is_english_);
    connect(state_->ai8_device_session_.get(),
            &VaporView::Ground::Devices::Ai8DeviceSession::availabilityChanged,
            this, &MainWindow::onAi8SessionAvailabilityChanged);
    connect(state_->ai8_device_session_.get(),
            &VaporView::Ground::Devices::Ai8DeviceSession::operationStarted,
            this, &MainWindow::onAi8SessionOperationStarted);
    connect(state_->ai8_device_session_.get(),
            &VaporView::Ground::Devices::Ai8DeviceSession::operationFinished,
            this, &MainWindow::onAi8SessionOperationFinished);
    connect(state_->ai8_device_session_.get(),
            &VaporView::Ground::Devices::Ai8DeviceSession::pageDataAvailable,
            this, &MainWindow::onAi8SessionPageDataAvailable);
    VaporView::Ground::Devices::Rd105DeviceSession::LocalAdapter rd105LocalAdapter;
    rd105LocalAdapter.sendCommand =
        [controller = state_->local_connection_controller_.get()](
            VaporView::CommandId command,
            const VaporView::TemperatureControllerCommand& payload) {
            return controller->sendTemperatureCommand(command, payload);
        };
    state_->rd105_device_session_ =
        std::make_unique<VaporView::Ground::Devices::Rd105DeviceSession>(
            std::move(rd105LocalAdapter), state_->remote_sky_controller_.get());
    state_->rd105_device_session_->setEnglish(state_->is_english_);
    connect(state_->rd105_device_session_.get(),
            &VaporView::Ground::Devices::Rd105DeviceSession::availabilityChanged,
            this, &MainWindow::onRd105SessionAvailabilityChanged);
    connect(state_->rd105_device_session_.get(),
            &VaporView::Ground::Devices::Rd105DeviceSession::operationStarted,
            this, &MainWindow::onRd105SessionOperationStarted);
    connect(state_->rd105_device_session_.get(),
            &VaporView::Ground::Devices::Rd105DeviceSession::operationFinished,
            this, &MainWindow::onRd105SessionOperationFinished);
    VaporView::Ground::Devices::EpsilonDeviceSession::LocalAdapter epsilonLocalAdapter;
    auto epsilonLogCallback = [this](const VaporView::Ground::EpsilonConfigurationLogEntry& entry) {
        QMetaObject::invokeMethod(this, [this, entry]() mutable {
            publishGroundLog(entry.level,
                             entry.category,
                             entry.event,
                             entry.message,
                             entry.fields);
        }, Qt::QueuedConnection);
    };
    epsilonLocalAdapter.configurePacketRates =
        [epsilonLogCallback](
            const VaporView::EpsilonPacketRatesOperation& operation,
            const VaporView::Ground::EpsilonDeviceOperation& deviceOperation) {
            return VaporView::Ground::EpsilonConfigurationService::configurePacketRates(
                deviceOperation,
                operation.output_rate_hz,
                operation.callback_rate_hz,
                operation.packet_rates,
                operation.packet_rate_signature,
                epsilonLogCallback);
        };
    epsilonLocalAdapter.configureMainAntennaLeverArm =
        [epsilonLogCallback](
            const VaporView::EpsilonMainAntennaLeverArmOperation& operation,
            const VaporView::Ground::EpsilonDeviceOperation& deviceOperation) {
            return VaporView::Ground::EpsilonConfigurationService::applyMainAntennaLeverArm(
                deviceOperation,
                operation.x_m,
                operation.y_m,
                operation.z_m,
                epsilonLogCallback);
        };
    epsilonLocalAdapter.configureRtcmInput =
        [epsilonLogCallback](
            const VaporView::EpsilonRtcmInputOperation& operation,
            const VaporView::Ground::EpsilonDeviceOperation& deviceOperation) {
            return VaporView::Ground::EpsilonConfigurationService::configureRtcmPort(
                deviceOperation,
                operation.device_port_index,
                operation.forward_port,
                operation.forward_baud,
                QString::number(operation.forward_baud),
                epsilonLogCallback);
        };
    state_->epsilon_device_session_ =
        std::make_unique<VaporView::Ground::Devices::EpsilonDeviceSession>(
            std::move(epsilonLocalAdapter), state_->remote_sky_controller_.get());
    state_->epsilon_device_session_->setEnglish(state_->is_english_);
    connect(state_->epsilon_device_session_.get(),
            &VaporView::Ground::Devices::EpsilonDeviceSession::availabilityChanged,
            this, &MainWindow::onEpsilonSessionAvailabilityChanged);
    connect(state_->epsilon_device_session_.get(),
            &VaporView::Ground::Devices::EpsilonDeviceSession::operationStarted,
            this, &MainWindow::onEpsilonSessionOperationStarted);
    connect(state_->epsilon_device_session_.get(),
            &VaporView::Ground::Devices::EpsilonDeviceSession::operationFinished,
            this, &MainWindow::onEpsilonSessionOperationFinished);
    configureLocalConnectionCoordinator();
    setupWindowBorderFrames();
    setupWindowResizeHandles();
    using RemoteSkyController = VaporView::Ground::Devices::RemoteSkyController;
    connect(state_->remote_sky_controller_.get(), &RemoteSkyController::linkOpenChanged,
            this, &MainWindow::onRemoteLinkOpenChanged);
    connect(state_->remote_sky_controller_.get(), &RemoteSkyController::basicTelemetryUpdated,
            this, &MainWindow::onRemoteBasicTelemetryUpdated);
    connect(state_->remote_sky_controller_.get(), &RemoteSkyController::waveformUpdated,
            this, &MainWindow::onRemoteWaveformUpdated);
    connect(state_->remote_sky_controller_.get(), &RemoteSkyController::waveformFeatureUpdated,
            this, &MainWindow::onRemoteWaveformFeatureUpdated);
    connect(state_->remote_sky_controller_.get(), &RemoteSkyController::statusUpdated,
            this, &MainWindow::onRemoteTelemetryStatusUpdated);
    connect(state_->remote_sky_controller_.get(), &RemoteSkyController::temperatureControllerStatusUpdated,
            this, &MainWindow::onRemoteTemperatureControllerStatusUpdated);
    connect(state_->remote_sky_controller_.get(), &RemoteSkyController::ai8TemperatureControllerStatusUpdated,
            this, &MainWindow::onRemoteAi8TemperatureControllerStatusUpdated);
    connect(state_->remote_sky_controller_.get(), &RemoteSkyController::commandAckReceived,
            this, &MainWindow::onRemoteCommandAckReceived);
    if (auto *telemetryService = state_->remote_sky_controller_->telemetryService())
    {
        connect(telemetryService,
                &VaporView::GroundTelemetryService::skyConfigReceived,
                this,
                &MainWindow::onRemoteSkyConfigReceived);
        connect(telemetryService,
                &VaporView::GroundTelemetryService::skyConfigApplyResultReceived,
                this,
                &MainWindow::onRemoteSkyConfigApplyResultReceived);
        connect(telemetryService,
                &VaporView::GroundTelemetryService::serialPortDetectionResultReceived,
                this,
                &MainWindow::onRemoteSerialPortDetectionResult);
    }
    connect(state_->remote_sky_controller_.get(), &RemoteSkyController::commandTimedOut,
            this, [this](VaporView::CommandId commandId, quint16 commandSeq) {
        if (commandId == VaporView::CommandId::GetSkyConfig &&
            commandSeq == state_->remote_sky_config_read_seq_)
        {
            state_->remote_sky_config_loading_ = false;
            clearPendingRemoteWaveTcpConnection();
            setRemoteSkyConfigStatus(state_->is_english_
                ? QStringLiteral("Remote Sky config read timed out.")
                : QStringLiteral("读取天空端配置超时。"),
                true);
            updateRemoteSkyConfigControlsState();
        }
        else if (commandId == VaporView::CommandId::SetSkyConfig &&
                 commandSeq == state_->remote_sky_config_apply_seq_)
        {
            state_->remote_sky_config_applying_ = false;
            clearPendingRemoteWaveTcpConnection();
            state_->remote_sky_config_dirty_ = true;
            setRemoteSkyConfigStatus(state_->is_english_
                ? QStringLiteral("Remote Sky config apply timed out.")
                : QStringLiteral("应用天空端配置超时。"),
                true);
            updateRemoteSkyConfigControlsState();
        }
        else if (commandId == VaporView::CommandId::SaveSkyConfig &&
                 commandSeq == state_->remote_sky_config_save_seq_)
        {
            state_->remote_sky_config_saving_ = false;
            setRemoteSkyConfigStatus(state_->is_english_
                ? QStringLiteral("Remote Sky config save timed out.")
                : QStringLiteral("保存天空端配置超时。"),
                true);
            updateRemoteSkyConfigControlsState();
        }
        else if (commandId == VaporView::CommandId::AutoDetectSerialPorts &&
                 commandSeq == state_->remote_serial_detection_seq_)
        {
            state_->port_detection_in_progress_ = false;
            state_->remote_serial_detection_seq_ = 0;
            setRemoteSkyConfigStatus(state_->is_english_
                ? QStringLiteral("Remote serial-port detection timed out.")
                : QStringLiteral("远程串口自动识别超时。"), true);
            updateConnectionStatus(state_->is_connected_);
        }
        else if (commandId == VaporView::CommandId::SetPeakSearchRange)
        {
            state_->remote_peak_search_commands_.remove(commandSeq);
            if (state_->tcp_wave_panel_)
            {
                state_->tcp_wave_panel_->rejectRemotePeakSearchRange(
                    state_->is_english_ ? QStringLiteral("ACK timed out") : QStringLiteral("ACK 超时"));
            }
        }
        else if (commandId == VaporView::CommandId::EnableWaveformStreaming ||
                 commandId == VaporView::CommandId::DisableWaveformStreaming)
        {
            state_->remote_wave_stream_enable_pending_ = false;
            updateRemoteDeviceButtonText(
                VaporView::SkyDeviceId::WaveTcp,
                state_->remote_sky_controller_->deviceState(VaporView::SkyDeviceId::WaveTcp));
        }
    });
    loadRememberedInputState();
    syncDeviceConfigPageFromHome();
    bindRememberedInputState();
    state_->restoring_persistent_settings_ = false;
    saveRememberedInputState();

    state_->base_minimum_window_size_ = QSize(kMinimumMainWindowWidth, kMinimumMainWindowHeight);
    state_->base_window_size_ = QSize(kDefaultMainWindowWidth, kDefaultMainWindowHeight);
    resize(state_->base_window_size_);
    setMinimumSize(state_->base_minimum_window_size_);

    state_->refresh_timer_ = new QTimer(this);
    connect(state_->refresh_timer_, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    state_->refresh_timer_->start(100);

    state_->scheduled_recording_timer_ = new QTimer(this);
    state_->scheduled_recording_timer_->setInterval(1000);
    state_->scheduled_recording_timer_->setTimerType(Qt::CoarseTimer);
    connect(state_->scheduled_recording_timer_, &QTimer::timeout, this, &MainWindow::onScheduledRecordingTick);

    setEnglish(false);
    applyStyleConfiguration();
    VaporView::centerWindowOnScreen(this);
    rememberNormalWindowGeometry();
    QTimer::singleShot(0, this, [this]() {
        setLogSidePanelToMinimumWidth();
        queueResponsiveHomeLayoutRefresh();
    });

    updateRecordingStatusLabel();
    updateConnectionStatus(false);
    updateSourceModeUi();
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    saveAppSidebarWidth();
    if (state_->rtk_config_dialog_)
    {
        delete state_->rtk_config_dialog_;
        state_->rtk_config_dialog_ = nullptr;
    }
    if (state_->session_viewer_window_)
    {
        delete state_->session_viewer_window_;
        state_->session_viewer_window_ = nullptr;
    }
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (state_->map3d_controller_)
    {
        state_->map3d_controller_->close();
    }
#endif

    if (state_->custom_title_bar_)
    {
        const auto buttons = state_->custom_title_bar_->findChildren<QToolButton *>();
        for (QToolButton *button : buttons)
        {
            if (button)
            {
                button->setDefaultAction(nullptr);
                button->setMenu(nullptr);
            }
        }
        state_->custom_title_bar_->removeEventFilter(this);
        if (state_->custom_title_label_)
        {
            state_->custom_title_label_->removeEventFilter(this);
        }
        if (state_->custom_logo_label_)
        {
            state_->custom_logo_label_->removeEventFilter(this);
        }
    }

    saveRememberedInputState();
    state_->cancel_connection_requested_.store(true);
    if (state_->port_detection_thread_.joinable())
    {
        state_->port_detection_thread_.join();
    }
    stopRecording(false);
    state_->recording_service_->setStatusCallback({});
    state_->recording_service_->setWarningCallback({});
    state_->recording_service_->setSensorSnapshotProvider({});
    stopAllCollectors();
    if (state_->local_connection_coordinator_)
    {
        state_->local_connection_coordinator_->disconnect();
    }
}

bool MainWindow::shouldStartWindowMove(QObject *watched) const
{
    return watched == state_->custom_title_bar_ ||
           watched == state_->custom_title_label_;
}

bool MainWindow::belongsToMainWindow(QWidget *widget) const
{
    for (QWidget *current = widget; current; current = current->parentWidget())
    {
        if (current == this)
        {
            return true;
        }
    }
    return false;
}

void MainWindow::syncMainHoverStateFromCursor()
{
    const QPoint cursorPos = QCursor::pos();
    setCustomLogoHovered(widgetContainsGlobalCursor(state_->custom_logo_label_, cursorPos));

    const QList<QToolButton*> titleButtons = findChildren<QToolButton *>();
    for (QToolButton *button : titleButtons)
    {
        if (!button || !button->property(kTitleBarHoverParticipantProperty).toBool())
        {
            continue;
        }
        setWidgetBooleanProperty(button,
                                 kTitleBarHoverProperty,
                                 widgetContainsGlobalCursor(button, cursorPos));
    }

    const QList<QPushButton*> sidebarButtons = findChildren<QPushButton *>(QStringLiteral("appSidebarButton"));
    for (QPushButton *button : sidebarButtons)
    {
        if (!button || !button->property(kSidebarHoverParticipantProperty).toBool())
        {
            continue;
        }
        setWidgetBooleanProperty(button,
                                 kSidebarHoverProperty,
                                 widgetContainsGlobalCursor(button, cursorPos));
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (isUiTestMode())
    {
        state_->ui_test_application_closing_ = true;
        // Keep the process-wide write barrier enabled through all child and
        // main-window destructors. Several widgets persist state on teardown.
        VaporView::setSettingsWritesSuspended(true);
    }
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (state_->map3d_controller_)
    {
        state_->map3d_controller_->close();
    }
#endif
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ToolTip && showAppTooltip(watched, event, state_->dark_theme_enabled_))
    {
        return true;
    }

    const QEvent::Type eventType = event->type();
    if (handleLocalSerialPortManualEntryEvent(watched, event))
    {
        return true;
    }
    if (handleRemoteSkySerialPortManualEntryEvent(watched, event))
    {
        return true;
    }
    if (eventType == QEvent::KeyPress)
    {
        auto *sidebarButton = qobject_cast<QPushButton *>(watched);
        if (sidebarButton && sidebarButton->objectName() == QStringLiteral("appSidebarButton"))
        {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            const QList<QPushButton *> buttons = {
                state_->home_nav_btn_,
                state_->device_config_nav_btn_,
                state_->temperature_nav_btn_,
                state_->rtk_config_nav_btn_,
            };
            const int currentIndex = buttons.indexOf(sidebarButton);
            if (currentIndex >= 0)
            {
                if (keyEvent->key() == Qt::Key_Return ||
                    keyEvent->key() == Qt::Key_Enter ||
                    keyEvent->key() == Qt::Key_Space)
                {
                    sidebarButton->click();
                    return true;
                }
                const bool previous = keyEvent->key() == Qt::Key_Up ||
                                      keyEvent->key() == Qt::Key_Left;
                const bool next = keyEvent->key() == Qt::Key_Down ||
                                  keyEvent->key() == Qt::Key_Right;
                if (previous || next)
                {
                    const int step = previous ? -1 : 1;
                    const int nextIndex = (currentIndex + step + buttons.size()) % buttons.size();
                    if (QPushButton *target = buttons.at(nextIndex))
                    {
                        target->setFocus(Qt::OtherFocusReason);
                        target->click();
                    }
                    return true;
                }
            }
        }
    }

    if (state_->log_list_view_ &&
        watched == state_->log_list_view_->viewport() &&
        eventType == QEvent::MouseButtonPress)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            const QModelIndex index =
                state_->log_list_view_->indexAt(mouseEvent->position().toPoint());
            const bool selected = index.isValid() &&
                                  state_->log_list_view_->selectionModel() &&
                                  state_->log_list_view_->selectionModel()->isSelected(index);
            state_->log_list_view_->setProperty("vaporViewLogPressedRow",
                                                index.isValid() ? index.row() : -1);
            state_->log_list_view_->setProperty("vaporViewLogPressedWasSelected", selected);
        }
    }

    if (eventType == QEvent::ApplicationActivate ||
        eventType == QEvent::WindowActivate ||
        eventType == QEvent::ActivationChange)
    {
        QTimer::singleShot(0, this, &MainWindow::syncMainHoverStateFromCursor);
    }

    if (auto *hoverWidget = qobject_cast<QWidget *>(watched))
    {
        const bool titleBarHoverParticipant =
            hoverWidget->property(kTitleBarHoverParticipantProperty).toBool();
        const bool sidebarHoverParticipant =
            hoverWidget->property(kSidebarHoverParticipantProperty).toBool();
        if (titleBarHoverParticipant || sidebarHoverParticipant)
        {
            const char *hoverProperty = titleBarHoverParticipant
                ? kTitleBarHoverProperty
                : kSidebarHoverProperty;
            if (isHoverEnterLikeEvent(eventType))
            {
                setWidgetBooleanProperty(hoverWidget, hoverProperty, true);
            }
            else if (isHoverLeaveLikeEvent(eventType))
            {
                setWidgetBooleanProperty(hoverWidget, hoverProperty, false);
            }
        }

        const bool hoverSyncAnchor = hoverWidget == this ||
                                     hoverWidget == state_->custom_title_bar_ ||
                                     hoverWidget == state_->app_sidebar_;
        if ((eventType == QEvent::Enter || eventType == QEvent::MouseMove) &&
            hoverSyncAnchor &&
            belongsToMainWindow(hoverWidget))
        {
            QTimer::singleShot(0, this, &MainWindow::syncMainHoverStateFromCursor);
        }
    }

    if (eventType == QEvent::Leave ||
        eventType == QEvent::MouseButtonPress ||
        eventType == QEvent::Wheel ||
        eventType == QEvent::KeyPress ||
        eventType == QEvent::ApplicationDeactivate ||
        eventType == QEvent::WindowDeactivate)
    {
        hideAppTooltipPopup();
    }

    const bool titleMenuVisible =
        (state_->title_application_panel_ && state_->title_application_panel_->isVisible()) ||
        (state_->title_application_sub_panel_ && state_->title_application_sub_panel_->isVisible()) ||
        (state_->title_application_nested_panel_ && state_->title_application_nested_panel_->isVisible());
    if (titleMenuVisible)
    {
        auto restoreTitleMenuButtonFocus = [this]() {
            if (state_->title_menu_btn_ && state_->title_menu_btn_->isVisible())
            {
                state_->title_menu_btn_->setFocus(Qt::OtherFocusReason);
            }
        };
        auto hideTitleMenu = [this, restoreTitleMenuButtonFocus]() {
            if (state_->title_application_panel_)
            {
                state_->title_application_panel_->hide();
            }
            if (state_->title_application_sub_panel_)
            {
                state_->title_application_sub_panel_->hide();
            }
            if (state_->title_application_nested_panel_)
            {
                state_->title_application_nested_panel_->hide();
            }
            restoreTitleMenuButtonFocus();
        };
        auto isTitleMenuWindow = [this](const QWidget *window) {
            return window &&
                   (window == state_->title_application_panel_ ||
                    window == state_->title_application_sub_panel_ ||
                    window == state_->title_application_nested_panel_);
        };
        // Let activation move between the three top-level menu panels before deciding that the app lost focus.
        auto scheduleTitleMenuClose = [this, hideTitleMenu, isTitleMenuWindow]() {
            QTimer::singleShot(50, this, [hideTitleMenu, isTitleMenuWindow]() {
                const QWidget *activeWindow = QApplication::activeWindow();
                if (QApplication::applicationState() != Qt::ApplicationActive &&
                    !isTitleMenuWindow(activeWindow))
                {
                    hideTitleMenu();
                }
            });
        };
        if (eventType == QEvent::ApplicationDeactivate)
        {
            scheduleTitleMenuClose();
        }
        else if (eventType == QEvent::WindowDeactivate && watched == this)
        {
            QTimer::singleShot(50, this, [this, hideTitleMenu, isTitleMenuWindow]() {
                const QWidget *activeWindow = QApplication::activeWindow();
                const bool switchedToAnotherWindow =
                    activeWindow && activeWindow != this && !isTitleMenuWindow(activeWindow);
                if (QApplication::applicationState() != Qt::ApplicationActive ||
                    switchedToAnotherWindow)
                {
                    hideTitleMenu();
                }
            });
        }
        else if (eventType == QEvent::MouseButtonPress)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPoint globalPos = mouseEvent->globalPosition().toPoint();
            auto containsGlobalPoint = [globalPos](const QWidget *widget) {
                return widget &&
                       widget->isVisible() &&
                       QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()).contains(globalPos);
            };

            const bool insideMenu =
                containsGlobalPoint(state_->title_application_panel_) ||
                containsGlobalPoint(state_->title_application_sub_panel_) ||
                containsGlobalPoint(state_->title_application_nested_panel_);
            const bool insideMenuButton = containsGlobalPoint(state_->title_menu_btn_);

            if (!insideMenu && !insideMenuButton)
            {
                if (state_->title_application_panel_)
                {
                    state_->title_application_panel_->hide();
                }
                if (state_->title_application_sub_panel_)
                {
                    state_->title_application_sub_panel_->hide();
                }
                if (state_->title_application_nested_panel_)
                {
                    state_->title_application_nested_panel_->hide();
                }
                restoreTitleMenuButtonFocus();
            }
        }
    }

    if (watched == state_->custom_logo_label_)
    {
        if (eventType == QEvent::Enter || eventType == QEvent::HoverEnter)
        {
            setCustomLogoHovered(true);
        }
        else if (eventType == QEvent::Leave || eventType == QEvent::HoverLeave)
        {
            setCustomLogoHovered(false);
        }
        else if (eventType == QEvent::MouseButtonPress)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                toggleAppSidebarFromLogo();
                return true;
            }
        }
        else if (eventType == QEvent::MouseButtonDblClick)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                toggleAppSidebarFromLogo();
                return true;
            }
        }
        else if (eventType == QEvent::KeyPress)
        {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Return ||
                keyEvent->key() == Qt::Key_Enter ||
                keyEvent->key() == Qt::Key_Space)
            {
                toggleAppSidebarFromLogo();
                return true;
            }
        }
    }

    if (shouldStartWindowMove(watched))
    {
        if (eventType == QEvent::MouseButtonDblClick)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && !isFullScreen())
            {
                toggleWindowMaximized();
                return true;
            }
        }
        else if (eventType == QEvent::MouseButtonPress)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && windowHandle())
            {
                windowHandle()->startSystemMove();
                return true;
            }
        }
    }

    if (state_->app_layout_splitter_ &&
        watched == state_->app_layout_splitter_->handle(1) &&
        eventType == QEvent::MouseButtonRelease)
    {
        QTimer::singleShot(0, this, &MainWindow::finishAppSidebarResize);
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange)
    {
        QTimer::singleShot(0, this, &MainWindow::syncMainHoverStateFromCursor);
    }
    if (event->type() == QEvent::WindowStateChange)
    {
        if (!isWindowMaximizedForUi())
        {
            rememberNormalWindowGeometry();
        }
        updateWindowControlButtons();
        updateWindowBorderFrames();
        updateWindowResizeHandles();
        queueResponsiveHomeLayoutRefresh();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!isWindowMaximizedForUi())
    {
        rememberNormalWindowGeometry();
    }
    updateWindowControlButtons();
    updateWindowBorderFrames();
    updateWindowResizeHandles();
    updateResponsiveHomeLayout();
    queueResponsiveHomeLayoutRefresh();
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray& eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType);

    auto *msg = static_cast<MSG *>(message);
    if (!msg || msg->message != WM_NCHITTEST || isFullScreen() || isWindowMaximizedForUi())
    {
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    const int x = GET_X_LPARAM(msg->lParam);
    const int y = GET_Y_LPARAM(msg->lParam);
    const QPoint pos = mapFromGlobal(QPoint(x, y));
    const int borderWidth = scalePixels(8);
    const bool inWindowX = pos.x() >= 0 && pos.x() < width();
    const bool inWindowY = pos.y() >= 0 && pos.y() < height();
    const bool onLeft = inWindowY && pos.x() >= 0 && pos.x() < borderWidth;
    const bool onRight = inWindowY && pos.x() < width() && pos.x() >= width() - borderWidth;
    const bool onTop = inWindowX && pos.y() >= 0 && pos.y() < borderWidth;
    const bool onBottom = inWindowX && pos.y() < height() && pos.y() >= height() - borderWidth;

    if (onTop && onLeft)
    {
        *result = HTTOPLEFT;
        return true;
    }
    if (onTop && onRight)
    {
        *result = HTTOPRIGHT;
        return true;
    }
    if (onBottom && onLeft)
    {
        *result = HTBOTTOMLEFT;
        return true;
    }
    if (onBottom && onRight)
    {
        *result = HTBOTTOMRIGHT;
        return true;
    }
    if (onTop)
    {
        *result = HTTOP;
        return true;
    }
    if (onBottom)
    {
        *result = HTBOTTOM;
        return true;
    }
    if (onLeft)
    {
        *result = HTLEFT;
        return true;
    }
    if (onRight)
    {
        *result = HTRIGHT;
        return true;
    }

    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif
