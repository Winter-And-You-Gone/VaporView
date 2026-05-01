#ifndef VaporView_WINDOW_SIZING_H_
#define VaporView_WINDOW_SIZING_H_

#include <QSize>
#include <QtGlobal>

class QWidget;

namespace VaporView
{

QSize screenFractionSize(const QWidget *contextWidget,
                         qreal fraction = 0.5,
                         const QSize& fallbackAvailableSize = QSize(1440, 860));

QSize defaultWindowSizeWithinScreenFraction(const QWidget *contextWidget,
                                            const QSize& preferredSize,
                                            qreal fraction = 0.5,
                                            const QSize& minimumSize = QSize(),
                                            const QSize& fallbackAvailableSize = QSize(1440, 860));

void centerWindowOnScreen(QWidget *window,
                          const QWidget *contextWidget = nullptr,
                          const QSize& fallbackAvailableSize = QSize(1440, 860));

int defaultFontScalePercentForScreen(const QWidget *contextWidget,
                                     int normalPercent = 100,
                                     int minimumPercent = 80,
                                     const QSize& fallbackAvailableSize = QSize(1920, 1080));

}

#endif
