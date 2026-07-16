#ifndef VAPORVIEW_SINGLE_LEVEL_POPUP_COMBO_BOX_H_
#define VAPORVIEW_SINGLE_LEVEL_POPUP_COMBO_BOX_H_

#include <QComboBox>
#include <QIcon>

#include <functional>

namespace VaporView
{

class SingleLevelPopupMenu;

class SingleLevelPopupComboBox final : public QComboBox
{
public:
    using SelectionCheckIconProvider = std::function<QIcon()>;

    explicit SingleLevelPopupComboBox(QWidget *parent = nullptr);

    void showPopup() override;
    void hidePopup() override;

    void setShowSelectionCheck(bool show);
    void setPopupFitContents(bool fit);
    void setSelectionCheckIconProvider(SelectionCheckIconProvider provider);
    SingleLevelPopupMenu *popupMenu() const;

private:
    bool itemEnabled(int index) const;
    void rebuildPopupRows();

    SingleLevelPopupMenu *popup_menu_ = nullptr;
    SelectionCheckIconProvider selection_check_icon_provider_;
    bool show_selection_check_ = true;
    bool popup_fit_contents_ = false;
};

} // namespace VaporView

#endif
