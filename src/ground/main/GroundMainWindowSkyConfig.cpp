#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/widgets/SerialPortComboSupport.h"

namespace
{

QString selectText(bool english)
{
    return english ? QStringLiteral("-- Select --") : QStringLiteral("未选择");
}

QString numberText(double value)
{
    return qAbs(value - qRound64(value)) < 0.000001
        ? QString::number(static_cast<qint64>(qRound64(value)))
        : QString::number(value, 'f', 1);
}

VaporView::SkyConfig unloadedSkyConfig()
{
    VaporView::SkyConfig config = VaporView::SkyConfig::defaults();
    config.epsilon.enabled = false;
    config.epsilon.port.clear();
    config.ptb.enabled = false;
    config.ptb.port.clear();
    config.hmp.enabled = false;
    config.hmp.port.clear();
    config.lidar.enabled = false;
    config.lidar.port.clear();
    config.temperature_controller.enabled = false;
    config.temperature_controller.port.clear();
    config.ai8_temperature_controller.enabled = false;
    config.ai8_temperature_controller.port.clear();
    return config;
}

bool validManualRemotePortText(const QString& text, bool english)
{
    const QString trimmed = text.trimmed();
    return !trimmed.isEmpty() &&
           !trimmed.startsWith(QStringLiteral("--")) &&
           trimmed != QStringLiteral("手动添加") &&
           trimmed != QStringLiteral("Add Port") &&
           trimmed != selectText(english);
}

} // namespace

QString MainWindow::remoteSkySerialPortManualOptionText() const
{
    return state_->is_english_ ? QStringLiteral("Add Port") : QStringLiteral("手动添加");
}

bool MainWindow::isRemoteSkySerialPortManualOption(const QComboBox *combo, int index) const
{
    return combo &&
           index >= 0 &&
           index < combo->count() &&
           combo->itemData(index).toString() == QString::fromLatin1(kLocalSerialPortManualOptionData);
}

QString MainWindow::remoteSkySerialPortItemValue(const QComboBox *combo, int index) const
{
    if (!combo || index < 0 || index >= combo->count())
    {
        return QString();
    }
    const QString itemData = combo->itemData(index).toString().trimmed();
    if (!itemData.isEmpty() && itemData != QString::fromLatin1(kLocalSerialPortManualOptionData))
    {
        return itemData;
    }
    const QString text = combo->itemText(index).trimmed();
    return validManualRemotePortText(text, state_->is_english_) ? text : QString();
}

QString MainWindow::remoteSkySerialPortComboValue(const QComboBox *combo) const
{
    if (!combo || combo->property(kRemoteSkySerialPortManualEntryProperty).toBool())
    {
        return QString();
    }
    return remoteSkySerialPortItemValue(combo, combo->currentIndex());
}

void MainWindow::installRemoteSkySerialPortComboBehavior(QComboBox *combo)
{
    if (!combo)
    {
        return;
    }
    combo->setProperty(kRemoteSkySerialPortComboProperty, true);
    combo->setProperty(kLocalSerialPortComboProperty, false);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setEditable(false);
    VaporView::installSerialPortPopupDelegate(combo);
    if (combo->property(kRemoteSkySerialPortManualHandlerProperty).toBool())
    {
        return;
    }
    combo->setProperty(kRemoteSkySerialPortManualHandlerProperty, true);
    const auto beginManualEntryIfSelected = [this, combo](int index) {
        if (combo->property(kRemoteSkySerialPortManualEntryProperty).toBool() &&
            !isRemoteSkySerialPortManualOption(combo, index))
        {
            if (QLineEdit *edit = combo->lineEdit())
            {
                edit->setProperty(kRemoteSkySerialPortManualEntryProperty, false);
                edit->removeEventFilter(this);
            }
            combo->setProperty(kRemoteSkySerialPortManualEntryProperty, false);
            combo->setEditable(false);
        }
        if (isRemoteSkySerialPortManualOption(combo, index))
        {
            beginManualRemoteSkySerialPortEntry(combo);
            return;
        }
        combo->setProperty(kRemoteSkySerialPortManualPreviousTextProperty,
                           remoteSkySerialPortItemValue(combo, index));
    };
    connect(combo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            beginManualEntryIfSelected);
    connect(combo,
            QOverload<int>::of(&QComboBox::activated),
            this,
            beginManualEntryIfSelected);
}

void MainWindow::refreshRemoteSkySerialPortComboOptions(QComboBox *combo, const QString& preferredText)
{
    if (!combo)
    {
        return;
    }
    if (combo->property(kRemoteSkySerialPortManualEntryProperty).toBool())
    {
        finishManualRemoteSkySerialPortEntry(combo, true);
    }
    installRemoteSkySerialPortComboBehavior(combo);
    const QString selectedValue = preferredText.isNull()
        ? remoteSkySerialPortComboValue(combo).trimmed()
        : preferredText.trimmed();
    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->setEditable(false);
    combo->addItem(selectText(state_->is_english_), QString());
    if (validManualRemotePortText(selectedValue, state_->is_english_))
    {
        combo->addItem(selectedValue, selectedValue);
    }
    combo->addItem(remoteSkySerialPortManualOptionText(), QString::fromLatin1(kLocalSerialPortManualOptionData));
    const int selectedIndex = selectedValue.isEmpty() ? 0 : combo->findData(selectedValue);
    combo->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
    combo->setProperty(kRemoteSkySerialPortManualPreviousTextProperty, selectedValue);
}

void MainWindow::setRemoteSkySerialPortComboText(QComboBox *combo, const QString& text)
{
    if (!combo)
    {
        return;
    }
    installRemoteSkySerialPortComboBehavior(combo);
    const QString trimmed = text.trimmed();
    const QSignalBlocker blocker(combo);
    int index = trimmed.isEmpty() ? 0 : combo->findData(trimmed);
    if (index < 0 && validManualRemotePortText(trimmed, state_->is_english_))
    {
        const int manualIndex = combo->findData(QString::fromLatin1(kLocalSerialPortManualOptionData));
        index = manualIndex >= 0 ? manualIndex : combo->count();
        combo->insertItem(index, trimmed, trimmed);
    }
    combo->setCurrentIndex(index >= 0 ? index : 0);
    combo->setProperty(kRemoteSkySerialPortManualPreviousTextProperty,
                       index >= 0 ? trimmed : QString());
}

