#pragma once

#include "shared/theme/AppTheme.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QWidget>
#include <QWindow>

#include <functional>

namespace VaporViewTest
{

using RequireFunction = void (*)(bool, const char *);

inline bool processEventsUntil(int timeoutMs, const std::function<bool()>& condition)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        if (condition())
        {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    return condition();
}

inline void processEventsFor(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
}

inline bool waitForWindowExposed(QWidget *widget, int timeoutMs = 2000)
{
    return processEventsUntil(timeoutMs, [widget]() {
        return widget && widget->isVisible() && widget->windowHandle() &&
               widget->windowHandle()->isExposed();
    });
}

inline void requireComboPopupStyled(QComboBox *combo,
                                    const char *message,
                                    RequireFunction require)
{
    require(combo != nullptr, message);
    require(combo->property("vaporViewComboPopupStyled").toBool(),
            "combo carries the shared popup style marker");
    require(combo->view() != nullptr, "combo has a popup view");
    require(combo->view()->property("vaporViewComboPopupStyled").toBool(),
            "combo popup view carries the shared style marker");
    require(!combo->view()->property("vaporViewComboPopupRoundedMaskEnabled").isValid(),
            "combo popup view leaves rounded masking to the outer popup container");
    require(!combo->view()->property("vaporViewComboPopupViewportMargin").isValid(),
            "combo popup view does not inset its viewport for an inner border");
    require(!combo->view()->testAttribute(Qt::WA_TranslucentBackground) &&
                !combo->view()->testAttribute(Qt::WA_NoSystemBackground),
            "combo popup view uses an opaque backing store to avoid transparent edge artifacts");
    require(combo->view()->viewport() != nullptr &&
                !combo->view()->viewport()->testAttribute(Qt::WA_TranslucentBackground) &&
                !combo->view()->viewport()->testAttribute(Qt::WA_NoSystemBackground),
            "combo popup viewport avoids transparent backing-store attributes");
    require(combo->view()->viewport()->styleSheet().contains(QStringLiteral("background-color:")) &&
                combo->view()->viewport()->styleSheet().contains(QStringLiteral("border: none")),
            "combo popup viewport has an explicit filled background without drawing its own border");
    require(combo->view()->findChild<QWidget *>(QStringLiteral("vaporViewComboPopupBorderOverlay")) == nullptr,
            "combo popup does not create a redundant child border overlay");
    require(!combo->view()->property("vaporViewComboPopupShadowEnabled").toBool(),
            "combo popup view does not request unsafe external shadow chrome");
    require(!combo->view()->property("floatingPanelChrome").toBool(),
            "combo popup view does not use floating panel chrome");
    require(combo->view()->objectName() == QStringLiteral("vaporViewComboPopupView"),
            "combo popup view uses the shared object name");
    const QString popupStyle = combo->view()->styleSheet();
    const QString hoverColor = VaporView::appThemeColorName(VaporView::AppThemeColor::MenuHover,
                                                             VaporView::isDarkThemeEnabled());
    require(popupStyle.contains(QStringLiteral("border: none")) &&
                !popupStyle.contains(QStringLiteral("border: 1px solid")) &&
                !popupStyle.contains(QStringLiteral("border-bottom: 1px solid")) &&
                popupStyle.contains(QStringLiteral("border-radius: 0px")) &&
                popupStyle.contains(QStringLiteral("padding: 12px 0px")) &&
                popupStyle.contains(QStringLiteral("padding: 7px 14px")) &&
                popupStyle.contains(QStringLiteral("min-height: 30px")) &&
                popupStyle.contains(QStringLiteral("selection-background-color: transparent")) &&
                popupStyle.contains(QStringLiteral("::item:selected")) &&
                popupStyle.contains(QStringLiteral("background-color: transparent")) &&
                popupStyle.contains(QStringLiteral("background-color: %1").arg(hoverColor)) &&
                !popupStyle.contains(QStringLiteral("padding: 12px 4px")),
            "combo popup stylesheet opens without a selected-row highlight and keeps hover feedback");
}

}  // namespace VaporViewTest
