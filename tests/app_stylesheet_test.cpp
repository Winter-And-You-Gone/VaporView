#include "shared/theme/AppTheme.h"
#include "test_ui_helpers.h"

#include <QApplication>
#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QVBoxLayout>
#include <cstdlib>
#include <iostream>

namespace
{
void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

class StyleEvents final : public QObject
{
public:
    int count = 0;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::StyleChange) ++count;
        return false;
    }
};

QString themeStyle(bool dark)
{
    return VaporView::applyAppThemeTokens(QStringLiteral(
        "QWidget { background-color: @vv-surface; }"
        "QLabel { color: @vv-text; }"
        "QPushButton { background-color: @vv-primary; color: @vv-white; border: none; padding: 8px; }"), dark);
}

void settle() { VaporViewTest::processEventsFor(80); }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    VaporView::installButtonFocusStyle();
    StyleEvents events;
    app.installEventFilter(&events);
    QWidget mainWindow;
    auto *layout = new QVBoxLayout(&mainWindow);
    auto *label = new QLabel(QStringLiteral("Theme text"));
    layout->addWidget(label);
    layout->addWidget(new QPushButton(QStringLiteral("Action")));
    QWidget *parent = &mainWindow;
    for (int depth = 0; depth < 8; ++depth)
    {
        auto *child = new QWidget(parent);
        child->setObjectName(QStringLiteral("nested%1").arg(depth));
        child->setGeometry(0, 0, 1, 1);
        parent = child;
    }
    mainWindow.resize(240, 140);
    app.setStyleSheet(themeStyle(false));
    mainWindow.show();
    require(VaporViewTest::waitForWindowExposed(&mainWindow), "main window is exposed");
    settle();
    const QImage lightBaseline = mainWindow.grab().toImage();
    events.count = 0;
    app.setProperty(VaporView::kAppDarkThemeProperty, true);
    app.setPalette(VaporView::appThemePalette(true));
    app.setStyleSheet(themeStyle(true));
    settle();
    const int globalEvents = events.count;
    const QImage darkBaseline = mainWindow.grab().toImage();

    VaporView::setAppStyleSheet(themeStyle(true));
    settle();
    require(app.styleSheet().isEmpty(), "application stylesheet is no longer repolished on theme changes");
    require(mainWindow.grab().toImage() == darkBaseline, "scoped dark rendering matches global rendering");
    events.count = 0;
    app.setProperty(VaporView::kAppDarkThemeProperty, false);
    app.setPalette(VaporView::appThemePalette(false));
    VaporView::setAppStyleSheet(themeStyle(false));
    settle();
    require(mainWindow.grab().toImage() == lightBaseline, "scoped light rendering matches global rendering");
    require(events.count < globalEvents / 2, "scoped update avoids repeated descendant style changes");

    QWidget viewer;
    auto *viewerLayout = new QVBoxLayout(&viewer);
    auto *viewerLabel = new QLabel(QStringLiteral("Viewer text"));
    viewerLayout->addWidget(viewerLabel);
    viewer.show();
    settle();
    require(viewerLabel->palette().color(QPalette::WindowText) ==
                VaporView::appThemeColor(VaporView::AppThemeColor::Text, false),
            "new independent windows receive the current theme before painting");
    viewer.hide();
    app.setProperty(VaporView::kAppDarkThemeProperty, true);
    app.setPalette(VaporView::appThemePalette(true));
    VaporView::setAppStyleSheet(themeStyle(true));
    settle();
    require(viewerLabel->palette().color(QPalette::WindowText) ==
                VaporView::appThemeColor(VaporView::AppThemeColor::Text, true),
            "hidden independent windows also receive theme changes");
    viewer.show();
    settle();

    QDialog dialog(&mainWindow);
    auto *dialogLayout = new QVBoxLayout(&dialog);
    auto *dialogLabel = new QLabel(QStringLiteral("Dialog text"));
    dialogLayout->addWidget(dialogLabel);
    dialog.show();
    settle();
    require(dialogLabel->palette().color(QPalette::WindowText) ==
                VaporView::appThemeColor(VaporView::AppThemeColor::Text, true),
            "parented top-level dialogs inherit the root stylesheet");
    dialog.setStyleSheet(QStringLiteral("QLabel { color: #12ab34; }"));
    settle();
    VaporView::setAppStyleSheet(themeStyle(false));
    settle();
    require(dialogLabel->palette().color(QPalette::WindowText) == QColor(QStringLiteral("#12ab34")),
            "dialog-local rules keep precedence during root updates");

    viewer.setStyleSheet(QStringLiteral("QLabel { color: #34ab12; }"));
    settle();
    VaporView::setAppStyleSheet(themeStyle(true));
    settle();
    require(viewerLabel->palette().color(QPalette::WindowText) == QColor(QStringLiteral("#34ab12")),
            "independent window local styles survive theme changes");
    viewer.setParent(&mainWindow);
    settle();
    require(viewer.styleSheet() == QStringLiteral("QLabel { color: #34ab12; }"),
            "reparenting releases the redundant root stylesheet");
    viewer.setParent(nullptr);
    viewer.show();
    settle();
    require(viewer.styleSheet().contains(themeStyle(true)), "detached windows receive the current root theme");

    VaporView::setAppStyleSheet(QString());
    settle();
    require(mainWindow.styleSheet().isEmpty(), "clearing shared theme restores the original root style");
    require(viewer.styleSheet() == QStringLiteral("QLabel { color: #34ab12; }"),
            "clearing shared theme retains window-local styling");
    dialog.close(); viewer.close(); mainWindow.close();
    std::cout << "app_stylesheet_test passed\n";
    return 0;
}