void MainWindow::beginManualRemoteSkySerialPortEntry(QComboBox *combo)
{
    if (!combo ||
        !combo->property(kRemoteSkySerialPortComboProperty).toBool() ||
        combo->property(kRemoteSkySerialPortManualEntryProperty).toBool())
    {
        return;
    }
    QString previous = combo->property(kRemoteSkySerialPortManualPreviousTextProperty).toString();
    if (previous.isEmpty())
    {
        previous = remoteSkySerialPortComboValue(combo);
    }
    combo->setProperty(kRemoteSkySerialPortManualPreviousTextProperty, previous);
    combo->setProperty(kRemoteSkySerialPortManualEntryProperty, true);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    if (QLineEdit *edit = combo->lineEdit())
    {
        edit->setProperty(kRemoteSkySerialPortManualEntryProperty, true);
        edit->setPlaceholderText(state_->is_english_ ? QStringLiteral("Enter...") : QStringLiteral("输入串口..."));
        edit->installEventFilter(this);
        edit->clear();
        edit->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::finishManualRemoteSkySerialPortEntry(QComboBox *combo, bool accept)
{
    if (!combo || !combo->property(kRemoteSkySerialPortManualEntryProperty).toBool())
    {
        return;
    }
    const QString previous = combo->property(kRemoteSkySerialPortManualPreviousTextProperty).toString();
    const QString entered = combo->lineEdit() ? combo->lineEdit()->text().trimmed() : combo->currentText().trimmed();
    combo->setProperty(kRemoteSkySerialPortManualEntryProperty, false);
    combo->setEditable(false);
    if (QLineEdit *edit = combo->lineEdit())
    {
        edit->setProperty(kRemoteSkySerialPortManualEntryProperty, false);
        edit->removeEventFilter(this);
    }
    setRemoteSkySerialPortComboText(
        combo,
        (accept && validManualRemotePortText(entered, state_->is_english_)) ? entered : previous);
    markRemoteSkyConfigDirty();
}

bool MainWindow::handleRemoteSkySerialPortManualEntryEvent(QObject *watched, QEvent *event)
{
    auto *edit = qobject_cast<QLineEdit *>(watched);
    if (!edit || !edit->property(kRemoteSkySerialPortManualEntryProperty).toBool())
    {
        return false;
    }
    auto *combo = qobject_cast<QComboBox *>(edit->parentWidget());
    if (!combo || !combo->property(kRemoteSkySerialPortManualEntryProperty).toBool())
    {
        return false;
    }
    if (event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
        {
            finishManualRemoteSkySerialPortEntry(combo, true);
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape)
        {
            finishManualRemoteSkySerialPortEntry(combo, false);
            return true;
        }
    }
    else if (event->type() == QEvent::FocusOut)
    {
        QTimer::singleShot(0, this, [this, combo]() {
            finishManualRemoteSkySerialPortEntry(combo, true);
        });
    }
    return false;
}

void MainWindow::refreshRemoteSkySerialPortManualOptionTexts()
{
    for (QComboBox *combo : {state_->device_config_.epsilon_port_combo,
                             state_->device_config_.ptb_port_combo,
                             state_->device_config_.hmp_port_combo,
                             state_->device_config_.lidar_port_combo,
                             state_->device_config_.temperature_port_combo,
                             state_->device_config_.ai8_temperature_port_combo})
    {
        if (!combo || !combo->property(kRemoteSkySerialPortComboProperty).toBool())
        {
            continue;
        }
        const QSignalBlocker blocker(combo);
        if (combo->count() > 0 && combo->itemData(0).toString().isEmpty())
        {
            combo->setItemText(0, selectText(state_->is_english_));
        }
        const int manualIndex = combo->findData(QString::fromLatin1(kLocalSerialPortManualOptionData));
        if (manualIndex >= 0)
        {
            combo->setItemText(manualIndex, remoteSkySerialPortManualOptionText());
        }
        if (combo->property(kRemoteSkySerialPortManualEntryProperty).toBool() && combo->lineEdit())
        {
            combo->lineEdit()->setPlaceholderText(state_->is_english_ ? QStringLiteral("Enter...") : QStringLiteral("输入串口..."));
        }
    }
}

void MainWindow::syncDeviceConfigPageForCurrentTarget()
{
    if (isRemoteSkyMode())
    {
        enterRemoteSkyDeviceConfigMode();
    }
    else
    {
        enterLocalDeviceConfigMode();
    }
}

void MainWindow::enterLocalDeviceConfigMode()
{
    for (QComboBox *combo : {state_->device_config_.epsilon_port_combo,
                             state_->device_config_.ptb_port_combo,
                             state_->device_config_.hmp_port_combo,
                             state_->device_config_.lidar_port_combo,
                             state_->device_config_.temperature_port_combo,
                             state_->device_config_.ai8_temperature_port_combo})
    {
        if (combo)
        {
            installLocalSerialPortComboBehavior(combo);
        }
    }
}

void MainWindow::enterRemoteSkyDeviceConfigMode()
{
    for (QComboBox *combo : {state_->device_config_.epsilon_port_combo,
                             state_->device_config_.ptb_port_combo,
                             state_->device_config_.hmp_port_combo,
                             state_->device_config_.lidar_port_combo,
                             state_->device_config_.temperature_port_combo,
                             state_->device_config_.ai8_temperature_port_combo})
    {
        if (combo)
        {
            installRemoteSkySerialPortComboBehavior(combo);
        }
    }
    VaporView::SkyConfig config = state_->remote_sky_config_loaded_
        ? state_->remote_sky_config_
        : unloadedSkyConfig();
    if (!state_->remote_sky_config_loaded_)
    {
        QSettings settings = VaporView::applicationConfigSettings();
        settings.beginGroup(QStringLiteral("MainWindow"));
        auto rememberedBaud = [&settings](const QString& source, const QString& legacyKey) {
            bool ok = false;
            const int baud = VaporView::Ground::MainSupport::rememberedSensorBaud(
                settings, source, legacyKey).toInt(&ok);
            return ok && baud > 0 ? baud : VaporView::Ground::MainSupport::sensorDefaultBaud(source).toInt();
        };
        config.ptb.source = settings.value(
            QStringLiteral("sensor/pressure_source"), QStringLiteral("ptb210")).toString();
        config.ptb.baud_rate = rememberedBaud(
            config.ptb.source, QStringLiteral("serial/ptb_baud"));
        config.hmp.source = settings.value(
            QStringLiteral("sensor/humidity_source"), QStringLiteral("hmp3")).toString();
        config.hmp.baud_rate = rememberedBaud(
            config.hmp.source, QStringLiteral("serial/hmp_baud"));
    }
    setRemoteSkyConfigUi(config);
    updateRemoteSkyConfigControlsState();
}

void MainWindow::setRemoteSkyConfigUi(const VaporView::SkyConfig& config)
{
    if (!state_->device_config_.page)
    {
        return;
    }
    state_->remote_sky_config_updating_ui_ = true;
    const auto finish = qScopeGuard([this]() {
        state_->remote_sky_config_updating_ui_ = false;
    });

    auto setSerial = [this](QCheckBox *enabled,
                            QComboBox *port,
                            QComboBox *baud,
                            QComboBox *rate,
                            bool isEnabled,
                            const QString& portText,
                            int baudRate,
                            double frequency) {
        if (enabled)
        {
            const QSignalBlocker blocker(enabled);
            enabled->setChecked(isEnabled);
        }
        if (port)
        {
            refreshRemoteSkySerialPortComboOptions(port, portText);
        }
        auto setComboText = [](QComboBox *combo, const QString& text) {
            if (!combo)
            {
                return;
            }
            const QSignalBlocker blocker(combo);
            combo->setEditable(true);
            if (combo->findText(text) < 0)
            {
                combo->addItem(text, text);
            }
            combo->setCurrentText(text);
        };
        setComboText(baud, QString::number(baudRate));
        setComboText(rate, numberText(frequency));
    };

    setSerial(state_->device_config_.epsilon_enabled_check,
              state_->device_config_.epsilon_port_combo,
              state_->device_config_.epsilon_baud_combo,
              state_->device_config_.epsilon_rate_combo,
              config.epsilon.enabled,
              config.epsilon.port,
              config.epsilon.baud_rate,
              config.epsilon.frequency_hz);
    setSerial(state_->device_config_.ptb_enabled_check,
              state_->device_config_.ptb_port_combo,
              state_->device_config_.ptb_baud_combo,
              state_->device_config_.ptb_rate_combo,
              config.ptb.enabled,
              config.ptb.port,
              config.ptb.baud_rate,
              config.ptb.frequency_hz);
    setSerial(state_->device_config_.hmp_enabled_check,
              state_->device_config_.hmp_port_combo,
              state_->device_config_.hmp_baud_combo,
              state_->device_config_.hmp_rate_combo,
              config.hmp.enabled,
              config.hmp.port,
              config.hmp.baud_rate,
              config.hmp.frequency_hz);
    setSerial(state_->device_config_.lidar_enabled_check,
              state_->device_config_.lidar_port_combo,
              state_->device_config_.lidar_baud_combo,
              state_->device_config_.lidar_rate_combo,
              config.lidar.enabled,
              config.lidar.port,
              config.lidar.baud_rate,
              config.lidar.frequency_hz);
    setSerial(state_->device_config_.temperature_enabled_check,
              state_->device_config_.temperature_port_combo,
              state_->device_config_.temperature_baud_combo,
              state_->device_config_.temperature_rate_combo,
              config.temperature_controller.enabled,
              config.temperature_controller.port,
              config.temperature_controller.baud_rate,
              config.temperature_controller.frequency_hz);
    setSerial(state_->device_config_.ai8_temperature_enabled_check,
              state_->device_config_.ai8_temperature_port_combo,
              state_->device_config_.ai8_temperature_baud_combo,
              state_->device_config_.ai8_temperature_rate_combo,
              config.ai8_temperature_controller.enabled,
              config.ai8_temperature_controller.port,
              config.ai8_temperature_controller.baud_rate,
              config.ai8_temperature_controller.frequency_hz);
    auto setComboData = [](QComboBox *combo, const QString& sourceValue) {
        if (!combo)
        {
            return;
        }
        const QSignalBlocker blocker(combo);
        const int index = combo->findData(sourceValue);
        combo->setCurrentIndex(index >= 0 ? index : 0);
        combo->setProperty(kSensorBaudSourceProperty, combo->currentData().toString());
    };
    setComboData(state_->device_config_.ptb_source_combo,
                 config.ptb.source.isEmpty() ? QStringLiteral("ptb210") : config.ptb.source);
    setComboData(state_->device_config_.hmp_source_combo,
                 config.hmp.source.isEmpty() ? QStringLiteral("hmp3") : config.hmp.source);

    const QList<QPair<QSpinBox *, int>> intSpins = {
        {state_->device_config_.remote_sky_rd105_slave_spin, config.temperature_controller.slave_address},
        {state_->device_config_.remote_sky_wave_port_spin, config.wave_tcp.port},
        {state_->device_config_.remote_sky_wave_downsample_spin, config.wave_tcp.downsample_ratio}
    };
    for (const auto& item : intSpins)
    {
        if (item.first)
        {
            const QSignalBlocker blocker(item.first);
            item.first->setValue(item.second);
        }
    }
    if (state_->device_config_.remote_sky_wave_enabled_check)
    {
        const QSignalBlocker blocker(state_->device_config_.remote_sky_wave_enabled_check);
        state_->device_config_.remote_sky_wave_enabled_check->setChecked(config.wave_tcp.enabled);
    }
    if (state_->device_config_.remote_sky_wave_host_edit)
    {
        const QSignalBlocker blocker(state_->device_config_.remote_sky_wave_host_edit);
        state_->device_config_.remote_sky_wave_host_edit->setText(config.wave_tcp.host);
    }
    if (isRemoteSkyMode() && state_->tcp_wave_panel_)
    {
        state_->tcp_wave_panel_->setConnectionEndpoint(config.wave_tcp.host,
                                                       config.wave_tcp.port);
    }
    const QList<QPair<QDoubleSpinBox *, double>> doubleSpins = {
        {state_->device_config_.remote_sky_telemetry_basic_spin, config.telemetry.basic_rate_hz},
        {state_->device_config_.remote_sky_telemetry_feature_spin, config.telemetry.feature_rate_hz},
        {state_->device_config_.remote_sky_telemetry_waveform_spin, config.telemetry.waveform_rate_hz},
        {state_->device_config_.remote_sky_telemetry_heartbeat_spin, config.telemetry.heartbeat_rate_hz},
        {state_->device_config_.remote_sky_telemetry_status_spin, config.telemetry.status_rate_hz}
    };
    for (const auto& item : doubleSpins)
    {
        if (item.first)
        {
            const QSignalBlocker blocker(item.first);
            item.first->setValue(item.second);
        }
    }
    if (state_->remote_sky_config_raw_mode_)
    {
        refreshRemoteSkyConfigRawFromVisual();
    }
}

VaporView::SkyConfig MainWindow::remoteSkyConfigFromDeviceConfigUi(QString *errorMessage) const
{
    if (state_->remote_sky_config_raw_mode_ &&
        state_->device_config_.remote_sky_raw_json_edit)
    {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(
            state_->device_config_.remote_sky_raw_json_edit->toPlainText().toUtf8(),
            &parseError);
        VaporView::SkyConfig config;
        if (parseError.error != QJsonParseError::NoError)
        {
            if (errorMessage)
            {
                *errorMessage = state_->is_english_
                    ? QStringLiteral("JSON parse error at offset %1: %2").arg(parseError.offset).arg(parseError.errorString())
                    : QStringLiteral("JSON 解析错误，位置 %1：%2").arg(parseError.offset).arg(parseError.errorString());
            }
            return state_->remote_sky_config_;
        }
        if (!document.isObject())
        {
            if (errorMessage)
            {
                *errorMessage = state_->is_english_
                    ? QStringLiteral("Sky config JSON root must be an object.")
                    : QStringLiteral("天空端配置 JSON 根节点必须是对象。");
            }
            return state_->remote_sky_config_;
        }
        if (!VaporView::SkyConfig::fromJson(document.object(), config, errorMessage))
        {
            return state_->remote_sky_config_;
        }
        return config;
    }

    VaporView::SkyConfig config = state_->remote_sky_config_loaded_
        ? state_->remote_sky_config_
        : VaporView::SkyConfig::defaults();
    auto readSerial = [this](QCheckBox *enabled,
                             QComboBox *port,
                             QComboBox *baud,
                             QComboBox *rate,
                             VaporView::SerialDeviceConfig& target) {
        target.enabled = enabled ? enabled->isChecked() : target.enabled;
        target.port = remoteSkySerialPortComboValue(port);
        target.baud_rate = baud ? baud->currentText().trimmed().toInt() : target.baud_rate;
        target.frequency_hz = rate ? rate->currentText().trimmed().toDouble() : target.frequency_hz;
    };
    readSerial(state_->device_config_.epsilon_enabled_check,
               state_->device_config_.epsilon_port_combo,
               state_->device_config_.epsilon_baud_combo,
               state_->device_config_.epsilon_rate_combo,
               config.epsilon);
    readSerial(state_->device_config_.ptb_enabled_check,
               state_->device_config_.ptb_port_combo,
               state_->device_config_.ptb_baud_combo,
               state_->device_config_.ptb_rate_combo,
               config.ptb);
    readSerial(state_->device_config_.hmp_enabled_check,
               state_->device_config_.hmp_port_combo,
               state_->device_config_.hmp_baud_combo,
               state_->device_config_.hmp_rate_combo,
               config.hmp);
    if (state_->device_config_.ptb_source_combo)
    {
        config.ptb.source = state_->device_config_.ptb_source_combo->currentData().toString();
    }
    if (state_->device_config_.hmp_source_combo)
    {
        config.hmp.source = state_->device_config_.hmp_source_combo->currentData().toString();
    }
    readSerial(state_->device_config_.lidar_enabled_check,
               state_->device_config_.lidar_port_combo,
               state_->device_config_.lidar_baud_combo,
               state_->device_config_.lidar_rate_combo,
               config.lidar);
    VaporView::SerialDeviceConfig temperatureSerial;
    temperatureSerial.enabled = config.temperature_controller.enabled;
    temperatureSerial.port = config.temperature_controller.port;
    temperatureSerial.baud_rate = config.temperature_controller.baud_rate;
    temperatureSerial.frequency_hz = config.temperature_controller.frequency_hz;
    readSerial(state_->device_config_.temperature_enabled_check,
               state_->device_config_.temperature_port_combo,
               state_->device_config_.temperature_baud_combo,
               state_->device_config_.temperature_rate_combo,
               temperatureSerial);
    config.temperature_controller.enabled = temperatureSerial.enabled;
    config.temperature_controller.port = temperatureSerial.port;
    config.temperature_controller.baud_rate = temperatureSerial.baud_rate;
    config.temperature_controller.frequency_hz = temperatureSerial.frequency_hz;
    VaporView::SerialDeviceConfig ai8TemperatureSerial;
    ai8TemperatureSerial.enabled = config.ai8_temperature_controller.enabled;
    ai8TemperatureSerial.port = config.ai8_temperature_controller.port;
    ai8TemperatureSerial.baud_rate = config.ai8_temperature_controller.baud_rate;
    ai8TemperatureSerial.frequency_hz = config.ai8_temperature_controller.frequency_hz;
    readSerial(state_->device_config_.ai8_temperature_enabled_check,
               state_->device_config_.ai8_temperature_port_combo,
               state_->device_config_.ai8_temperature_baud_combo,
               state_->device_config_.ai8_temperature_rate_combo,
               ai8TemperatureSerial);
    config.ai8_temperature_controller.enabled = ai8TemperatureSerial.enabled;
    config.ai8_temperature_controller.port = ai8TemperatureSerial.port;
    config.ai8_temperature_controller.baud_rate = ai8TemperatureSerial.baud_rate;
    config.ai8_temperature_controller.frequency_hz = ai8TemperatureSerial.frequency_hz;
    if (state_->ai8_temperature_controller_panel_)
    {
        config.ai8_temperature_controller.slave_address =
            state_->ai8_temperature_controller_panel_->currentPageData().global.address;
    }
    if (state_->device_config_.remote_sky_rd105_slave_spin)
    {
        config.temperature_controller.slave_address =
            state_->device_config_.remote_sky_rd105_slave_spin->value();
    }
    if (state_->device_config_.remote_sky_wave_enabled_check) config.wave_tcp.enabled = state_->device_config_.remote_sky_wave_enabled_check->isChecked();
    if (state_->device_config_.remote_sky_wave_host_edit) config.wave_tcp.host = state_->device_config_.remote_sky_wave_host_edit->text().trimmed();
    if (state_->device_config_.remote_sky_wave_port_spin) config.wave_tcp.port = state_->device_config_.remote_sky_wave_port_spin->value();
    if (state_->device_config_.remote_sky_wave_downsample_spin) config.wave_tcp.downsample_ratio = state_->device_config_.remote_sky_wave_downsample_spin->value();
    if (state_->device_config_.remote_sky_telemetry_basic_spin) config.telemetry.basic_rate_hz = state_->device_config_.remote_sky_telemetry_basic_spin->value();
    if (state_->device_config_.remote_sky_telemetry_feature_spin) config.telemetry.feature_rate_hz = state_->device_config_.remote_sky_telemetry_feature_spin->value();
    if (state_->device_config_.remote_sky_telemetry_waveform_spin) config.telemetry.waveform_rate_hz = state_->device_config_.remote_sky_telemetry_waveform_spin->value();
    if (state_->device_config_.remote_sky_telemetry_heartbeat_spin) config.telemetry.heartbeat_rate_hz = state_->device_config_.remote_sky_telemetry_heartbeat_spin->value();
    if (state_->device_config_.remote_sky_telemetry_status_spin) config.telemetry.status_rate_hz = state_->device_config_.remote_sky_telemetry_status_spin->value();
    if (!config.validate(errorMessage))
    {
        return state_->remote_sky_config_;
    }
    return config;
}

void MainWindow::refreshRemoteSkyConfigRawFromVisual()
{
    if (!state_->device_config_.remote_sky_raw_json_edit)
    {
        return;
    }
    QString error;
    const VaporView::SkyConfig config = state_->remote_sky_config_raw_mode_
        ? state_->remote_sky_config_
        : remoteSkyConfigFromDeviceConfigUi(&error);
    const QSignalBlocker blocker(state_->device_config_.remote_sky_raw_json_edit);
    state_->device_config_.remote_sky_raw_json_edit->setPlainText(
        QJsonDocument(config.toJson()).toJson(QJsonDocument::Indented));
}

bool MainWindow::applyRemoteSkyConfigRawToVisual(QString *errorMessage)
{
    const bool wasRaw = state_->remote_sky_config_raw_mode_;
    state_->remote_sky_config_raw_mode_ = true;
    const VaporView::SkyConfig config = remoteSkyConfigFromDeviceConfigUi(errorMessage);
    state_->remote_sky_config_raw_mode_ = wasRaw;
    if (errorMessage && !errorMessage->isEmpty())
    {
        return false;
    }
    state_->remote_sky_config_ = config;
    setRemoteSkyConfigUi(config);
    return true;
}

void MainWindow::onRemoteSkyConfigRawModeToggled(bool checked)
{
    if (checked == state_->remote_sky_config_raw_mode_)
    {
        updateDeviceConfigTexts();
        return;
    }
    if (checked)
    {
        state_->remote_sky_config_raw_mode_ = true;
        refreshRemoteSkyConfigRawFromVisual();
        if (state_->device_config_.remote_sky_raw_json_edit)
        {
            state_->device_config_.remote_sky_raw_json_edit->setVisible(true);
        }
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Editing the same JSON used by SkyConfig.")
            : QStringLiteral("可直接编辑天空端 SkyConfig JSON。"));
    }
    else
    {
        QString error;
        if (!applyRemoteSkyConfigRawToVisual(&error))
        {
            setRemoteSkyConfigStatus(error, true);
            const QSignalBlocker blocker(state_->device_config_.remote_sky_raw_mode_btn);
            state_->device_config_.remote_sky_raw_mode_btn->setChecked(true);
            return;
        }
        state_->remote_sky_config_raw_mode_ = false;
        if (state_->device_config_.remote_sky_raw_json_edit)
        {
            state_->device_config_.remote_sky_raw_json_edit->setVisible(false);
        }
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Visual form is synchronized from JSON.")
            : QStringLiteral("可视化表单已从 JSON 同步。"));
        markRemoteSkyConfigDirty();
    }
    updateDeviceConfigTexts();
    updateRemoteSkyConfigControlsState();
}

