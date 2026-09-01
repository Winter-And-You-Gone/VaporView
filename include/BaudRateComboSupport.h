#ifndef VAPORVIEW_BAUD_RATE_COMBO_SUPPORT_H_
#define VAPORVIEW_BAUD_RATE_COMBO_SUPPORT_H_

#include "SerialBaudRate.h"

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

inline bool isSerialBaudRateCombo(const QComboBox *combo)
{
    return combo && combo->property(kSerialBaudRateComboProperty).toBool();
}

inline bool setSerialBaudRateComboText(QComboBox *combo, const QString& value)
{
    if (!combo)
    {
        return false;
    }
    const QString normalized = normalizedSerialBaudRateText(value);
    if (normalized.isEmpty())
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
    return combo ? parseSerialBaudRate(combo->currentText()) : std::nullopt;
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
                                         const QStringList& presets,
                                         const QString& defaultValue = QString())
{
    if (!combo)
    {
        return;
    }
    combo->setProperty(kSerialBaudRateComboProperty, true);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setDuplicatesEnabled(false);
    if (combo->lineEdit())
    {
        combo->lineEdit()->setValidator(
            new QIntValidator(1, std::numeric_limits<int>::max(), combo->lineEdit()));
        combo->lineEdit()->setCompleter(nullptr);
        combo->lineEdit()->installEventFilter(
            new SerialBaudRateComboEditFilter(combo, combo->lineEdit()));
    }
    for (const QString& preset : presets)
    {
        const QString normalized = normalizedSerialBaudRateText(preset);
        if (!normalized.isEmpty() && combo->findText(normalized, Qt::MatchExactly) < 0)
        {
            combo->addItem(normalized, normalized.toInt());
        }
    }
    if (combo->property(kSerialBaudRateEditingGuardProperty).toBool())
    {
        return;
    }
    combo->setProperty(kSerialBaudRateEditingGuardProperty, true);
    QObject::connect(combo,
                     &QComboBox::currentTextChanged,
                     combo,
                     [combo](const QString& text) {
                         const QString normalized = normalizedSerialBaudRateText(text);
                         if (!normalized.isEmpty())
                         {
                             combo->setProperty(kSerialBaudRateLastValidProperty, normalized);
                         }
                     });
    if (combo->lineEdit())
    {
        QObject::connect(combo->lineEdit(),
                         &QLineEdit::editingFinished,
                         combo,
                         [combo]() {
                             const QString normalized = normalizedSerialBaudRateText(combo->currentText());
                             if (!normalized.isEmpty())
                             {
                                 setSerialBaudRateComboText(combo, normalized);
                                 return;
                             }
                             const QString last = combo->property(kSerialBaudRateLastValidProperty).toString();
                             if (!last.isEmpty())
                             {
                                 setSerialBaudRateComboText(combo, last);
                             }
                         });
    }
    const QString requested = defaultValue.isEmpty()
        ? (combo->currentText().isEmpty() && combo->count() > 0 ? combo->itemText(0) : combo->currentText())
        : defaultValue;
    setSerialBaudRateComboText(combo, requested);
}

}  // namespace VaporView

#endif
