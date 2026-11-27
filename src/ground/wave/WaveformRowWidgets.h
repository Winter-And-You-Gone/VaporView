#pragma once

#include "shared/theme/AppTheme.h"

#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <utility>

namespace VaporView::Ground::Wave
{
class VerticalRowLabel final : public QWidget
{
public:
    explicit VerticalRowLabel(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        updateLabelWidth();
    }

    void setText(const QString &text)
    {
        text_ = text;
        setToolTip(text);
        setAccessibleName(text);
        update();
    }

    QString text() const { return text_; }

protected:
    void changeEvent(QEvent *event) override
    {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::FontChange)
            updateLabelWidth();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const bool dark = isDarkThemePalette(palette());
        painter.fillRect(rect(), appThemeColor(AppThemeColor::Surface, dark));
        painter.setPen(appThemeColor(AppThemeColor::Border, dark));
        painter.drawLine(rect().topRight(), rect().bottomRight());
        if (text_.isEmpty())
            return;

        painter.translate(QRectF(rect()).center());
        painter.setPen(appThemeColor(AppThemeColor::Text, dark));
        const qreal available = std::max(1, height() - 4);
        const QFontMetricsF metrics(painter.font());
        const auto characters = text_.toUcs4();
        const bool stacked = std::any_of(characters.cbegin(), characters.cend(), [](uint c) {
            const auto script = QChar::script(c);
            return script == QChar::Script_Han || script == QChar::Script_Hangul ||
                   script == QChar::Script_Hiragana || script == QChar::Script_Katakana;
        });
        if (stacked)
        {
            const qreal step = metrics.height();
            const qreal total = step * characters.size();
            painter.scale(1.0, std::min<qreal>(1.0, available / total));
            qreal y = -total / 2.0;
            for (char32_t c : characters)
            {
                painter.drawText(QRectF(-width() / 2.0, y, width(), step), Qt::AlignCenter,
                                 QString::fromUcs4(&c, 1));
                y += step;
            }
        }
        else
        {
            painter.rotate(-90);
            const qreal length = std::max<qreal>(1.0, metrics.horizontalAdvance(text_) + 2.0);
            painter.scale(std::min<qreal>(1.0, available / length), 1.0);
            painter.drawText(QRectF(-length / 2.0, -width() / 2.0, length, width()),
                             Qt::AlignCenter | Qt::TextSingleLine, text_);
        }
    }

private:
    void updateLabelWidth() { setFixedWidth(std::clamp(fontMetrics().height() + 14, 30, 34)); }

    QString text_;
};

class RowResizeHandle final : public QWidget
{
public:
    explicit RowResizeHandle(QWidget *targetRow, int minHeight, int maxHeight,
                             std::function<void(int)> onHeightChanged = {}, QWidget *parent = nullptr)
        : QWidget(parent), targetRow_(targetRow), onHeightChanged_(std::move(onHeightChanged))
    {
        setHeightBounds(minHeight, maxHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(6);
        setCursor(Qt::SplitVCursor);
    }

    void setTargetRow(QWidget *targetRow)
    {
        targetRow_ = targetRow;
        dragging_ = false;
    }

    void setHeightBounds(int minHeight, int maxHeight)
    {
        minHeight_ = std::max(0, minHeight);
        maxHeight_ = std::max(minHeight_, maxHeight);
    }

    int minimumHeightBound() const { return minHeight_; }
    int maximumHeightBound() const { return maxHeight_; }
    void setMinHeightProvider(std::function<int()> provider) { minHeightProvider_ = std::move(provider); }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !targetRow_)
            return QWidget::mousePressEvent(event);

        if (minHeightProvider_) setHeightBounds(minHeightProvider_(), maxHeight_);
        dragging_ = true;
        startY_ = event->globalPosition().y();
        startHeight_ = targetRow_->height();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!dragging_ || !targetRow_)
            return QWidget::mouseMoveEvent(event);

        event->accept();
        resizeRow(event->globalPosition().y());
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !dragging_)
            return QWidget::mouseReleaseEvent(event);

        dragging_ = false;
        event->accept();
        if (targetRow_)
            resizeRow(event->globalPosition().y());
    }

private:
    void resizeRow(qreal globalY)
    {
        if (minHeightProvider_) setHeightBounds(minHeightProvider_(), maxHeight_);
        const int height = std::clamp(startHeight_ + qRound(globalY - startY_), minHeight_, maxHeight_);
        if (targetRow_->height() == height)
            return;

        targetRow_->setFixedHeight(height);
        targetRow_->updateGeometry();
        if (auto *parent = targetRow_->parentWidget(); parent && parent->layout())
            parent->layout()->activate();
        if (onHeightChanged_)
            onHeightChanged_(height);
    }

    QPointer<QWidget> targetRow_;
    std::function<void(int)> onHeightChanged_;
    std::function<int()> minHeightProvider_;
    int minHeight_;
    int maxHeight_;
    qreal startY_ = 0;
    int startHeight_ = 0;
    bool dragging_ = false;
};
}
