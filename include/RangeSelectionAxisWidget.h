#ifndef VaporView_RANGE_SELECTION_AXIS_WIDGET_H_
#define VaporView_RANGE_SELECTION_AXIS_WIDGET_H_

#include <QMouseEvent>
#include <QPainter>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <functional>

class RangeSelectionAxisWidget : public QWidget
{
public:
    explicit RangeSelectionAxisWidget(QWidget *parent = nullptr)
        : QWidget(parent)
        , total_count_(0)
        , start_index_(0)
        , visible_count_(0)
        , compact_mode_(false)
        , drag_mode_(DragMode::None)
        , drag_anchor_index_(0)
        , drag_anchor_count_(0)
        , empty_text_(tr("No range"))
    {
        applySizing();
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMouseTracking(true);
    }

    void setRangeChangedCallback(std::function<void(int, int)> callback)
    {
        on_range_changed_ = std::move(callback);
    }

    void setCompactMode(bool compact)
    {
        if (compact_mode_ == compact)
        {
            return;
        }

        compact_mode_ = compact;
        applySizing();
        update();
    }

    void setRange(int totalCount, int startIndex, int visibleCount)
    {
        const int normalizedTotal = std::max(0, totalCount);
        if (normalizedTotal <= 0)
        {
            if (total_count_ == 0 && start_index_ == 0 && visible_count_ == 0)
            {
                return;
            }
            total_count_ = 0;
            start_index_ = 0;
            visible_count_ = 0;
            update();
            return;
        }

        int normalizedStart = 0;
        int normalizedVisible = 0;
        if (visibleCount <= 0 || visibleCount >= normalizedTotal)
        {
            normalizedStart = 0;
            normalizedVisible = normalizedTotal;
        }
        else
        {
            normalizedVisible = std::clamp(visibleCount, 1, normalizedTotal);
            normalizedStart = std::clamp(startIndex, 0, std::max(0, normalizedTotal - normalizedVisible));
        }

        if (total_count_ == normalizedTotal &&
            start_index_ == normalizedStart &&
            visible_count_ == normalizedVisible)
        {
            return;
        }

        total_count_ = normalizedTotal;
        start_index_ = normalizedStart;
        visible_count_ = normalizedVisible;
        update();
    }

    void setEmptyText(const QString& text)
    {
        empty_text_ = text;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor("#ffffff"));

        const QRectF trackRect = compact_mode_
            ? rect().adjusted(8, 11, -8, -5)
            : rect().adjusted(10, 16, -10, -16);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#e7edf5"));
        painter.drawRoundedRect(trackRect, 4, 4);

        if (total_count_ <= 0)
        {
            painter.setPen(QColor("#7a8899"));
            painter.drawText(rect(), Qt::AlignCenter, empty_text_);
            return;
        }

        const int startIndex = currentStartIndex();
        const int endIndex = currentEndIndex();
        const qreal leftX = positionForIndex(startIndex, trackRect);
        const qreal rightX = positionForIndex(endIndex, trackRect);
        const QRectF selectionRect(leftX, trackRect.top(), std::max(8.0, rightX - leftX), trackRect.height());

        painter.setBrush(QColor("#7fb3ff"));
        painter.drawRoundedRect(selectionRect, 4, 4);

        const QRectF leftHandle(leftX - 4, trackRect.top() - 4, 8, trackRect.height() + 8);
        const QRectF rightHandle(rightX - 4, trackRect.top() - 4, 8, trackRect.height() + 8);
        painter.setBrush(QColor("#2f6fd6"));
        painter.drawRoundedRect(leftHandle, 3, 3);
        painter.drawRoundedRect(rightHandle, 3, 3);

