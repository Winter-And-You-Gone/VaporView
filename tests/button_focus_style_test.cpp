#include "test_settings_sandbox.h"
#include "shared/theme/AppTheme.h"

#include <QApplication>
#include <QCheckBox>
#include <QFocusEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QStyleOptionButton>
#include <QStyleOptionToolButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <cstdlib>
#include <iostream>

void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

QImage renderButton(QAbstractButton *button, bool focus)
{
    QImage image(button->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(button->palette().color(QPalette::Window));
    QPainter painter(&image);
    QStyleOptionButton option;
    option.initFrom(button);
    option.state &= ~(QStyle::State_HasFocus | QStyle::State_MouseOver | QStyle::State_Sunken);
    if (focus) option.state |= QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange;
    option.text = button->text();
    if (auto *tool = qobject_cast<QToolButton *>(button))
    {
        QStyleOptionToolButton toolOption;
        toolOption.initFrom(tool);
        toolOption.state = option.state;
        toolOption.text = option.text;
        toolOption.toolButtonStyle = Qt::ToolButtonTextOnly;
        button->style()->drawComplexControl(QStyle::CC_ToolButton, &toolOption, &painter, button);
    }
    else
    {
        const auto control = qobject_cast<QCheckBox *>(button) ? QStyle::CE_CheckBox
            : qobject_cast<QRadioButton *>(button) ? QStyle::CE_RadioButton : QStyle::CE_PushButton;
        button->style()->drawControl(control, &option, &painter, button);
    }
    return image;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    VaporView::installButtonFocusStyle();
    auto *style = app.style();
    VaporView::installButtonFocusStyle();
    require(app.style() == style, "focus style installs once");
    for (bool dark : {false, true})
    {
        app.setProperty(VaporView::kAppDarkThemeProperty, dark);
        app.setPalette(VaporView::appThemePalette(dark));
        for (bool styled : {false, true})
        {
            QWidget window;
            auto *layout = new QVBoxLayout(&window);
            QList<QAbstractButton *> buttons{new QPushButton("Export"), new QToolButton,
                                            new QCheckBox("Check"), new QRadioButton("Radio")};
            buttons[1]->setText("Tool");
            for (auto *button : buttons)
            {
                button->setFixedSize(180, 44);
                button->setFocusPolicy(Qt::StrongFocus);
                layout->addWidget(button);
            }
            if (styled)
                window.setStyleSheet(VaporView::applyAppThemeTokens(QStringLiteral(
                    "QPushButton, QToolButton { background: @vv-primary; color: @vv-white; border: none; border-radius: 6px; padding: 4px 16px; }"), dark));
            window.show();
            window.activateWindow();
            app.processEvents();
            for (auto *button : buttons)
            {
                QFocusEvent mouseFocus(QEvent::FocusIn, Qt::MouseFocusReason);
                app.sendEvent(button, &mouseFocus);
                const auto noFocus = renderButton(button, false);
                require(renderButton(button, true) == noFocus, "mouse focus adds no native button rectangle");
                QFocusEvent keyboardFocus(QEvent::FocusIn, Qt::TabFocusReason);
                app.sendEvent(button, &keyboardFocus);
                // QSS tool buttons bypass native focus drawing; their existing
                // custom indicators are covered by the menu/switch UI tests.
                const bool nativeIndicator = !styled || !qobject_cast<QToolButton *>(button);
                if (nativeIndicator)
                    require(renderButton(button, true) != noFocus, "keyboard focus is visibly indicated");
                QFocusEvent popupRestore(QEvent::FocusIn, Qt::PopupFocusReason);
                app.sendEvent(button, &popupRestore);
                if (nativeIndicator)
                    require(renderButton(button, true) != noFocus, "keyboard menu return keeps keyboard indicator");
                QMouseEvent press(QEvent::MouseButtonPress, QPointF(8, 8), QPointF(button->mapToGlobal(QPoint(8, 8))),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                app.sendEvent(button, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, QPointF(8, 8), QPointF(button->mapToGlobal(QPoint(8, 8))),
                                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                app.sendEvent(button, &release);
                app.sendEvent(button, &popupRestore);
                require(renderButton(button, true) == renderButton(button, false), "click and popup return clear keyboard ring without removing focusability");
                require(button->focusPolicy() == Qt::StrongFocus, "button stays keyboard accessible");
            }
        }
    }
    std::cout << "button focus style passed\n";
}
