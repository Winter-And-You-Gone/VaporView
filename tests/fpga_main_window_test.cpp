#include "test_settings_sandbox.h"
#include "ground/main/MainWindow.h"
#include "ground/widgets/FpgaControlPage.h"
#include "shared/config/ApplicationConfig.h"
#include <QApplication>
#include <QAbstractButton>
#include <QComboBox>
#include <QJsonDocument>
#include <QLabel>
#include <QPointer>
#include <QStackedWidget>
#include <QThread>
#include <cstdlib>
#include <iostream>

static void require(bool v,const char *why) { if(!v) {std::cerr<<why<<'\n';std::exit(1);} }
int main(int argc,char **argv)
{
    require(VaporViewTest::SettingsSandbox::initialized,"isolated native/INI application settings");
    QApplication app(argc,argv);
    app.setOrganizationName(QStringLiteral("VaporViewFpgaTest"));
    app.setApplicationName(QStringLiteral("fpga_main_window_test"));
    QSettings native("VaporView","MainWindow");
    native.setValue("font_scale_percent",100);
    native.setValue("app_sidebar_width",62);
    QPointer<QThread> worker;
    {
        MainWindow window;
        auto *stack=window.findChild<QStackedWidget*>(QStringLiteral("mainPageStack"));
        auto *page=window.findChild<FpgaControlPage*>(QStringLiteral("fpgaControlPage"));
        require(stack && stack->count()==5 && page && stack->widget(4)==page,"fifth standalone persistent FPGA page");
        worker=window.findChild<QThread*>(QStringLiteral("fpgaUsbWorkerThread"));
        require(worker && worker->isRunning(),"USB worker has its own running event loop");
        require(page->configuration().pressureSource==0x40 && page->configuration().backend=="auto","PTB210 and auto backend defaults");
        auto *status=page->findChild<QLabel*>(QStringLiteral("fpgaConnectionStatus"));
        require(status && !status->text().contains(QStringLiteral("GP01 FX3")),"construction does not auto-connect hardware");
        auto *acquisition=page->findChild<QAbstractButton*>(QStringLiteral("fpgaAcquisitionStart"));
        auto *connectButton=page->findChild<QAbstractButton*>(QStringLiteral("fpgaConnect"));
        require(acquisition && !acquisition->isEnabled() && connectButton && connectButton->isEnabled(),"ADC stays stopped and hardware remains disconnected until requested");
        auto *pressure=page->findChild<QComboBox*>(QStringLiteral("fpgaPressureSource"));
        require(pressure && pressure->currentData().toUInt()==0x40,"default pressure choice visible");
        pressure->setCurrentIndex(pressure->findData(0x43));
        require(page->configuration().pressureSource==0x43,"pressure source edits are model-backed");
        stack->setCurrentIndex(4); stack->setCurrentIndex(0); stack->setCurrentIndex(4);
        require(stack->widget(4)==page && pressure->currentData().toUInt()==0x43,"switching pages preserves editor and choice");
        const auto settings=VaporView::applicationConfigSettings();
        const auto saved=QJsonDocument::fromJson(settings.value(QStringLiteral("Fpga/desiredConfiguration")).toByteArray());
        require(saved.object().value("pressureSource").toInt()==0x43,"desired configuration persisted through application settings");
        require(!window.findChild<QThread*>(QStringLiteral("fpgaUsbWorkerThread"))->isInterruptionRequested(),"worker still alive before teardown");
    }
    require(worker.isNull(),"MainWindow teardown joins and deletes USB worker");
    {
        MainWindow restored;
        auto *page=restored.findChild<FpgaControlPage*>(QStringLiteral("fpgaControlPage"));
        require(page && page->configuration().pressureSource==0x43,"next window restores desired config without reconnect");
    }
    require(native.value("font_scale_percent").toInt()==100,"font scale stays at standard 100 percent");
    std::cout<<"fpga main window tests passed\n";
}
