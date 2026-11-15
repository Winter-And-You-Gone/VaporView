#include "shared/theme/AppTheme.h"
#include "shared/theme/TopLevelCardStyle.h"
#include "test_ui_helpers.h"

#include <QApplication>
#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
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

class PaintEvents final : public QObject
{
public:
    int count = 0;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Paint) ++count;
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

void testShadowLayoutUpdates()
{
    QWidget window;
    window.resize(300, 200);
    auto *host = new QWidget(&window);
    host->setGeometry(window.rect());
    auto *card = new QWidget(host);
    card->setGeometry(30, 30, 120, 90);
    VaporView::configureTopLevelCard(card);
    auto *fade = new QWidget(host);
    fade->setObjectName(QStringLiteral("mainContentBottomFade"));
    fade->setGeometry(0, 185, 300, 15);
    window.show();
    require(VaporViewTest::waitForWindowExposed(&window), "shadow test window is exposed");
    VaporView::updateTopLevelCardShadows(&window, 1.0);
    settle();
    auto *shadow = host->findChild<QWidget *>(
        QString::fromLatin1(VaporView::kTopLevelCardShadowLayerName), Qt::FindDirectChildrenOnly);
    require(shadow && shadow->isVisible(), "card shadow layer is visible");
    PaintEvents paints;
    shadow->installEventFilter(&paints);
    QEvent layoutRequest(QEvent::LayoutRequest);
    QCoreApplication::sendEvent(card, &layoutRequest);
    settle();
    require(paints.count == 0, "unchanged layout requests do not repaint the shadow layer");

    auto *sibling = new QWidget(host);
    sibling->setGeometry(160, 30, 40, 40);
    sibling->show();
    QEvent stackingRequest(QEvent::LayoutRequest);
    QCoreApplication::sendEvent(host, &stackingRequest);
    settle();
    const auto children = host->children();
    require(children.indexOf(sibling) < children.indexOf(shadow) &&
                children.indexOf(shadow) < children.indexOf(fade),
            "layout requests restore shadow and fade stacking after a sibling is added");

    const QImage shadowBeforeMove = shadow->grab().toImage();
    paints.count = 0;
    card->move(50, 40);
    settle();
    require(paints.count > 0, "moving a card repaints its shadow");
    const QImage shadowAfterMove = shadow->grab().toImage();
    require(shadowBeforeMove != shadowAfterMove, "moving a card moves the rendered shadow");
    paints.count = 0;
    card->resize(140, 100);
    settle();
    require(paints.count > 0, "resizing a card repaints its shadow");
    require(shadowAfterMove != shadow->grab().toImage(), "resizing a card updates the rendered shadow");
    shadow->removeEventFilter(&paints);
    window.close();

    QScrollArea scroll;
    scroll.resize(300, 200);
    scroll.setWidgetResizable(true);
    scroll.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    scroll.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    auto *content = new QWidget;
    auto *scrollCard = new QWidget(content);
    scrollCard->setGeometry(30, 30, 120, 90);
    VaporView::configureTopLevelCard(scrollCard);
    scroll.setWidget(content);
    scroll.show();
    require(VaporViewTest::waitForWindowExposed(&scroll), "scroll shadow test window is exposed");
    VaporView::updateTopLevelCardShadows(&scroll, 1.0);
    settle();
    auto *scrollShadow = scroll.findChild<QWidget *>(
        QString::fromLatin1(VaporView::kTopLevelCardShadowLayerName), Qt::FindDirectChildrenOnly);
    require(scrollShadow && scrollShadow->isVisible(), "scroll area has a visible shadow layer");
    QWidget *verticalTrack = scroll.verticalScrollBar()->parentWidget();
    QWidget *horizontalTrack = scroll.horizontalScrollBar()->parentWidget();
    verticalTrack->raise();
    horizontalTrack->raise();
    settle();
    require(scroll.children().indexOf(verticalTrack) > scroll.children().indexOf(scrollShadow) &&
                scroll.children().indexOf(horizontalTrack) > scroll.children().indexOf(scrollShadow),
            "scrollbar tracks are above the clipped shadow layer");
    PaintEvents scrollPaints;
    scrollShadow->installEventFilter(&scrollPaints);
    QEvent scrollLayoutRequest(QEvent::LayoutRequest);
    QCoreApplication::sendEvent(content, &scrollLayoutRequest);
    settle();
    require(scrollPaints.count == 0, "scrollbar tracks do not trigger redundant shadow stacking");
    scrollShadow->removeEventFilter(&scrollPaints);
    scroll.close();
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    VaporView::installButtonFocusStyle();
    testShadowLayoutUpdates();
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
