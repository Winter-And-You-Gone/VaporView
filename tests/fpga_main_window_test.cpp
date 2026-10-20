#include "test_settings_sandbox.h"
#include "ground/main/MainWindow.h"
#include "ground/widgets/FpgaControlPage.h"
#include "ground/widgets/TelemetryPanels.h"
#include "ground/navigation/CombinationNavigationPage.h"
#include "ground/rtk/RtkConfigDialog.h"
#include "LogService.h"
#include "ground/widgets/SegmentedSwitchButton.h"
#include "shared/config/ApplicationConfig.h"
#include "shared/theme/AppTheme.h"
#include "ground/devices/RemoteSkyController.h"
#include "TcpTelemetryLink.h"
#include "TelemetryCodec.h"
#include "FpgaTelemetry.h"
#include "FpgaDeviceController.h"
#include "FpgaVlp1.h"
#include "ground/session/SessionWaveformRepository.h"
#include <QFile>
#include "ground/wave/TcpWavePanel.h"
#include "SkyConfig.h"
#include <QApplication>
#include <QAction>
#include <QStyleFactory>
#include <QTabWidget>
#include <QScrollArea>
#include <QAbstractButton>
#include <QToolButton>
#include <QComboBox>
#include <QCheckBox>
#include <QGridLayout>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QRegularExpression>
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
#include <QSignalBlocker>
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
static QLabel *deviceWaveRate(MainWindow &window)
{
    for(auto *label:window.findChildren<QLabel*>())
        if(label->property("deviceTcpWaveHint")==QStringLiteral("deviceTcpWaveTransportHint"))return label;
    return nullptr;
}
static bool onlyPressureDataAvailable(MainWindow& window)
{
    int found=0;
    for(auto *pill:window.findChildren<QFrame*>("homeTelemetrySummaryPill")) {
        auto *name=pill->findChild<QLabel*>("homeTelemetrySummaryNameLabel");
        auto *value=pill->findChild<QLabel*>("homeTelemetrySummaryValueLabel");
        if(!name||!value)continue;
        if(QStringList{QStringLiteral("基础"),QStringLiteral("特征值"),QStringLiteral("状态"),QStringLiteral("原始波形"),QStringLiteral("谐波波形"),QStringLiteral("波形采集"),QStringLiteral("天→地"),QStringLiteral("地→天"),QStringLiteral("合")}.contains(name->text())&&value->property("telemetryAvailable").toBool())return false;
        if(!QStringList{"EPSILON","PTB","HMP","Lidar",QStringLiteral("波形")}.contains(name->text()))continue;
        if(value->property("telemetryAvailable").toBool()!=(name->text()=="PTB"))return false;
        ++found;
    }
    return found>=5;
}
static bool onlyPressureConnected(MainWindow &window)
{
    if(homeAction(window,"PTB210")->property("state")!="connected")return false;
    for(const auto& name:QStringList{"EPSILON","HMP","TFA1500","AI-8288",QStringLiteral("波形源")})
        if(homeAction(window,name)->property("state")!="disconnected")return false;
    int internalSensors=0;
    for(auto *button:window.findChildren<QToolButton*>("homeDeviceActionButton")) {
        const auto source=button->property("fpgaSensorSource").toUInt();
        if(source==0x43||source==0x44) {
            if(button->property("state")!="disconnected")return false;
            ++internalSensors;
        }
    }
    return internalSensors==2;
}
static void verifyRelayIndicators(MainWindow &window,const QStringList &enabled)
{
    for(const auto &item:{qMakePair("Epsilon","deviceRemoteEpsilonEnabledCheck"),
            qMakePair("Pressure","deviceRemotePressureEnabledCheck"),
            qMakePair("Humidity","deviceRemoteHumidityEnabledCheck"),
            qMakePair("Lidar","deviceRemoteLidarEnabledCheck"),
            qMakePair("Temperature","deviceRemoteTemperatureEnabledCheck"),
            qMakePair("Ai8","deviceRemoteAi8TemperatureEnabledCheck"),
            qMakePair("Wave","deviceFpgaAcquisitionEnabledCheck"),
            qMakePair("Bmp390","deviceInternalEnabledBMP390"),
            qMakePair("Sht45","deviceInternalEnabledSHT45")}) {
        auto *check=window.findChild<QCheckBox*>(item.second);
        require(check&&!check->isEnabled(),"all relay enable indicators are read-only");
        require(check->isChecked()==enabled.contains(item.first),"relay indicators reflect only confirmed FPGA channel states");
        require(!check->toolTip().isEmpty()&&check->accessibleDescription()==check->toolTip(),
            "relay readback availability and operation guidance are accessible");
        check->click();
        require(check->isChecked()==enabled.contains(item.first),"clicking a read-only indicator cannot toggle its state");
    }
}
static void verifyCardMargins(MainWindow &window,FpgaControlPage *page,int scale)
{
    const QSize originalSize=window.size();
    window.resize(originalSize.expandedTo(QSize(2000,1000))); processFor(50);
    auto *stack=window.findChild<QStackedWidget*>("mainPageStack");
    auto *homeCards=window.findChild<QWidget*>("homeOverviewSplitter");
    stack->setCurrentIndex(0); processFor(50);
    auto *internal=window.findChild<QLabel*>("homeInternalEnvironment");
    require(internal && internal->height()>=internal->sizeHint().height()
        && internal->width()>=internal->sizeHint().width(),
        "home internal environment title fits three metrics at the current font scale");
    QWidget *standardTitle=nullptr;
    for(auto *title:window.findChildren<QWidget*>("sectionTitleBar"))
        if(title->parentWidget()->property("homeLaserTemperatureOverviewCard").toBool())standardTitle=title;
    require(!internal->text().contains('\n') && standardTitle
        && internal->parentWidget()->height()==standardTitle->height(),
        "internal environment stays on one line without increasing the standard title bar height");
    const QRect homeRect(homeCards->mapTo(stack,QPoint()),homeCards->size());
    stack->setCurrentIndex(4); processFor(50);
    page->setUiTestState(true,false,false,5000);page->simulateOperation({{"op","connect"}});
    require(waitUntil([&]{return page->uiTestReady()&&!page->uiTestBusy();}),"layout fixture reads actual simulated board registers");
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
            for(const auto *name : {"fpgaDliaChannels","fpgaWaveChannels","fpgaRecordingCards"}) {
                auto *row=page->findChild<QHBoxLayout*>(name);
                require(row&&row->count()==2,"paired FPGA rows each retain two cards");
                auto *first=row->itemAt(0)->widget(),*second=row->itemAt(1)->widget();
                channelCards.append(first);channelCards.append(second);
                if(!first->isVisibleTo(page))continue;
                const QRect firstRect(first->mapTo(stack,QPoint()),first->size()),secondRect(second->mapTo(stack,QPoint()),second->size());
                require(firstRect.top()==secondRect.top()&&firstRect.bottom()==secondRect.bottom(),"paired channel cards share one row and equal heights");
                require(qAbs(firstRect.width()-secondRect.width())<=1,"paired channel cards divide the available width equally");
                require(secondRect.left()-firstRect.right()-1==qRound(12*scale/100.0),"paired cards retain the shared scaled card gap");
                require(firstRect.left()==homeRect.left()&&secondRect.right()==homeRect.right(),"paired channel cards fill the row within the home outer margins");
            }
            if(tab==tabs->count()-1) {
                auto *row=page->findChild<QHBoxLayout*>("fpgaRecordingCards");auto *recording=row->itemAt(0)->widget();
                for(const auto *name:{"fpgaRawStart","fpgaRawStop","fpgaRecord","fpgaReplay","fpgaExport"}) {
                    auto *button=page->findChild<QAbstractButton*>(name);const QRect rect(button->mapTo(recording,QPoint()),button->size());
                    require(recording->rect().contains(rect)&&button->width()<recording->width(),"recording buttons fit inside the half-width card");
                    require(button->fontMetrics().horizontalAdvance(button->text())<=button->contentsRect().width(),"half-width recording buttons retain their full text at large fonts");
                }
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
            require(sensorGrid&&sensorGrid->rowCount()==7&&sensorGrid->columnCount()==4,"sensor card includes independent RD105 and HMP rows");
            if(tab==1) {
                require(!page->findChild<QComboBox*>("fpgaPressureSource") && !page->findChild<QComboBox*>("fpgaHumiditySource"),
                    "independent sensors have no interchangeable source selectors");
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
        worker = window.testFpgaController()->thread();
        auto *page = window.findChild<FpgaControlPage*>("fpgaControlPage");
        auto *acquisition = page->findChild<QAbstractButton*>("fpgaAcquisitionStart");
        auto *disconnect = page->findChild<QAbstractButton*>("fpgaDisconnect");
        auto *summary = page->findChild<QLabel*>("fpgaWaveSummary32");
        auto *plot = page->findChild<QWidget*>("fpgaWavePlot32");
        auto *diagnostics = page->findChild<QPlainTextEdit*>("fpgaDiagnostics");
        require(acquisition && disconnect && summary && plot && diagnostics, "remote page controls");
        require(QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 0)), "Local slot callable");
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
    QTemporaryDir logs;require(logs.isValid(),"isolated remote home action logs");
    LogService logService("FpgaHomeActionsTest",nullptr,logs.path(),logs.path());
    TcpTelemetryLink server;require(server.listen("127.0.0.1",0),"home control fake Sky server");
    MainWindow window;
    int navigationChanges=0;
    QObject::connect(&logService,&LogService::recordPublished,&window,[&](const LogRecord& record) {
        if(record.fields.value("event")=="navigation_correction_status_changed")++navigationChanges;
    });
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
            if (frame.type!=MsgType::Command || !TelemetryCodec::parseCommand(frame.payload,command)) continue;
            if (command.command_id==CommandId::GetSkyConfig) {
                CommandAck ack;ack.command_id=command.command_id;ack.command_seq=command.command_seq;
                server.writeBytes(encoder.encodeFrame(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack),sequence++,1));
                server.writeBytes(encoder.encodeFrame(MsgType::SkyConfig,QJsonDocument(sky.toJson()).toJson(QJsonDocument::Compact),sequence++,1));
                continue;
            }
            if (command.command_id!=CommandId::DeviceOperation
                || !TelemetryCodec::parseDeviceOperationRequest(command.payload,request)) continue;
            require(request.device_id==SkyDeviceId::Fpga && request.operation==DeviceOperation::FpgaControl,
                "home actions use typed FPGA controls, never legacy serial connect commands");
            requests.append(request);commandSequence=command.command_seq;
        }
    });
    require(remote->openTcp("127.0.0.1",server.localPort())&&waitUntil([&]{return remote->isOpen();}),"home remote link opens");
    window.testInjectRemoteSkyConfig(sky.toJson());
    FpgaControlConfig config;config.pressureSource=0x43;
    QJsonObject registers;
    for (quint32 address:{0x6004u,0x6204u,0x6304u,0x6404u,0x6504u,0x6604u,0x4004u,0x4104u,0x5004u,0x5104u})
        registers[QString::number(address)]=1;
    for (quint32 address:{0x50u,0x403cu,0x413cu}) registers[QString::number(address)]=0;
    registers[QString::number(0x4014)]=250000;
    registers[QString::number(0x504c)]=1000;
    registers[QString::number(0x514c)]=2000;
    QJsonObject status{{"connected",true},{"ready",true},{"busy",false},{"detail","ready"},
        {"configuration",config.toJson()},{"registers",registers},{"archived_records","0"}};
    status["sensor_rates_hz"]=QJsonObject{{"64",1.0},{"66",750.0},{"67",2.0},{"68",4.0},{"69",1000.0}};
    const auto send=[&](MsgType type,const QByteArray &payload) {
        const auto bytes=encoder.encodeFrame(type,payload,sequence++,1);
        require(server.writeBytes(bytes)==bytes.size(),"home control Sky frame sent");
    };
    auto *navigation=window.findChild<Ground::Navigation::CombinationNavigationPage*>("combinationNavigationPage");
    auto *navigationPanel=static_cast<Ground::Navigation::NavigationStatusPanel*>(window.findChild<QWidget*>("navigationStatusPanel"));
    require(navigation&&navigationPanel,"remote navigation status widgets exist");
    const auto refreshNavigation=[&] {
        TelemetryStatus telemetryStatus;
        DeviceStatusItem epsilonStatus;epsilonStatus.device_id=SkyDeviceId::Epsilon;epsilonStatus.state=DeviceState::Connected;
        telemetryStatus.devices.append(epsilonStatus);
        send(MsgType::TelemetryStatus,TelemetryCodec::serializeTelemetryStatus(telemetryStatus));
        TelemetryBasic basic;basic.validity_flags=BasicHasEpsilonTime|BasicHasGnssQuality;
        basic.epsilon_time_us=1;basic.gnss_fix_code=6;basic.gnss_satellites=18;
        send(MsgType::TelemetryBasic,TelemetryCodec::serializeBasicTelemetry(basic));
        require(waitUntil([&]{navigation->refreshStatus();return navigationPanel->snapshot().navigationDataAvailable
            &&navigationPanel->snapshot().gnssFixCode==6;}),"fresh remote navigation baseline is available");
    };
    auto disconnected=status;disconnected["connected"]=false;disconnected["ready"]=false;disconnected["registers"]=QJsonObject{};
    send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(disconnected));
    auto *epsilon=homeAction(window,"EPSILON");
    require(waitUntil([&]{return epsilon->isEnabled()&&epsilon->property("state")=="disconnected";}),
        "remote home can connect FPGA before a USB session exists");
    epsilon->click();
    require(waitUntil([&]{return requests.size()==1;}),"home begins a remote FPGA connection");
    QJsonObject connectControl;require(FpgaRemote::parseControl(requests.last().payload,connectControl)
        &&connectControl.value("op")=="connect"&&connectControl.value("locator")==sky.fpga.locator
        &&connectControl.value("backend")==sky.fpga.configuration.backend
        &&connectControl.value("recover").isBool()&&!connectControl.value("recover").toBool(),
        "home uses the Sky USB endpoint with selective read-only handshake");
    CommandAck connectAck;connectAck.command_id=CommandId::DeviceOperation;connectAck.command_seq=commandSequence;
    send(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(connectAck));processFor(30);
    require(requests.size()==1,"USB acceptance ACK does not enable the sensor early");
    DeviceOperationResponse connected;connected.request_id=requests.last().request_id;connected.device_id=SkyDeviceId::Fpga;
    connected.operation=DeviceOperation::FpgaControl;connected.error_code=CommandErrorCode::InternalError;
    connected.payload=FpgaRemote::encodeStatus(disconnected);
    send(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(connected));
    require(waitUntil([&]{return epsilon->isEnabled();}),"USB failure unlocks the home action");
    processFor(60);require(requests.size()==1,"USB failure never enables a sensor or retries connection automatically");
    epsilon->click();require(waitUntil([&]{return requests.size()==2;}),"a manual retry reconnects USB");
    connectAck.command_seq=commandSequence;send(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(connectAck));
    connected.request_id=requests.last().request_id;connected.error_code=CommandErrorCode::Ok;
    connected.payload=FpgaRemote::encodeStatus(status);
    send(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(connected));
    require(waitUntil([&]{return requests.size()==3;}),"successful USB readback continues with the requested sensor only");
    QJsonObject enableControl;require(FpgaRemote::parseControl(requests.last().payload,enableControl)
        &&enableControl.value("op")=="sensor"&&enableControl.value("source").toInt()==0x42,"remote home enables the requested source");
    connectAck.command_seq=commandSequence;send(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(connectAck));
    connected.request_id=requests.last().request_id;
    send(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(connected));
    require(waitUntil([&]{return epsilon->isEnabled()&&epsilon->property("state")=="connected";}),
        "register readback enables home controls without any valid measurement packets");
    require(!homeAction(window,"RD105")->isEnabled(),"unsupported RD105 remains unavailable");
    auto *pressureRate=window.findChild<PtbPanel*>()->findChild<QLabel*>("rateLabel");
    auto *humidityRate=window.findChild<HmpPanel*>()->findChild<QLabel*>("rateLabel");
    auto *distanceRate=window.findChild<LidarPanel*>()->findChild<QLabel*>("rateLabel");
    auto *epsilonRate=window.findChild<QWidget*>("epsilonPanel")->parentWidget()->findChild<QLabel*>("rateLabel");
    require(pressureRate&&humidityRate&&distanceRate&&epsilonRate,"home sensor frequency labels exist");
    const bool ratesShown=waitUntil([&]{QMetaObject::invokeMethod(&window,"onRefreshTimer");
        return pressureRate->text().contains("1.0")&&humidityRate->text().contains("--")
            &&distanceRate->text().contains("1000.0")&&epsilonRate->text().contains("750.0");});
    if(!ratesShown)std::cerr<<"FPGA rates: "<<qPrintable(pressureRate->text())<<", "<<qPrintable(humidityRate->text())
        <<", "<<qPrintable(distanceRate->text())<<", "<<qPrintable(epsilonRate->text())<<'\n';
    require(ratesShown,
        "remote FPGA rates come from Sky raw sensors and fixed PTB, with HMP unavailable until HMP data arrives");
    auto *internal=window.findChild<QLabel*>("homeInternalEnvironment");
    require(internal,"home internal metrics exist");
    FpgaSensor::Reading bmp;bmp.source=0x43;bmp.kind=FpgaSensor::SensorKind::Bmp390;
    bmp.validity={true,true,true,true};bmp.pressurePa=100000;
    FpgaSensor::Reading sht;sht.source=0x44;sht.kind=FpgaSensor::SensorKind::Sht45;
    sht.validity={true,true,true,true};sht.temperatureC=22;sht.humidityPct=33;
    send(MsgType::FpgaSensor,FpgaRemote::encodeSensor(bmp,1));
    send(MsgType::FpgaSensor,FpgaRemote::encodeSensor(sht,2));
    require(waitUntil([&]{return internal->text().contains("1000.00") && internal->text().contains("22.00") && internal->text().contains("33.00");}),
        "remote internal metrics use typed FPGA sensors independently of basic environmental telemetry");
    sht.validity.measurement=false;send(MsgType::FpgaSensor,FpgaRemote::encodeSensor(sht,2));
    require(waitUntil([&]{return internal->text().count(QStringLiteral("—"))==2 && internal->text().contains("1000.00");}),
        "invalid SHT clears temperature and humidity without clearing independent BMP pressure");
    registers[QString::number(0x6304)]=0;status["registers"]=registers;send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));
    require(waitUntil([&]{return internal->text().count(QStringLiteral("—"))==3;}),"disabled BMP cannot retain a live internal value");
    registers[QString::number(0x6304)]=1;status["registers"]=registers;send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));
    const QStringList names{"EPSILON","PTB210","TFA1500","AI-8288",QStringLiteral("波形源")};
    const QList<quint16> sources{0x42,0x40,0x45,0x46,0};
    auto *waveEnabled=window.findChild<QCheckBox*>("deviceFpgaAcquisitionEnabledCheck");
    require(waitUntil([&]{return window.findChild<QCheckBox*>("deviceInternalEnabledBMP390")->isChecked();}),
        "remote BMP enable readback arrives before indicator verification");
    require(waveEnabled&&!waveEnabled->isEnabled()&&waveEnabled->isChecked(),"remote acquisition read-only state reflects confirmed registers");
    verifyRelayIndicators(window,{"Epsilon","Pressure","Lidar","Ai8","Wave","Bmp390","Sht45"});
    require(deviceWaveRate(window)&&deviceWaveRate(window)->text().contains("250000")
        &&deviceWaveRate(window)->toolTip().contains("2000"),"remote ADC and DLIA rates use actual register readback");
    for (int index=0;index<names.size();++index) {
        auto *button=homeAction(window,names[index]);
        for (bool enabled:{false,true}) {
            if(sources[index]!=0x42)refreshNavigation();
            const int changesBefore=navigationChanges;
            const auto before=registers;const int count=requests.size();
            button->click();
            require(waitUntil([&]{return requests.size()==count+1;}),"home click sends one FPGA request");
            require(stack->currentIndex()==0,"remote per-device action keeps the home page selected");
            QJsonObject control;require(FpgaRemote::parseControl(requests.last().payload,control),"home payload validates");
            require(control.value("enabled").toBool()==enabled && control.value("op")== (sources[index]?"sensor":"acquisition"),
                "home connect/disconnect selects the expected FPGA operation");
            if(sources[index])require(control.value("source").toInt()==sources[index],"home pressure mapping stays PTB210");
            CommandAck ack;ack.command_id=CommandId::DeviceOperation;ack.command_seq=commandSequence;
            send(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack));processFor(30);
            require(!button->isEnabled()&&button->property("state")=="connecting","ACK does not change confirmed device state");
            if(!sources[index])require(!waveEnabled->isEnabled()&&waveEnabled->isChecked()!=enabled,
                "remote acquisition checkbox keeps readback state until final confirmation");
            if(sources[index]!=0x42) {
                status["busy"]=true;send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));processFor(20);
                navigation->refreshStatus();
                require(navigationPanel->snapshot().navigationDataAvailable&&navigationPanel->snapshot().gnssFixCode==6,
                    "a pending remote sensor command preserves EPSILON navigation");
                require(!epsilon->isEnabled(),"unrelated EPSILON action remains locked during the remote transaction");
            }
            button->click();processFor(20);require(requests.size()==count+1,"in-flight home action cannot be duplicated");
            if(sources[index])registers[QString::number(0x6004+(sources[index]-0x40)*0x100)]=enabled?1:0;
            else for(quint32 address:{0x4004u,0x4104u,0x5004u,0x5104u})registers[QString::number(address)]=enabled?1:0;
            status["registers"]=registers;status["busy"]=false;
            DeviceOperationResponse response;response.request_id=requests.last().request_id;response.device_id=SkyDeviceId::Fpga;
            response.operation=DeviceOperation::FpgaControl;response.payload=FpgaRemote::encodeStatus(status);
            send(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(response));
            require(waitUntil([&]{return button->isEnabled()&&button->property("state")== (enabled?"connected":"disconnected");}),
                "final register readback confirms the home connection state");
            if(!sources[index])require(!waveEnabled->isEnabled()&&waveEnabled->isChecked()==enabled,
                "remote checkbox changes only after final acquisition readback");
            if(sources[index]!=0x42) {
                navigation->refreshStatus();
                require(navigationChanges==changesBefore,"remote sensor actions do not log false GNSS loss and recovery");
            }
            if(sources[index])for(const auto &key:before.keys())
                if(key!=QString::number(0x6004+(sources[index]-0x40)*0x100))require(registers.value(key)==before.value(key),"one sensor action preserves other modules");
        }
    }
    auto *monitor=window.findChild<TcpWavePanel*>();
    auto *raw=monitor->findChild<QAbstractButton*>("fpgaMonitorRawButton");
    auto *harmonic=monitor->findChild<QAbstractButton*>("fpgaMonitorHarmonicButton");
    require(raw&&harmonic&&raw->isEnabled()&&harmonic->isEnabled(),"relay monitor has two acquisition controls");
    const auto complete=[&](CommandErrorCode error=CommandErrorCode::Ok) {
        status["registers"]=registers;status["busy"]=false;
        DeviceOperationResponse response;response.request_id=requests.last().request_id;response.device_id=SkyDeviceId::Fpga;
        response.operation=DeviceOperation::FpgaControl;response.error_code=error;response.payload=FpgaRemote::encodeStatus(status);
        send(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(response));
    };
    const auto expectRequest=[&](QAbstractButton *button,const QString &op,bool enabled) {
        const int count=requests.size();button->click();
        require(waitUntil([&]{return requests.size()==count+1;}),"monitor sends one typed FPGA operation");
        QJsonObject control;require(FpgaRemote::parseControl(requests.last().payload,control)
            &&control.value("op")==op&&control.value("enabled").toBool()==enabled,"monitor chooses RAW versus shared acquisition");
        require(!raw->isEnabled()&&!harmonic->isEnabled()&&stack->currentIndex()==0,"monitor actions lock during commands and keep the home page");
    };
    expectRequest(raw,"raw",true);
    registers[QString::number(0x50)]=3;registers[QString::number(0x403c)]=registers[QString::number(0x413c)]=1;
    complete();require(waitUntil([&]{return raw->isEnabled()&&raw->text().contains(QStringLiteral("停止"));}),"RAW readback updates monitor button");
    expectRequest(raw,"raw",false);
    registers[QString::number(0x50)]=registers[QString::number(0x403c)]=registers[QString::number(0x413c)]=0;
    complete();require(waitUntil([&]{return raw->isEnabled();}),"RAW stop completes");
    require(harmonic->text().contains(QStringLiteral("停止")),"stopping RAW preserves harmonic acquisition");
    expectRequest(harmonic,"acquisition",false);
    for(quint32 address:{0x4004u,0x4104u,0x5004u,0x5104u})registers[QString::number(address)]=0;
    complete();require(waitUntil([&]{return raw->isEnabled()&&harmonic->text().contains(QStringLiteral("开始"));}),"shared acquisition stop updates both controls");
    expectRequest(raw,"acquisition",true);
    const int acquisitionCount=requests.size();
    CommandAck ack;ack.command_id=CommandId::DeviceOperation;ack.command_seq=commandSequence;
    send(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack));processFor(40);
    for(quint32 address:{0x4004u,0x4104u,0x5004u,0x5104u})registers[QString::number(address)]=1;
    status["registers"]=registers;status["busy"]=true;
    send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));processFor(40);
    require(requests.size()==acquisitionCount,"ACK and intermediate register status cannot start RAW early");
    complete();require(waitUntil([&]{return requests.size()==acquisitionCount+1;}),"RAW follows confirmed acquisition completion");
    QJsonObject rawControl;require(FpgaRemote::parseControl(requests.last().payload,rawControl)
        &&rawControl.value("op")=="raw"&&rawControl.value("enabled").toBool(),"chained second operation enables RAW");
    registers[QString::number(0x50)]=3;registers[QString::number(0x403c)]=registers[QString::number(0x413c)]=1;
    complete();require(waitUntil([&]{return raw->isEnabled();}),"chained RAW completes");
    expectRequest(harmonic,"acquisition",false);
    for(quint32 address:{0x4004u,0x4104u,0x5004u,0x5104u,0x50u,0x403cu,0x413cu})registers[QString::number(address)]=0;
    complete();require(waitUntil([&]{return raw->isEnabled()&&raw->text().contains(QStringLiteral("开始"));}),"shared stop also disables RAW");
    expectRequest(raw,"acquisition",true);
    const int failedCount=requests.size();complete(CommandErrorCode::InternalError);
    require(waitUntil([&]{return raw->isEnabled();}),"failed acquisition unlocks monitor");
    send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));processFor(60);
    require(requests.size()==failedCount,"failed acquisition never chains or retries RAW");
    monitor->setEnglish(true);
    require(raw->text().contains("Start")&&harmonic->text().contains("Start"),"both monitor controls translate");
    monitor->setEnglish(false);
    registers[QString::number(0x6210)]=921600;status["registers"]=registers;
    send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));processFor(50);
    auto *baud=window.findChild<QComboBox*>("deviceEpsilonBaudComboFpgaEditor");
    require(baud&&!baud->isEnabled()&&baud->currentText()=="921600","connected remote device locks its inline baud editor");
    const auto presets=window.testLocalDeviceConfigSnapshot();
    const int stopCount=requests.size();epsilon->click();
    require(waitUntil([&]{return requests.size()==stopCount+1;}),"disconnect sends one device stop");
    registers[QString::number(0x6204)]=0;complete();
    require(waitUntil([&]{return baud->isEnabled();}),"only disconnected remote sensor becomes editable");
    const int settingsCount=requests.size();baud->setCurrentText("460800");
    send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));processFor(70);
    require(requests.size()==settingsCount&&baud->currentText()=="460800","remote parameter edits are staged and survive periodic readback without network writes");
    epsilon->click();
    require(waitUntil([&]{return requests.size()==settingsCount+1;}),"connect submits staged settings over the existing radio TCP");
    QJsonObject settingsRequest;require(FpgaRemote::parseControl(requests.last().payload,settingsRequest)
        &&settingsRequest.value("op")=="sensor"&&settingsRequest.value("source").toInt()==0x42
        &&settingsRequest.value("enabled").toBool()&&settingsRequest.value("baud").toInt()==460800,
        "remote connection atomically carries staged UART settings");
    ack.command_seq=commandSequence;send(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack));processFor(30);
    require(!baud->isEnabled()&&epsilon->property("state")=="connecting","remote ACK cannot confirm parameter write or connection");
    complete(CommandErrorCode::ConfigApplyFailed);
    require(waitUntil([&]{return baud->isEnabled()&&epsilon->isEnabled();})&&baud->currentText()=="460800","failed connect retains draft and permits correction");
    const int draftFailedCount=requests.size();processFor(60);require(requests.size()==draftFailedCount,"failed parameter connection never retries automatically");
    epsilon->click();require(waitUntil([&]{return requests.size()==draftFailedCount+1;}),"manual reconnect resubmits retained draft");
    registers[QString::number(0x6210)]=460800;registers[QString::number(0x6204)]=1;complete();
    require(waitUntil([&]{return !baud->isEnabled()&&epsilon->property("state")=="connected"&&baud->currentText()=="460800";}),
        "final Sky readback locks the editor and confirms connection");
    require(window.testLocalDeviceConfigSnapshot()==presets,"remote staged FPGA parameters preserve Direct presets");
    const int count=requests.size();remote->close();
    require(waitUntil([&]{return !epsilon->isEnabled();}),"telemetry loss disables FPGA home controls");
    require(!raw->isEnabled()&&!harmonic->isEnabled(),"telemetry loss disables both monitor controls");
    verifyRelayIndicators(window,{});
    QMetaObject::invokeMethod(&window,"onRefreshTimer");
    require(pressureRate->text().contains("--")&&distanceRate->text().contains("--")
        &&!epsilonRate->text().contains("750.0"),"telemetry loss clears cached FPGA frequencies");
    navigation->refreshStatus();
    require(!navigationPanel->snapshot().navigationDataAvailable,"real telemetry loss still clears navigation availability");
    processFor(50);require(requests.size()==count&&stack->currentIndex()==0,"link closure sends no FPGA stop and never changes page");
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));window.close();server.close();
}
static void localSimulatorRouting()
{
    const auto check=[](bool ok,const char *message){if(!ok)throw std::runtime_error(message);};
    QTemporaryDir output;check(output.isValid(),"isolated local recording directory");
    VaporView::LogService logService("FpgaLocalHomeActionsTest",nullptr,output.filePath("logs"),output.filePath("logs"));
    auto settings=VaporView::applicationConfigSettings();
    QSettings native("VaporView","MainWindow");
    native.setValue("recording_directory",output.path());
    settings.remove("Fpga/desiredConfiguration");
    MainWindow window;
    window.resize(1600,1000);
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));
    auto *path=window.findChild<QComboBox*>("deviceSensorTransportPathCombo");
    check(path,"local sensor path selector exists");path->setCurrentIndex(path->findData("fpga_relay"));
    verifyRelayIndicators(window,{});
    auto *page=window.findChild<FpgaControlPage*>("fpgaControlPage");
    window.findChild<QStackedWidget*>("mainPageStack")->setCurrentWidget(page);window.show();
    auto *backend=page->findChild<QComboBox*>("fpgaBackend");
    auto *devices=page->findChild<QComboBox*>("fpgaLocator");
    auto *connectButton=page->findChild<QAbstractButton*>("fpgaConnect");
    auto *disconnect=page->findChild<QAbstractButton*>("fpgaDisconnect");
    auto *acquisition=page->findChild<QAbstractButton*>("fpgaAcquisitionStart");
    check(backend&&devices&&connectButton&&disconnect&&acquisition,"normal FPGA controls are present");
    backend->setCurrentIndex(backend->findData("simulator"));
    check(!page->findChild<QComboBox*>("fpgaPressureSource"),"pressure source selection is removed");
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
    QSet<quint16> connectionSuccesses;
    QObject::connect(&logService,&VaporView::LogService::recordPublished,&window,[&](const VaporView::LogRecord& record) {
        if(record.fields.value("event")=="fpga_device_connection_result"&&record.fields.value("result")=="succeeded") {
            check(record.fields.value("ui_visibility")=="attention","per-device connection results appear in the default log card");
            connectionSuccesses.insert(quint16(record.fields.value("fpga_source").toUInt()));
        }
    });
    QVector<VaporView::FpgaVlp1::Frame> selectiveCommands;
    const auto commandObserver=QObject::connect(window.testFpgaController(),&VaporView::Ground::Devices::FpgaDeviceController::rawCommand,
        &window,[&](quint64,const QByteArray& bytes) {
            VaporView::FpgaVlp1::StreamParser parser;
            for(const auto& frame:parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),bytes.size()))
                selectiveCommands.append(frame);
        },Qt::QueuedConnection);
    pressureAction->click();
    check(waitUntil([&]{return pressureAction->isEnabled()&&pressureAction->property("state")=="connected"&&disconnect->isEnabled();},12000),
        "individual home action starts only the requested device on an idle simulated FPGA");
    QObject::disconnect(commandObserver);
    check(onlyPressureConnected(window),"connecting one simulated device leaves all six other sensors and waveform acquisition disconnected");
    verifyRelayIndicators(window,{"Pressure"});
    check(waitUntil([&]{return onlyPressureDataAvailable(window);}),"local summary reports only actual PTB data and no fabricated telemetry");
    check(!selectiveCommands.isEmpty(),"individual home action performs a real protocol handshake");
    bool targetStarted=false;
    for(const auto& frame:selectiveCommands) {
        const auto message=frame.header.message;
        if(message==0xb) {
            check(frame.header.source==0x40,"home connection never starts or stops another sensor");targetStarted=true;
        } else if(message==3) {
            check(frame.payload.size()>=4,"complete register write payload");
            const auto address=qFromLittleEndian<quint32>(frame.payload.data());
            check((address>=0x6000&&address<0x6100)||address==0x2c||address==0x30||address==0x7004||address==0x8004,
                "home connection writes only the requested sensor and shared upload controls");
        } else check(message==0xfe||message==1||message==2,"other handshake operations are read-only");
    }
    check(targetStarted,"idle simulator actually receives the target start command");
    check(waitUntil([&]{return connectionSuccesses.contains(0x40);})&&connectionSuccesses.size()==1,
        "only the requested simulated sensor reports a connection result");
    homeAction(window,"HMP")->click();
    check(waitUntil([&]{return homeAction(window,"HMP")->isEnabled()&&homeAction(window,"HMP")->property("state")=="connected";})
        &&pressureAction->property("state")=="connected","connecting a second home device preserves the first");
    check(waitUntil([&]{return connectionSuccesses.contains(0x41);}),"individual HMP connection confirms valid data before reconnect");
    disconnect->click();check(waitUntil([&]{return connectButton->isEnabled();}),"individual connection releases USB before full-connect regression");
    QMetaObject::invokeMethod(&window,"onConnectClicked");
    check(waitUntil([&]{auto *wave=homeAction(window,QStringLiteral("波形源"));
        return wave->isEnabled()&&wave->property("state")=="connected";},20000),
        "title connection opens FPGA then starts every supported sensor and shared acquisition");
    check(waitUntil([&]{return connectionSuccesses==QSet<quint16>{0,0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x48};}),
        "all eight sensors and shared acquisition report confirmed connections");
    verifyRelayIndicators(window,{"Epsilon","Pressure","Humidity","Lidar","Temperature","Ai8","Wave","Bmp390","Sht45"});
    check(connectionSuccesses.contains(0x41),"HMP has its own confirmed connection log");
    {
        int rdReads=0,aiReads=0,asciiStarts=0;
        const auto observer=QObject::connect(window.testFpgaController(),&VaporView::Ground::Devices::FpgaDeviceController::rawCommand,
            &window,[&](quint64,const QByteArray& bytes) {
                VaporView::FpgaVlp1::StreamParser parser;
                for(const auto& frame:parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),bytes.size()))if(frame.header.message==3 && frame.payload.size()>=12) {
                    const auto address=qFromLittleEndian<quint32>(frame.payload.data());rdReads+=address==0x6880;aiReads+=address==0x6680;asciiStarts+=address==0x6240;
                }
            },Qt::QueuedConnection);
        auto* readRd=window.findChild<QToolButton*>("temperatureFpgaReadParameters");
        check(readRd&&readRd->isEnabled(),"RD105 existing page exposes its FPGA full-parameter read action");readRd->click();
        check(waitUntil([&]{return rdReads>10&&readRd->isEnabled();},15000),"RD105 parameter page completes asynchronous verified FPGA reads");
        auto* readAi=window.findChild<QAbstractButton*>("ai8ReadParametersButton");
        check(readAi&&readAi->isEnabled(),"AI8 existing parameter page is enabled by extension capabilities");readAi->click();
        check(waitUntil([&]{return aiReads>5&&readAi->isEnabled();},12000),"AI8 existing parameter page reaches real FPGA register transactions");
        auto* readEpsilon=window.findChild<QAbstractButton*>("epsilonSettingsReadButton");
        check(readEpsilon&&readEpsilon->isEnabled(),"EPSILON existing settings page is enabled by extension capabilities");readEpsilon->click();
        check(waitUntil([&]{return asciiStarts>5&&readEpsilon->isEnabled();},12000),"EPSILON existing parameter page uses the mailbox through its production worker");
        auto* packetRates=window.findChild<QAbstractButton*>("deviceEpsilonPacketRatesButton");
        check(packetRates&&packetRates->isEnabled(),"new firmware enables the shared EPSILON packet-rate entry");
        QObject::disconnect(observer);
    }
    for(const auto &entry: {qMakePair("deviceEpsilonBaudComboFpgaEditor","921600"),
            qMakePair("devicePressureBaudComboFpgaEditor","9600"),
            qMakePair("deviceLidarBaudComboFpgaEditor","500000"),
            qMakePair("deviceAi8TemperatureBaudComboFpgaReadback","19200"),
            qMakePair("devicePressureRateComboFpgaEditor","1"),
            qMakePair("deviceInternalRateSHT45FpgaEditor","1")}) {
        auto *label=window.findChild<QLabel*>(entry.first);auto *editor=window.findChild<QComboBox*>(entry.first);
        check((label?label->text():editor?editor->currentText():QString())==QString::fromUtf8(entry.second),
            "local VLP simulator readbacks reach device configuration fields");
    }
    const auto presets=window.testLocalDeviceConfigSnapshot();
    auto *baud=window.findChild<QComboBox*>("deviceEpsilonBaudComboFpgaEditor");
    check(baud&&!baud->isEnabled(),"local connected sensor locks baud editing");
    auto *baudAction=homeAction(window,"EPSILON");baudAction->click();
    check(waitUntil([&]{return baud->isEnabled()&&baudAction->isEnabled();}),"local per-device disconnect enables parameter editing");
    baud->setCurrentText("460800");
    struct RefreshLockObserver : QObject {
        bool observed=false;
        bool eventFilter(QObject *object,QEvent *event) override {
            if(event->type()==QEvent::EnabledChange)
                observed|=!static_cast<QWidget*>(object)->isEnabled();
            return false;
        }
    } refreshLock;
    baud->installEventFilter(&refreshLock);
    emit page->refreshRequested();
    check(waitUntil([&]{return refreshLock.observed;}),"manual refresh locks editing while reading registers");
    baud->removeEventFilter(&refreshLock);
    check(waitUntil([&]{return baud->isEnabled()&&baudAction->isEnabled();})&&baud->currentText()=="460800","local readback preserves the staged value");
    baudAction->click();
    check(waitUntil([&]{return !baud->isEnabled()&&baudAction->isEnabled()&&baudAction->property("state")=="connected"
        &&baud->currentText()=="460800";},12000),"local connection applies staged baud and locks editing after verified enable");
    check(window.testLocalDeviceConfigSnapshot()==presets,"FPGA parameter writes preserve all Direct presets");
    check(!page->findChild<QLabel*>("fpgaConnectionStatus")->text().contains(QStringLiteral("修改 BMP 地址")),"already-enabled BMP does not receive a duplicate address-setting start");
    check(stack->currentIndex()==0,"local auto-connect keeps the home page selected");
    const QStringList homeNames{"EPSILON","PTB210","TFA1500","AI-8288",QStringLiteral("波形源")};
    auto *waveEnabled=window.findChild<QCheckBox*>("deviceFpgaAcquisitionEnabledCheck");
    check(waveEnabled&&deviceWaveRate(window)&&deviceWaveRate(window)->text().contains("1000000"),
        "local ADC actual sampling rate is displayed");
    for(const auto &name:homeNames) {
        auto *button=homeAction(window,name);
        for(bool enabled:{false,true}) {
            button->click();
            check(waitUntil([&]{return button->isEnabled()&&button->property("state")== (enabled?"connected":"disconnected");},10000),
                "local home action confirms sensor or acquisition enable through actual VLP1 readback");
            check(disconnect->isEnabled()&&stack->currentIndex()==0,"individual stop keeps USB open and stays on home");
            if(name==QStringLiteral("波形源"))check(!waveEnabled->isEnabled()&&waveEnabled->isChecked()==enabled,
                "local acquisition checkbox follows confirmed VLP1 start and stop");
        }
    }
    check(homeAction(window,"RD105")->isEnabled(),"local relay exposes RD105 capability from GET_CAPABILITIES");
    auto *navigation=window.findChild<VaporView::Ground::Navigation::CombinationNavigationPage*>("combinationNavigationPage");
    auto *navigationPanel=static_cast<VaporView::Ground::Navigation::NavigationStatusPanel*>(window.findChild<QWidget*>("navigationStatusPanel"));
    check(navigation&&navigationPanel,"local navigation status widgets exist");
    check(waitUntil([&]{navigation->refreshStatus();return navigationPanel->snapshot().navigationDataAvailable
        &&navigationPanel->snapshot().gnssFixCode==6;}),"local simulator supplies a fresh GNSS fix");
    int navigationChanges=0;
    QObject::connect(&logService,&VaporView::LogService::recordPublished,&window,[&](const VaporView::LogRecord& record) {
        if(record.fields.value("event")=="navigation_correction_status_changed")++navigationChanges;
    });
    for(bool enabled:{false,true}) {
        pressureAction->click();navigation->refreshStatus();
        check(!homeAction(window,"EPSILON")->isEnabled(),"EPSILON action remains locked during the local PTB transaction");
        check(navigationPanel->snapshot().navigationDataAvailable&&navigationPanel->snapshot().gnssFixCode==6,
            "a pending PTB210 command preserves local EPSILON navigation");
        check(waitUntil([&]{return pressureAction->isEnabled()&&pressureAction->property("state")== (enabled?"connected":"disconnected");},10000),
            "PTB210 home stop and start complete through VLP1 readback");
        navigation->refreshStatus();
        check(navigationChanges==0,"PTB210 stop and start do not log false GNSS loss and recovery");
    }
    auto *epsilonAction=homeAction(window,"EPSILON");epsilonAction->click();
    check(waitUntil([&]{return epsilonAction->isEnabled()&&epsilonAction->property("state")=="disconnected";}),
        "actual EPSILON stop completes");
    navigation->refreshStatus();
    check(!navigationPanel->snapshot().navigationDataAvailable&&navigationChanges>0,
        "actual EPSILON stop still clears navigation and logs the transition");
    epsilonAction->click();
    check(waitUntil([&]{return epsilonAction->isEnabled()&&epsilonAction->property("state")=="connected";}),
        "EPSILON is re-enabled after the disconnect regression");
    check(waitUntil([&]{navigation->refreshStatus();return navigationPanel->snapshot().navigationDataAvailable;}),
        "fresh navigation returns after actual EPSILON reconnect");
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
    auto *pressureRate=window.findChild<PtbPanel*>()->findChild<QLabel*>("rateLabel");
    auto *humidityRate=window.findChild<HmpPanel*>()->findChild<QLabel*>("rateLabel");
    auto *distanceRate=window.findChild<LidarPanel*>()->findChild<QLabel*>("rateLabel");
    auto *epsilonRate=window.findChild<QWidget*>("epsilonPanel")->parentWidget()->findChild<QLabel*>("rateLabel");
    check(pressureRate&&humidityRate&&distanceRate&&epsilonRate,"local home sensor frequency labels exist");
    const auto rateHz=[](QLabel *label){return QRegularExpression("([0-9]+\\.[0-9]+)\\s*Hz")
        .match(label->text().simplified()).captured(1).toDouble();};
    check(waitUntil([&]{return rateHz(pressureRate)>0.0&&rateHz(humidityRate)>0.0
        &&rateHz(distanceRate)>0.0&&rateHz(epsilonRate)>20.0;}),
        "local FPGA simulator supplies visible rates without serial collectors");
    auto *internal=window.findChild<QLabel*>("homeInternalEnvironment");
    check(internal && waitUntil([&]{return !internal->text().contains(QStringLiteral("—"));}),
        "both internal sensors reach the home title bar through real VLP frames");
    auto *environmentPressure=window.findChild<PtbPanel*>()->findChild<QLabel*>("highlightedValue");
    const double pressureHpa=QRegularExpression("([0-9]+\\.[0-9]+)").match(environmentPressure->text()).captured(1).toDouble();
    check(pressureHpa>=1012.97&&pressureHpa<=1013.53,"BMP pressure never replaces environmental PTB pressure across the PTB simulation cycle");
    check(homeAction(window,"HMP")->isEnabled(),"HMP firmware channel is available through the common home connection action");

    QWidget *deviceConfig=path;
    while(stack->indexOf(deviceConfig)<0)deviceConfig=deviceConfig->parentWidget();
    stack->setCurrentWidget(deviceConfig);processFor(100);
    verifyRelayIndicators(window,{"Epsilon","Pressure","Humidity","Lidar","Temperature","Ai8","Wave","Bmp390","Sht45"});
    check(deviceWaveRate(window)->width()>=deviceWaveRate(window)->sizeHint().width(),"actual ADC sampling rate fits its device configuration cell");
    stack->setCurrentIndex(0);processFor(100);

    auto *ai8Action=homeAction(window,"AI-8288");
    auto *ai8Channel=page->findChild<QDoubleSpinBox*>("fpgaAi8Channel");
    auto *apply=page->findChild<QAbstractButton*>("fpgaApply");
    QLabel *channel8=nullptr;
    for(auto *label:window.findChildren<QLabel*>("ai8TemperatureOverviewValueLabel"))
        if(label->property("channelIndex").toInt()==7)channel8=label;
    check(ai8Channel&&apply&&channel8&&channel8->text()=="---","unpolled AI8 channel eight is unavailable");
    ai8Action->click();
    check(waitUntil([&]{return ai8Action->isEnabled()&&ai8Channel->isEnabled();}),
        "AI8 disconnect unlocks channel selection");
    ai8Channel->setValue(8);apply->click();
    check(waitUntil([&]{
        for(auto *label:page->findChildren<QLabel*>())
            if(label->text().contains("0x661c = 8"))return apply->isEnabled();
        return false;
    }),"AI8 channel eight configuration is read back");
    ai8Action->click();
    check(waitUntil([&]{return ai8Action->isEnabled()&&channel8->text()!="---";}),
        "AI8 channel eight traverses simulator, register readback, adapter and home overview");
    for(auto *label:window.findChildren<QLabel*>("ai8TemperatureOverviewValueLabel"))
        if(label->property("channelIndex").toInt()>0&&label->property("channelIndex").toInt()<7)
            check(label->text()=="---","a selected-channel snapshot never fabricates other AI8 channels");

    acquisition->click();
    auto *resumedHarmonic=window.findChild<TcpWavePanel*>()->findChild<QAbstractButton*>("fpgaMonitorHarmonicButton");
    check(waitUntil([&]{return acquisition->isEnabled()&&resumedHarmonic->isEnabled()
        &&resumedHarmonic->text().contains(QStringLiteral("停止"));}),
        "sampling resumes after the isolated AI8 channel configuration");

    QDoubleSpinBox *target=nullptr;
    for(auto *spin:page->findChildren<QDoubleSpinBox*>())if(spin->suffix()==QStringLiteral(" °C"))target=spin;
    check(target,"AI8 target editor exists");target->setValue(31.5);
    auto *temperature=page->findChild<QAbstractButton*>("fpgaSetTemperature");temperature->click();
    check(waitUntil([&]{return temperature->isEnabled();}),"local AI8 transaction finishes");
    check(page->findChild<QLabel*>("fpgaConnectionStatus")->text().contains("AI8"),"local setpoint completion is visible in connection status");
    auto *monitor=window.findChild<TcpWavePanel*>();
    auto *raw=monitor->findChild<QAbstractButton*>("fpgaMonitorRawButton");
    auto *harmonic=monitor->findChild<QAbstractButton*>("fpgaMonitorHarmonicButton");
    check(raw&&harmonic&&raw->isEnabled()&&harmonic->isEnabled(),"local monitor acquisition controls available");
    harmonic->click();
    check(waitUntil([&]{return raw->isEnabled()&&harmonic->text().contains(QStringLiteral("开始"));}),"monitor stops shared local acquisition");
    raw->click();
    check(!raw->isEnabled()&&!harmonic->isEnabled(),"local monitor blocks duplicate commands immediately");
    check(waitUntil([&]{return raw->isEnabled()&&raw->text().contains(QStringLiteral("停止"));},10000),
        "local monitor starts acquisition then RAW after final readback");
    check(stack->currentIndex()==0,"local monitor acquisition does not jump to FPGA page");
    check(waitUntil([&]{return page->findChild<QWidget*>("fpgaWavePlot32")->property("sampleCount").toInt()>0;}),
          "normal local RAW control receives fragmented ADC data");
    check(monitor&&monitor->property("fpgaWaveformMode").toBool(),"home monitor selects FPGA data");
    check(waitUntil([&]{return monitor->findChild<QWidget*>("tcpWaveRawPlot")->property("sampleCount").toInt()>0
        &&monitor->findChild<QWidget*>("tcpWaveHarmonicPlot")->property("sampleCount").toInt()>0;}),
        "real VLP1 simulator supplies DLIA H1 and normalized H2 to the home monitor");
    raw->click();
    check(waitUntil([&]{return raw->isEnabled()&&raw->text().contains(QStringLiteral("开始"));}),"local monitor stops RAW upload");
    check(harmonic->text().contains(QStringLiteral("停止")),"local RAW stop preserves DLIA acquisition");
    raw->click();check(waitUntil([&]{return raw->isEnabled()&&raw->text().contains(QStringLiteral("停止"));}),"local RAW restarts without resetting running acquisition");
    auto *record=page->findChild<QAbstractButton*>("fpgaRecord");record->click();
    check(waitUntil([&]{return record->text().contains("停止");}),"local record action starts recording");
    processFor(500);record->click();check(waitUntil([&]{return record->text().contains("开始");}),"local record action stops and drains");
    const QDir recordings(output.filePath("simulated_sessions"));
    const auto sessions=recordings.entryList(QDir::Dirs|QDir::NoDotAndDotDot);
    check(sessions.size()==1,"local simulator records one isolated session");
    const auto sessionDirectory=recordings.filePath(sessions.first());
    check(!QFile::exists(sessionDirectory+"/raw/fpga_vlp1.dat")&&!QFile::exists(sessionDirectory+"/sensors/fpga_frames.jsonl"),
        "local recording does not archive forwarded protocol bytes");
    const auto metadata=VaporView::Ground::SessionLoader::loadMetadata(sessionDirectory);
    check(metadata.success,"local recording uses the common session metadata");
    const auto catalog=VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata.metadata);
    check(catalog.success&&catalog.catalog.fpgaChoices.size()==6&&!catalog.catalog.fpgaSampleCache,
        "local recording exposes both H1/H2 lanes and on-demand normalized components");
    check(VaporView::Ground::SessionWaveformRepository::readFrame(catalog.catalog,0).success,
        "local full-resolution harmonic data remains readable after recording stops");
    check(VaporView::Ground::SessionWaveformRepository::loadCachedPeakSeries(catalog.catalog).success,
        "local recording preserves full-cycle normalized peak features");
    disconnect->click();check(waitUntil([&]{return connectButton->isEnabled();}),"local disconnect releases the simulator");
    verifyRelayIndicators(window,{});
    QMetaObject::invokeMethod(&window,"onRefreshTimer");
    check(pressureRate->text().contains("--")&&humidityRate->text().contains("--")
        &&distanceRate->text().contains("--"),"local USB disconnect clears all sensor frequencies");
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
static void simulatedMonitorActions()
{
    MainWindow window;
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));
    auto *mode=window.findChild<QAction*>("uiTestModeAction");
    auto *page=window.findChild<FpgaControlPage*>();
    auto *monitor=window.findChild<TcpWavePanel*>();
    auto *raw=monitor->findChild<QAbstractButton*>("fpgaMonitorRawButton");
    auto *harmonic=monitor->findChild<QAbstractButton*>("fpgaMonitorHarmonicButton");
    require(mode&&page&&raw&&harmonic,"simulated monitor controls exist");
    mode->trigger();
    require(waitUntil([&]{return page->uiTestController()!=nullptr;}),"UI test installs an isolated protocol controller");
    require(!page->uiTestConnected()&&!raw->isEnabled(),"UI test mode does not fabricate an FPGA connection");
    QMetaObject::invokeMethod(&window,"onConnectClicked",Qt::DirectConnection);
    QMetaObject::invokeMethod(&window,"onDisconnectClicked",Qt::DirectConnection);
    require(waitUntil([&]{return !page->uiTestConnected()&&!page->uiTestBusy()
        &&homeAction(window,"EPSILON")->isEnabled();}),"disconnect cancels a pending title connection");
    processFor(150);
    require(!page->uiTestConnected(),"cancelled title connection cannot reconnect or continue starting sensors");
    QMetaObject::invokeMethod(&window,"onConnectClicked",Qt::DirectConnection);
    emit page->disconnectRequested();
    require(waitUntil([&]{return !page->uiTestConnected()&&!page->uiTestBusy()
        &&homeAction(window,"EPSILON")->isEnabled();}),"FPGA page disconnect also cancels a pending title connection");
    processFor(150);
    require(!page->uiTestConnected(),"FPGA page disconnect cannot leave a queued reconnect");
    QMetaObject::invokeMethod(&window,"onConnectClicked",Qt::DirectConnection);
    require(waitUntil([&]{return harmonic->isEnabled()&&harmonic->text().contains(QStringLiteral("停止"));},20000),
        "title connection starts every supported device on a cold simulated board");
    verifyRelayIndicators(window,{"Epsilon","Pressure","Humidity","Lidar","Temperature","Ai8","Wave","Bmp390","Sht45"});
    require(raw->text().contains(QStringLiteral("开始")),"connecting all devices does not start RAW upload");
    mode->trigger();mode->trigger();
    require(waitUntil([&]{return page->uiTestController()!=nullptr&&!page->uiTestConnected();}),"reset protocol board for selective connection regression");
    require(QMetaObject::invokeMethod(&window,"onDisconnectClicked",Qt::DirectConnection),"disconnect layout simulation before individual connection");
    auto *pressure=homeAction(window,"PTB210");
    require(waitUntil([&]{return pressure->isEnabled()&&pressure->property("state")=="disconnected";}),"layout simulation is disconnected");
    pressure->click();
    require(waitUntil([&]{return pressure->isEnabled()&&pressure->property("state")=="connected";}),"layout simulation connects the requested device");
    require(onlyPressureConnected(window),"layout simulation leaves all other sensors and waveform acquisition disconnected");
    verifyRelayIndicators(window,{"Pressure"});
    require(waitUntil([&]{return onlyPressureDataAvailable(window);}),"UI summary reports only actual PTB data and no fabricated telemetry");
    require(raw->text().contains(QStringLiteral("开始"))&&harmonic->text().contains(QStringLiteral("开始")),"individual simulation connection leaves waveform acquisition stopped");
    require(QMetaObject::invokeMethod(&window,"onConnectClicked",Qt::DirectConnection),"batch simulation connection is available");
    require(waitUntil([&]{return harmonic->isEnabled()&&harmonic->text().contains(QStringLiteral("停止"));},20000),
        "UI test title connection starts all devices through the production VLP1 controller");
    verifyRelayIndicators(window,{"Epsilon","Pressure","Humidity","Lidar","Temperature","Ai8","Wave","Bmp390","Sht45"});
    for(const auto &entry:{qMakePair("deviceEpsilonBaudComboFpgaEditor","921600"),
            qMakePair("devicePressureBaudComboFpgaEditor","9600"),
            qMakePair("deviceLidarBaudComboFpgaEditor","500000"),
            qMakePair("deviceInternalRateSHT45FpgaEditor","1")}) {
        auto *value=window.findChild<QComboBox*>(entry.first);
        require(value&&value->currentText()==QString::fromUtf8(entry.second),"UI test sensor register values update inline fields immediately");
    }
    auto *rate=window.findChild<QComboBox*>("deviceInternalRateSHT45FpgaEditor");
    QToolButton *humidity=nullptr;
    for(auto *button:window.findChildren<QToolButton*>("homeDeviceActionButton"))
        if(button->property("fpgaSensorSource").toUInt()==0x44)humidity=button;
    require(rate&&humidity&&!rate->isEnabled(),"UI test connected sensor locks inline frequency");
    require(waitUntil([&]{return humidity->isEnabled();}),"title connection releases individual controls after their action spinner settles");humidity->click();
    require(waitUntil([&]{return rate->isEnabled();}),"UI test individual disconnect enables frequency");rate->setCurrentText("2.");
    processFor(150);require(rate->currentText()=="2.","refresh preserves partially typed decimal frequency");
    rate->setCurrentText("2.5");
    processFor(150);require(rate->currentText()=="2.5","simulation refresh preserves staged frequency");
    humidity->click();
    require(waitUntil([&]{return !rate->isEnabled()&&humidity->isEnabled();}),
        "UI test connect applies staged frequency");
    processFor(150);require(rate->currentText()=="2.5","periodic simulation retains applied frequency");
    harmonic->click();
    require(waitUntil([&]{return harmonic->isEnabled()&&harmonic->text().contains(QStringLiteral("开始"));}),"simulated monitor stops acquisition");
    raw->click();
    require(!raw->isEnabled()&&!harmonic->isEnabled(),"simulated RAW start locks until acquisition completes");
    require(waitUntil([&]{return raw->isEnabled()&&raw->text().contains(QStringLiteral("停止"));}),"simulated RAW chains after acquisition readback");
    raw->click();
    require(waitUntil([&]{return raw->isEnabled()&&raw->text().contains(QStringLiteral("开始"))&&harmonic->text().contains(QStringLiteral("停止"));}),"simulated RAW stop preserves harmonics");
    harmonic->click();raw->click();mode->trigger();processFor(400);
    mode->trigger();
    require(waitUntil([&]{return page->uiTestController()!=nullptr&&!page->uiTestConnected();}),"leaving UI test cancels pending RAW and the next simulation starts at cold boot");
    mode->trigger();
    require(!mode->isChecked(),"monitor simulation restores normal application mode");
}