        painter.setPen(QColor("#5e6b78"));
        const qreal labelHeight = compact_mode_ ? 10.0 : 14.0;
        painter.drawText(QRectF(trackRect.left(), 0, trackRect.width() * 0.5, labelHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString("%1").arg(startIndex + 1));
        painter.drawText(QRectF(trackRect.left(), 0, trackRect.width(), labelHeight),
                         Qt::AlignCenter | Qt::AlignVCenter,
                         QString("%1-%2").arg(startIndex + 1).arg(endIndex + 1));
        painter.drawText(QRectF(trackRect.left(), 0, trackRect.width(), labelHeight),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString("%1").arg(total_count_));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (total_count_ <= 0 || event->button() != Qt::LeftButton)
        {
            QWidget::mousePressEvent(event);
            return;
        }

        const QRectF trackRect = compact_mode_
            ? rect().adjusted(8, 11, -8, -5)
            : rect().adjusted(10, 16, -10, -16);
        const int startIndex = currentStartIndex();
        const int endIndex = currentEndIndex();
        const qreal leftX = positionForIndex(startIndex, trackRect);
        const qreal rightX = positionForIndex(endIndex, trackRect);
        const qreal handleTouchWidth = compact_mode_ ? 14.0 : 16.0;
        const qreal handleTouchTopInset = compact_mode_ ? 4.0 : 6.0;
        const qreal handleTouchExtraHeight = compact_mode_ ? 8.0 : 12.0;
        const QRectF leftHandle(leftX - handleTouchWidth * 0.5, trackRect.top() - handleTouchTopInset,
            handleTouchWidth, trackRect.height() + handleTouchExtraHeight);
        const QRectF rightHandle(rightX - handleTouchWidth * 0.5, trackRect.top() - handleTouchTopInset,
            handleTouchWidth, trackRect.height() + handleTouchExtraHeight);
        const QRectF selectionRect(leftX, trackRect.top(), std::max(8.0, rightX - leftX), trackRect.height());

        if (leftHandle.contains(event->position()))
        {
            drag_mode_ = DragMode::LeftHandle;
        }
        else if (rightHandle.contains(event->position()))
        {
            drag_mode_ = DragMode::RightHandle;
        }
        else if (selectionRect.contains(event->position()))
        {
            drag_mode_ = DragMode::Selection;
        }
        else
        {
            drag_mode_ = DragMode::None;
            jumpSelectionTo(event->position().x(), trackRect);
            return;
        }

        drag_anchor_index_ = startIndex;
        drag_anchor_count_ = currentVisibleCount();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (total_count_ <= 0)
        {
            QWidget::mouseMoveEvent(event);
            return;
        }

        const QRectF trackRect = compact_mode_
            ? rect().adjusted(8, 11, -8, -5)
            : rect().adjusted(10, 16, -10, -16);
        if (drag_mode_ == DragMode::None)
        {
            QWidget::mouseMoveEvent(event);
            return;
        }

        const int hoveredIndex = indexFromPosition(event->position().x(), trackRect);
        const int oldStart = currentStartIndex();
        const int oldCount = currentVisibleCount();

        if (drag_mode_ == DragMode::LeftHandle)
        {
            const int endIndex = oldStart + oldCount - 1;
            const int newStart = std::clamp(hoveredIndex, 0, endIndex);
            applyRangeChange(newStart, endIndex - newStart + 1);
        }
        else if (drag_mode_ == DragMode::RightHandle)
        {
            const int startIndex = oldStart;
            const int newEnd = std::clamp(hoveredIndex, startIndex, total_count_ - 1);
            applyRangeChange(startIndex, newEnd - startIndex + 1);
        }
        else if (drag_mode_ == DragMode::Selection)
        {
            const int halfCount = std::max(0, drag_anchor_count_ / 2);
            int newStart = hoveredIndex - halfCount;
            newStart = std::clamp(newStart, 0, std::max(0, total_count_ - drag_anchor_count_));
            applyRangeChange(newStart, drag_anchor_count_);
        }

        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            drag_mode_ = DragMode::None;
            unsetCursor();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && total_count_ > 0)
        {
            applyRangeChange(0, total_count_);
            drag_mode_ = DragMode::None;
            unsetCursor();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

private:
    void applySizing()
    {
        if (compact_mode_)
        {
            setMinimumHeight(28);
            setMaximumHeight(32);
        }
        else
        {
            setMinimumHeight(42);
            setMaximumHeight(54);
        }
    }

    enum class DragMode
    {
        None,
        LeftHandle,
        RightHandle,
        Selection
    };

    int currentStartIndex() const
    {
        if (total_count_ <= 0)
        {
            return 0;
        }
        return std::clamp(start_index_, 0, std::max(0, total_count_ - currentVisibleCount()));
    }

    int currentVisibleCount() const
    {
        if (total_count_ <= 0)
        {
            return 0;
        }
        return std::clamp(visible_count_ <= 0 ? total_count_ : visible_count_, 1, total_count_);
    }

    int currentEndIndex() const
    {
        return std::max(0, currentStartIndex() + currentVisibleCount() - 1);
    }

    qreal positionForIndex(int index, const QRectF& trackRect) const
    {
        if (total_count_ <= 1)
        {
            return trackRect.left();
        }
        const qreal ratio = static_cast<qreal>(index) / static_cast<qreal>(total_count_ - 1);
        return trackRect.left() + ratio * trackRect.width();
    }

    int indexFromPosition(qreal x, const QRectF& trackRect) const
    {
        if (total_count_ <= 1 || trackRect.width() <= 0.0)
        {
            return 0;
        }
        const qreal ratio = std::clamp((x - trackRect.left()) / trackRect.width(), 0.0, 1.0);
        return static_cast<int>(std::llround(ratio * static_cast<qreal>(total_count_ - 1)));
    }

    void jumpSelectionTo(qreal x, const QRectF& trackRect)
    {
        const int count = currentVisibleCount();
        const int centerIndex = indexFromPosition(x, trackRect);
        const int startIndex = std::clamp(centerIndex - count / 2, 0, std::max(0, total_count_ - count));
        applyRangeChange(startIndex, count);
    }

    void applyRangeChange(int startIndex, int visibleCount)
    {
        if (total_count_ <= 0)
        {
            return;
        }

        visibleCount = std::clamp(visibleCount, 1, total_count_);
        startIndex = std::clamp(startIndex, 0, std::max(0, total_count_ - visibleCount));
        if (start_index_ == startIndex && currentVisibleCount() == visibleCount)
        {
            return;
        }

        start_index_ = startIndex;
        visible_count_ = visibleCount;
        update();
        if (on_range_changed_)
        {
            on_range_changed_(start_index_, visible_count_);
        }
    }

    int total_count_;
    int start_index_;
    int visible_count_;
    bool compact_mode_;
    DragMode drag_mode_;
    int drag_anchor_index_;
    int drag_anchor_count_;
    QString empty_text_;
    std::function<void(int, int)> on_range_changed_;
};

#endif
