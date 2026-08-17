#include "ground/main/GroundMainWindowImplementation.h"

void MainWindow::onRemoteBasicTelemetryUpdated(const VaporView::TelemetryBasic& telemetry)
{
    const auto now = std::chrono::steady_clock::now();
    auto hasFlag = [&telemetry](quint32 flag) {
        return (telemetry.validity_flags & flag) != 0;
    };

    const VaporView::Ground::RemoteEpsilonTelemetry remoteEpsilon =
        VaporView::Ground::decodeRemoteEpsilonTelemetry(telemetry, now);
    state_->current_epsilon_ = remoteEpsilon.data;
    state_->current_remote_epsilon_validity_flags_ = telemetry.validity_flags;
    if (state_->epsilon_device_session_)
    {
        state_->epsilon_device_session_->setRemoteAvailable(
            remoteEpsilon.available,
            state_->is_english_ ? QStringLiteral("Remote Sky")
                                : QStringLiteral("天空端远程"));
    }
    if (remoteEpsilon.available)
    {
#ifdef VAPORVIEW_HAS_OSGEARTH
        if (remoteEpsilon.hasPosition)
        {
            maybeForwardMap3DSample(state_->current_epsilon_, telemetry.host_time_us);
        }
        else
        {
            noteMap3DSampleDrop(QStringLiteral("Remote"),
                                QStringLiteral("missing BasicHasPosition"),
                                telemetry.host_time_us);
        }
#endif
    }

    state_->current_lidar_ = VaporView::LidarData();
    if (hasFlag(VaporView::BasicHasLidar))
    {
        state_->current_lidar_.valid = true;
        state_->current_lidar_.timestamp = now;
        state_->current_lidar_.distance_m = telemetry.lidar_height_m;
        if (hasFlag(VaporView::BasicHasLidarStrength))
        {
            state_->current_lidar_.signal_strength = telemetry.lidar_signal_strength;
        }
    }
    else
    {
        state_->current_lidar_.error_message = remoteNoDataText(state_->is_english_).toStdString();
    }

    state_->current_hmp_ = VaporView::HmpData();
    if (hasFlag(VaporView::BasicHasTemperature) && hasFlag(VaporView::BasicHasHumidity))
    {
        state_->current_hmp_.valid = true;
        state_->current_hmp_.timestamp = now;
        state_->current_hmp_.temperature = telemetry.temperature_c;
        state_->current_hmp_.humidity = telemetry.humidity_percent;
    }
    else
    {
        state_->current_hmp_.error_message = remoteNoDataText(state_->is_english_).toStdString();
    }

    state_->current_ptb_ = VaporView::PtbData();
    if (hasFlag(VaporView::BasicHasPressure))
    {
        state_->current_ptb_.valid = true;
        state_->current_ptb_.timestamp = now;
        state_->current_ptb_.pressure_hpa = telemetry.pressure_hpa;
    }
    else
    {
        state_->current_ptb_.error_message = remoteNoDataText(state_->is_english_).toStdString();
    }

    refreshRemoteSkyDataUi();
}

#ifdef VAPORVIEW_HAS_OSGEARTH
void MainWindow::maybeForwardMap3DSample(const VaporView::EpsilonData& epsilonData, quint64 recordTimestampUs)
{
    if (state_->map3d_controller_)
    {
        state_->map3d_controller_->forwardEpsilonSample(epsilonData, recordTimestampUs);
    }
}

void MainWindow::noteMap3DSampleDrop(const QString& source, const QString& reason, quint64 recordTimestampUs)
{
    if (state_->map3d_controller_)
    {
        state_->map3d_controller_->noteDrop(source, reason, recordTimestampUs);
    }
}

#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
int MainWindow::testPendingMap3DSampleCount() const
{
    return state_->map3d_controller_ ? state_->map3d_controller_->pendingSampleCount() : 0;
}

