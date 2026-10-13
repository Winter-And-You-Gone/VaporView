#include "test_settings_sandbox.h"
#include "ground/main/MainWindow.h"
#include "ground/widgets/FpgaControlPage.h"
#include "ground/widgets/SegmentedSwitchButton.h"
#include "shared/config/ApplicationConfig.h"
#include "ground/devices/RemoteSkyController.h"
#include "TcpTelemetryLink.h"
#include "TelemetryCodec.h"
#include "FpgaTelemetry.h"
#include "shared/session/FpgaSessionArchive.h"
#include "ground/wave/TcpWavePanel.h"
#include "SkyConfig.h"
#include <QApplication>
#include <QStyleFactory>
#include <QTabWidget>
#include <QScrollArea>
#include <QAbstractButton>
#include <QToolButton>
#include <QComboBox>
#include <QGridLayout>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QImage>
#include <QPixmap>
#include <QLayout>
#include <QBoxLayout>
#include <QPointer>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QDoubleSpinBox>
#include <QTemporaryDir>
#include <QDir>
#include <QStandardItemModel>
#include <QSet>
#include <stdexcept>
#include <cmath>
#include <QtEndian>
#include <cstdlib>
#include <iostream>

static int fontWarnings=0;
static void captureFont(QtMsgType,const QMessageLogContext&,const QString &message){if(message.contains("QFont::setPointSize")){++fontWarnings;std::cerr<<message.toStdString()<<'\n';}}
static void require(bool v,const char *why) { if(!v) {std::cerr<<why<<'\n';std::exit(1);} }
static void verifyConnectionCard(FpgaControlPage *page,int scale)
{
    const int side=qRound(40*scale/100.0);
    for(const auto *name : {"fpgaDiscover","fpgaConnect","fpgaDisconnect"}) {
        auto *button=page->findChild<QAbstractButton*>(name);
        require(button&&button->size()==QSize(side,side),"main-window FPGA icon actions retain square dimensions after global styles");
    }
    auto *disconnect=page->findChild<QAbstractButton*>("fpgaDisconnect");
    require(disconnect->geometry().right()==disconnect->parentWidget()->layout()->contentsRect().right(),"FPGA connection controls fill the card to its right content edge");
    auto *discovery=page->findChild<QLabel*>("fpgaDiscoveryStatus");
    auto *status=page->findChild<QLabel*>("fpgaConnectionStatus");
    require(discovery&&status&&discovery->parentWidget()==status->parentWidget()
        &&status->parentWidget()->objectName()=="sectionTitleBar","FPGA discovery and connection status share the card title bar");
}
template<typename Predicate> static bool waitUntil(Predicate predicate, int timeoutMs = 3000)
{
    QElapsedTimer elapsed; elapsed.start();
    while (elapsed.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate()) return true;
        QThread::msleep(1);
    }
    return predicate();
}
static void processFor(int milliseconds)
{
    QElapsedTimer elapsed; elapsed.start();
    waitUntil([&] { return elapsed.elapsed() >= milliseconds; }, milliseconds + 1000);
}
static QToolButton *homeAction(MainWindow &window,const QString &name)
{
    for (auto *button:window.findChildren<QToolButton*>("homeDeviceActionButton"))
        if (!button->property("deviceConfigAction").toBool() && button->toolTip().contains(name)) return button;
    std::cerr << "Missing home action: " << name.toStdString() << '\n';
    require(false,"requested home device action exists");return nullptr;
}
static void verifyCardMargins(MainWindow &window,FpgaControlPage *page,int scale)
{
    const QSize originalSize=window.size();
    window.resize(originalSize.expandedTo(QSize(2000,1000))); processFor(50);
    auto *stack=window.findChild<QStackedWidget*>("mainPageStack");
    auto *homeCards=window.findChild<QWidget*>("homeOverviewSplitter");
    stack->setCurrentIndex(0); processFor(50);
    const QRect homeRect(homeCards->mapTo(stack,QPoint()),homeCards->size());
    stack->setCurrentIndex(4); processFor(50);
    page->setUiTestState(true,false,false,5000);processFor(50);
    auto *connection=page->findChild<QAbstractButton*>("fpgaDisconnect")->parentWidget()->parentWidget();
    const QRect connectionRect(connection->mapTo(stack,QPoint()),connection->size());
    require(connectionRect.left()==homeRect.left()&&connectionRect.right()==homeRect.right(),"FPGA connection card matches the actual home card outer margins");
    auto *tabs=page->findChild<QTabWidget*>("fpgaTabs");
    auto *parameterTabs=page->findChild<QTabWidget*>("fpgaParameterTabs");
    const int originalTab=tabs->currentIndex(),originalParameterTab=parameterTabs->currentIndex();
    for(int tab=0;tab<tabs->count();++tab) {
        tabs->setCurrentIndex(tab);
        for(int parameterTab=0;parameterTab<(tab==0?parameterTabs->count():1);++parameterTab) {
            if(tab==0)parameterTabs->setCurrentIndex(parameterTab);
            processFor(50);
            QList<QWidget*> channelCards;
            for(const auto *name : {"fpgaDliaChannels","fpgaWaveChannels"}) {
                auto *row=page->findChild<QHBoxLayout*>(name);
                require(row&&row->count()==2,"parameter and waveform channel rows each retain two cards");
                auto *first=row->itemAt(0)->widget(),*second=row->itemAt(1)->widget();
                channelCards.append(first);channelCards.append(second);
                if(!first->isVisibleTo(page))continue;
                const QRect firstRect(first->mapTo(stack,QPoint()),first->size()),secondRect(second->mapTo(stack,QPoint()),second->size());
                require(firstRect.top()==secondRect.top()&&firstRect.bottom()==secondRect.bottom(),"paired channel cards share one row and equal heights");
                require(qAbs(firstRect.width()-secondRect.width())<=1,"paired channel cards divide the available width equally");
                require(secondRect.left()-firstRect.right()-1==qRound(12*scale/100.0),"paired cards retain the shared scaled card gap");
                require(firstRect.left()==homeRect.left()&&secondRect.right()==homeRect.right(),"paired channel cards fill the row within the home outer margins");
            }
            if(tab==0&&parameterTab==0) {
                auto *first=page->findChild<QWidget*>("fpga0Scan"),*second=page->findChild<QWidget*>("fpga1Scan");
                require(first->parentWidget()==second->parentWidget()&&first->geometry().top()==second->geometry().top(),"merged WMS card aligns independent channel inputs on the same row");
                require(page->findChild<QLabel*>("fpga0ScanLabel")&&!page->findChild<QLabel*>("fpga1ScanLabel"),"main window shares WMS field labels across channels");
                require(page->findChild<QLabel*>("fpgaRegister3028")->text()!=QStringLiteral("—")
                    &&page->findChild<QLabel*>("fpgaRegister3128")->text()!=QStringLiteral("—"),"UI test mode populates both channel diagnostic counters");
            }
            if(tab==2)for(int channel=0;channel<2;++channel) {
                auto *adc=page->findChild<QWidget*>(QString("fpgaWavePlot%1").arg(32+channel));
                auto *h1=page->findChild<QWidget*>(QString("fpgaWavePlot%1").arg(48+channel));
                auto *h2=page->findChild<QWidget*>(QString("fpgaWaveSecondPlot%1").arg(48+channel));
                require(adc->height()==scale&&h1->height()==scale&&h2->height()==scale,"global UI scaling preserves compact FPGA plot heights");
                require(h1->geometry().left()==h2->geometry().left()&&h1->width()==h2->width()
                    &&h1->geometry().bottom()<h2->geometry().top(),"main window stacks H1 and H2 at full channel width");
            }
            auto *sensorGrid=page->findChild<QGridLayout*>("fpgaSensorGrid");
            require(sensorGrid&&sensorGrid->count()==5,"sensor grid retains five compact cards");
            for(int row=0;row<2;++row) {
                auto *first=sensorGrid->itemAtPosition(row,0)->widget();if(!first->isVisibleTo(page))continue;
                const QRect firstRect(first->mapTo(stack,QPoint()),first->size());
                require(firstRect.left()==homeRect.left(),"sensor grid retains the home left card margin");
                for(int column=0;column<3;++column) {
                    auto *item=sensorGrid->itemAtPosition(row,column);if(!item)continue;auto *card=item->widget();channelCards.append(card);
                    const QRect rect(card->mapTo(stack,QPoint()),card->size());
                    require(qAbs(rect.width()-firstRect.width())<=1&&rect.top()==firstRect.top()&&rect.bottom()==firstRect.bottom(),"sensor cards share equal compact widths and row heights");
                    if(column>0) {
                        auto *previous=sensorGrid->itemAtPosition(row,column-1)->widget();const QRect previousRect(previous->mapTo(stack,QPoint()),previous->size());
                        require(rect.left()-previousRect.right()-1==qRound(12*scale/100.0),"sensor cards retain the shared scaled card gap");
                    }
                    if(column==2)require(rect.right()==homeRect.right(),"sensor grid fills the row to the home right card margin");
                }
            }
            for(auto *card : page->findChildren<QWidget*>("fpgaSectionCard")) {
                if(!card->isVisibleTo(page)||channelCards.contains(card))continue;
                const QRect cardRect(card->mapTo(stack,QPoint()),card->size());
                require(cardRect.left()==homeRect.left()&&cardRect.right()==homeRect.right(),"full-width FPGA cards align with the home card edges");
            }
        }
    }
    page->setUiTestState(false);
    parameterTabs->setCurrentIndex(originalParameterTab); tabs->setCurrentIndex(originalTab); window.resize(originalSize); processFor(50);
}
static void remotePageRouting()
{
    using namespace VaporView;
    using namespace VaporView::Ground::Devices;
    TcpTelemetryLink server;
    require(server.listen("127.0.0.1", 0), "fake Sky TCP listen");
    QPointer<QThread> worker;
    int commands = 0;
    int disconnectCommands = 0;
    TelemetryCodec decoder, encoder;
    DeviceOperationRequest lastRequest;
    quint16 commandSequence = 0;
    {
        MainWindow window;
        worker = window.findChild<QThread*>("fpgaUsbWorkerThread");
        auto *page = window.findChild<FpgaControlPage*>("fpgaControlPage");
        auto *acquisition = page->findChild<QAbstractButton*>("fpgaAcquisitionStart");
        auto *disconnect = page->findChild<QAbstractButton*>("fpgaDisconnect");
        auto *pressure = page->findChild<QComboBox*>("fpgaPressureSource");
        auto *summary = page->findChild<QLabel*>("fpgaWaveSummary32");
        auto *plot = page->findChild<QWidget*>("fpgaWavePlot32");
        auto *diagnostics = page->findChild<QPlainTextEdit*>("fpgaDiagnostics");
        require(acquisition && disconnect && pressure && summary && plot && diagnostics, "remote page controls");
        require(QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 0)), "Local slot callable");
        pressure->setCurrentIndex(pressure->findData(0x43));
        const auto localConfiguration = page->configuration();
        require(QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 1)), "Remote slot callable");
        auto *remote = window.testRemoteSkyController();
        require(remote, "testing remote controller getter");
        QObject::connect(&server, &TelemetryLink::bytesReceived, &window, [&](const QByteArray& bytes) {
            for (const auto& frame : decoder.feedBytes(bytes)) {
                CommandMessage command;
                DeviceOperationRequest request;
                if (frame.type != MsgType::Command || !TelemetryCodec::parseCommand(frame.payload, command)
                    || command.command_id != CommandId::DeviceOperation
                    || !TelemetryCodec::parseDeviceOperationRequest(command.payload, request)) continue;
                require(request.device_id == SkyDeviceId::Fpga && request.operation == DeviceOperation::FpgaControl,
                    "page only sends typed FPGA operation");
                QJsonObject control;
                require(FpgaRemote::parseControl(request.payload, control), "page payload passes FPGA allowlist");
                if (control.value("op").toString() == "disconnect") ++disconnectCommands;
                lastRequest = request; commandSequence = command.command_seq; ++commands;
            }
        });
        require(remote->openTcp("127.0.0.1", server.localPort()), "GUI Remote opens fake Sky TCP");
        require(waitUntil([&] { return remote->isOpen(); }), "GUI TCP connected");
        FpgaControlConfig skyConfiguration;
        QJsonObject status{{"connected", true}, {"ready", true}, {"busy", false}, {"detail", "simulated ready"},
            {"configuration", skyConfiguration.toJson()}, {"registers", QJsonObject{}}, {"archived_records", "12"},
            {"recording_state", 1}, {"session_directory", "simulated-session"}};
        quint16 sequence = 1;
        auto send = [&](MsgType type, const QByteArray& payload) {
            const auto bytes = encoder.encodeFrame(type, payload, sequence++, 1);
            require(server.writeBytes(bytes) == bytes.size(), "fake Sky frame sent");
        };
        send(MsgType::FpgaStatus, FpgaRemote::encodeStatus(status));
        require(waitUntil([&] { return acquisition->isEnabled(); }), "remote status enables ready controls");
        require(page->configuration().pressureSource == 0x40, "Sky desired config appears in Remote editor");
        pressure->setCurrentIndex(pressure->findData(0x43));
        pressure->setCurrentIndex(pressure->findData(0x40));
        const auto settings = applicationConfigSettings();
        require(QJsonDocument::fromJson(settings.value("Fpga/desiredConfiguration").toByteArray()).object()
            .value("pressureSource").toInt() == 0x43, "Remote edit preserves Local desired config");
        require(QJsonDocument::fromJson(settings.value("Fpga/remoteDesiredConfiguration").toByteArray()).object()
            .value("pressureSource").toInt() == 0x40, "Remote desired config has separate persistence");
        acquisition->click();
        require(waitUntil([&] { return commands == 1; }), "acquisition sent once through TCP");
        QJsonObject control;
        require(FpgaRemote::parseControl(lastRequest.payload, control)
            && control.value("op").toString() == "acquisition" && control.value("enabled").toBool(),
            "Acq click routes to Sky FPGA acquisition");
        require(!acquisition->isEnabled(), "in-flight final readback locks actions");
        int acknowledgments = 0;
        QObject::connect(remote, &RemoteSkyController::commandAckReceived, &window,
            [&](const CommandAck& ack) { if (ack.command_seq == commandSequence) ++acknowledgments; });
        CommandAck ack; ack.command_id = CommandId::DeviceOperation; ack.command_seq = commandSequence;
        send(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack));
        require(waitUntil([&] { return acknowledgments == 1; }), "ACK received through real TCP codec");
        require(!acquisition->isEnabled(), "acceptance ACK does not finish operation");
        DeviceOperationResponse response;
        response.request_id = lastRequest.request_id; response.device_id = SkyDeviceId::Fpga;
        response.operation = DeviceOperation::FpgaControl; response.payload = FpgaRemote::encodeStatus(status);
        send(MsgType::DeviceOperationResponse, TelemetryCodec::serializeDeviceOperationResponse(response));
        require(waitUntil([&] { return acquisition->isEnabled(); }), "final response unlocks actions");
        FpgaWave::CompletedStream wave;
        wave.source = 0x20; wave.message = 0x1000; wave.schema = 2; wave.format = 0;
        wave.rate = 1000000; wave.totalPoints = 4096; wave.bytesPerPoint = 4; wave.adcBits = 24;
        wave.complete = true;
        for (int point = 0; point < 4096; ++point) wave.signed32Samples.append(point - 2048);
        send(MsgType::FpgaWaveformPreview, FpgaRemote::encodeWaveformPreview(wave));
        require(waitUntil([&] { return plot->property("sampleCount").toInt() == 64; }), "preview plots actual 64 received points");
        require(summary->text().contains("64/4096") && plot->property("previewStride").toInt() == 64,
            "preview shows actual/original counts and stride");
        send(MsgType::FpgaStatus, FpgaRemote::encodeStatus(status));
        require(waitUntil([&] { return acquisition->isEnabled(); }), "fresh status precedes per-source freshness check");
        QTimer statusOnly;
        statusOnly.setInterval(1000);
        QObject::connect(&statusOnly, &QTimer::timeout, &window, [&] { send(MsgType::FpgaStatus, FpgaRemote::encodeStatus(status)); });
        statusOnly.start();
        require(waitUntil([&] { return summary->text().contains(QStringLiteral("历史")) || summary->text().contains("historical"); }, 5500),
            "an unrefreshed source becomes historical while live USB status continues");
        require(acquisition->isEnabled(), "fresh USB status does not refresh old waveform arrival time");
        statusOnly.stop();
        require(waitUntil([&] { return !acquisition->isEnabled(); }, 5500), "four-second status expires write controls");
        const int beforeExpired = commands;
        emit page->acquisitionRequested(true); // A queued/forced request must also be gated, beyond disabled buttons.
        processFor(100);
        require(commands == beforeExpired, "expired status refuses writes at dispatch boundary");
        require(summary->text().contains(QStringLiteral("历史")) || summary->text().contains("historical"),
            "expired preview marked historical");
        send(MsgType::FpgaStatus, FpgaRemote::encodeStatus(status));
        require(waitUntil([&] { return acquisition->isEnabled(); }), "fresh status restores controls");
        remote->close();
        require(waitUntil([&] { return !acquisition->isEnabled(); }), "link close disables writes");
        processFor(100);
        require(disconnectCommands == 0 && commands == 1, "link close never sends Sky USB disconnect or retries control");
        require(QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 0)), "return Local");
        require(page->configuration().pressureSource == localConfiguration.pressureSource,
            "Local desired config restored independently");
        require(!acquisition->isEnabled() && !disconnect->isEnabled(), "Remote acquisition did not open Local USB");
        require(QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 1)), "return Remote editor");
        require(page->configuration().pressureSource == 0x40, "Remote desired config restored independently");
        require(remote->openTcp("127.0.0.1", server.localPort()), "reopen Sky for read-only discovery");
        require(waitUntil([&] { return remote->isOpen(); }), "discovery link opened without ready status");
        auto *scan = page->findChild<QAbstractButton*>("fpgaDiscover");
        auto *devices = page->findChild<QComboBox*>("fpgaLocator");
        scan->click();
        require(waitUntil([&] { return commands == 2; }), "discovery routes to Sky while USB is disconnected");
        require(FpgaRemote::parseControl(lastRequest.payload, control) && control.value("op") == "discover", "typed Sky discovery request");
        status["connected"] = false; status["ready"] = false;
        status["usb_discovery"] = QJsonObject{{"devices",QJsonArray{QJsonObject{{"locator","sky-only-device"},{"label","GP01 Sky"},
            {"driver","WinUSB"},{"backend","winusb"},{"available",true}}}},{"error",""}};
        response.request_id = lastRequest.request_id; response.payload = FpgaRemote::encodeStatus(status);
        send(MsgType::DeviceOperationResponse, TelemetryCodec::serializeDeviceOperationResponse(response));
        require(waitUntil([&] { return devices->currentData().toString() == "sky-only-device"; }), "Sky result populates USB selector");
        page->findChild<QAbstractButton*>("fpgaConnect")->click();
        require(waitUntil([&] { return commands == 3; }), "selected Sky device connects through remote request");
        require(FpgaRemote::parseControl(lastRequest.payload, control) && control.value("op") == "connect"
            && control.value("locator") == "sky-only-device", "Sky opaque locator is preserved by selection");
        remote->close();
        require(waitUntil([&] { return devices->count() == 1; }), "link closure clears Sky USB candidates");
        QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 0));
        require(devices->count() == 1, "Sky locator never becomes a Local USB candidate");
    }
    require(worker.isNull(), "Remote regression teardown joins and deletes Local USB worker");
    server.close();
}
static void remoteHomeActions()
{
    using namespace VaporView;
    TcpTelemetryLink server;require(server.listen("127.0.0.1",0),"home control fake Sky server");
    MainWindow window;
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,1));
    auto sky=SkyConfig::defaults();sky.sensor_transport_path=SensorTransportPath::FpgaRelay;
    window.testInjectRemoteSkyConfig(sky.toJson());
    auto *stack=window.findChild<QStackedWidget*>("mainPageStack");stack->setCurrentIndex(0);
    auto *remote=window.testRemoteSkyController();
    TelemetryCodec decoder,encoder;quint16 sequence=1,commandSequence=0;
    QList<DeviceOperationRequest> requests;
    QObject::connect(&server,&TelemetryLink::bytesReceived,&window,[&](const QByteArray &bytes) {
        for (const auto &frame:decoder.feedBytes(bytes)) {
            CommandMessage command;DeviceOperationRequest request;
            if (frame.type!=MsgType::Command || !TelemetryCodec::parseCommand(frame.payload,command)
                || command.command_id!=CommandId::DeviceOperation
                || !TelemetryCodec::parseDeviceOperationRequest(command.payload,request)) continue;
            require(request.device_id==SkyDeviceId::Fpga && request.operation==DeviceOperation::FpgaControl,
                "home actions use typed FPGA controls, never legacy serial connect commands");
            requests.append(request);commandSequence=command.command_seq;
        }
    });
    require(remote->openTcp("127.0.0.1",server.localPort())&&waitUntil([&]{return remote->isOpen();}),"home remote link opens");
    FpgaControlConfig config;config.pressureSource=0x43;
    QJsonObject registers;
    for (quint32 address:{0x6004u,0x6204u,0x6304u,0x6404u,0x6504u,0x6604u,0x4004u,0x4104u,0x5004u,0x5104u})
        registers[QString::number(address)]=1;
    QJsonObject status{{"connected",true},{"ready",true},{"busy",false},{"detail","ready"},
        {"configuration",config.toJson()},{"registers",registers},{"archived_records","0"}};
    const auto send=[&](MsgType type,const QByteArray &payload) {
        const auto bytes=encoder.encodeFrame(type,payload,sequence++,1);
        require(server.writeBytes(bytes)==bytes.size(),"home control Sky frame sent");
    };
    send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));
    auto *epsilon=homeAction(window,"EPSILON");
    require(waitUntil([&]{return epsilon->isEnabled()&&epsilon->property("state")=="connected";}),
        "register readback enables home controls without any valid measurement packets");
    require(!homeAction(window,"RD105")->isEnabled(),"unsupported RD105 remains unavailable");
    const QStringList names{"EPSILON","PTB210","HMP","TFA1500","AI-8288",QStringLiteral("波形源")};
    const QList<quint16> sources{0x42,0x43,0x44,0x45,0x46,0};
    for (int index=0;index<names.size();++index) {
        auto *button=homeAction(window,names[index]);
        for (bool enabled:{false,true}) {
            const auto before=registers;const int count=requests.size();button->click();
            require(waitUntil([&]{return requests.size()==count+1;}),"home click sends one FPGA request");
            require(stack->currentIndex()==0,"remote per-device action keeps the home page selected");
            QJsonObject control;require(FpgaRemote::parseControl(requests.last().payload,control),"home payload validates");
            require(control.value("enabled").toBool()==enabled && control.value("op")== (sources[index]?"sensor":"acquisition"),
                "home connect/disconnect selects the expected FPGA operation");
            if(sources[index])require(control.value("source").toInt()==sources[index],"home sensor mapping includes selected BMP pressure source");
            CommandAck ack;ack.command_id=CommandId::DeviceOperation;ack.command_seq=commandSequence;
            send(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack));processFor(30);
            require(!button->isEnabled()&&button->property("state")=="connecting","ACK does not change confirmed device state");
            button->click();processFor(20);require(requests.size()==count+1,"in-flight home action cannot be duplicated");
            if(sources[index])registers[QString::number(0x6004+(sources[index]-0x40)*0x100)]=enabled?1:0;
            else for(quint32 address:{0x4004u,0x4104u,0x5004u,0x5104u})registers[QString::number(address)]=enabled?1:0;
            status["registers"]=registers;
            DeviceOperationResponse response;response.request_id=requests.last().request_id;response.device_id=SkyDeviceId::Fpga;
            response.operation=DeviceOperation::FpgaControl;response.payload=FpgaRemote::encodeStatus(status);
            send(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(response));
            require(waitUntil([&]{return button->isEnabled()&&button->property("state")== (enabled?"connected":"disconnected");}),
                "final register readback confirms the home connection state");
            if(sources[index])for(const auto &key:before.keys())
                if(key!=QString::number(0x6004+(sources[index]-0x40)*0x100))require(registers.value(key)==before.value(key),"one sensor action preserves other modules");
        }
    }
    const int count=requests.size();remote->close();
    require(waitUntil([&]{return !epsilon->isEnabled();}),"telemetry loss disables FPGA home controls");
    processFor(50);require(requests.size()==count&&stack->currentIndex()==0,"link closure sends no FPGA stop and never changes page");
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));window.close();server.close();
}
static void localSimulatorRouting()
{
    const auto check=[](bool ok,const char *message){if(!ok)throw std::runtime_error(message);};
    QTemporaryDir output;check(output.isValid(),"isolated local recording directory");
    auto settings=VaporView::applicationConfigSettings();
    QSettings native("VaporView","MainWindow");
    native.setValue("recording_directory",output.path());
    settings.remove("Fpga/desiredConfiguration");
    MainWindow window;
    window.resize(1600,1000);
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));
    auto *path=window.findChild<QComboBox*>("deviceSensorTransportPathCombo");
    check(path,"local sensor path selector exists");path->setCurrentIndex(path->findData("fpga_relay"));
    auto *page=window.findChild<FpgaControlPage*>("fpgaControlPage");
    window.findChild<QStackedWidget*>("mainPageStack")->setCurrentWidget(page);window.show();
    auto *backend=page->findChild<QComboBox*>("fpgaBackend");
    auto *devices=page->findChild<QComboBox*>("fpgaLocator");
    auto *connectButton=page->findChild<QAbstractButton*>("fpgaConnect");
    auto *disconnect=page->findChild<QAbstractButton*>("fpgaDisconnect");
    auto *acquisition=page->findChild<QAbstractButton*>("fpgaAcquisitionStart");
    check(backend&&devices&&connectButton&&disconnect&&acquisition,"normal FPGA controls are present");
    backend->setCurrentIndex(backend->findData("simulator"));
    auto *pressureSource=page->findChild<QComboBox*>("fpgaPressureSource");
    check(pressureSource,"pressure source selector exists");pressureSource->setCurrentIndex(pressureSource->findData(0x43));
    check(waitUntil([&]{return devices->currentData().toString()=="simulation://fpga"&&connectButton->isEnabled();},10000),
          "local simulator is enumerated and connectable");
    check(!page->property("fpgaUiTestMode").toBool(),"local protocol test does not use UI test mode");
    using VaporView::Ground::Widgets::SegmentedSwitchButton;
    auto *sourceSwitch=window.findChild<SegmentedSwitchButton*>("sourceModeOverviewSwitch");
    auto *pathSwitch=window.findChild<SegmentedSwitchButton*>("titleSensorTransportSwitch");
    check(sourceSwitch&&pathSwitch,"both title-bar mode capsules exist");
    check(waitUntil([&]{return !sourceSwitch->switchAnimationRunning()&&!pathSwitch->switchAnimationRunning();}),
          "mode capsule appearances settle before connecting");
    const QImage sourceAppearance=sourceSwitch->grab().toImage(),pathAppearance=pathSwitch->grab().toImage();
    const auto expectLockedPrompt=[&](SegmentedSwitchButton *button,const QString &expectedText){
        QString message;
        QTimer closePrompt;
        QObject::connect(&closePrompt,&QTimer::timeout,&window,[&]{
            if(auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){
                message=box->text();box->accept();
            }
        });
        closePrompt.start(10);button->click();closePrompt.stop();
        check(message.contains(expectedText),"locked capsule click explains how to unlock switching");
    };
    auto *stack=window.findChild<QStackedWidget*>("mainPageStack");stack->setCurrentIndex(0);processFor(50);
    auto *pressureAction=homeAction(window,"PTB210");
    check(pressureAction->isEnabled(),"FPGA home connect does not require a serial port or an existing USB connection");
    pressureAction->click();
    check(waitUntil([&]{return acquisition->isEnabled()&&disconnect->isEnabled();},12000),"local factory handshake enables normal controls");
    check(waitUntil([&]{return pressureAction->isEnabled()&&pressureAction->property("state")=="connected";}),"home connect opens USB and confirms the already-enabled BMP sensor");
    check(!page->findChild<QLabel*>("fpgaConnectionStatus")->text().contains(QStringLiteral("修改 BMP 地址")),"already-enabled BMP does not receive a duplicate address-setting start");
    check(stack->currentIndex()==0,"local auto-connect keeps the home page selected");
    const QStringList homeNames{"EPSILON","PTB210","HMP","TFA1500","AI-8288",QStringLiteral("波形源")};
    for(const auto &name:homeNames) {
        auto *button=homeAction(window,name);
        for(bool enabled:{false,true}) {
            button->click();
            check(waitUntil([&]{return button->isEnabled()&&button->property("state")== (enabled?"connected":"disconnected");},10000),
                "local home action confirms sensor or acquisition enable through actual VLP1 readback");
            check(disconnect->isEnabled()&&stack->currentIndex()==0,"individual stop keeps USB open and stays on home");
        }
    }
    check(!homeAction(window,"RD105")->isEnabled(),"local relay does not invent RD105 support");
    check(sourceSwitch->isEnabled()&&pathSwitch->isEnabled()&&!sourceSwitch->switchChecked()&&pathSwitch->switchChecked()&&!path->isEnabled(),
          "connected capsules stay clickable and display Local/FPGA while the mode is locked");
    check(sourceSwitch->grab().toImage()==sourceAppearance&&pathSwitch->grab().toImage()==pathAppearance,
          "locking either capsule preserves its rendered colors and selection");
    expectLockedPrompt(sourceSwitch,QStringLiteral("先断开连接"));
    expectLockedPrompt(pathSwitch,QStringLiteral("先断开连接"));
    check(!sourceSwitch->switchChecked()&&pathSwitch->switchChecked()&&path->currentData().toString()=="fpga_relay"&&
          !sourceSwitch->switchAnimationRunning()&&!pathSwitch->switchAnimationRunning()&&acquisition->isEnabled()&&disconnect->isEnabled(),
          "locked requests keep both modes and the active FPGA connection unchanged without selection animation");
    auto *first=page->findChild<QWidget*>("fpgaWavePlot48");
    auto *second=page->findChild<QWidget*>("fpgaWavePlot49");
    auto *pressure=page->findChild<QLabel*>("fpgaSensor0040");
    check(waitUntil([&]{return first->property("sampleCount").toInt()>0&&second->property("sampleCount").toInt()>0&&pressure->text().contains("Pa");}),
          "normal local GUI receives both decoded DLIA channels and sensor readings");
    QDoubleSpinBox *target=nullptr;
    for(auto *spin:page->findChildren<QDoubleSpinBox*>())if(spin->suffix()==QStringLiteral(" °C"))target=spin;
    check(target,"AI8 target editor exists");target->setValue(31.5);
    auto *temperature=page->findChild<QAbstractButton*>("fpgaSetTemperature");temperature->click();
    check(waitUntil([&]{return temperature->isEnabled();}),"local AI8 transaction finishes");
    check(page->findChild<QLabel*>("fpgaConnectionStatus")->text().contains("AI8"),"local setpoint completion is visible in connection status");
    auto *raw=page->findChild<QAbstractButton*>("fpgaRawStart");raw->click();
    check(waitUntil([&]{return page->findChild<QWidget*>("fpgaWavePlot32")->property("sampleCount").toInt()>0;}),
          "normal local RAW control receives fragmented ADC data");
    auto *monitor=window.findChild<TcpWavePanel*>();
    check(monitor&&monitor->property("fpgaWaveformMode").toBool(),"home monitor selects FPGA data");
    check(waitUntil([&]{return monitor->findChild<QWidget*>("tcpWaveRawPlot")->property("sampleCount").toInt()>0
        &&monitor->findChild<QWidget*>("tcpWaveHarmonicPlot")->property("sampleCount").toInt()>0;}),
        "real VLP1 simulator supplies both ADC and DLIA to the home monitor");
    auto *record=page->findChild<QAbstractButton*>("fpgaRecord");record->click();
    check(waitUntil([&]{return record->text().contains("停止");}),"local record action starts recording");
    processFor(500);record->click();check(waitUntil([&]{return record->text().contains("开始");}),"local record action stops and drains");
    const QDir recordings(output.filePath("simulated_sessions"));
    const auto sessions=recordings.entryList(QDir::Dirs|QDir::NoDotAndDotDot);
    check(sessions.size()==1,"local simulator records one isolated session");
    QSet<quint16> sources;QString error;
    check(VaporView::Session::FpgaSessionArchive::replay(recordings.filePath(sessions.first()),[&](quint64,const auto& frame){
        if(frame.header.frameType==VaporView::FpgaVlp1::FrameType::Data)sources.insert(frame.header.source);return true;
    },&error),qPrintable(error));
    check(sources.size()==10,"local recording replays six sensor sources and four waveform sources");
    disconnect->click();check(waitUntil([&]{return connectButton->isEnabled();}),"local disconnect releases the simulator");
    sourceSwitch->click();
    auto *model=qobject_cast<QStandardItemModel*>(backend->model());
    check(sourceSwitch->switchChecked()&&model&&!model->item(backend->findData("simulator"))->isEnabled(),"unlocked capsule switches to Remote and disables the local simulator option");
    const bool remoteFpgaSelected=pathSwitch->switchChecked();
    expectLockedPrompt(pathSwitch,QStringLiteral("连接天空端并读取设备配置"));
    check(pathSwitch->switchChecked()==remoteFpgaSelected,"offline Remote transport keeps its current mode after its prerequisite prompt");
    sourceSwitch->click();
    check(!sourceSwitch->switchChecked()&&model->item(backend->findData("simulator"))->isEnabled(),"unlocked capsule returns to Local and restores the simulator option");
    pathSwitch->click();
    check(!pathSwitch->switchChecked()&&path->currentData().toString()=="direct_devices","unlocked capsule switches to Direct devices");
    pathSwitch->click();
    check(pathSwitch->switchChecked()&&path->currentData().toString()=="fpga_relay","unlocked capsule switches back to FPGA");
    window.close();
}
static void relayConfigurationPresentation()
{
    MainWindow window;
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));
    auto *stack=window.findChild<QStackedWidget*>("mainPageStack");
    auto *path=window.findChild<QComboBox*>("deviceSensorTransportPathCombo");
    QWidget *configPage=path;
    while(stack->indexOf(configPage)<0)configPage=configPage->parentWidget();
    stack->setCurrentWidget(configPage);window.resize(1600,1000);window.show();processFor(50);
    path->setCurrentIndex(path->findData("direct_devices"));
    const QStringList devices{"Epsilon","Pressure","Humidity","Lidar","Temperature","Ai8Temperature"};
    QStringList ports;
    for(const auto &device:devices) {
        auto *port=window.findChild<QComboBox*>("device"+device+"PortCombo");
        require(port,"device serial editor exists");ports.append(port->currentText());
    }
    const auto before=window.testLocalDeviceConfigSnapshot();
    path->setCurrentIndex(path->findData("fpga_relay"));processFor(50);
    for(int i=0;i<devices.size();++i) {
        const auto name="device"+devices[i]+"PortCombo";
        auto *port=window.findChild<QComboBox*>(name);
        auto *endpoint=window.findChild<QLabel*>(name+"FpgaEndpoint");
        auto *baud=window.findChild<QComboBox*>("device"+devices[i]+"BaudCombo");
        require(endpoint&&endpoint->isVisible()&&endpoint->text()=="FPGA"&&!port->isVisible()&&!port->isEnabled(),
            "all six relay serial ports are replaced by a read-only FPGA endpoint");
        require(baud&&baud->isVisible()&&baud->isEnabled(),"FPGA baud remains editable");
    }
    auto *baud=window.findChild<QComboBox*>("deviceEpsilonBaudCombo");
    window.findChild<FpgaControlPage*>()->setUiTestState(true,false,false,5000,VaporView::SensorTransportPath::FpgaRelay);
    window.findChild<FpgaControlPage*>()->simulateOperation({{"op","raw"},{"enabled",true}});
    stack->setCurrentIndex(0);processFor(80);
    const auto adcBefore=window.findChild<QWidget*>("tcpWaveRawPlot")->property("sampleCount").toInt();
    require(adcBefore>0,"bounded simulated RAW reaches the home ADC graph");
    auto *monitor=window.findChild<TcpWavePanel*>();
    auto *page=window.findChild<FpgaControlPage*>();
    QMetaObject::invokeMethod(monitor,"onClearPeakPlotClicked");
    VaporView::FpgaWave::CompletedStream full;full.source=0x30;full.format=0;full.bytesPerPoint=8;
    full.totalPoints=2002;full.complete=true;full.cycleId=9001;full.timestamp=900100;
    for(quint32 i=0;i<full.totalPoints;++i) {
        char word[4];qToLittleEndian<qint32>(900000,reinterpret_cast<uchar*>(word));full.pointBytes.append(word,4);
        qToLittleEndian<qint32>(i==1?1234:-5,reinterpret_cast<uchar*>(word));full.pointBytes.append(word,4);
    }
    page->updateWaveform(full);
    require(monitor->testPeakValues()==QVector<float>({1234}),"H2 peak is computed before display decimation and never divided by H1");
    require(monitor->testHarmonicSamples().first()==-5,"home waveform selects H2 rather than H1");
    page->setLanguage(true);page->setLanguage(false);page->updateWaveform(full);
    require(monitor->testPeakValues()==QVector<float>({1234}),"FPGA rerender and repeated cycle do not duplicate trend entries");
    VaporView::FpgaWave::CompletedStream partial;partial.source=0x20;partial.totalPoints=2;partial.partial=true;
    window.findChild<FpgaControlPage*>()->updateWaveform(partial);
    require(window.findChild<QWidget*>("tcpWaveRawPlot")->property("sampleCount").toInt()==adcBefore,
        "incomplete ADC retains the same historical cycle on both FPGA and home pages");
    partial.source=0x30;partial.cycleId=9002;window.findChild<FpgaControlPage*>()->updateWaveform(partial);
    require(window.findChild<QWidget*>("tcpWaveHarmonicPlot")->property("sampleCount").toInt()==0,
        "incomplete DLIA clears the home graph through the shared validated display");
    require(monitor->testPeakValues().size()==2&&std::isnan(monitor->testPeakValues().last()),"missing DLIA fragments create a trend gap");
    window.findChild<FpgaControlPage*>()->setUiTestState(false);
    stack->setCurrentWidget(configPage);
    baud->setCurrentIndex(baud->findText("460800"));
    // Changing path commits the edited baud without replacing the saved port.
    path->setCurrentIndex(path->findData("direct_devices"));processFor(50);
    require(window.testLocalDeviceConfigSnapshot().value("epsilon").toObject().value("baud")=="460800",
        "relay baud edit persists in the local device model");
    for(int i=0;i<devices.size();++i) {
        auto *port=window.findChild<QComboBox*>("device"+devices[i]+"PortCombo");
        require(port->isVisible()&&port->isEnabled()&&port->currentText()==ports[i],"Direct restores the original port");
    }
    for(const auto &key:before.keys())if(before.value(key).isObject())
        require(window.testLocalDeviceConfigSnapshot().value(key).toObject().value("port")==before.value(key).toObject().value("port"),
            "FPGA display never enters saved local serial fields");
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,1));
    auto remote=VaporView::SkyConfig::defaults();remote.sensor_transport_path=VaporView::SensorTransportPath::FpgaRelay;
    remote.epsilon.port="REMOTE-COM7";remote.ptb.port="REMOTE-COM8";remote.hmp.port="REMOTE-COM9";
    remote.lidar.port="REMOTE-COM10";remote.temperature_controller.port="REMOTE-COM11";
    remote.ai8_temperature_controller.port="REMOTE-COM12";
    window.testInjectRemoteSkyConfig(remote.toJson());processFor(50);
    for(const auto &device:devices) {
        auto *port=window.findChild<QComboBox*>("device"+device+"PortCombo");
        require(!port->isEnabled()&&!port->isVisible(),"remote config refresh cannot enable relay serial ports");
    }
    QString error;
    const auto saved=window.testRemoteSkyConfigFromDeviceConfigUi(&error);
    require(error.isEmpty()&&saved.value("epsilon").toObject().value("port")=="REMOTE-COM7",
        "FPGA presentation preserves the remote serial configuration too");
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));
    window.close();
}
int main(int argc,char **argv)
{
    require(VaporViewTest::SettingsSandbox::initialized,"isolated native/INI application settings");
    QApplication app(argc,argv);
#ifdef Q_OS_WIN
    auto *windowsStyle=QStyleFactory::create("windows11");
    require(windowsStyle,"Windows 11 style is available for font regression");
    QApplication::setStyle(windowsStyle);
#endif
    app.setOrganizationName(QStringLiteral("VaporViewFpgaTest"));
    app.setApplicationName(QStringLiteral("fpga_main_window_test"));
    QSettings native("VaporView","MainWindow");
    native.setValue("font_scale_percent",100);
    native.setValue("app_sidebar_width",62);
    if(app.arguments().contains("--relay-configuration")) {
        relayConfigurationPresentation();std::cout<<"FPGA relay configuration test passed\n";return 0;
    }
    if(app.arguments().contains("--local-simulator")) {
        try { localSimulatorRouting();std::cout<<"Local FPGA simulator GUI test passed\n";return 0; }
        catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
    }
    const auto standardFont=app.font();
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
        require(acquisition && !acquisition->isEnabled() && connectButton && !connectButton->isEnabled(),"ADC stays stopped and connect waits for discovery until page is opened");
        auto *pressure=page->findChild<QComboBox*>(QStringLiteral("fpgaPressureSource"));
        require(pressure && pressure->currentData().toUInt()==0x40,"default pressure choice visible");
        pressure->setCurrentIndex(pressure->findData(0x43));
        require(page->configuration().pressureSource==0x43,"pressure source edits are model-backed");
        stack->setCurrentIndex(4); stack->setCurrentIndex(0); stack->setCurrentIndex(4);
        require(stack->widget(4)==page && pressure->currentData().toUInt()==0x43,"switching pages preserves editor and choice");
        window.show();page->findChild<QTabWidget*>("fpgaTabs")->setCurrentIndex(2);processFor(100);
        verifyConnectionCard(page,100);
        verifyCardMargins(window,page,100);
        auto handler=qInstallMessageHandler(captureFont);
        VaporView::FpgaWave::CompletedStream wave;wave.source=0x30;wave.message=0x1001;wave.format=0;wave.bytesPerPoint=8;wave.totalPoints=2;wave.rate=10000;wave.complete=true;wave.pointBytes=QByteArray(16,0);
        page->updateWaveform(wave);
        auto *first=page->findChild<QWidget*>("fpgaWavePlot48");
        auto *second=page->findChild<QWidget*>("fpgaWaveSecondPlot48");
        require(first&&second&&first->property("sampleCount").toInt()==2&&second->property("sampleCount").toInt()==2,"main window displays both harmonics from one cycle");
        qInstallMessageHandler(handler);require(fontWarnings==0,"parallel harmonic update produces no font warning");
        window.close();
        const auto settings=VaporView::applicationConfigSettings();
        const auto saved=QJsonDocument::fromJson(settings.value(QStringLiteral("Fpga/desiredConfiguration")).toByteArray());
        require(saved.object().value("pressureSource").toInt()==0x43,"desired configuration persisted through application settings");
        require(!window.findChild<QThread*>(QStringLiteral("fpgaUsbWorkerThread"))->isInterruptionRequested(),"worker still alive before teardown");
    }
    require(worker.isNull(),"MainWindow teardown joins and deletes USB worker");
    for(int scale : {150,100}) {
        app.setFont(standardFont);
        native.setValue("font_scale_percent",scale);
        MainWindow restored;
        auto *page=restored.findChild<FpgaControlPage*>(QStringLiteral("fpgaControlPage"));
        require(page && page->configuration().pressureSource==0x43,"next window restores desired config without reconnect");
        restored.findChild<QStackedWidget*>("mainPageStack")->setCurrentIndex(4);
        restored.show();processFor(100);verifyConnectionCard(page,scale);
        verifyCardMargins(restored,page,scale);restored.close();
    }
    app.setFont(standardFont);
    relayConfigurationPresentation();
    remotePageRouting();
    remoteHomeActions();
    require(native.value("font_scale_percent").toInt()==100,"font scale stays at standard 100 percent");
    std::cout<<"fpga main window tests passed\n";
}