static void harmonicHomeGraphs()
{
    const QFont standardFont = qApp->font();
    QSettings settings("VaporView", "MainWindow");
    for (int scale : {100, 130}) {
        qApp->setFont(standardFont);
        settings.setValue("font_scale_percent", scale);
        MainWindow window;
        QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 0));
        auto *path = window.findChild<QComboBox*>("deviceSensorTransportPathCombo");
        path->setCurrentIndex(path->findData("fpga_relay"));
        auto *monitor = window.findChild<TcpWavePanel*>();
        auto *page = window.findChild<FpgaControlPage*>();
        window.findChild<QStackedWidget*>("mainPageStack")->setCurrentIndex(0);
        window.show();
        VaporView::FpgaWave::CompletedStream wave;
        wave.source=0x30; wave.format=0; wave.bytesPerPoint=8; wave.totalPoints=128;
        wave.complete=true; wave.cycleId=1;
        for (int point=0; point<128; ++point) {
            char word[4];
            qToLittleEndian<qint32>(100+point, reinterpret_cast<uchar*>(word)); wave.pointBytes.append(word,4);
            qToLittleEndian<qint32>(20+qRound(10*std::sin(point*0.1)), reinterpret_cast<uchar*>(word)); wave.pointBytes.append(word,4);
        }
        page->updateWaveform(wave);
        for (const QSize size : {QSize(1600,1000), QSize(1150,850)}) {
            window.resize(size);
            for (bool english : {false,true}) {
                monitor->setEnglish(english); processFor(100);
                const QStringList names{"tcpWaveRawPlot","fpgaWaveH2Plot","tcpWaveHarmonicPlot","tcpWavePeakTrendPlot"};
                QList<QRect> cards;
                for (const auto &name : names) {
                    auto *plot = monitor->findChild<QWidget*>(name);
                    require(plot && plot->isVisibleTo(monitor) && plot->property("sampleCount").toInt()>0,
                        "all four home graphs render the validated DLIA cycle");
                    auto *card = plot->parentWidget();
                    require(card->rect().contains(plot->geometry()),
                        "harmonic plot and its sample axis stay inside the card at larger font scales");
                    const QRect rect(card->mapTo(monitor,QPoint()),card->size());
                    require(monitor->rect().contains(rect),"all four graph cards fit inside the home waveform panel");
                    for (const auto &other : cards)
                        require(!rect.intersects(other),"home graph cards do not overlap after resize or translation");
                    cards.append(rect);
                    for (auto *title : card->findChildren<QLabel*>("sectionTitleLabel"))
                        require(title->width()>=title->fontMetrics().horizontalAdvance(title->text()),
                            "home harmonic chart titles remain fully visible");
                }
            }
        }
        monitor->setEnglish(false);
        path->setCurrentIndex(path->findData("direct_devices")); processFor(50);
        require(!monitor->findChild<QWidget*>("fpgaWaveH2Plot")->isVisibleTo(monitor),
            "returning to Direct restores the original three graph layout");
        window.close();
    }
    qApp->setFont(standardFont);
    settings.setValue("font_scale_percent",100);
}