qint64 MainWindow::testLatestPendingMap3DRecordTimestampUs() const
{
    return state_->map3d_controller_ ? state_->map3d_controller_->latestPendingRecordTimestampUs() : -1;
}

bool MainWindow::testMap3DFlushTimerActive() const
{
    return state_->map3d_controller_ && state_->map3d_controller_->flushTimerActive();
}

QString MainWindow::testLastMap3DDropReason() const
{
    return state_->map3d_controller_ ? state_->map3d_controller_->lastDropReason() : QString();
}

void MainWindow::testMaybeForwardMap3DSampleForMap3D(const VaporView::EpsilonData& epsilonData,
                                                     quint64 recordTimestampUs)
{
    maybeForwardMap3DSample(epsilonData, recordTimestampUs);
}

void MainWindow::testOnRemoteBasicTelemetryUpdatedForMap3D(const VaporView::TelemetryBasic& telemetry)
{
    onRemoteBasicTelemetryUpdated(telemetry);
}
#endif
#endif

void MainWindow::onRemoteWaveformUpdated(const VaporView::DownsampledWaveform& waveform)
{
    if (!state_->remote_wave_stream_requested_)
    {
        return;
    }
    if (state_->tcp_wave_panel_)
    {
        if (waveform.channel_id == 1)
        {
            state_->tcp_wave_panel_->injectRemoteRawSignalFrame(waveform.host_time_us, waveform.samples);
        }
        else if (waveform.channel_id == 4)
        {
            state_->tcp_wave_panel_->injectRemoteSecondHarmonicFrame(waveform.host_time_us, waveform.samples);
        }
    }
}

void MainWindow::onRemoteWaveformFeatureUpdated(const VaporView::WaveformFeature& feature)
{
    if (!state_->remote_wave_stream_requested_)
    {
        return;
    }
    if (state_->tcp_wave_panel_)
    {
        state_->tcp_wave_panel_->injectRemoteWaveformFeature(feature);
    }
}

