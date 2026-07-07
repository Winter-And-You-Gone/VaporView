#ifndef VAPORVIEW_SINGLE_LEVEL_POPUP_MENU_H_
#define VAPORVIEW_SINGLE_LEVEL_POPUP_MENU_H_

#include <QIcon>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QSize>
#include <QWidget>

#include <functional>

class QLabel;
class QEnterEvent;
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

class SingleLevelPopupMenuRow final : public QWidget
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
    void refreshTheme();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void clicked();

protected:
    void changeEvent(QEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void layoutChildren();
    void updateCheckIcon();
    void updateLabelStyles();

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
    void refreshTheme();
    void popupFrom(QWidget *anchor,
                   SingleLevelPopupAnchor anchorEdge = SingleLevelPopupAnchor::Left,
                   const QPoint& offset = QPoint());
    void applyRoundedMask();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int shadowMargin() const;
    QRect panelRect() const;
    QSize maskSize() const;
    void syncRowWidths();

    int corner_radius_ = 16;
    int panel_padding_ = 8;
};

}

#endif