void MainWindow::markRemoteSkyConfigDirty()
{
    if (!isRemoteSkyMode() || state_->remote_sky_config_updating_ui_ || !state_->remote_sky_config_loaded_)
    {
        return;
    }
    if (!state_->remote_sky_config_raw_mode_)
    {
        QString error;
        const VaporView::SkyConfig config = remoteSkyConfigFromDeviceConfigUi(&error);
        if (error.isEmpty())
        {
            state_->remote_sky_config_ = config;
        }
    }
    state_->remote_sky_config_dirty_ = true;
    setRemoteSkyConfigStatus(state_->is_english_
        ? QStringLiteral("Remote Sky config has unapplied edits.")
        : QStringLiteral("天空端配置有未应用改动。"));
    updateRemoteSkyConfigControlsState();
}

void MainWindow::setRemoteSkyConfigStatus(const QString& text, bool error)
{
    state_->remote_sky_config_status_text_ = text;
    state_->remote_sky_config_status_error_ = error;
    if (!state_->device_config_.remote_sky_config_status_lbl)
    {
        return;
    }
    state_->device_config_.remote_sky_config_status_lbl->setText(text);
    state_->device_config_.remote_sky_config_status_lbl->setToolTip(text);
    const bool linkOpen = isUiTestMode() ||
        (state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen());
    QString status = QStringLiteral("normal");
    if (error)
    {
        status = QStringLiteral("error");
    }
    else if (state_->remote_sky_config_loading_ ||
             state_->remote_sky_config_applying_ ||
             state_->remote_sky_config_saving_)
    {
        status = QStringLiteral("pending");
    }
    else if (isRemoteSkyMode() && !linkOpen)
    {
        status = QStringLiteral("disabled");
    }
    else if (state_->remote_sky_config_dirty_)
    {
        status = QStringLiteral("dirty");
    }
    else if (state_->remote_sky_config_loaded_)
    {
        status = QStringLiteral("success");
    }
    state_->device_config_.remote_sky_config_status_lbl->setProperty(
        "status",
        status);
    state_->device_config_.remote_sky_config_status_lbl->style()->unpolish(
        state_->device_config_.remote_sky_config_status_lbl);
    state_->device_config_.remote_sky_config_status_lbl->style()->polish(
        state_->device_config_.remote_sky_config_status_lbl);
}

