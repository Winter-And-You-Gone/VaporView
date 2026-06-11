#ifndef VAPORVIEW_CUSTOM_TITLE_BAR_H_
#define VAPORVIEW_CUSTOM_TITLE_BAR_H_

class QWidget;

namespace VaporView
{

void installCustomTitleBar(QWidget *window, bool showMaximizeButton = true);
bool addWidgetToCustomTitleBar(QWidget *window, QWidget *widget);

}  // namespace VaporView

#endif
