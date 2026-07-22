#ifndef VaporView_SERIAL_PORT_COMBO_SUPPORT_H_
#define VaporView_SERIAL_PORT_COMBO_SUPPORT_H_

#include "shared/theme/AppTheme.h"

#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QComboBox>
#include <QFontMetrics>
#include <QPainter>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionViewItem>

namespace VaporView
{

inline constexpr int kSerialPortHistoryItemRole = Qt::UserRole + 1;

inline bool serialPortNamesMatch(const QString& first, const QString& second)
{
    const QString normalizedFirst = first.trimmed();
    const QString normalizedSecond = second.trimmed();
    if (normalizedFirst.isEmpty() || normalizedSecond.isEmpty())
    {
        return false;
    }
#ifdef Q_OS_WIN
    return normalizedFirst.compare(normalizedSecond, Qt::CaseInsensitive) == 0;
#else
    return normalizedFirst == normalizedSecond;
#endif
}

inline bool serialPortListContains(const QStringList& ports, const QString& port)
{
    for (const QString& candidate : ports)
    {
        if (serialPortNamesMatch(candidate, port))
        {
            return true;
        }
    }
    return false;
}

inline QStringList rememberedSerialPortHistory()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("SerialPortHistory"));
    return settings.value(QStringLiteral("ports")).toStringList();
}

inline bool isRememberedSerialPort(const QString& port)
{
    return serialPortListContains(rememberedSerialPortHistory(), port);
}

inline void rememberSerialPort(const QString& port)
{
    const QString normalized = port.trimmed();
    if (normalized.isEmpty() ||
        normalized == QStringLiteral("未选择") ||
        normalized.startsWith(QStringLiteral("--")))
    {
        return;
    }

    QStringList history = rememberedSerialPortHistory();
    if (serialPortListContains(history, normalized))
    {
        return;
    }
    history.push_back(normalized);
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("SerialPortHistory"));
    settings.setValue(QStringLiteral("ports"), history);
}

class SerialPortPopupDelegate final : public QStyledItemDelegate
{
public:
    explicit SerialPortPopupDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
        setProperty("vaporViewSerialPortHistoryDelegate", true);
        setProperty("vaporViewLocalSerialHistoryDelegate", true);
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        constexpr int badgeWidth = 18;
        constexpr int badgeGap = 4;
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.rwidth() += badgeWidth + badgeGap;
        return size;
    }

    void paint(QPainter *painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        constexpr int badgeWidth = 18;
        constexpr int badgeGap = 4;
        constexpr int badgeLeftInset = 4;
        constexpr int textOpticalCorrection = 1;

        const bool isHistory = index.data(kSerialPortHistoryItemRole).toBool();
        const bool dark = isDarkThemeEnabled();
        const bool highlighted = option.state.testFlag(QStyle::State_MouseOver);
        if (highlighted)
        {
            painter->fillRect(option.rect, appThemeColor(AppThemeColor::MenuHover, dark));
        }

        QStyleOptionViewItem textOption(option);
        initStyleOption(&textOption, index);
        const int desiredTextLeft = option.rect.left() + badgeLeftInset + badgeWidth + badgeGap;
        const QString displayText = index.data(Qt::DisplayRole).toString();
        const int textBearing = QFontMetrics(textOption.font).boundingRect(displayText).left();
        QStyle *style = option.widget ? option.widget->style() : nullptr;
        const QRect defaultTextRect = style
                                          ? style->subElementRect(
                                                QStyle::SE_ItemViewItemText,
                                                &textOption,
                                                option.widget)
                                          : QRect();
        if (defaultTextRect.isValid())
        {
            const int focusFrameMargin = style->pixelMetric(
                QStyle::PM_FocusFrameHMargin,
                &textOption,
                option.widget);
            textOption.rect.translate(
                desiredTextLeft -
                    (defaultTextRect.left() + focusFrameMargin + textBearing +
                     textOpticalCorrection),
                0);
        }
        textOption.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver);
        QStyledItemDelegate::paint(painter, textOption, index);

        if (!isHistory)
        {
            return;
        }

        const int badgeHeight = option.rect.height();
        const QRect badgeRect(option.rect.left() + badgeLeftInset,
                              option.rect.center().y() - badgeHeight / 2,
                              badgeWidth,
                              badgeHeight);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(appThemeColor(AppThemeColor::FieldBorder, dark));
        painter->setBrush(appThemeColor(AppThemeColor::DisabledFill, dark));
        painter->drawRoundedRect(badgeRect, 9, 9);

        QFont badgeFont(option.font);
        badgeFont.setPointSizeF(qMax<qreal>(7.0, badgeFont.pointSizeF() * 0.72));
        badgeFont.setWeight(QFont::Medium);
        painter->setFont(badgeFont);
        painter->setPen(appThemeColor(AppThemeColor::MenuMetaText, dark));
        painter->drawText(badgeRect, Qt::AlignCenter, QStringLiteral("历\n史"));
        painter->restore();
    }
};

inline void installSerialPortPopupDelegate(QComboBox *combo)
{
    if (!combo || !combo->view())
    {
        return;
    }
    QAbstractItemDelegate *delegate = combo->view()->itemDelegate();
    if (!delegate || !delegate->property("vaporViewSerialPortHistoryDelegate").toBool())
    {
        combo->view()->setItemDelegate(new SerialPortPopupDelegate(combo->view()));
    }
}

}  // namespace VaporView

#endif