void MainWindow::updateRemoteSkyConfigControlsState()
{
    const bool remote = isRemoteSkyMode();
    const bool linkOpen = isUiTestMode() ||
        (state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen());
    const bool hasConfig = remote && state_->remote_sky_config_loaded_;
    const bool pending = state_->remote_sky_config_loading_ ||
                         state_->remote_sky_config_applying_ ||
                         state_->remote_sky_config_saving_;
    const bool fieldsEnabled = hasConfig && linkOpen && !pending;
    const QList<QWidget *> targetWidgets = {
        state_->device_config_.epsilon_port_combo,
        state_->device_config_.epsilon_baud_combo,
        state_->device_config_.ptb_port_combo,
        state_->device_config_.ptb_baud_combo,
        state_->device_config_.ptb_rate_combo,
        state_->device_config_.ptb_source_combo,
        state_->device_config_.hmp_port_combo,
        state_->device_config_.hmp_baud_combo,
        state_->device_config_.hmp_rate_combo,
        state_->device_config_.hmp_source_combo,
        state_->device_config_.lidar_port_combo,
        state_->device_config_.lidar_baud_combo,
        state_->device_config_.lidar_rate_combo,
        state_->device_config_.temperature_port_combo,
        state_->device_config_.temperature_baud_combo,
        state_->device_config_.temperature_rate_combo,
        state_->device_config_.ai8_temperature_port_combo,
        state_->device_config_.ai8_temperature_baud_combo,
        state_->device_config_.ai8_temperature_rate_combo,
    };
    if (remote)
    {
        for (QWidget *widget : targetWidgets)
        {
            if (widget)
            {
                widget->setEnabled(fieldsEnabled);
            }
        }
    }

    for (QWidget *widget : {static_cast<QWidget *>(state_->device_config_.epsilon_enabled_check),
                            static_cast<QWidget *>(state_->device_config_.ptb_enabled_check),
                            static_cast<QWidget *>(state_->device_config_.hmp_enabled_check),
                            static_cast<QWidget *>(state_->device_config_.lidar_enabled_check),
                            static_cast<QWidget *>(state_->device_config_.temperature_enabled_check),
                            static_cast<QWidget *>(state_->device_config_.ai8_temperature_enabled_check),
                            static_cast<QWidget *>(state_->device_config_.remote_sky_rd105_slave_spin),
                            static_cast<QWidget *>(state_->device_config_.remote_sky_wave_enabled_check),
                            static_cast<QWidget *>(state_->device_config_.remote_sky_wave_host_edit),
                            static_cast<QWidget *>(state_->device_config_.remote_sky_wave_port_spin),
                            static_cast<QWidget *>(state_->device_config_.remote_sky_wave_downsample_spin),
                            static_cast<QWidget *>(state_->device_config_.remote_sky_telemetry_basic_spin),
                            static_cast<QWidget *>(state_->device_config_.remote_sky_telemetry_feature_spin),
                            static_cast<QWidget *>(state_->device_config_.remote_sky_telemetry_waveform_spin),
                            static_cast<QWidget *>(state_->device_config_.remote_sky_telemetry_heartbeat_spin),
                            static_cast<QWidget *>(state_->device_config_.remote_sky_telemetry_status_spin),
                            static_cast<QWidget *>(state_->device_config_.remote_sky_raw_json_edit)})
    {
        if (widget)
        {
            widget->setEnabled(remote && fieldsEnabled);
        }
    }
    if (state_->device_config_.remote_sky_read_btn)
    {
        state_->device_config_.remote_sky_read_btn->setEnabled(remote && linkOpen && !pending);
    }
    if (state_->device_config_.remote_sky_apply_btn)
    {
        state_->device_config_.remote_sky_apply_btn->setEnabled(remote && linkOpen && hasConfig && state_->remote_sky_config_dirty_ && !pending);
    }
    if (state_->device_config_.remote_sky_save_btn)
    {
        state_->device_config_.remote_sky_save_btn->setEnabled(remote && linkOpen && hasConfig && !pending);
    }
    if (state_->device_config_.remote_sky_raw_mode_btn)
    {
        state_->device_config_.remote_sky_raw_mode_btn->setEnabled(hasConfig && !pending);
    }
    if (state_->device_config_.remote_sky_config_status_lbl &&
        state_->remote_sky_config_status_text_.isEmpty())
    {
        setRemoteSkyConfigStatus(linkOpen
            ? (state_->is_english_
                ? QStringLiteral("Sky config is not loaded. Use Refresh to read it.")
                : QStringLiteral("尚未读取天空端配置，请使用“刷新”读取。"))
            : (state_->is_english_
                ? QStringLiteral("Connect the sky-ground telemetry link before refreshing Sky config.")
                : QStringLiteral("请先连接天地数传链路，再读取天空端配置。")),
            false);
    }
}

