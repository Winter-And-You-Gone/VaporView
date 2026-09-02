#ifndef VAPORVIEW_BAUD_RATE_COMBO_SUPPORT_H_
#define VAPORVIEW_BAUD_RATE_COMBO_SUPPORT_H_

#include "SerialBaudRateCapabilities.h"

#include <QComboBox>
#include <QEvent>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QSignalBlocker>

#include <limits>

namespace VaporView
{

inline constexpr const char *kSerialBaudRateComboProperty = "_vv_serial_baud_rate_combo";
inline constexpr const char *kSerialBaudRateLastValidProperty = "_vv_serial_baud_rate_last_valid";
inline constexpr const char *kSerialBaudRateEditingGuardProperty = "_vv_serial_baud_rate_editing_guard";
inline constexpr const char *kSerialBaudRateEditFilterProperty = "_vv_serial_baud_rate_edit_filter";
inline constexpr const char *kSerialBaudRateEditingFinishedBoundProperty = "_vv_serial_baud_rate_editing_finished_bound";
inline constexpr const char *kSerialBaudRateInputModeProperty = "_vv_serial_baud_rate_input_mode";
inline constexpr const char *kSerialBaudRateCustomMinimumProperty = "_vv_serial_baud_rate_custom_minimum";
inline constexpr const char *kSerialBaudRateCustomMaximumProperty = "_vv_serial_baud_rate_custom_maximum";

inline bool isSerialBaudRateCombo(const QComboBox *combo)
{
    return combo && combo->property(kSerialBaudRateComboProperty).toBool();
}

inline BaudRateCapabilities serialBaudRateComboCapabilities(const QComboBox *combo)
{
    BaudRateCapabilities capabilities;
    if (!combo || !isSerialBaudRateCombo(combo))
    {
        return capabilities;
    }
    capabilities.inputMode = static_cast<BaudRateInputMode>(
        combo->property(kSerialBaudRateInputModeProperty).toInt());
    capabilities.customMinimum = combo->property(kSerialBaudRateCustomMinimumProperty).toInt();
    capabilities.customMaximum = combo->property(kSerialBaudRateCustomMaximumProperty).toInt();
    for (int index = 0; index < combo->count(); ++index)
    {
        capabilities.presets.append(combo->itemText(index));
    }
    return capabilities;
}

inline bool serialBaudRateComboAccepts(const QComboBox *combo, int baudRate)
{
    return !isSerialBaudRateCombo(combo) ||
        isBaudRateSupported(serialBaudRateComboCapabilities(combo), baudRate);
}

inline bool setSerialBaudRateComboText(QComboBox *combo, const QString& value)
{
    if (!combo)
    {
        return false;
    }
    const QString normalized = normalizedSerialBaudRateText(value);
    const auto baudRate = parseSerialBaudRate(normalized);
    if (!baudRate || !serialBaudRateComboAccepts(combo, *baudRate))
    {
        return false;
    }

    const QSignalBlocker blocker(combo);
    const int index = combo->findText(normalized, Qt::MatchExactly);
    if (index >= 0)
    {
        combo->setCurrentIndex(index);
    }
    else
    {
        // NoInsert keeps manually entered values out of the preset model.
        combo->setEditText(normalized);
    }
    combo->setProperty(kSerialBaudRateLastValidProperty, normalized);
    return true;
}

inline QString serialBaudRateComboText(const QComboBox *combo)
{
    return combo ? combo->currentText().trimmed() : QString();
}

inline std::optional<int> serialBaudRateComboValue(const QComboBox *combo)
{
    const auto baudRate = combo ? parseSerialBaudRate(combo->currentText()) : std::nullopt;
    return baudRate && serialBaudRateComboAccepts(combo, *baudRate)
        ? baudRate
        : std::nullopt;
}

class SerialBaudRateComboEditFilter final : public QObject
{
public:
    explicit SerialBaudRateComboEditFilter(QComboBox *combo, QObject *parent)
        : QObject(parent)
        , combo_(combo)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress)
        {
            const auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Escape && combo_)
            {
                const QString last = combo_->property(kSerialBaudRateLastValidProperty).toString();
                if (!last.isEmpty())
                {
                    setSerialBaudRateComboText(combo_, last);
                    event->accept();
                    return true;
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QPointer<QComboBox> combo_;
};

inline void configureSerialBaudRateCombo(QComboBox *combo,
                                         const BaudRateCapabilities& capabilities,
                                         const QString& defaultValue = QString())
{
    if (!combo)
    {
        return;
    }
    const QString previousValue = combo->currentText().trimmed();
    const QSignalBlocker blocker(combo);
    combo->setProperty(kSerialBaudRateComboProperty, true);
    combo->setProperty(kSerialBaudRateInputModeProperty,
                       static_cast<int>(capabilities.inputMode));
    combo->setProperty(kSerialBaudRateCustomMinimumProperty, capabilities.customMinimum);
    combo->setProperty(kSerialBaudRateCustomMaximumProperty, capabilities.customMaximum);
    combo->setEditable(capabilities.inputMode == BaudRateInputMode::PresetAndCustom);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setDuplicatesEnabled(false);
    if (capabilities.inputMode == BaudRateInputMode::PresetAndCustom && combo->lineEdit())
    {
        combo->lineEdit()->setValidator(
            new QIntValidator(capabilities.customMinimum,
                              capabilities.customMaximum,
                              combo->lineEdit()));
        combo->lineEdit()->setCompleter(nullptr);
        if (!combo->lineEdit()->property(kSerialBaudRateEditFilterProperty).toBool())
        {
            combo->lineEdit()->installEventFilter(
                new SerialBaudRateComboEditFilter(combo, combo->lineEdit()));
            combo->lineEdit()->setProperty(kSerialBaudRateEditFilterProperty, true);
        }
        if (!combo->lineEdit()->property(kSerialBaudRateEditingFinishedBoundProperty).toBool())
        {
            QObject::connect(combo->lineEdit(),
                             &QLineEdit::editingFinished,
                             combo,
                             [combo]() {
                                 const QString normalized = normalizedSerialBaudRateText(combo->currentText());
                                 if (setSerialBaudRateComboText(combo, normalized))
                                 {
                                     return;
                                 }
                                 const QString last = combo->property(kSerialBaudRateLastValidProperty).toString();
                                 if (!last.isEmpty())
                                 {
                                     setSerialBaudRateComboText(combo, last);
                                 }
                             });
            combo->lineEdit()->setProperty(kSerialBaudRateEditingFinishedBoundProperty, true);
        }
    }
    combo->clear();
    for (const QString& preset : capabilities.presets)
    {
        const QString normalized = normalizedSerialBaudRateText(preset);
        if (!normalized.isEmpty() && combo->findText(normalized, Qt::MatchExactly) < 0)
        {
            combo->addItem(normalized, normalized.toInt());
            combo->setItemData(combo->count() - 1,
                               Qt::AlignCenter,
                               Qt::TextAlignmentRole);
        }
    }
    if (!combo->property(kSerialBaudRateEditingGuardProperty).toBool())
    {
        combo->setProperty(kSerialBaudRateEditingGuardProperty, true);
        QObject::connect(combo,
                         &QComboBox::currentTextChanged,
                         combo,
                         [combo](const QString& text) {
                             const auto baudRate = parseSerialBaudRate(text);
                             if (baudRate && serialBaudRateComboAccepts(combo, *baudRate))
                             {
                                 combo->setProperty(kSerialBaudRateLastValidProperty,
                                                    QString::number(*baudRate));
                             }
                         });
    }
    const QString requested = defaultValue.isEmpty()
        ? (previousValue.isEmpty() && combo->count() > 0 ? combo->itemText(0) : previousValue)
        : defaultValue;
    if (!setSerialBaudRateComboText(combo, requested) && combo->count() > 0)
    {
        setSerialBaudRateComboText(combo, combo->itemText(0));
    }
}

}  // namespace VaporView

#endif
