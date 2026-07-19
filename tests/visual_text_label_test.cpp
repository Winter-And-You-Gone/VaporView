#include "ground/widgets/VisualTextLabel.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
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
    const QRect textBounds = metrics.tightBoundingRect(QStringLiteral("TCP波形监视"));
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

}  // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testVisualCentering();
    testHorizontalAlignment();
    testVerticalAlignment();
    testSelectableTextPaintsSelection();
    std::cout << "visual_text_label_test passed\n";
    return 0;
}