void MainWindow::requestRemoteSkyConfigIfAvailable(bool force)
{
    if (!isRemoteSkyMode())
    {
        return;
    }
    if (isUiTestMode())
    {
        VaporView::SkyConfig config = VaporView::SkyConfig::defaults();
        config.epsilon.port = QStringLiteral("UI-TEST-EPSILON");
        config.ptb.port = QStringLiteral("UI-TEST-PTB");
        config.hmp.port = QStringLiteral("UI-TEST-HMP");
        config.lidar.port = QStringLiteral("UI-TEST-LIDAR");
        config.temperature_controller.enabled = true;
        config.temperature_controller.port = QStringLiteral("UI-TEST-RD105");
        config.ai8_temperature_controller.enabled = true;
        config.ai8_temperature_controller.port = QStringLiteral("UI-TEST-AI8");
        state_->remote_sky_config_ = config;
        state_->remote_sky_baseline_config_ = config;
        state_->remote_sky_config_loaded_ = true;
        state_->remote_sky_config_loaded_generation_ = 0;
        state_->remote_sky_config_dirty_ = false;
        state_->remote_sky_config_loading_ = false;
        setRemoteSkyConfigUi(config);
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("[UI Test] Fixed Remote Sky config loaded in memory.")
            : QStringLiteral("[界面测试] 已在内存中载入固定天空端配置。"));
        updateRemoteSkyConfigControlsState();
        return;
    }
    if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
    {
        state_->remote_sky_config_loading_ = false;
        setRemoteSkyConfigStatus(state_->remote_sky_config_loaded_
            ? (state_->is_english_
                ? QStringLiteral("Telemetry link is disconnected. Showing last loaded Remote Sky config.")
                : QStringLiteral("天地数传已断开，当前显示上次读取到的天空端配置。"))
            : (state_->is_english_
                ? QStringLiteral("Connect the sky-ground telemetry link before refreshing Sky config.")
                : QStringLiteral("请先连接天地数传链路，再读取天空端配置。")),
            false);
        updateRemoteSkyConfigControlsState();
        return;
    }

    const quint64 linkGeneration = state_->remote_sky_controller_->linkGeneration();
    const bool pending = state_->remote_sky_config_loading_ ||
                         state_->remote_sky_config_applying_ ||
                         state_->remote_sky_config_saving_;
    if (pending)
    {
        updateRemoteSkyConfigControlsState();
        return;
    }
    if (!force &&
        state_->remote_sky_config_loaded_ &&
        state_->remote_sky_config_loaded_generation_ == linkGeneration)
    {
        updateRemoteSkyConfigControlsState();
        return;
    }
    state_->remote_sky_config_loading_ = true;
    state_->remote_sky_config_read_generation_ = linkGeneration;
    state_->remote_sky_config_read_seq_ = state_->remote_sky_controller_->requestSkyConfig();
    if (state_->remote_sky_config_read_seq_ == 0)
    {
        state_->remote_sky_config_loading_ = false;
        state_->remote_sky_config_read_generation_ = 0;
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Remote Sky config read was not sent.")
            : QStringLiteral("未发送天空端配置读取命令。"),
            true);
    }
    else
    {
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Reading Remote Sky config...")
            : QStringLiteral("正在读取天空端配置..."));
    }
    updateRemoteSkyConfigControlsState();
}

