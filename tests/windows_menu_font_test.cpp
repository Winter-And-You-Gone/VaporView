#include "test_settings_sandbox.h"
#include "test_ui_helpers.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/SingleLevelPopupComboBox.h"
#include <QApplication>
#include <QComboBox>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include <QToolButton>
#include <QVBoxLayout>
#include <cstdlib>
#include <iostream>

namespace {
int fontWarnings=0;
void require(bool value,const char* message) {
    if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
void capture(QtMsgType,const QMessageLogContext&,const QString& message) {
    if(message.contains(QStringLiteral("QFont::setPointSize"))) {
        ++fontWarnings;std::cerr<<message.toStdString()<<'\n';
    }
}
}

int main(int argc,char** argv) {
    QApplication app(argc,argv);
#ifndef Q_OS_WIN
    return 77;
#endif
    auto* windowsStyle=QStyleFactory::create(QStringLiteral("windows11"));
    require(windowsStyle,"Windows 11 style is available");
    app.setStyle(windowsStyle);
    VaporView::installButtonFocusStyle();
    const auto oldHandler=qInstallMessageHandler(capture);
    for(bool dark:{false,true})for(int pixels:{16,24})for(bool styled:{false,true}) {
        app.setPalette(VaporView::appThemePalette(dark));
        QWidget window;window.setWindowTitle(QStringLiteral("Windows menu font regression"));
        QFont font=window.font();font.setPixelSize(pixels);window.setFont(font);
        auto* layout=new QVBoxLayout(&window);
        auto* combo=new QComboBox;combo->addItems({"Native combo","Second item"});layout->addWidget(combo);
        auto* editable=new QComboBox;editable->setEditable(true);editable->addItems({"COM1","COM2"});layout->addWidget(editable);
        auto* custom=new VaporView::SingleLevelPopupComboBox;custom->addItems({"Styled combo","Second item"});layout->addWidget(custom);
        QMenu menu(&window);menu.addAction(QStringLiteral("Action"));
        auto* sub=menu.addMenu(QStringLiteral("Submenu"));sub->addAction(QStringLiteral("Child action"));
        auto* push=new QPushButton(QStringLiteral("Menu button"));push->setMenu(&menu);layout->addWidget(push);
        auto* tool=new QToolButton;tool->setText(QStringLiteral("Tool menu"));tool->setMenu(&menu);
        tool->setPopupMode(QToolButton::MenuButtonPopup);layout->addWidget(tool);
        if(styled)window.setStyleSheet(VaporView::startupAppThemeStyleSheet(dark));
        window.resize(360,320);window.show();
        require(VaporViewTest::waitForWindowExposed(&window),"test window exposed");
        VaporViewTest::processEventsFor(50);
        for(auto* widget:QList<QWidget*>{combo,editable,push,tool}) {
            require(widget->style()->pixelMetric(QStyle::PM_MenuButtonIndicator,nullptr,widget)>0,"menu indicator has a positive width");
            widget->grab();
            require(widget->font().pixelSize()==pixels && widget->font().pointSize()==-1,"pixel font and inheritance preserved");
        }
        for(QComboBox* box:{combo,editable,static_cast<QComboBox*>(custom)}) {
            box->showPopup();VaporViewTest::processEventsFor(60);box->hidePopup();
        }
        menu.popup(push->mapToGlobal(QPoint(0,push->height())));
        VaporViewTest::processEventsFor(60);menu.grab();menu.hide();
        // Exercise the native submenu-arrow path even when a stylesheet paints
        // menu rows itself. The proxy must preserve the caller's painter font.
        QImage image(240,50,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        QPainter painter(&image);painter.setFont(font);
        QStyleOptionMenuItem option;option.initFrom(&menu);option.font=font;
        option.rect=image.rect();option.menuItemType=QStyleOptionMenuItem::SubMenu;option.text="Submenu";
        app.style()->drawControl(QStyle::CE_MenuItem,&option,&painter,&menu);
        require(painter.font()==font,"menu painting restores painter font");
        painter.end();
    }
    qInstallMessageHandler(oldHandler);
    require(fontWarnings==0,"all menu paths accept pixel-sized fonts without Qt font warnings");
    std::cout<<"Windows menu pixel-font regression passed\n";
}
