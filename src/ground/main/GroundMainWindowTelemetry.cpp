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
        log(state_->is_english_ ? "Remote Sky handshake confirmed" : "天空端握手成功");
    }
    state_->remote_status_ = status;
    const quint64 rawTotal =
        status.raw_epsilon_record_count +
        status.raw_ptb_record_count +
        status.raw_hmp_record_count +
        status.raw_lidar_record_count +
        status.raw_tcp_wave_record_count;
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
                invalidateTemperatureControllerDataUi();
            }
        }
    }
    if (state_->remote_wave_stream_auto_start_ &&
        !state_->remote_wave_stream_requested_ &&
        !state_->remote_wave_stream_enable_pending_ &&
        state_->remote_sky_controller_->deviceState(VaporView::SkyDeviceId::WaveTcp) == VaporView::DeviceState::Connected &&
        state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen())
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
    if (state_->device_panel_coordinator_)
    {
        state_->device_panel_coordinator_->updateTemperatureData(state_->current_temperature_controller_);
    }
}

void MainWindow::onRemoteCommandAckReceived(const VaporView::CommandAck& ack)
{
    const bool ok = ack.result == 0;
    const QString commandName = VaporView::commandIdName(ack.command_id);
    const QString errorText = VaporView::commandErrorCodeText(ack.error_code, state_->is_english_);
    const bool noError = ack.error_code == VaporView::CommandErrorCode::Ok;
    const QString detailLabel = ok && noError
        ? (state_->is_english_ ? QStringLiteral("detail") : QStringLiteral("详情"))
        : (state_->is_english_ ? QStringLiteral("error") : QStringLiteral("错误"));
    const QString detailText = ok && noError
        ? (state_->is_english_ ? QStringLiteral("no error") : QStringLiteral("无错误"))
        : errorText;
    log(QString(state_->is_english_ ? "Remote ACK command=%1(%2) seq=%3 result=%4 %5=%6(%7)"
                            : "远程ACK 命令=%1(%2) 序号=%3 结果=%4 %5=%6(%7)")
            .arg(commandName)
            .arg(static_cast<quint16>(ack.command_id))
            .arg(ack.command_seq)
            .arg(ok ? (state_->is_english_ ? QStringLiteral("ok") : QStringLiteral("成功"))
                    : (state_->is_english_ ? QStringLiteral("error") : QStringLiteral("失败")))
            .arg(detailLabel)
            .arg(detailText)
            .arg(static_cast<quint32>(ack.error_code)));

    if (ack.command_id == VaporView::CommandId::EnableWaveformStreaming)
    {
        state_->remote_wave_stream_enable_pending_ = false;
        state_->remote_wave_stream_requested_ = ok;
        updateRemoteDeviceButtonText(VaporView::SkyDeviceId::WaveTcp,
                                     state_->remote_sky_controller_->deviceState(VaporView::SkyDeviceId::WaveTcp));
        if (!ok)
        {
            log(state_->is_english_ ? "Remote waveform stream was not enabled" : "远程波形流启用失败");
        }
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
                    log(state_->is_english_
                            ? QStringLiteral("Peak search range accepted. Old remote peak trend was cleared.")
                            : QStringLiteral("峰值搜索区间已生效，旧远程峰值趋势已清空。"));
                }
                else
                {
                    state_->tcp_wave_panel_->rejectRemotePeakSearchRange(errorText);
                }
            }
        }
    }
    else if (isTemperatureCommand(ack.command_id))
    {
        const auto it = state_->remote_temperature_commands_.find(ack.command_seq);
        const VaporView::TemperatureControllerCommand request = it != state_->remote_temperature_commands_.end()
            ? it.value()
            : VaporView::TemperatureControllerCommand();
        if (it != state_->remote_temperature_commands_.end())
        {
            state_->remote_temperature_commands_.erase(it);
        }
        if (state_->temperature_controller_panel_)
        {
            if (!(ok && noError))
            {
                state_->temperature_controller_panel_->clearCommandPending(ack.command_id, request.channel == 0 ? 1 : request.channel);
            }
            state_->temperature_controller_panel_->setCommandStatus(
                temperatureCommandStatusText(ack.command_id,
                                             request.channel == 0 ? 1 : request.channel,
                                             ok && noError,
                                             ok && noError ? QString() : errorText),
                !(ok && noError));
        }
        restoreTemperatureCommandUi(ack.command_id, request.channel == 0 ? 1 : request.channel);
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
        updateConnectionStatus(open);
    }
}

void MainWindow::onSkyDeviceConfigClicked()
{
    if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
    {
        log(state_->is_english_ ? "Connect Remote Sky telemetry before opening the Sky Device Config dialog"
                        : "打开天空端设备配置前，请先连接天空端数传");
        return;
    }
    if (!state_->sky_device_config_dialog_)
    {
        state_->sky_device_config_dialog_ = new VaporView::SkyDeviceConfigDialog(
            state_->remote_sky_controller_->telemetryService());
        state_->sky_device_config_dialog_->setAttribute(Qt::WA_QuitOnClose, false);
        state_->sky_device_config_dialog_->setEnglish(state_->is_english_);
        state_->sky_device_config_dialog_->setFontScale(state_->font_scale_percent_);
    }
    VaporView::centerWindowOnScreen(state_->sky_device_config_dialog_, this);
    state_->sky_device_config_dialog_->show();
    state_->sky_device_config_dialog_->raise();
    state_->sky_device_config_dialog_->activateWindow();
    state_->remote_sky_controller_->requestSkyConfig();
}

void MainWindow::onClearLogClicked()
{
    state_->log_entries_.clear();
    state_->log_text_edit_->clear();
    state_->has_inline_progress_log_ = false;
    log(state_->is_english_ ? "Log cleared" : "日志已清空");
}
