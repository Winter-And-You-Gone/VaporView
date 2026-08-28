#include "shared/theme/SingleLevelPopupComboBox.h"

#include "shared/theme/SingleLevelPopupMenu.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QModelIndex>
#include <QVariant>
#include <QWidgetAction>

#include <algorithm>
#include <utility>

namespace VaporView
{
namespace
{
QString stableObjectToken(QString value)
{
    value = value.trimmed();
    if (value.isEmpty())
    {
        return QString();
    }

    QString result;
    result.reserve(value.size());
    bool uppercaseNext = false;
    for (const QChar ch : value)
    {
        if (ch.isLetterOrNumber())
        {
            const QString lower = ch.toLower();
            result += uppercaseNext && !result.isEmpty() ? lower.toUpper() : lower;
            uppercaseNext = false;
        }
        else
        {
            uppercaseNext = true;
        }
    }
    return result;
}

QString itemObjectToken(const QComboBox *combo, int index)
{
    if (!combo)
    {
        return QStringLiteral("item%1").arg(index);
    }

    const QVariant data = combo->itemData(index);
    if (data.isValid())
    {
        const QString dataToken = stableObjectToken(data.toString());
        if (!dataToken.isEmpty())
        {
            return dataToken;
        }
    }
    const QString textToken = stableObjectToken(combo->itemText(index));
    if (!textToken.isEmpty())
    {
        return textToken;
    }
    return QStringLiteral("item%1").arg(index);
}
}

SingleLevelPopupComboBox::SingleLevelPopupComboBox(QWidget *parent)
    : QComboBox(parent)
    , popup_menu_(new SingleLevelPopupMenu(this))
{
    popup_menu_->setObjectName(QStringLiteral("singleLevelComboPopupMenu"));
    popup_menu_->setCornerRadius(10);
    popup_menu_->setPanelPadding(12);
    setProperty("usesSingleLevelPopupMenu", true);
}

void SingleLevelPopupComboBox::showPopup()
{
    rebuildPopupRows();
    popup_menu_->popupFrom(this);
}

void SingleLevelPopupComboBox::hidePopup()
{
    if (popup_menu_)
    {
        popup_menu_->hide();
    }
    QComboBox::hidePopup();
}

void SingleLevelPopupComboBox::setShowSelectionCheck(bool show)
{
    show_selection_check_ = show;
}

void SingleLevelPopupComboBox::setPopupFitContents(bool fit)
{
    popup_fit_contents_ = fit;
}

void SingleLevelPopupComboBox::setSelectionCheckIconProvider(SelectionCheckIconProvider provider)
{
    selection_check_icon_provider_ = std::move(provider);
}

SingleLevelPopupMenu *SingleLevelPopupComboBox::popupMenu() const
{
    return popup_menu_;
}

bool SingleLevelPopupComboBox::itemEnabled(int index) const
{
    if (!model())
    {
        return true;
    }
    const QModelIndex modelIndex = model()->index(index, modelColumn(), rootModelIndex());
    return !modelIndex.isValid() || (modelIndex.flags() & Qt::ItemIsEnabled);
}

void SingleLevelPopupComboBox::rebuildPopupRows()
{
    if (!popup_menu_)
    {
        return;
    }

    popup_menu_->setFont(font());
    const QList<SingleLevelPopupMenuRow *> staleRows = popup_menu_->rows();
    popup_menu_->clear();
    for (SingleLevelPopupMenuRow *row : staleRows)
    {
        delete row;
    }
    const QIcon checkIcon = show_selection_check_ && selection_check_icon_provider_
        ? selection_check_icon_provider_()
        : QIcon();
    int selectedIndex = currentIndex();
    const QString visibleText = currentText();
    if (!visibleText.isEmpty())
    {
        const int visibleTextIndex = findText(visibleText);
        if (visibleTextIndex >= 0)
        {
            selectedIndex = visibleTextIndex;
        }
    }
    for (int i = 0; i < count(); ++i)
    {
        auto *row = new SingleLevelPopupMenuRow(popup_menu_);
        row->setFont(font());
        row->setText(itemText(i));
        row->setChecked(show_selection_check_ && i == selectedIndex);
        if (show_selection_check_)
        {
            row->setCheckIcon(checkIcon);
            row->setCheckIconSize(QSize(16, 16));
        }
        row->setTextAlignment(SingleLevelPopupTextAlignment::Left);
        row->setHorizontalPadding(18, 14);
        row->setCheckSlotWidth(show_selection_check_ ? 18 : 0);
        row->setRowSpacing(show_selection_check_ ? 6 : 0);
        row->setRowHeight(40);
        row->setMinimumRowWidth(width());
        row->setEnabled(itemEnabled(i));
        QWidgetAction *action = popup_menu_->addRow(row);
        if (!action)
        {
            continue;
        }
        const QString comboName = objectName().isEmpty() ? QStringLiteral("singleLevelCombo") : objectName();
        action->setObjectName(QStringLiteral("%1MenuAction_%2").arg(comboName, itemObjectToken(this, i)));
        action->setData(i);
        action->setEnabled(row->isEnabled());
        connect(action, &QAction::triggered, this, [this, i]() {
            if (i >= 0 && i < count())
            {
                setCurrentIndex(i);
            }
        });
    }
    popup_menu_->refreshTheme();
    int popupContentWidth = width();
    if (popup_fit_contents_)
    {
        for (SingleLevelPopupMenuRow *row : popup_menu_->rows())
        {
            popupContentWidth = std::max(popupContentWidth, row->sizeHint().width());
        }
    }
    popup_menu_->setPanelContentWidth(popupContentWidth);
}

} // namespace VaporView