void MainWindow::onRemoteSkyConfigReadClicked()
{
    requestRemoteSkyConfigIfAvailable(true);
}

void MainWindow::onRemoteSkyConfigApplyClicked()
{
    if (state_->remote_sky_config_raw_mode_)
    {
        QString rawError;
        if (!applyRemoteSkyConfigRawToVisual(&rawError))
        {
            setRemoteSkyConfigStatus(rawError, true);
            return;
        }
    }
    QString error;
    const VaporView::SkyConfig config = remoteSkyConfigFromDeviceConfigUi(&error);
    if (!error.isEmpty() || !config.validate(&error))
    {
        setRemoteSkyConfigStatus(error, true);
        return;
    }
    state_->remote_sky_config_ = config;
    if (isUiTestMode())
    {
        state_->remote_sky_baseline_config_ = config;
        state_->remote_sky_config_dirty_ = false;
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("[UI Test] Remote Sky config validated and applied in memory.")
            : QStringLiteral("[界面测试] 天空端配置已验证并在内存中应用。"));
        updateRemoteSkyConfigControlsState();
        return;
    }
    if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
    {
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Telemetry link is disconnected; Apply was not sent.")
            : QStringLiteral("天地数传已断开，未发送应用命令。"),
            true);
        updateRemoteSkyConfigControlsState();
        return;
    }
    state_->remote_sky_config_applying_ = true;
    state_->remote_sky_config_apply_generation_ = state_->remote_sky_controller_->linkGeneration();
    state_->remote_sky_config_apply_seq_ =
        state_->remote_sky_controller_->telemetryService()->setSkyConfig(config.toJson());
    if (state_->remote_sky_config_apply_seq_ == 0)
    {
        state_->remote_sky_config_applying_ = false;
        state_->remote_sky_config_apply_generation_ = 0;
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Remote Sky config apply was not sent.")
            : QStringLiteral("未发送天空端配置应用命令。"),
            true);
    }
    else
    {
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Remote Sky config sent; waiting for apply result...")
            : QStringLiteral("天空端配置已发送，等待应用结果..."));
    }
    updateRemoteSkyConfigControlsState();
}

