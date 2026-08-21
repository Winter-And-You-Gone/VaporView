#include "ground/widgets/LabelTextSelection.h"
#include "ground/widgets/VisualTextLabel.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QMouseEvent>
#include <QPalette>
#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QRect placedTextRect(const QRect& area, const QRect& textBounds, Qt::Alignment alignment)
{
    const QPoint origin = VaporView::VisualTextLabel::textOriginForVisualAlignment(area, textBounds, alignment);
    return textBounds.translated(origin);
}

void testVisualCentering()
{
    QFont font;
    font.setFamily(QStringLiteral("Microsoft YaHei"));
    font.setPointSize(16);
    font.setBold(true);
    const QFontMetrics metrics(font);
    const QRect textBounds = metrics.tightBoundingRect(QStringLiteral("波形监视"));
    require(!textBounds.isEmpty(), "text bounds available");

    const QRect area(0, 0, 240, 36);
    const QRect placed = placedTextRect(area, textBounds, Qt::AlignLeft | Qt::AlignVCenter);
    require(std::abs(placed.center().y() - area.center().y()) <= 1, "visual text center matches area center");
    require(placed.left() == area.left(), "left aligned text starts at left edge");
}

void testHorizontalAlignment()
{
    const QRect area(10, 20, 200, 40);
    const QRect textBounds(-2, -18, 80, 24);

    const QRect left = placedTextRect(area, textBounds, Qt::AlignLeft | Qt::AlignVCenter);
    const QRect center = placedTextRect(area, textBounds, Qt::AlignHCenter | Qt::AlignVCenter);
    const QRect right = placedTextRect(area, textBounds, Qt::AlignRight | Qt::AlignVCenter);

    require(left.left() == area.left(), "left alignment");
    require(std::abs(center.center().x() - area.center().x()) <= 1, "center alignment");
    require(right.right() == area.right(), "right alignment");
}

void testVerticalAlignment()
{
    const QRect area(10, 20, 200, 40);
    const QRect textBounds(-2, -18, 80, 24);

    const QRect top = placedTextRect(area, textBounds, Qt::AlignLeft | Qt::AlignTop);
    const QRect middle = placedTextRect(area, textBounds, Qt::AlignLeft | Qt::AlignVCenter);
    const QRect bottom = placedTextRect(area, textBounds, Qt::AlignLeft | Qt::AlignBottom);

    require(top.top() == area.top(), "top alignment");
    require(std::abs(middle.center().y() - area.center().y()) <= 1, "vertical center alignment");
    require(bottom.bottom() == area.bottom(), "bottom alignment");
}

void testSelectableTextPaintsSelection()
{
    VaporView::VisualTextLabel label(QStringLiteral("卡片标题"));
    label.resize(180, 40);
    label.setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label.setTextInteractionFlags(Qt::TextSelectableByMouse);

    QPalette palette = label.palette();
    palette.setColor(QPalette::Highlight, QColor(255, 0, 255));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    label.setPalette(palette);
    label.setSelection(0, 2);
    label.show();
    QApplication::processEvents();

    require(label.selectedText() == QStringLiteral("卡片"),
            "selectable visual label exposes the selected title text");
    const QImage image = label.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    bool selectionHighlightPainted = false;
    for (int y = 0; y < image.height() && !selectionHighlightPainted; ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.red() > 200 && pixel.green() < 80 && pixel.blue() > 200)
            {
                selectionHighlightPainted = true;
                break;
            }
        }
    }
    require(selectionHighlightPainted,
            "selectable visual label paints the title selection highlight");
}

void dragSelection(VaporView::VisualTextLabel& label,
                   const QPoint& start,
                   const QPoint& end,
                   QEvent::Type pressType = QEvent::MouseButtonPress)
{
    const QPoint globalStart = label.mapToGlobal(start);
    const QPoint globalEnd = label.mapToGlobal(end);
    QMouseEvent press(pressType,
                      start,
                      globalStart,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&label, &press);
    require(label.selectedText().isEmpty(),
            "a new drag press immediately clears the previous title selection");
    QMouseEvent move(QEvent::MouseMove,
                     end,
                     globalEnd,
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(&label, &move);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        end,
                        globalEnd,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(&label, &release);
    QApplication::processEvents();
}

void testRepeatedMouseSelectionRemainsResponsive()
{
    const QString text = QStringLiteral("Card title text");
    VaporView::VisualTextLabel label(text);
    QFont font = label.font();
    font.setPointSize(18);
    label.setFont(font);
    label.resize(220, 48);
    label.setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    VaporView::configureSelectableCardTitle(&label);
    require(!label.textInteractionFlags().testFlag(Qt::TextSelectableByMouse),
            "card titles disable QLabel's native selected-text dragging");
    require(label.textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard),
            "card titles retain keyboard selection and copy support");
    label.show();
    QApplication::processEvents();

    const QFontMetrics metrics(label.font());
    const QRect textBounds = metrics.tightBoundingRect(text);
    const QPoint origin = VaporView::VisualTextLabel::textOriginForVisualAlignment(
        label.contentsRect(), textBounds, label.alignment());
    const auto pointAtCursor = [&](int cursor, int y) {
        return QPoint(origin.x() + metrics.horizontalAdvance(text.left(cursor)), y);
    };
    const int centerY = textBounds.translated(origin).center().y();

    struct DragCase
    {
        int start;
        int end;
        int y;
        QEvent::Type pressType;
    };
    const DragCase cases[] = {
        {0, 5, centerY, QEvent::MouseButtonPress},
        {1, 4, centerY, QEvent::MouseButtonDblClick},
        {4, 1, centerY, QEvent::MouseButtonPress},
        {2, 3, centerY, QEvent::MouseButtonDblClick},
        {5, 0, centerY, QEvent::MouseButtonPress},
    };

    for (const DragCase& drag : cases)
    {
        dragSelection(label,
                      pointAtCursor(drag.start, drag.y),
                      pointAtCursor(drag.end, drag.y),
                      drag.pressType);
        const int selectionStart = std::min(drag.start, drag.end);
        const int selectionLength = std::abs(drag.end - drag.start);
        require(label.selectionStart() == selectionStart,
                "repeated drag starts a fresh selection at the pointer anchor");
        require(label.selectedText() == text.mid(selectionStart, selectionLength),
                "repeated drag selects the intended characters");
        require(QApplication::focusWidget() == &label,
                "repeated drag keeps keyboard focus on the selected title");
    }

    const QPoint contextPosition = label.rect().center();
    QContextMenuEvent contextEvent(QContextMenuEvent::Mouse,
                                   contextPosition,
                                   label.mapToGlobal(contextPosition));
    contextEvent.ignore();
    QApplication::sendEvent(&label, &contextEvent);
    require(contextEvent.isAccepted(),
            "selectable title suppresses the standard copy context menu");
}

}  // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testVisualCentering();
    testHorizontalAlignment();
    testVerticalAlignment();
    testSelectableTextPaintsSelection();
    testRepeatedMouseSelectionRemainsResponsive();
    std::cout << "visual_text_label_test passed\n";
    return 0;
}