void MainWindow::onRemoteTelemetryStatusUpdated(const VaporView::TelemetryStatus& status)
{
    if (!state_->remote_sky_online_)
    {
        state_->remote_sky_online_ = true;
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("telemetry.connection"),
                         QStringLiteral("remote_sky_handshake_confirmed"),
                         QStringLiteral("天空端握手成功。"),
                         {{QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    state_->remote_status_ = status;
    const quint64 rawTotal =
        status.raw_navigation_record_count +
        status.raw_pressure_record_count +
        status.raw_temperature_humidity_record_count +
        status.raw_distance_record_count +
        status.raw_waveform_record_count;
    if (!status.session_name.isEmpty() ||
        status.telemetry_record_count > 0 ||
        status.waveform_feature_record_count > 0 ||
        status.waveform_snapshot_record_count > 0 ||
        rawTotal > 0)
    {
        state_->last_remote_recording_status_ = status;
        state_->has_last_remote_recording_status_ = true;
    }
    if (state_->tcp_wave_panel_)
    {
        state_->tcp_wave_panel_->setRemoteFeatureRateHz(status.feature_rate_hz);
    }
    state_->remote_recording_state_ = status.recording_state;
    for (const VaporView::DeviceStatusItem& item : status.devices)
    {
        updateRemoteDeviceButtonText(item.device_id, item.state);
        if (item.state != VaporView::DeviceState::Connected)
        {
            if (item.device_id == VaporView::SkyDeviceId::Epsilon)
            {
                state_->current_epsilon_ = VaporView::EpsilonData();
                state_->current_remote_epsilon_validity_flags_ = 0;
                if (state_->epsilon_device_session_)
                {
                    state_->epsilon_device_session_->setRemoteAvailable(false);
                }
            }
            else if (item.device_id == VaporView::SkyDeviceId::Ptb)
            {
                state_->current_ptb_ = VaporView::PtbData();
                state_->current_ptb_.error_message = remoteDisconnectedText(state_->is_english_).toStdString();
            }
            else if (item.device_id == VaporView::SkyDeviceId::Hmp)
            {
                state_->current_hmp_ = VaporView::HmpData();
                state_->current_hmp_.error_message = remoteDisconnectedText(state_->is_english_).toStdString();
            }
            else if (item.device_id == VaporView::SkyDeviceId::Lidar)
            {
                state_->current_lidar_ = VaporView::LidarData();
                state_->current_lidar_.error_message = remoteDisconnectedText(state_->is_english_).toStdString();
            }
            else if (item.device_id == VaporView::SkyDeviceId::TemperatureController)
            {
                if (state_->rd105_device_session_)
                {
                    state_->rd105_device_session_->setRemoteAvailable(false);
                }
                invalidateTemperatureControllerDataUi();
            }
            else if (item.device_id == VaporView::SkyDeviceId::Ai8TemperatureController &&
                     state_->ai8_temperature_controller_panel_)
            {
                if (state_->ai8_device_session_)
                {
                    state_->ai8_device_session_->setRemoteAvailable(false);
                }
                state_->ai8_temperature_controller_panel_->setBackendConnected(false);
                state_->ai8_temperature_controller_panel_->applyLiveData({});
                updateAi8TemperatureTitleStatus();
            }
        }
    }
    if (state_->remote_wave_stream_auto_start_ &&
        !state_->remote_wave_stream_requested_ &&
        !state_->remote_wave_stream_enable_pending_ &&
        state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen() &&
        state_->remote_sky_controller_->deviceState(VaporView::SkyDeviceId::WaveTcp) == VaporView::DeviceState::Connected)
    {
        state_->remote_wave_stream_enable_pending_ = true;
        state_->remote_sky_controller_->sendCommand(VaporView::CommandId::EnableWaveformStreaming);
    }
    refreshRemoteSkyDataUi();
    updateRecordingStatusLabel();
    updateSourceModeUi();
}

void MainWindow::onRemoteTemperatureControllerStatusUpdated(const VaporView::TemperatureControllerData& controllerData)
{
    state_->current_temperature_controller_ = controllerData;
    if (state_->rd105_device_session_)
    {
        state_->rd105_device_session_->setRemoteAvailable(
            controllerData.valid,
            state_->is_english_ ? QStringLiteral("Remote Sky")
                                : QStringLiteral("天空端远程"));
    }
    if (state_->device_panel_coordinator_)
    {
        state_->device_panel_coordinator_->updateTemperatureData(state_->current_temperature_controller_);
    }
}

void MainWindow::onRemoteAi8TemperatureControllerStatusUpdated(
    const VaporView::Ai8TemperatureControllerProtocol::LiveData& liveData)
{
    if (!state_->ai8_temperature_controller_panel_)
    {
        return;
    }
    const QString detail = state_->is_english_
        ? QStringLiteral("Remote Sky")
        : QStringLiteral("天空端远程");
    state_->ai8_temperature_controller_panel_->setBackendConnected(true, detail);
    if (state_->ai8_device_session_)
    {
        state_->ai8_device_session_->setRemoteAvailable(
            remoteDeviceDataValid(VaporView::SkyDeviceId::Ai8TemperatureController, 3000),
            detail);
    }
    state_->ai8_temperature_controller_panel_->applyLiveData(liveData);
    updateAi8TemperatureTitleStatus();
}

void MainWindow::onRemoteCommandAckReceived(const VaporView::CommandAck& ack)
{
    const bool ok = ack.result == 0;
    const QString commandName = VaporView::commandIdName(ack.command_id);
    const QString errorText = VaporView::commandErrorCodeText(ack.error_code, state_->is_english_);
    const bool noError = ack.error_code == VaporView::CommandErrorCode::Ok;
    if (!isTemperatureCommand(ack.command_id) &&
        ack.command_id != VaporView::CommandId::DeviceOperation)
    {
        QVariantMap fields{{QStringLiteral("command"), commandName},
                           {QStringLiteral("command_id"), static_cast<quint16>(ack.command_id)},
                           {QStringLiteral("command_seq"), ack.command_seq},
                           {QStringLiteral("ack_result"), ack.result},
                           {QStringLiteral("command_error_code"), commandErrorCodeIdentifier(ack.error_code)}};
        if (ok && noError)
        {
            fields.insert(QStringLiteral("ui_visibility"), QStringLiteral("hidden"));
            publishGroundLog(VaporView::LogLevel::Debug,
                             QStringLiteral("telemetry.command"),
                             QStringLiteral("remote_command_ack_received"),
                             QStringLiteral("远程命令 ACK 已收到。"),
                             fields);
        }
        else
        {
            fields.insert(QStringLiteral("error_code"),
                          noError ? QStringLiteral("REMOTE_COMMAND_FAILED")
                                  : commandErrorCodeIdentifier(ack.error_code));
            fields.insert(QStringLiteral("ui_dedupe_key"),
                          QStringLiteral("remote_command:%1:failed").arg(commandName));
            publishGroundLog(VaporView::LogLevel::Error,
                             QStringLiteral("telemetry.command"),
                             QStringLiteral("remote_command_failed"),
                             QStringLiteral("远程命令执行失败。"),
                             fields);
        }
    }

    if (ack.command_id == VaporView::CommandId::GetSkyConfig &&
        ack.command_seq == state_->remote_sky_config_read_seq_ &&
        !(ok && noError))
    {
        state_->remote_sky_config_loading_ = false;
        clearPendingRemoteWaveTcpConnection();
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Remote Sky config read was rejected: %1").arg(errorText)
            : QStringLiteral("读取天空端配置被拒绝：%1").arg(errorText),
            true);
        updateRemoteSkyConfigControlsState();
    }
    else if (ack.command_id == VaporView::CommandId::SetSkyConfig &&
             ack.command_seq == state_->remote_sky_config_apply_seq_)
    {
        if (ok && noError)
        {
            setRemoteSkyConfigStatus(state_->is_english_
                ? QStringLiteral("Remote Sky config ACK received; waiting for apply result...")
                : QStringLiteral("天空端配置 ACK 已收到，等待应用结果..."));
        }
        else
        {
            state_->remote_sky_config_applying_ = false;
            clearPendingRemoteWaveTcpConnection();
            state_->remote_sky_config_dirty_ = true;
            setRemoteSkyConfigStatus(state_->is_english_
                ? QStringLiteral("Remote Sky config apply was rejected: %1").arg(errorText)
                : QStringLiteral("天空端配置应用被拒绝：%1").arg(errorText),
                true);
        }
        updateRemoteSkyConfigControlsState();
    }
    else if (ack.command_id == VaporView::CommandId::SaveSkyConfig &&
             ack.command_seq == state_->remote_sky_config_save_seq_)
    {
        state_->remote_sky_config_saving_ = false;
        setRemoteSkyConfigStatus(ok && noError
            ? (state_->is_english_ ? QStringLiteral("Remote Sky config saved on sky.") : QStringLiteral("天空端配置已保存。"))
            : (state_->is_english_ ? QStringLiteral("Remote Sky config save failed: %1").arg(errorText)
                                  : QStringLiteral("天空端配置保存失败：%1").arg(errorText)),
            !(ok && noError));
        updateRemoteSkyConfigControlsState();
    }

    if (ack.command_id == VaporView::CommandId::AutoDetectSerialPorts &&
        ack.command_seq == state_->remote_serial_detection_seq_ &&
        !(ok && noError))
    {
        state_->port_detection_in_progress_ = false;
        state_->remote_serial_detection_seq_ = 0;
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Remote serial-port detection was rejected: %1").arg(errorText)
            : QStringLiteral("远程串口自动识别被拒绝：%1").arg(errorText), true);
        updateConnectionStatus(state_->is_connected_);
    }
    else if (ack.command_id == VaporView::CommandId::CancelSerialPortDetection &&
             ack.command_seq == state_->remote_serial_detection_cancel_seq_ &&
             !(ok && noError))
    {
        state_->remote_serial_detection_cancel_seq_ = 0;
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.connection"),
                         QStringLiteral("remote_serial_port_detection_cancel_failed"),
                         QStringLiteral("天空端取消串口自动识别失败。"),
                         {{QStringLiteral("error_code"), commandErrorCodeIdentifier(ack.error_code)},
                          {QStringLiteral("ui_visibility"), QStringLiteral("attention")} });
    }

    if (ack.command_id == VaporView::CommandId::EnableWaveformStreaming)
    {
        state_->remote_wave_stream_enable_pending_ = false;
        state_->remote_wave_stream_requested_ = ok;
        updateRemoteDeviceButtonText(VaporView::SkyDeviceId::WaveTcp,
                                     state_->remote_sky_controller_->deviceState(VaporView::SkyDeviceId::WaveTcp));
    }
    else if (ack.command_id == VaporView::CommandId::DisableWaveformStreaming)
    {
        state_->remote_wave_stream_enable_pending_ = false;
        state_->remote_wave_stream_requested_ = false;
        updateRemoteDeviceButtonText(VaporView::SkyDeviceId::WaveTcp,
                                     state_->remote_sky_controller_->deviceState(VaporView::SkyDeviceId::WaveTcp));
    }

    if (ack.command_id == VaporView::CommandId::SetPeakSearchRange)
    {
        const auto it = state_->remote_peak_search_commands_.find(ack.command_seq);
        if (it != state_->remote_peak_search_commands_.end())
        {
            const VaporView::PeakSearchRange range = it.value();
            state_->remote_peak_search_commands_.erase(it);
            if (state_->tcp_wave_panel_)
            {
                if (ok)
                {
                    state_->tcp_wave_panel_->applyRemotePeakSearchRange(range.start_index, range.end_index);
                    publishGroundLog(VaporView::LogLevel::Info,
                                     QStringLiteral("telemetry.command"),
                                     QStringLiteral("peak_search_range_applied"),
                                     QStringLiteral("峰值搜索区间已生效，旧远程峰值趋势已清空。"),
                                     {{QStringLiteral("command"), commandName},
                                      {QStringLiteral("command_id"), static_cast<quint16>(ack.command_id)},
                                      {QStringLiteral("command_seq"), ack.command_seq},
                                      {QStringLiteral("range_start_index"), range.start_index},
                                      {QStringLiteral("range_end_index"), range.end_index},
                                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
                }
                else
                {
                    state_->tcp_wave_panel_->rejectRemotePeakSearchRange(errorText);
                }
            }
        }
    }
}

void MainWindow::onRemoteLinkOpenChanged(bool open)
{
    if (isRemoteSkyMode())
    {
        if (!open)
        {
            markRemoteSkyLinkClosed();
        }
        else
        {
            requestRemoteSkyConfigIfAvailable(false);
        }
        updateConnectionStatus(open);
    }
}

void MainWindow::onClearLogClicked()
{
    state_->pending_ui_log_records_.clear();
    state_->pending_ui_log_records_dropped_ = 0;
    if (state_->log_model_)
    {
        state_->log_model_->clearEntries();
    }
    state_->has_inline_progress_log_ = false;
    clearLogUnreadState();
    VaporView::LogService::withCurrentInstance([&](VaporView::LogService& logService) {
        logService.publish(VaporView::LogLevel::Info,
                           QStringLiteral("Ground"),
                           QStringLiteral("ui.log"),
                           QStringLiteral("日志面板显示已清空。"),
                           {{QStringLiteral("event"), QStringLiteral("ui_log_view_cleared")},
                            {QStringLiteral("ui_visibility"), QStringLiteral("hidden")}});
    });
}