void MainWindow::onRemoteSkyConfigSaveClicked()
{
    if (isUiTestMode())
    {
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("[UI Test] Simulated Remote Sky config save completed.")
            : QStringLiteral("[界面测试] 已模拟保存天空端配置。"));
        return;
    }
    if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
    {
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Telemetry link is disconnected; Save was not sent.")
            : QStringLiteral("天地数传已断开，未发送保存命令。"),
            true);
        return;
    }
    state_->remote_sky_config_saving_ = true;
    state_->remote_sky_config_save_seq_ =
        state_->remote_sky_controller_->telemetryService()->saveSkyConfig();
    setRemoteSkyConfigStatus(state_->is_english_
        ? QStringLiteral("Saving Remote Sky config on sky...")
        : QStringLiteral("正在保存天空端配置..."));
    updateRemoteSkyConfigControlsState();
}

void MainWindow::onRemoteSkyConfigReceived(const QJsonObject& object)
{
    handleRemoteSkyConfigReceived(object, false);
}

void MainWindow::handleRemoteSkyConfigReceived(const QJsonObject& object, bool bypassGenerationGuard)
{
    if (!bypassGenerationGuard &&
        !isUiTestMode() &&
        (!state_->remote_sky_controller_ ||
         !state_->remote_sky_controller_->isOpen() ||
         state_->remote_sky_controller_->linkGeneration() != state_->remote_sky_config_read_generation_))
    {
        state_->remote_sky_config_loading_ = false;
        clearPendingRemoteWaveTcpConnection();
        updateRemoteSkyConfigControlsState();
        return;
    }
    state_->remote_sky_config_loading_ = false;
    VaporView::SkyConfig config;
    QString error;
    if (!VaporView::SkyConfig::fromJson(object, config, &error))
    {
        if (state_->device_config_.remote_sky_raw_json_edit)
        {
            const QSignalBlocker blocker(state_->device_config_.remote_sky_raw_json_edit);
            state_->device_config_.remote_sky_raw_json_edit->setPlainText(
                QJsonDocument(object).toJson(QJsonDocument::Indented));
            state_->device_config_.remote_sky_raw_json_edit->setVisible(true);
        }
        state_->remote_sky_config_raw_mode_ = true;
        if (state_->device_config_.remote_sky_raw_mode_btn)
        {
            const QSignalBlocker blocker(state_->device_config_.remote_sky_raw_mode_btn);
            state_->device_config_.remote_sky_raw_mode_btn->setChecked(true);
        }
        setRemoteSkyConfigStatus(QStringLiteral("Invalid config from sky: %1").arg(error), true);
        clearPendingRemoteWaveTcpConnection();
        updateRemoteSkyConfigControlsState();
        return;
    }
    state_->remote_sky_config_ = config;
    state_->remote_sky_baseline_config_ = config;
    state_->remote_sky_config_loaded_ = true;
    state_->remote_sky_config_loaded_generation_ = bypassGenerationGuard || isUiTestMode()
        ? 0
        : state_->remote_sky_config_read_generation_;
    state_->remote_sky_config_dirty_ = false;
    setRemoteSkyConfigUi(config);
    setRemoteSkyConfigStatus(state_->is_english_
        ? QStringLiteral("Remote Sky config read from sky.")
        : QStringLiteral("已读取天空端配置。"));
    updateRemoteSkyConfigControlsState();
    if (state_->remote_serial_detection_pending_)
    {
        state_->remote_serial_detection_pending_ = false;
        startRemoteSerialPortDetection();
    }
    if (state_->remote_wave_connect_after_config_read_)
    {
        const QString pendingHost = state_->remote_wave_pending_host_;
        const int pendingPort = state_->remote_wave_pending_port_;
        state_->remote_wave_connect_after_config_read_ = false;
        state_->remote_wave_pending_host_.clear();
        state_->remote_wave_pending_port_ = 0;
        requestRemoteWaveTcpConnection(true, pendingHost, pendingPort);
    }
}