static void relayConfigurationPresentation()
{
    const QStringList presetNames{"deviceRemoteEpsilonEnabledCheck","deviceRemotePressureEnabledCheck",
        "deviceRemoteHumidityEnabledCheck","deviceRemoteLidarEnabledCheck","deviceRemoteTemperatureEnabledCheck",
        "deviceRemoteAi8TemperatureEnabledCheck","deviceTcpWaveEnabledCheck","deviceInternalEnabledBMP390","deviceInternalEnabledSHT45"};
    QList<bool> enabledPresets;
    {
    MainWindow window;
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));
    auto *stack=window.findChild<QStackedWidget*>("mainPageStack");
    auto *path=window.findChild<QComboBox*>("deviceSensorTransportPathCombo");
    QWidget *configPage=path;
    while(stack->indexOf(configPage)<0)configPage=configPage->parentWidget();
    stack->setCurrentWidget(configPage);window.resize(1600,1000);window.show();processFor(50);
    path->setCurrentIndex(path->findData("direct_devices"));
    for(int i=0;i<presetNames.size();++i) {
        auto *check=window.findChild<QCheckBox*>(presetNames[i]);
        require(check,"Direct startup preset exists");check->setChecked(i%2==0);enabledPresets.append(check->isChecked());
    }
    const QStringList devices{"Epsilon","Pressure","Humidity","Lidar","Temperature","Ai8Temperature"};
    QStringList ports;
    for(const auto &device:devices) {
        auto *port=window.findChild<QComboBox*>("device"+device+"PortCombo");
        require(port,"device serial editor exists");ports.append(port->currentText());
    }
    const auto before=window.testLocalDeviceConfigSnapshot();
    path->setCurrentIndex(path->findData("fpga_relay"));processFor(50);
    verifyRelayIndicators(window,{});
    auto *waveEnabled=window.findChild<QCheckBox*>("deviceFpgaAcquisitionEnabledCheck");
    auto *grid=qobject_cast<QGridLayout*>(waveEnabled->parentWidget()->layout());
    require(grid&&grid->columnCount()==7,"device actions immediately follow enable without an empty source column");
    require(waveEnabled->isVisible()&&!waveEnabled->isEnabled()&&deviceWaveRate(window)->text().contains(QStringLiteral("—")),
        "unavailable FPGA acquisition cannot show a preset as actual sampling or enabled state");
    const bool originalDark=VaporView::isDarkThemeEnabled();
    for(bool dark:{true,false}) {
        if(VaporView::isDarkThemeEnabled()!=dark) {
            QMetaObject::invokeMethod(&window,"onToggleTheme");processFor(80);
        }
        for(const auto *name:{"deviceRemoteEpsilonEnabledCheck","deviceRemotePressureEnabledCheck",
                "deviceRemoteHumidityEnabledCheck","deviceRemoteLidarEnabledCheck",
                "deviceRemoteTemperatureEnabledCheck","deviceRemoteAi8TemperatureEnabledCheck"}) {
            auto *check=window.findChild<QCheckBox*>(name);
            require(check&&!check->isEnabled(),"FPGA channel readback stays read-only");
            const QSignalBlocker blocker(check);
            const bool beforeChecked=check->isChecked();
            for(bool checked:{false,true}) {
                check->setChecked(checked);
                const auto image=check->grab().toImage();
                const auto primary=VaporView::appThemeColor(VaporView::AppThemeColor::Primary,dark);
                int themePixels=0;
                for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
                    const auto pixel=image.pixelColor(x,y);
                    themePixels+=std::abs(pixel.red()-primary.red())<12
                        &&std::abs(pixel.green()-primary.green())<12&&std::abs(pixel.blue()-primary.blue())<12;
                }
                require(themePixels>8,"read-only checked and unchecked device indicators render the active theme accent");
            }
            check->setChecked(beforeChecked);
        }
    }
    if(VaporView::isDarkThemeEnabled()!=originalDark) {
        QMetaObject::invokeMethod(&window,"onToggleTheme");processFor(80);
    }
    for(int i=0;i<devices.size();++i) {
        const auto name="device"+devices[i]+"PortCombo";
        auto *port=window.findChild<QComboBox*>(name);
        auto *endpoint=window.findChild<QLabel*>(name+"FpgaEndpoint");
        auto *baud=window.findChild<QComboBox*>("device"+devices[i]+"BaudCombo");
        require(endpoint&&endpoint->isVisible()&&endpoint->text()=="FPGA"&&!port->isVisible()&&!port->isEnabled(),
            "all six relay serial ports are replaced by a read-only FPGA endpoint");
        auto *readback=window.findChild<QLabel*>(baud->objectName()+"FpgaReadback");
        auto *editor=window.findChild<QComboBox*>(baud->objectName()+"FpgaEditor");
        require(baud&&!baud->isVisible()&&!baud->isEnabled()&&((editor&&editor->isVisible())||(readback&&readback->isVisible())),
            "FPGA uses separate inline fields while preserving Direct presets");
        require(!window.findChild<QAbstractButton*>(baud->objectName()+"FpgaEdit"),"no redundant parameter settings buttons remain");
    }
    window.findChild<FpgaControlPage*>()->setUiTestState(true,false,false,5000,VaporView::SensorTransportPath::FpgaRelay);
    auto *simulationPage=window.findChild<FpgaControlPage*>();
    const auto simulate=[&](const QJsonObject& request) {
        simulationPage->simulateOperation(request);
        require(waitUntil([&]{return !simulationPage->uiTestBusy()&&(request.value("op")!="connect"||simulationPage->uiTestReady());}),"layout fixture waits for simulated protocol response");
    };
    simulate({{"op","connect"}});simulate({{"op","dac"},{"channel",0},{"enabled",true}});
    simulate({{"op","acquisition"},{"enabled",true}});simulate({{"op","raw"},{"enabled",true}});
    require(waitUntil([&]{return window.findChild<QWidget*>("tcpWaveRawPlot")->property("sampleCount").toInt()>0;}),"protocol simulation reaches the home graph");
    stack->setCurrentIndex(0);processFor(80);
    require(window.findChild<QWidget*>("fpgaWaveH2Plot")->property("sampleCount").toInt()>0,
        "simulated DLIA supplies the additional H2 home graph");
    auto *monitor=window.findChild<TcpWavePanel*>();
    auto *page=window.findChild<FpgaControlPage*>();
    QMetaObject::invokeMethod(monitor,"onClearPeakPlotClicked");
    VaporView::FpgaWave::CompletedStream full;full.source=0x30;full.format=0;full.bytesPerPoint=8;
    full.totalPoints=2002;full.complete=true;full.cycleId=9001;full.timestamp=900100;
    for(quint32 i=0;i<full.totalPoints;++i) {
        char word[4];qToLittleEndian<qint32>(i==1?1000:i==3?2:i==5?0:10,reinterpret_cast<uchar*>(word));full.pointBytes.append(word,4);
        qToLittleEndian<qint32>(i==1?1234:i==3?20:i==5?99999:-5,reinterpret_cast<uchar*>(word));full.pointBytes.append(word,4);
    }
    page->updateWaveform(full);
    require(monitor->testPeakValues()==QVector<float>({10}),"normalized peak uses same-point H1 before display decimation and skips invalid references");
    require(monitor->testHarmonicSamples().size()>3&&monitor->testHarmonicSamples().first()==-0.5&&std::isnan(monitor->testHarmonicSamples()[3]),
        "home waveform displays H2/H1 and preserves invalid-reference gaps across display decimation");
    page->setLanguage(true);page->setLanguage(false);page->updateWaveform(full);
    require(monitor->testPeakValues()==QVector<float>({10}),"FPGA rerender and repeated cycle do not duplicate trend entries");
    const auto h1Before=monitor->testFirstHarmonicSamples();
    const auto h2Before=monitor->testSecondHarmonicSamples();
    VaporView::FpgaWave::CompletedStream partial;partial.source=0x20;partial.totalPoints=2;partial.partial=true;
    window.findChild<FpgaControlPage*>()->updateWaveform(partial);
    require(monitor->testFirstHarmonicSamples()==h1Before&&monitor->testSecondHarmonicSamples()==h2Before,
        "incomplete ADC cannot change the home H1 and H2 graphs");
    partial.source=0x30;partial.cycleId=9002;window.findChild<FpgaControlPage*>()->updateWaveform(partial);
    require(monitor->testFirstHarmonicSamples().isEmpty()&&monitor->testSecondHarmonicSamples().isEmpty()
        &&window.findChild<QWidget*>("tcpWaveHarmonicPlot")->property("sampleCount").toInt()==0,
        "incomplete DLIA clears all three home harmonic graphs through the shared validated display");
    require(monitor->testPeakValues().size()==2&&std::isnan(monitor->testPeakValues().last()),"missing DLIA fragments create a trend gap");
    window.findChild<FpgaControlPage*>()->setUiTestState(false);
    stack->setCurrentWidget(configPage);
    require(!window.findChild<QComboBox*>("devicePressureSourceCombo") && !window.findChild<QComboBox*>("deviceHumiditySourceCombo"),
        "environmental devices have no source selectors");
    require(window.findChild<QWidget*>("deviceInternalNameBMP390")->isVisible() && window.findChild<QWidget*>("deviceInternalNameSHT45")->isVisible(),
        "BMP390 and SHT45 have independent relay device rows");
    path->setCurrentIndex(path->findData("direct_devices"));processFor(50);
    for(int i=0;i<presetNames.size();++i) {
        auto *check=window.findChild<QCheckBox*>(presetNames[i]);
        require(check->isChecked()==enabledPresets[i],"switching back to Direct restores every original startup preset");
        require(!check->toolTip().contains(QStringLiteral("FPGA"))&&check->accessibleDescription().isEmpty(),
            "Direct presets do not retain FPGA readback guidance");
    }
    require(!waveEnabled->isVisible()&&window.findChild<QCheckBox*>("deviceTcpWaveEnabledCheck")->isVisible(),
        "direct TCP startup preset is separate from the FPGA acquisition checkbox");
    require(window.findChild<QWidget*>("deviceInternalNameBMP390")->isVisible()
        &&window.findChild<QComboBox*>("deviceInternalPortBMP390")->isVisible()
        &&window.findChild<QComboBox*>("deviceInternalBaudBMP390")->currentText()=="115200",
        "internal devices have independent serial rows on the direct path");
    require(window.testLocalDeviceConfigSnapshot().value("epsilon").toObject().value("baud")==before.value("epsilon").toObject().value("baud"),
        "FPGA display never overwrites Direct baud presets");
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
    for(const auto *key:{"epsilon","ptb","hmp","lidar","temperature_controller","ai8_temperature_controller","bmp390","sht45"})
        require(saved.value(key).toObject().value("enabled")==remote.toJson().value(key).toObject().value("enabled"),
            "FPGA readback never overwrites remote Direct startup presets");
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));
    path->setCurrentIndex(path->findData("fpga_relay"));
    window.close();
    }
    const auto stored=VaporView::applicationConfigSettings();
    const QStringList presetKeys{"local_enabled/epsilon","local_enabled/ptb","local_enabled/hmp","local_enabled/lidar",
        "local_enabled/temperature","local_enabled/ai8_temperature","local_enabled/wave_tcp","internal/bmp390/enabled","internal/sht45/enabled"};
    for(int i=0;i<presetKeys.size();++i)
        require(stored.value("MainWindow/"+presetKeys[i]).toBool()==enabledPresets[i],
            "saving settings on the relay path preserves every Direct startup preset");
    MainWindow restored;
    auto *path=restored.findChild<QComboBox*>("deviceSensorTransportPathCombo");
    require(path->currentData()=="fpga_relay","restarted window retains the FPGA path");
    path->setCurrentIndex(path->findData("direct_devices"));
    for(int i=0;i<presetNames.size();++i)
        require(restored.findChild<QCheckBox*>(presetNames[i])->isChecked()==enabledPresets[i],
            "restarting on the FPGA path preserves all Direct startup presets");
    restored.close();
}
static void remoteTransportAndCapabilities()
{
    using namespace VaporView;
    TcpTelemetryLink server; require(server.listen("127.0.0.1",0),"transport fake Sky listens");
    MainWindow window;
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,1));
    auto sky=SkyConfig::defaults();
    sky.ptb.source="ptb210"; sky.hmp.source="hmp3";
    window.testInjectRemoteSkyConfig(sky.toJson());
    TelemetryCodec decoder, encoder; quint16 sequence=1; int applies=0; bool reject=false;
    QObject::connect(&server,&TelemetryLink::bytesReceived,&window,[&](const QByteArray &bytes) {
        for (const auto &frame:decoder.feedBytes(bytes)) {
            CommandMessage command;
            if(frame.type!=MsgType::Command||!TelemetryCodec::parseCommand(frame.payload,command))continue;
            if(command.command_id!=CommandId::GetSkyConfig&&command.command_id!=CommandId::SetSkyConfig)continue;
            CommandAck ack;ack.command_id=command.command_id;ack.command_seq=command.command_seq;
            if(command.command_id==CommandId::SetSkyConfig) {
                ++applies;
                if(!reject) require(SkyConfig::fromJson(QJsonDocument::fromJson(command.payload).object(),sky),"parse submitted Sky configuration");
            }
            server.writeBytes(encoder.encodeFrame(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack),sequence++,1));
            const bool read=command.command_id==CommandId::GetSkyConfig;
            server.writeBytes(encoder.encodeFrame(read?MsgType::SkyConfig:MsgType::SkyConfigApplyResult,
                QJsonDocument(read?sky.toJson():QJsonObject{{"success",!reject},{"error",reject?"rejected test":""}}).toJson(QJsonDocument::Compact),sequence++,1));
        }
    });
    auto *remote=window.testRemoteSkyController();
    require(remote->openTcp("127.0.0.1",server.localPort())&&waitUntil([&]{return remote->isOpen();}),"transport radio TCP opens");
    window.testInjectRemoteSkyConfig(sky.toJson());
    const auto generation=remote->linkGeneration();
    auto *path=window.findChild<QComboBox*>("deviceSensorTransportPathCombo");
    path->setCurrentIndex(path->findData("fpga_relay"));
    require(waitUntil([&]{return applies==1&&path->isEnabled();}),"FPGA path is applied and acknowledged without disconnecting radio");
    require(remote->isOpen()&&remote->linkGeneration()==generation&&sky.sensor_transport_path==SensorTransportPath::FpgaRelay&&sky.fpga.enabled,
        "radio TCP session survives Direct to FPGA and Sky FPGA is enabled");
    require(sky.fpga.configuration.pressureSource==0x40 && sky.ptb.source=="ptb210" && sky.hmp.source=="hmp3",
        "remote environmental device identity stays fixed");
    QMetaObject::invokeMethod(&window,"onRtkConfigClicked");
    auto *rtk=window.findChild<RtkConfigDialog*>();
    auto *start=rtk->findChild<QAbstractButton*>("rtkStartButton");
    require(start&&!start->isEnabled()&&start->toolTip().contains("VLP1"),"FPGA RTK start is disabled with a protocol capability reason");
    QMetaObject::invokeMethod(rtk,"onStartClicked");
    require(!rtk->isRunning(),"direct slot invocation cannot start a bypass RTCM stream");
    auto *readAi8=window.findChild<QAbstractButton*>("ai8ReadParametersButton");
    auto *readEpsilon=window.findChild<QAbstractButton*>("epsilonSettingsReadButton");
    require(readAi8&&!readAi8->isEnabled()&&readAi8->toolTip().contains("VLP1"),"FPGA AI8 page commands explain unsupported capability");
    require(readEpsilon&&!readEpsilon->isEnabled(),"FPGA EPSILON parameter commands are disabled");
    reject=true;path->setCurrentIndex(path->findData("direct_devices"));
    require(waitUntil([&]{return applies==2&&path->isEnabled();}),"path rejection completes");
    require(path->currentData()=="fpga_relay"&&remote->isOpen(),"rejected path change restores the confirmed mode and keeps radio");
    reject=false;path->setCurrentIndex(path->findData("direct_devices"));
    require(waitUntil([&]{return applies==3&&path->isEnabled();}),"return to Direct completes");
    require(start->isEnabled()&&sky.sensor_transport_path==SensorTransportPath::DirectDevices&&remote->linkGeneration()==generation,
        "Direct restores RTK availability using the same radio session");
    remote->close();window.close();
}

