#ifndef VAPORVIEW_SINGLE_LEVEL_POPUP_MENU_H_
#define VAPORVIEW_SINGLE_LEVEL_POPUP_MENU_H_

#include <QIcon>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QPointer>
#include <QSize>
#include <QToolButton>

#include <functional>

class QAction;
class QLabel;
class QEnterEvent;
class QFocusEvent;
class QHideEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QWidgetAction;

namespace VaporView
{

enum class SingleLevelPopupTextAlignment
{
    Left,
    Center
};

enum class SingleLevelPopupAnchor
{
    Left,
    Right
};

class SingleLevelPopupMenuRow final : public QToolButton
{
    Q_OBJECT

public:
    explicit SingleLevelPopupMenuRow(QWidget *parent = nullptr);

    QString text() const;
    void setText(const QString& text);
    void setChecked(bool checked);
    bool isChecked() const;
    void setCheckIcon(const QIcon& icon);
    void setCheckIconSize(const QSize& size);
    void setTextAlignment(SingleLevelPopupTextAlignment alignment);
    void setTextFixedWidth(int width);
    void setCheckSlotWidth(int width);
    void setHorizontalPadding(int left, int right);
    void setRowSpacing(int spacing);
    void setRowHeight(int height);
    void setMinimumRowWidth(int width);
    void setCloseOnClick(bool close);
    bool closeOnClick() const;
    QLabel *textLabel() const;
    QLabel *checkLabel() const;
    void clearHover();
    void syncFromDefaultAction();
    void setKeyboardFocusHighlight(bool active);
    void refreshTheme();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void changeEvent(QEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void layoutChildren();
    void updateCheckIcon();
    void updateCheckedState(bool checked);
    void updateLabelStyles();
    void setHovered(bool hovered);

    QLabel *text_label_;
    QLabel *check_label_;
    QIcon check_icon_;
    QSize check_icon_size_ = QSize(16, 16);
    SingleLevelPopupTextAlignment text_alignment_ = SingleLevelPopupTextAlignment::Left;
    int text_fixed_width_ = 0;
    int check_slot_width_ = 18;
    int left_padding_ = 18;
    int right_padding_ = 14;
    int row_spacing_ = 6;
    int row_height_ = 32;
    int minimum_row_width_ = 72;
    bool checked_ = false;
    bool hovered_ = false;
    bool keyboard_focus_highlight_ = false;
    bool syncing_from_action_ = false;
    bool close_on_click_ = true;
};

class SingleLevelPopupMenu final : public QMenu
{
    Q_OBJECT

public:
    explicit SingleLevelPopupMenu(QWidget *parent = nullptr);

    QWidgetAction *addRow(SingleLevelPopupMenuRow *row);
    QList<SingleLevelPopupMenuRow *> rows() const;
    void setCornerRadius(int radius);
    int cornerRadius() const;
    void setPanelPadding(int padding);
    int panelPadding() const;
    void setShadowMargin(int margin);
    void setBottomShadowMargin(int margin);
    void setPanelContentWidth(int width);
    void refreshTheme();
    void popupFrom(QWidget *anchor,
                   SingleLevelPopupAnchor anchorEdge = SingleLevelPopupAnchor::Left,
                   const QPoint& offset = QPoint());
    void applyRoundedMask();

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    static constexpr int kDefaultShadowMargin = 22;
    static constexpr int kDefaultBottomShadowMargin = 50;

    int shadowMargin() const;
    int bottomShadowMargin() const;
    QRect panelRect() const;
    QSize maskSize() const;
    QList<SingleLevelPopupMenuRow *> focusableRows() const;
    void focusFirstAvailableRow();
    void focusRow(SingleLevelPopupMenuRow *row, Qt::FocusReason reason = Qt::OtherFocusReason);
    bool handleNavigationKey(QKeyEvent *event, SingleLevelPopupMenuRow *sourceRow);
    void prepareKeepOpenAfterTrigger(SingleLevelPopupMenuRow *row);
    void restoreKeptOpenPopup();
    void syncRowWidths();
    void clearRowHoverStates();

    int corner_radius_ = 10;
    int panel_padding_ = 12;
    int shadow_margin_ = kDefaultShadowMargin;
    int bottom_shadow_margin_ = kDefaultBottomShadowMargin;
    QPointer<QWidget> focus_restore_widget_;
    bool keep_open_after_trigger_ = false;
    QPoint keep_open_position_;
    QPointer<SingleLevelPopupMenuRow> keep_open_focus_row_;
};

}

#endif
