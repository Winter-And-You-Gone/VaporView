#ifndef VaporView_VISUAL_TEXT_LABEL_H_
#define VaporView_VISUAL_TEXT_LABEL_H_

#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPoint>
#include <QRect>
#include <QMouseEvent>
#include <algorithm>

namespace VaporView
{

class VisualTextLabel : public QLabel
{
public:
    explicit VisualTextLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        initialize();
    }

    explicit VisualTextLabel(const QString& text, QWidget *parent = nullptr)
        : QLabel(text, parent)
    {
        initialize();
    }

    static QPoint textOriginForVisualAlignment(const QRect& area,
                                               const QRect& textBounds,
                                               Qt::Alignment alignment)
    {
        const Qt::Alignment horizontal = alignment & Qt::AlignHorizontal_Mask;
        int x = area.left() - textBounds.left();
        if (horizontal == Qt::AlignHCenter)
        {
            x = area.left() + (area.width() - textBounds.width()) / 2 - textBounds.left();
        }
        else if (horizontal == Qt::AlignRight || horizontal == Qt::AlignTrailing)
        {
            x = area.right() - textBounds.right();
        }

        const Qt::Alignment vertical = alignment & Qt::AlignVertical_Mask;
        int y = area.top() + (area.height() - textBounds.height()) / 2 - textBounds.top();
        if (vertical == Qt::AlignTop)
        {
            y = area.top() - textBounds.top();
        }
        else if (vertical == Qt::AlignBottom)
        {
            y = area.bottom() - textBounds.bottom();
        }

        return QPoint(x, y);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        QLabel::mousePressEvent(event);
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        QLabel::mouseMoveEvent(event);
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QLabel::mouseReleaseEvent(event);
        update();
    }

    void paintEvent(QPaintEvent *event) override
    {
        if (textFormat() != Qt::PlainText || wordWrap())
        {
            QLabel::paintEvent(event);
            return;
        }

        Q_UNUSED(event);
        const QString value = text();
        if (value.isEmpty())
        {
            return;
        }

        QPainter painter(this);
        painter.setFont(font());
        const QPalette::ColorRole role = foregroundRole() == QPalette::NoRole
            ? QPalette::WindowText
            : foregroundRole();
        const QPalette::ColorGroup group = isEnabled() ? QPalette::Active : QPalette::Disabled;
        painter.setPen(palette().color(group, role));

        const QRect area = contentsRect();
        painter.setClipRect(area);
        const QFontMetrics metrics(font());
        const QRect textBounds = metrics.tightBoundingRect(value);
        const QPoint origin = textOriginForVisualAlignment(area, textBounds, alignment());
        painter.drawText(origin, value);

        const int start = selectionStart();
        if (start < 0 || !hasSelectedText())
        {
            return;
        }

        const int end = std::min(value.size(), start + selectedText().size());
        if (end <= start)
        {
            return;
        }

        const int selectionLeft = origin.x() + metrics.horizontalAdvance(value.left(start));
        const int selectionRight = origin.x() + metrics.horizontalAdvance(value.left(end));
        const QRect selectionRect(selectionLeft,
                                  origin.y() - metrics.ascent(),
                                  std::max(1, selectionRight - selectionLeft),
                                  metrics.height());
        const QPalette::ColorGroup selectionGroup = isEnabled()
            ? (hasFocus() ? QPalette::Active : QPalette::Inactive)
            : QPalette::Disabled;
        painter.fillRect(selectionRect, palette().brush(selectionGroup, QPalette::Highlight));
        painter.save();
        painter.setClipRect(selectionRect, Qt::IntersectClip);
        painter.setPen(palette().color(selectionGroup, QPalette::HighlightedText));
        painter.drawText(origin, value);
        painter.restore();
    }

private:
    void initialize()
    {
        setTextFormat(Qt::PlainText);
        setWordWrap(false);
    }
};

}  // namespace VaporView

#endif
