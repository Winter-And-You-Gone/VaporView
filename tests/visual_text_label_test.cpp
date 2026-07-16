#include "ground/widgets/VisualTextLabel.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
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

}  // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testVisualCentering();
    testHorizontalAlignment();
    testVerticalAlignment();
    std::cout << "visual_text_label_test passed\n";
    return 0;
}