static void directInternalEnvironmentRouting()
{
    using namespace VaporView;
    MainWindow window;QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));
    auto *path=window.findChild<QComboBox*>("deviceSensorTransportPathCombo");
    path->setCurrentIndex(path->findData("direct_devices"));
    auto *stack=window.findChild<QStackedWidget*>("mainPageStack");
    QWidget *configPage=path;while(stack->indexOf(configPage)<0)configPage=configPage->parentWidget();
    stack->setCurrentWidget(configPage);window.resize(1600,1000);window.show();processFor(100);
    for(const auto &name:{QStringLiteral("BMP390"),QStringLiteral("SHT45")}) {
        auto *port=window.findChild<QComboBox*>("deviceInternalPort"+name);
        auto *baud=window.findChild<QComboBox*>("deviceInternalBaud"+name);
        auto *rate=window.findChild<QComboBox*>("deviceInternalRate"+name);
        require(port&&port->isVisible()&&baud->isVisible()&&baud->currentText()=="115200"&&rate->currentText()=="1",
            "direct internal serial rows start with 115200 8N1 and 1 Hz receive rate");
        port->addItem("COM-"+name,"COM-"+name);port->setCurrentText("COM-"+name);
        rate->setCurrentText("2");window.findChild<QCheckBox*>("deviceInternalEnabled"+name)->setChecked(true);
    }
    const auto before=window.testLocalDeviceConfigSnapshot();
    require(before.value("bmp390").toObject().value("port")=="COM-BMP390"
        &&before.value("sht45").toObject().value("rate_hz")==2,"internal model contains independent direct settings");
    path->setCurrentIndex(path->findData("fpga_relay"));processFor(50);
    path->setCurrentIndex(path->findData("direct_devices"));processFor(50);
    require(window.testLocalDeviceConfigSnapshot()==before,"FPGA path and readback preserve direct internal presets");
    MainWindow restored;require(restored.testLocalDeviceConfigSnapshot().value("bmp390")==before.value("bmp390"),
        "independent internal serial settings persist in isolated application settings");restored.close();
    TcpTelemetryLink server;require(server.listen("127.0.0.1",0),"direct internal fake Sky listens");
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,1));
    auto sky=SkyConfig::defaults();sky.bmp390={true,"SKY-BMP",115200,1.0,{}};sky.sht45={true,"SKY-SHT",57600,2.0,{}};
    auto *remote=window.testRemoteSkyController();
    require(remote->openTcp("127.0.0.1",server.localPort())&&waitUntil([&]{return remote->isOpen();}),"same telemetry TCP opens for direct internal sensors");
    window.testInjectRemoteSkyConfig(sky.toJson());
    QString error;const auto submitted=window.testRemoteSkyConfigFromDeviceConfigUi(&error);
    require(error.isEmpty()&&submitted.value("sht45").toObject().value("baud")==57600,"remote internal settings round trip independently");
    TelemetryCodec encoder;quint16 sequence=1;
    auto send=[&](MsgType type,const QByteArray &payload){server.writeBytes(encoder.encodeFrame(type,payload,sequence++,1));};
    TelemetryStatus status;
    for(auto id:{SkyDeviceId::Bmp390,SkyDeviceId::Sht45}){DeviceStatusItem item;item.device_id=id;item.state=DeviceState::Connected;status.devices.append(item);}
    send(MsgType::TelemetryStatus,TelemetryCodec::serializeTelemetryStatus(status));
    auto *label=window.findChild<QLabel*>("homeInternalEnvironment");
    require(waitUntil([&]{return remote->deviceState(SkyDeviceId::Sht45)==DeviceState::Connected;}),"independent remote device IDs are accepted");
    const auto environmentBefore=window.findChild<PtbPanel*>()->findChild<QLabel*>("highlightedValue")->text();
    FpgaSensor::Reading reading;reading.source=0x43;reading.kind=FpgaSensor::SensorKind::Bmp390;reading.pressurePa=100000;reading.validity={true,true,true,true};
    send(MsgType::InternalEnvironment,FpgaRemote::encodeSensor(reading,1));
    reading.source=0x44;reading.kind=FpgaSensor::SensorKind::Sht45;reading.pressurePa.reset();reading.temperatureC=30;reading.humidityPct=70;
    send(MsgType::InternalEnvironment,FpgaRemote::encodeSensor(reading,2));
    require(waitUntil([&]{return label->text().contains("1000.00")&&label->text().contains("30.00")&&label->text().contains("70.00");}),
        "direct Sky internal telemetry reaches the three home internal metrics");
    require(window.findChild<PtbPanel*>()->findChild<QLabel*>("highlightedValue")->text()==environmentBefore,
        "internal serial telemetry never overwrites the environmental PTB panel");
    status.devices[0].state=DeviceState::Disconnected;send(MsgType::TelemetryStatus,TelemetryCodec::serializeTelemetryStatus(status));
    require(waitUntil([&]{return label->text().contains(QStringLiteral("—"))&&label->text().contains("30.00");}),
        "individual BMP disconnect invalidates pressure while preserving SHT values");
    require(waitUntil([&]{return label->text().count(QStringLiteral("—"))==3;},6000),"stale direct internal data expires without a disconnect");
    remote->close();window.close();
}