void MainWindow::onRemoteSkyConfigApplyResultReceived(const QJsonObject& result)
{
    handleRemoteSkyConfigApplyResultReceived(result, false);
}

void MainWindow::handleRemoteSkyConfigApplyResultReceived(const QJsonObject& result, bool bypassGenerationGuard)
{
    if (!bypassGenerationGuard &&
        !isUiTestMode() &&
        (!state_->remote_sky_controller_ ||
         !state_->remote_sky_controller_->isOpen() ||
         state_->remote_sky_controller_->linkGeneration() != state_->remote_sky_config_apply_generation_))
    {
        state_->remote_sky_config_applying_ = false;
        clearPendingRemoteWaveTcpConnection();
        updateRemoteSkyConfigControlsState();
        return;
    }
    state_->remote_sky_config_applying_ = false;
    const bool success = result.value(QStringLiteral("success")).toBool(false);
    const QString error = result.value(QStringLiteral("error")).toString();
    const bool connectWaveAfterApply = state_->remote_wave_connect_after_config_apply_;
    state_->remote_wave_connect_after_config_apply_ = false;
    if (success)
    {
        state_->remote_sky_baseline_config_ = state_->remote_sky_config_;
        state_->remote_sky_config_loaded_ = true;
        state_->remote_sky_config_loaded_generation_ = bypassGenerationGuard || isUiTestMode()
            ? 0
            : state_->remote_sky_config_apply_generation_;
        state_->remote_sky_config_dirty_ = false;
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Sky accepted the Remote Sky config.")
            : QStringLiteral("天空端已应用配置。"));
    }
    else
    {
        clearPendingRemoteWaveTcpConnection();
        state_->remote_sky_config_dirty_ = true;
        setRemoteSkyConfigStatus(error.isEmpty()
            ? (state_->is_english_ ? QStringLiteral("Sky failed to apply the config.") : QStringLiteral("天空端应用配置失败。"))
            : (state_->is_english_ ? QStringLiteral("Sky failed to apply the config: %1").arg(error)
                                  : QStringLiteral("天空端应用配置失败：%1").arg(error)),
            true);
    }
    if (state_->device_config_.remote_sky_config_status_lbl)
    {
        state_->device_config_.remote_sky_config_status_lbl->setToolTip(
            QJsonDocument(result).toJson(QJsonDocument::Indented));
    }
    updateRemoteSkyConfigControlsState();
    if (success && connectWaveAfterApply)
    {
        requestRemoteWaveTcpConnection(true);
    }
}

#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
void MainWindow::testInjectRemoteSkyConfig(const QJsonObject& object)
{
    handleRemoteSkyConfigReceived(object, true);
}

void MainWindow::testInjectRemoteSkyApplyResult(const QJsonObject& result)
{
    handleRemoteSkyConfigApplyResultReceived(result, true);
}

QJsonObject MainWindow::testRemoteSkyConfigFromDeviceConfigUi(QString *errorMessage) const
{
    return remoteSkyConfigFromDeviceConfigUi(errorMessage).toJson();
}

QString MainWindow::testRemoteSkyConfigStatusText() const
{
    return state_->remote_sky_config_status_text_;
}
#endif