static void epsilonMixedPacketPresentation()
{
    QTemporaryDir output;
    require(output.isValid(),"isolated EPSILON regression logs");
    VaporView::LogService logs("FpgaEpsilonTest",nullptr,output.path(),output.path());
    MainWindow window;
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));
    auto *path=window.findChild<QComboBox*>("deviceSensorTransportPathCombo");
    path->setCurrentIndex(path->findData("fpga_relay"));
    auto *controller=window.testFpgaController();
    auto *panel=window.findChild<QWidget*>("epsilonPanel");
    require(controller&&panel,"local FPGA controller and EPSILON panel exist");
    VaporView::FpgaSensor::FpgaSensorAdapter adapter;
    VaporView::FpgaSensor::Reading reading;
    reading.kind=VaporView::FpgaSensor::SensorKind::Epsilon;reading.source=0x42;
    reading.validity.structure=true;reading.validity.measurement=true;
    auto deliver=[&](quint8 id,const QByteArray &payload,bool online=true) {
        reading.epsilonMessageId=id;reading.epsilonData=payload;
        reading.validity.deviceOnline=online;++reading.timestamp;
        const auto measurement=adapter.accept(reading);
        QMetaObject::invokeMethod(controller,[controller,measurement] {
            emit controller->measurementUpdated(measurement);
        },Qt::BlockingQueuedConnection);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);
        QMetaObject::invokeMethod(&window,"onRefreshTimer");
    };
    auto hasData=[&] {
        for(auto *label:panel->findChildren<QLabel*>("valueLabel"))
            if(label->text()!="--")return true;
        return false;
    };
    // Indoor/no-fix AHRS followed by valid packets with neither attitude nor position.
    QByteArray ahrs(48,0);ahrs[26]=char(0x80);ahrs[27]=char(0x3f); // quaternion w = 1
    deliver(0x41,ahrs);require(hasData(),"AHRS displays without a GNSS fix");
    for(int round=0;round<3;++round) {
        deliver(0x53,QByteArray(4,0));require(hasData(),"status packet must not blank EPSILON");
        deliver(0x51,QByteArray(8,0));require(hasData(),"time packet must not blank EPSILON");
        deliver(0x40,QByteArray(56,0));require(hasData(),"IMU packet must not blank EPSILON");
        deliver(0x41,ahrs);require(hasData(),"interleaved AHRS remains visible");
    }
    deliver(0x41,ahrs,false);require(!hasData(),"offline reading still clears EPSILON");
    deliver(0x41,ahrs);require(hasData(),"online reading restores EPSILON");
    QMetaObject::invokeMethod(controller,[controller] {
        emit controller->transportConnectionChanged(false);
    },Qt::BlockingQueuedConnection);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);
    require(!hasData(),"disconnect clears cached EPSILON data");
}

static void harmonicRecordingUi()
{
    const auto check=[](bool ok,const char *message){if(!ok)throw std::runtime_error(message);};
    QTemporaryDir output;check(output.isValid(),"isolated harmonic recording directory");
    QSettings native("VaporView","MainWindow");native.setValue("recording_directory",output.path());
    VaporView::applicationConfigSettings().remove("Fpga/desiredConfiguration");
    MainWindow window;window.resize(1600,1000);
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));
    auto *path=window.findChild<QComboBox*>("deviceSensorTransportPathCombo");
    check(path,"sensor transport selector exists");path->setCurrentIndex(path->findData("fpga_relay"));
    auto *page=window.findChild<FpgaControlPage*>("fpgaControlPage");
    window.findChild<QStackedWidget*>("mainPageStack")->setCurrentWidget(page);window.show();
    auto *backend=page->findChild<QComboBox*>("fpgaBackend");backend->setCurrentIndex(backend->findData("simulator"));
    auto *devices=page->findChild<QComboBox*>("fpgaLocator");auto *connect=page->findChild<QAbstractButton*>("fpgaConnect");
    check(waitUntil([&]{return devices->currentData().toString()=="simulation://fpga"&&connect->isEnabled();},10000),"simulator enumerates for recording");
    connect->click();auto *acquisition=page->findChild<QAbstractButton*>("fpgaAcquisitionStart");
    check(waitUntil([&]{return acquisition->isEnabled();},20000),"recording simulator handshake completes");
    acquisition->click();
    check(waitUntil([&]{return acquisition->isEnabled()&&page->findChild<QWidget*>("fpgaWavePlot48")->property("sampleCount").toInt()>0;}),"DLIA acquisition reaches the page");
    page->findChild<QAbstractButton*>("fpgaRawStart")->click();
    check(waitUntil([&]{return page->findChild<QWidget*>("fpgaWavePlot32")->property("sampleCount").toInt()>0;}),"RAW upload is active during recording verification");
    auto *record=page->findChild<QAbstractButton*>("fpgaRecord");record->click();
    check(waitUntil([&]{return record->text().contains(QStringLiteral("停止"));}),"UI starts common session recording");
    processFor(600);record->click();
    check(waitUntil([&]{return record->text().contains(QStringLiteral("开始"));}),"UI stops and drains recording");
    QDir recordings(output.filePath("simulated_sessions"));const auto sessions=recordings.entryList(QDir::Dirs|QDir::NoDotAndDotDot);
    check(sessions.size()==1,"UI writes one isolated session");const auto directory=recordings.filePath(sessions.first());
    check(!QFile::exists(directory+"/raw/fpga_vlp1.dat"),"active RAW previews do not create protocol archives");
    const auto metadata=VaporView::Ground::SessionLoader::loadMetadata(directory);
    check(metadata.success&&metadata.metadata.waveformFrames>0,"UI records full harmonic cycles");
    const auto catalog=VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata.metadata);
    check(catalog.success&&catalog.catalog.fpgaChoices.size()==6&&!catalog.catalog.fpgaSampleCache,"both H1/H2 lanes are indexed without normalized history storage");
    check(VaporView::Ground::SessionWaveformRepository::readFrame(catalog.catalog,0).success,
          "UI-recorded waveform normalizes on demand");
    check(VaporView::Ground::SessionWaveformRepository::loadCachedPeakSeries(catalog.catalog).success,
          "UI-recorded full-cycle normalized peaks persist");
    auto *recordingStatus=page->findChild<QLabel*>("fpgaRecordingStatus");
    check(recordingStatus&&recordingStatus->text().contains(
        QStringLiteral("H1/H2 周期：%1").arg(metadata.metadata.waveformFrames)),"recording UI reports actual harmonic cycle counts");
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
    if(app.arguments().contains("--epsilon-packets")) {epsilonMixedPacketPresentation();return 0;}
    if(app.arguments().contains("--internal-direct")) {directInternalEnvironmentRouting();return 0;}
    if(app.arguments().contains("--relay-configuration")) {
        relayConfigurationPresentation();std::cout<<"FPGA relay configuration test passed\n";return 0;
    }
    if(app.arguments().contains("--remote-home-actions")) {
        remoteHomeActions();std::cout<<"Remote FPGA home actions test passed\n";return 0;
    }
    if(app.arguments().contains("--transport-capabilities")) {
        remoteTransportAndCapabilities();std::cout<<"Transport capabilities test passed\n";return 0;
    }
    if(app.arguments().contains("--monitor-ui-actions")) {
        simulatedMonitorActions();std::cout<<"Simulated FPGA monitor actions test passed\n";return 0;
    }
    if(app.arguments().contains("--harmonic-recording")) {
        try { harmonicRecordingUi();std::cout<<"Harmonic recording UI test passed\n";return 0; }
        catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
    }
    if(app.arguments().contains("--harmonic-home")) {
        harmonicHomeGraphs();std::cout<<"FPGA harmonic home graphs test passed\n";return 0;
    }
    if(app.arguments().contains("--local-simulator")) {
        try { localSimulatorRouting();std::cout<<"Local FPGA simulator GUI test passed\n";return 0; }
        catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
    }
    epsilonMixedPacketPresentation();
    const auto standardFont=app.font();
    QPointer<QThread> worker;
    {
        MainWindow window;
        auto *stack=window.findChild<QStackedWidget*>(QStringLiteral("mainPageStack"));
        auto *page=window.findChild<FpgaControlPage*>(QStringLiteral("fpgaControlPage"));
        require(stack && stack->count()==5 && page && stack->widget(4)==page,"fifth standalone persistent FPGA page");
        worker=window.testFpgaController()->thread();
        require(worker && worker->isRunning(),"USB worker has its own running event loop");
        require(page->configuration().pressureSource==0x40 && page->configuration().backend=="auto","PTB210 and auto backend defaults");
        auto *status=page->findChild<QLabel*>(QStringLiteral("fpgaConnectionStatus"));
        require(status && !status->text().contains(QStringLiteral("GP01 FX3")),"construction does not auto-connect hardware");
        auto *acquisition=page->findChild<QAbstractButton*>(QStringLiteral("fpgaAcquisitionStart"));
        auto *connectButton=page->findChild<QAbstractButton*>(QStringLiteral("fpgaConnect"));
        require(acquisition && !acquisition->isEnabled() && connectButton && !connectButton->isEnabled(),"ADC stays stopped and connect waits for discovery until page is opened");
        require(!page->findChild<QComboBox*>(QStringLiteral("fpgaPressureSource")),"source selection removed from FPGA page");
        stack->setCurrentIndex(4); stack->setCurrentIndex(0); stack->setCurrentIndex(4);
        require(stack->widget(4)==page,"switching pages preserves the FPGA editor");
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
        require(page->configuration().pressureSource==0x40,"desired configuration persisted through application settings");
        require(!worker->isInterruptionRequested(),"worker still alive before teardown");
    }
    require(worker.isNull(),"MainWindow teardown joins and deletes USB worker");
    for(int scale : {150,100}) {
        app.setFont(standardFont);
        native.setValue("font_scale_percent",scale);
        MainWindow restored;
        auto *page=restored.findChild<FpgaControlPage*>(QStringLiteral("fpgaControlPage"));
        require(page && page->configuration().pressureSource==0x40,"next window restores desired config without reconnect");
        restored.findChild<QStackedWidget*>("mainPageStack")->setCurrentIndex(4);
        restored.show();processFor(100);verifyConnectionCard(page,scale);
        verifyCardMargins(restored,page,scale);restored.close();
    }
    app.setFont(standardFont);
    relayConfigurationPresentation();
    remotePageRouting();
    remoteHomeActions();
    remoteTransportAndCapabilities();
    require(native.value("font_scale_percent").toInt()==100,"font scale stays at standard 100 percent");
    std::cout<<"fpga main window tests passed\n";
}
