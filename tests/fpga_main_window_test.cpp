#include "test_settings_sandbox.h"
#include "IndependentCalibrationFixture.h"
#include "ground/main/MainWindow.h"
#include "ground/main/GroundMainWindowState.h"
#include "ground/widgets/FpgaControlPage.h"
#include "ground/widgets/Ai8TemperatureControllerPanel.h"
#include "ground/widgets/TelemetryPanels.h"
#include "ground/navigation/CombinationNavigationPage.h"
#include "ground/rtk/RtkConfigDialog.h"
#include "LogService.h"
#include "ground/widgets/SegmentedSwitchButton.h"
#include "shared/config/ApplicationConfig.h"
#include "shared/theme/AppTheme.h"
#include "ground/devices/RemoteSkyController.h"
#include "ground/devices/Rd105DeviceSession.h"
#include "TcpTelemetryLink.h"
#include "TelemetryCodec.h"
#include "FpgaTelemetry.h"
#include "FpgaDeviceController.h"
#include "FpgaVirtualTcpTransport.h"
#include "FpgaVlp1.h"
#include "FpgaExtensions.h"
#include "ground/session/SessionWaveformRepository.h"
#include "ground/main/RecordingStatusView.h"
#include <QFile>
#include "ground/wave/TcpWavePanel.h"
#include "SkyConfig.h"
#include <QApplication>
#include <QAction>
#include <QStyleFactory>
#include <QProxyStyle>
#include <QTabWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QAbstractButton>
#include <QToolButton>
#include <QComboBox>
#include <QCheckBox>
#include <QGridLayout>
#include <QElapsedTimer>
#include <QDateTime>
#include <QDialog>
#include <QEventLoop>
#include <QEvent>
#include <QMouseEvent>
#include <QJsonDocument>
#include <QJsonArray>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QMessageBox>
#include <QImage>
#include <QPixmap>
#include <QLayout>
#include <QBoxLayout>
#include <QSplitter>
#include <QPointer>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QStyleOptionSpinBox>
#include <QTemporaryDir>
#include <QDir>
#include <QStandardItemModel>
#include <QSet>
#include <QSignalBlocker>
#include <QScopeGuard>
#include <stdexcept>
#include <cmath>
#include <cstring>
#include <limits>
#include <QtEndian>
#include <cstdlib>
#include <iostream>
#include <memory>

struct MainWindowRtcmTestAccess
{
    static auto summary(MainWindow& window) { return window.remoteTelemetrySummarySections(); }
    static void refreshSummary(MainWindow& window) { window.updateRemoteTelemetrySummaryLabel(); }
    static void refreshCapsules(MainWindow& window) { window.updateHomeDeviceStatusCapsules(); }
    static VaporView::Ground::Main::MainWindowState& state(MainWindow& window) { return *window.state_; }
    static void temperatureResult(MainWindow& window,const VaporView::Ground::Devices::Rd105SessionResult& result)
    { window.onRd105SessionOperationFinished(result); }
    static std::function<void(const QJsonObject&)> completion(MainWindow& window, const QString& id)
    { return window.fpgaRtcmCompletion(id); }
};

struct MainWindowRecordingTestAccess
{
    static bool startScheduled(MainWindow& window) { return window.tryStartScheduledRecording(); }
    static bool stopScheduled(MainWindow& window) { return window.tryStopScheduledRecording(); }
};

static int fontWarnings=0;
static void captureFont(QtMsgType,const QMessageLogContext&,const QString &message){if(message.contains("QFont::setPointSize")){++fontWarnings;std::cerr<<message.toStdString()<<'\n';}}
static void require(bool v,const char *why) { if(!v) {std::cerr<<why<<'\n';std::exit(1);} }
static void rtcmCompletionContext()
{
    QTemporaryDir logs;
    require(logs.isValid(), "RTCM log directory exists");
    VaporView::LogService service("RtcmCompletionTest", nullptr, logs.path(), logs.path());
    QVector<VaporView::LogRecord> records;
    MainWindow window;
    auto& state = MainWindowRtcmTestAccess::state(window);
    auto *rtk = state.rtk_config_dialog_;
    require(rtk, "RTCM dialog exists");
    QObject::connect(&service, &VaporView::LogService::recordPublished, &window,
        [&records](const VaporView::LogRecord& record) { records.append(record); }, Qt::DirectConnection);
    rtk->setUiTestMode(true);
    require(QMetaObject::invokeMethod(rtk, "onStartClicked", Qt::DirectConnection), "start old stream");
    const QString oldId = rtk->rtcmCorrelationId();
    require(!oldId.isEmpty(), "old stream has an identity");
    auto oldCompletion = MainWindowRtcmTestAccess::completion(window, oldId);
    require(QMetaObject::invokeMethod(rtk, "onStopClicked", Qt::DirectConnection), "stop old stream");
    require(QMetaObject::invokeMethod(rtk, "onStartClicked", Qt::DirectConnection), "start new stream");
    const QString newId = rtk->rtcmCorrelationId();
    require(!newId.isEmpty() && newId != oldId, "new stream has another identity");
    state.fpga_rtcm_queue_.append(qMakePair(qint64(1), QJsonObject{{"op", "new-stream"}}));
    state.fpga_rtcm_buffer_ = QByteArray("new-stream");
    state.fpga_rtcm_busy_ = true;
    oldCompletion({{"ok", false}, {"detail", "old failure"}});
    require(!state.fpga_rtcm_busy_ && state.fpga_rtcm_queue_.size() == 1 &&
            state.fpga_rtcm_buffer_ == QByteArray("new-stream"), "old failure preserves new stream work");
    bool found = false;
    for (const auto& record : records)
        if (record.fields.value("ui_message").toString().contains("old failure"))
            found = record.correlation_id == oldId && record.session_id.isEmpty();
    require(found, "old failure keeps its original stream identity without inventing a session");
    state.fpga_rtcm_busy_ = true;
    MainWindowRtcmTestAccess::completion(window, newId)({{"ok", false}, {"detail", "current failure"}});
    require(!state.fpga_rtcm_busy_ && state.fpga_rtcm_queue_.isEmpty() && state.fpga_rtcm_buffer_.isEmpty(),
            "current stream failure preserves the existing cleanup behavior");
    require(QMetaObject::invokeMethod(rtk, "onStopClicked", Qt::DirectConnection), "stop new stream");
}
static QString internalEnvironmentText(QWidget *row)
{
    QStringList values;
    for (auto *label : row->findChildren<QLabel*>("homeInternalEnvironmentValueLabel"))
        values.append(label->text());
    return values.join(QLatin1Char(' '));
}
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
static bool onlyPressureDataAvailable(MainWindow& window, bool demoRates = false)
{
    const auto summary = MainWindowRtcmTestAccess::summary(window);
    if (summary.deviceItems.size() != 9)
        return false;
    if (!demoRates)
    {
        for (const auto &item : summary.rateItems)
            if (item.hasData) return false;
        for (const auto &item : summary.linkItems)
            if (item.hasData) return false;
    }
    for (const auto &item : summary.deviceItems)
        if (item.hasData != (item.label == QStringLiteral("PTB"))) return false;
    return true;
}
static bool onlyPressureConnected(MainWindow &window)
{
    if(homeAction(window,"PTB210")->property("state")!="connected")return false;
    for(const auto& name:QStringList{"EPSILON","HC2A","SDFM300","AI-8288","BMP390","SHT45",QStringLiteral("波形源")})
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
        require(check&&check->isChecked(),"relay desired enable selection survives actual channel changes");
        require(!check->toolTip().isEmpty()&&check->accessibleDescription()==check->toolTip(),
            "relay enable selection guidance is accessible");
        const QMap<QString,VaporView::SkyDeviceId> ids{{"Epsilon",VaporView::SkyDeviceId::Epsilon},
            {"Pressure",VaporView::SkyDeviceId::Ptb},{"Humidity",VaporView::SkyDeviceId::Hc2a},
            {"Lidar",VaporView::SkyDeviceId::Lidar},{"Temperature",VaporView::SkyDeviceId::TemperatureController},
            {"Ai8",VaporView::SkyDeviceId::Ai8TemperatureController},{"Wave",VaporView::SkyDeviceId::WaveTcp},
            {"Bmp390",VaporView::SkyDeviceId::Bmp390},{"Sht45",VaporView::SkyDeviceId::Sht45}};
        QToolButton *action=nullptr;
        for(auto *button:window.findChildren<QToolButton*>("homeDeviceActionButton"))
            if(button->property("deviceConfigAction").toBool()
                &&button->property("deviceConfigRemoteDevice").toInt()==int(ids.value(item.first))) action=button;
        const bool confirmed=waitUntil([&]{return action&&action->property("connected").toBool()==enabled.contains(item.first);},2000);
        if(!confirmed) std::cerr<<"relay action "<<item.first<<" expected "<<enabled.contains(item.first)
            <<" state "<<(action?action->property("state").toString().toStdString():"missing")<<"\n";
        require(confirmed,"actual relay state remains on the action button, independent of enable selection");
    }
}
static void verifyCardMargins(MainWindow &window,FpgaControlPage *page,int scale)
{
    const QSize originalSize=window.size();
    window.resize(originalSize.expandedTo(QSize(2000,1000))); processFor(50);
    auto *stack=window.findChild<QStackedWidget*>("mainPageStack");
    auto *homeCards=window.findChild<QWidget*>("homeOverviewSplitter");
    stack->setCurrentIndex(0); processFor(200);
    auto *homeScroll=window.findChild<QScrollArea*>("mainCardsScrollArea");
    const int homeOverflow=homeScroll->horizontalScrollBar()->maximum();
    auto *internal=window.findChild<QWidget*>("homeInternalEnvironment");
    require(internal && internal->height()>=internal->sizeHint().height()
        && internal->width()>=internal->sizeHint().width(),
        "home internal environment row fits three metrics at the current font scale");
    QWidget *standardTitle=nullptr;
    for(auto *title:window.findChildren<QWidget*>("sectionTitleBar"))
        if(title->parentWidget()->property("homeLaserTemperatureOverviewCard").toBool())standardTitle=title;
    require(internal->findChildren<QLabel*>("homeInternalEnvironmentValueLabel").size()==3 && standardTitle
        && internal->y()>=standardTitle->height(),
        "three internal environment capsules sit below the standard title bar");
    for (auto *label : internal->findChildren<QLabel*>())
        require(label->width()>=label->sizeHint().width() && label->height()>=label->sizeHint().height(),
            "internal metric names and values remain fully visible at the current font scale");
    const auto values=internal->findChildren<QLabel*>("homeInternalEnvironmentValueLabel");
    QStringList originalTexts;
    QList<QRect> originalRects;
    for (auto *value:values) {
        originalTexts.append(value->text());
        originalRects.append(value->geometry());
    }
    const QList<QStringList> readings{
        {"9.99 hPa",QStringLiteral("9.99 °C"),"9.99 %RH"},
        {"1000.00 hPa",QStringLiteral("-0.01 °C"),"100.00 %RH"},
        {"-9999.99 hPa",QStringLiteral("-999.99 °C"),"-999.99 %RH"},
        {QStringLiteral("— hPa"),QStringLiteral("— °C"),QStringLiteral("— %RH")}
    };
    for (const auto &reading:readings) {
        for (int index=0;index<values.size();++index)values[index]->setText(reading[index]);
        internal->layout()->activate();
        for (auto *value:values)value->parentWidget()->layout()->activate();
        for (int index=0;index<values.size();++index) {
            auto *value=values[index];
            require(value->text()==reading[index],"layout check retains the injected internal reading");
            require(value->geometry()==originalRects[index],
                "internal metric value bounds remain stable across placeholders, digit counts and negative readings");
            require(value->alignment().testFlag(Qt::AlignRight),"internal metric text is right aligned inside the value area");
            require(value->width()>=value->fontMetrics().horizontalAdvance(value->text()),
                "internal metric reading and full unit fit without clipping");
        }
    }
    for (int index=0;index<values.size();++index)values[index]->setText(originalTexts[index]);
    processFor(40);
    // Large fonts may require horizontal scrolling; compare the visible card edge.
    const QRect homeRect=QRect(homeCards->mapTo(stack,QPoint()),homeCards->size()).adjusted(0,0,-homeOverflow,0);
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
        const bool channelSettings=tabs->widget(tab)->isAncestorOf(parameterTabs);
        for(int parameterTab=0;parameterTab<(channelSettings?parameterTabs->count():1);++parameterTab) {
            if(channelSettings)parameterTabs->setCurrentIndex(parameterTab);
            processFor(50);
            QList<QWidget*> channelCards;
            for(const auto *name : {"fpgaWmsChannels","fpgaDliaChannels","fpgaWaveChannels","fpgaRecordingCards"}) {
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
                for(const auto *name:{"fpgaRecord","fpgaReplay","fpgaExport"}) {
                    auto *button=page->findChild<QAbstractButton*>(name);const QRect rect(button->mapTo(recording,QPoint()),button->size());
                    require(recording->rect().contains(rect)&&button->width()<recording->width(),"recording buttons fit inside the half-width card");
                    require(button->fontMetrics().horizontalAdvance(button->text())<=button->contentsRect().width(),"half-width recording buttons retain their full text at large fonts");
                }
            }
            if(tab==0) {
                auto *capture=page->findChild<QLabel*>("fpgaCaptureStatus")->parentWidget();
                for(const auto *name:{"fpgaAcquisitionStart","fpgaAcquisitionStop","fpgaRawStart","fpgaRawStop"}) {
                    auto *button=page->findChild<QAbstractButton*>(name);const QRect rect(button->mapTo(capture,QPoint()),button->size());
                    require(capture->rect().contains(rect),"primary acquisition controls stay together");
                    require(button->fontMetrics().horizontalAdvance(button->text())<=button->contentsRect().width(),"primary acquisition labels fit");
                }
            }
            if(channelSettings&&parameterTab==0) {
                auto *row = page->findChild<QHBoxLayout*>("fpgaWmsChannels");
                for (int channel = 0; channel < row->count(); ++channel) {
                    auto *card = row->itemAt(channel)->widget();
                    const QString name = QStringLiteral("fpga%1Scan").arg(channel);
                    auto *input = card->findChild<QDoubleSpinBox*>(name);
                    auto *label = card->findChild<QLabel*>(name + QStringLiteral("Label"));
                    require(input && label && label->buddy() == input && label->isVisible() && input->isVisible(),
                            "each WMS card shows the scan input and its label for the corresponding channel");
                }
                require(page->findChild<QLabel*>("fpgaRegister3028")->text()!=QStringLiteral("—")
                    &&page->findChild<QLabel*>("fpgaRegister3128")->text()!=QStringLiteral("—"),"UI test mode populates both channel diagnostic counters");
            }
            if(tab==0)for(int channel=0;channel<2;++channel) {
                auto *adc=page->findChild<QWidget*>(QString("fpgaWavePlot%1").arg(32+channel));
                auto *h1=page->findChild<QWidget*>(QString("fpgaWavePlot%1").arg(48+channel));
                auto *h2=page->findChild<QWidget*>(QString("fpgaWaveSecondPlot%1").arg(48+channel));
                require(adc->height()==scale&&h1->height()==scale&&h2->height()==scale,"global UI scaling preserves compact FPGA plot heights");
                require(h1->geometry().left()==h2->geometry().left()&&h1->width()==h2->width()
                    &&h1->geometry().bottom()<h2->geometry().top(),"main window stacks H1 and H2 at full channel width");
            }
            auto *sensorGrid=page->findChild<QGridLayout*>("fpgaSensorGrid");
            require(sensorGrid&&sensorGrid->rowCount()==7&&sensorGrid->columnCount()==4,"sensor card includes independent RD105 and HC2A rows");
            if(tab==3) {
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
        {
            auto *run=window.findChild<QAbstractButton*>("fpgaMotorRun");
            auto *stop=window.findChild<QAbstractButton*>("fpgaMotorStop");
            auto *steps=window.findChild<QDoubleSpinBox*>("fpgaMotorSteps");
            auto *progress=window.findChild<QLabel*>("fpgaMotorProgress");
            require(run&&stop&&steps&&progress,"remote motor controls expose command progress");
            QJsonObject registers;
            for(const auto pair:{qMakePair(0x6700u,0x00470200u),qMakePair(0x6704u,1u),
                    qMakePair(0x6708u,0x63u),qMakePair(0x6714u,100u),qMakePair(0x6728u,0u),
                    qMakePair(0x6734u,0u),qMakePair(0x673cu,0x21u)})
                registers[QString::number(pair.first)]=double(pair.second);
            status["registers"]=registers;
            send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));
            require(waitUntil([&]{return run->isEnabled();}),"fake Sky idle motor allows a move");
            const int beforeMotor=commands;
            steps->setValue(200);run->click();
            require(!run->isEnabled()&&stop->isEnabled()&&progress->text().contains(QStringLiteral("等待指令结果")),
                "remote MOVE locks immediately while STOP remains available");
            require(QMetaObject::invokeMethod(run,"clicked",Qt::DirectConnection,Q_ARG(bool,false)),
                "force duplicate remote click through the page command guard");
            require(waitUntil([&]{return commands==beforeMotor+1;}),"remote MOVE reaches fake Sky exactly once");
            require(FpgaRemote::parseControl(lastRequest.payload,control)&&control.value("op")=="motor"
                &&control.value("action").toInt()==1,"remote motor progress tracks its typed MOVE request");
            ack.command_seq=commandSequence;
            send(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack));processFor(50);
            require(progress->text().contains(QStringLiteral("等待指令结果")),"remote acceptance ACK does not start a movement estimate");
            const auto motorReply=[&] {
                ack.command_seq=commandSequence;
                send(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack));
                response.request_id=lastRequest.request_id;response.payload=FpgaRemote::encodeStatus(status);
                send(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(response));
            };
            registers[QString::number(0x6708)]=0x69;registers[QString::number(0x673c)]=0x23;
            registers[QString::number(0x6734)]=1;registers[QString::number(0x6728)]=200;
            status["registers"]=registers;motorReply();
            require(waitUntil([&]{return progress->text().contains(QStringLiteral("预计进度"))&&acquisition->isEnabled();}),
                "remote final MOVE result starts the estimate without locking other modules");
            require(!run->isEnabled()&&stop->isEnabled(),"remote motion stays locked after command transport becomes idle");
            stop->click();
            require(waitUntil([&]{return commands==beforeMotor+2;}),"remote STOP bypasses the motion lock");
            registers[QString::number(0x6708)]=0x63;registers[QString::number(0x673c)]=0x21;
            status["registers"]=registers;motorReply();
            require(waitUntil([&]{return run->isEnabled()&&progress->text().contains(QStringLiteral("已停止"));}),
                "remote STOP final result ends the estimate and unlocks the next move");
            run->click();
            require(waitUntil([&]{return commands==beforeMotor+3;}),"a new remote MOVE is accepted after confirmed STOP");
            registers[QString::number(0x6708)]=0x69;registers[QString::number(0x673c)]=0x23;
            registers[QString::number(0x6734)]=2;status["registers"]=registers;motorReply();
            require(waitUntil([&]{return progress->text().contains(QStringLiteral("预计进度"));}),"second remote final result starts a fresh estimate");
            registers[QString::number(0x6708)]=0x73;registers[QString::number(0x673c)]=0x25;
            registers[QString::number(0x6728)]=0;status["registers"]=registers;
            send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));
            require(waitUntil([&]{return run->isEnabled()&&progress->text().contains(QStringLiteral("设备已确认运动结束"));}),
                "matching remote motion ID and idle completion unlock the move independently of the local timer");
            steps->setValue(40);
        }
        const int completedCommands=commands;
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
        require(waitUntil([&] { return summary->text().contains(QStringLiteral("无实时波形")) || summary->text().contains("No live waveform"); }, 5500),
            "an unrefreshed source is cleared while live USB status continues");
        require(acquisition->isEnabled(), "fresh USB status does not refresh old waveform arrival time");
        statusOnly.stop();
        require(waitUntil([&] { return !acquisition->isEnabled(); }, 5500), "four-second status expires write controls");
        const int beforeExpired = commands;
        emit page->acquisitionRequested(true); // A queued/forced request must also be gated, beyond disabled buttons.
        processFor(100);
        require(commands == beforeExpired, "expired status refuses writes at dispatch boundary");
        require(summary->text().contains(QStringLiteral("无实时波形")) || summary->text().contains("No live waveform"),
            "expired preview is marked unavailable");
        send(MsgType::FpgaStatus, FpgaRemote::encodeStatus(status));
        require(waitUntil([&] { return acquisition->isEnabled(); }), "fresh status restores controls");
        remote->close();
        require(waitUntil([&] { return !acquisition->isEnabled(); }), "link close disables writes");
        processFor(100);
        require(disconnectCommands == 0 && commands == completedCommands, "link close never sends Sky USB disconnect or retries control");
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
        require(waitUntil([&] { return commands == completedCommands+1; }), "discovery routes to Sky while USB is disconnected");
        require(FpgaRemote::parseControl(lastRequest.payload, control) && control.value("op") == "discover", "typed Sky discovery request");
        status["connected"] = false; status["ready"] = false;
        status["usb_discovery"] = QJsonObject{{"devices",QJsonArray{QJsonObject{{"locator","sky-only-device"},{"label","GP01 Sky"},
            {"driver","WinUSB"},{"backend","winusb"},{"available",true}}}},{"error",""}};
        response.request_id = lastRequest.request_id; response.payload = FpgaRemote::encodeStatus(status);
        send(MsgType::DeviceOperationResponse, TelemetryCodec::serializeDeviceOperationResponse(response));
        require(waitUntil([&] { return devices->currentData().toString() == "sky-only-device"; }), "Sky result populates USB selector");
        page->findChild<QAbstractButton*>("fpgaConnect")->click();
        require(waitUntil([&] { return commands == completedCommands+2; }), "selected Sky device connects through remote request");
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
static void configurationActionReadback()
{
    using namespace VaporView;
    TcpTelemetryLink server;
    require(server.listen("127.0.0.1",0),"configuration readback fake Sky server");
    auto owner=std::make_unique<MainWindow>();
    auto &window=*owner;
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,1));
    auto sky=SkyConfig::defaults();sky.sensor_transport_path=SensorTransportPath::FpgaRelay;
    window.testInjectRemoteSkyConfig(sky.toJson());
    auto *remote=window.testRemoteSkyController();
    auto *page=window.findChild<FpgaControlPage*>("fpgaControlPage");
    TelemetryCodec decoder,encoder;quint16 sequence=1;
    QList<DeviceOperationRequest> requests;
    QList<quint16> commandSequences;
    int replies=0,acknowledgments=0;
    QObject::connect(remote,&Ground::Devices::RemoteSkyController::deviceOperationResponseReceived,
        &window,[&](const DeviceOperationResponse&){++replies;});
    QObject::connect(remote,&Ground::Devices::RemoteSkyController::commandAckReceived,
        &window,[&](const CommandAck&){++acknowledgments;});
    const auto send=[&](MsgType type,const QByteArray &payload) {
        const auto bytes=encoder.encodeFrame(type,payload,sequence++,1);
        require(server.writeBytes(bytes)==bytes.size(),"configuration fixture sends a Sky frame");
    };
    QObject::connect(&server,&TelemetryLink::bytesReceived,&window,[&](const QByteArray &bytes) {
        for(const auto &frame:decoder.feedBytes(bytes)) {
            CommandMessage command;
            if(frame.type!=MsgType::Command||!TelemetryCodec::parseCommand(frame.payload,command))continue;
            if(command.command_id==CommandId::GetSkyConfig) {
                CommandAck ack;ack.command_id=command.command_id;ack.command_seq=command.command_seq;
                send(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack));
                send(MsgType::SkyConfig,QJsonDocument(sky.toJson()).toJson(QJsonDocument::Compact));
            } else if(command.command_id==CommandId::DeviceOperation) {
                DeviceOperationRequest request;
                require(TelemetryCodec::parseDeviceOperationRequest(command.payload,request)
                    &&request.device_id==SkyDeviceId::Fpga&&request.operation==DeviceOperation::FpgaControl,
                    "configuration action uses the existing typed FPGA control path");
                requests.append(request);commandSequences.append(command.command_seq);
            }
        }
    });
    require(remote->openTcp("127.0.0.1",server.localPort())&&waitUntil([&]{return remote->isOpen();}),
        "configuration fixture opens the real asynchronous telemetry controller");
    window.testInjectRemoteSkyConfig(sky.toJson());
    auto *stack=window.findChild<QStackedWidget*>("mainPageStack");
    QWidget *configurationPage=window.findChild<QCheckBox*>("deviceRemotePressureEnabledCheck");
    while(configurationPage&&stack->indexOf(configurationPage)<0)configurationPage=configurationPage->parentWidget();
    require(configurationPage,"configuration action test finds the actual device configuration page");
    stack->setCurrentWidget(configurationPage);window.resize(1200,800);window.show();
    const auto action=[&](SkyDeviceId id) {
        for(auto *button:window.findChildren<QToolButton*>("homeDeviceActionButton"))
            if(button->property("deviceConfigAction").toBool()
                &&button->property("deviceConfigRemoteDevice").toInt()==int(id))return button;
        return static_cast<QToolButton*>(nullptr);
    };
    QJsonObject registers;
    for(quint32 address:{0x6004u,0x6104u,0x6204u,0x6304u,0x6404u,0x6504u,0x6604u,0x6a04u,
                        0x4004u,0x4104u,0x5004u,0x5104u})registers[QString::number(address)]=0;
    registers[QString::number(0x6010)]=4800;
    registers[QString::number(0x6800)]=0x00480100;
    registers[QString::number(0x6900)]=0x00490100;
    QJsonObject status{{"connected",true},{"ready",true},{"busy",false},{"detail","ready"},
        {"extensions",double(FpgaExtensions::Rd105)},{"configuration",FpgaControlConfig{}.toJson()},{"archived_records","0"}};
    int marker=0;
    const auto confirmed=[&] { return MainWindowRtcmTestAccess::state(window).fpga_session_.remoteStatus.value("registers").toObject(); };
    const auto publish=[&] {
        registers["256"]=++marker;status["registers"]=registers;
        QJsonObject parsed;
        require(FpgaRemote::parseStatus(FpgaRemote::encodeStatus(status),parsed),"configuration fixture supplies a valid status payload");
        send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));
        require(waitUntil([&]{return page->uiTestHardwareValues().value(0x100)==quint32(marker)
            &&confirmed().value("256").toInt()==marker;}),
            "fresh register state reaches the authoritative FPGA page model");
    };
    publish();
    const QList<SkyDeviceId> devices{SkyDeviceId::Epsilon,SkyDeviceId::Ptb,SkyDeviceId::Hc2a,SkyDeviceId::Lidar,
        SkyDeviceId::TemperatureController,SkyDeviceId::Ai8TemperatureController,SkyDeviceId::WaveTcp,
        SkyDeviceId::Bmp390,SkyDeviceId::Sht45};
    // No language switch, checkbox click or page re-open may repair stale buttons.
    for(bool enabled:{true,false}) {
        for(auto it=registers.begin();it!=registers.end();++it)
            if(it.key()!="256"&&it.key()!=QString::number(0x6010)
                &&it.key()!=QString::number(0x6800)&&it.key()!=QString::number(0x6900))it.value()=enabled?1:0;
        status["tec103_connected"]=enabled;
        publish();
        for(auto id:devices) {
            auto *button=action(id);
            if(!button||button->property("connected").toBool()!=enabled)
                std::cerr<<"configuration readback mismatch device="<<int(id)<<" expected="<<enabled<<'\n';
            require(button&&button->property("connected").toBool()==enabled,
                "configuration buttons immediately match external register readback");
            require(button->isEnabled()&&button->toolTip().contains(enabled?QStringLiteral("断开"):QStringLiteral("连接")),
                "configuration action and accessible text reflect actual running state");
        }
    }
    auto *button=action(SkyDeviceId::Ptb);
    QComboBox *baud=nullptr;
    for(auto *editor:window.findChildren<QComboBox*>())
        if(editor->objectName().endsWith("FpgaEditor")&&editor->property("source").toUInt()==0x40
            &&editor->property("parameter")=="baud")baud=editor;
    require(baud&&baud->isEnabled(),"disconnected PTB staged editor exists");
    const auto click=[&](bool enabled) {
        const int before=requests.size();button->click();
        require(waitUntil([&]{return requests.size()==before+1;}),"configuration button sends one asynchronous request");
        QJsonObject control;
        require(FpgaRemote::parseControl(requests.last().payload,control)&&control.value("op")=="sensor"
            &&control.value("source").toInt()==0x40&&control.value("enabled").toBool()==enabled,
            "configuration button sends the correct sensor intent");
        require(!button->isEnabled()&&button->property("state")=="connecting","pending configuration is visibly busy");
        return control;
    };
    const auto complete=[&](CommandErrorCode error=CommandErrorCode::Ok) {
        status["registers"]=registers;
        DeviceOperationResponse response;response.request_id=requests.last().request_id;
        response.device_id=SkyDeviceId::Fpga;response.operation=DeviceOperation::FpgaControl;
        response.error_code=error;response.error_message=error==CommandErrorCode::Ok?QString():QStringLiteral("fixture configuration rejected");
        response.payload=FpgaRemote::encodeStatus(status);
        send(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(response));
        return response;
    };
    baud->setCurrentText("9600");
    require(click(true).value("baud").toInt()==9600,"connection carries the staged configuration");
    const auto firstId=requests.last().request_id;
    const int beforeAck=acknowledgments;
    CommandAck ack;ack.command_id=CommandId::DeviceOperation;ack.command_seq=commandSequences.last();
    send(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack));
    require(waitUntil([&]{return acknowledgments>beforeAck;}),"acceptance ACK is delivered");
    require(page->uiTestHardwareValues().value(0x6004)==0&&!button->property("connected").toBool()
        &&button->property("state")=="connecting","ACK alone neither writes confirmed state nor announces success");
    button->click();button->click();
    registers[QString::number(0x6004)]=1;registers[QString::number(0x6010)]=9600;
    auto firstResponse=complete();
    require(waitUntil([&]{return button->isEnabled()&&button->property("connected").toBool()&&!baud->isEnabled();}),
        "successful result automatically refreshes the configuration page");
    require(page->uiTestHardwareValues().value(0x6010)==9600&&confirmed().value(QString::number(0x6010)).toInt()==9600
        &&requests.size()==1,"confirmed readback matches UI without duplicate writes");
    click(false);complete(CommandErrorCode::ConfigApplyFailed);
    require(waitUntil([&]{return button->isEnabled()&&button->property("connected").toBool();})
        &&page->uiTestHardwareValues().value(0x6004)==1&&confirmed().value(QString::number(0x6004)).toInt()==1,
        "failure preserves the last valid running state");
    require(page->findChild<QPlainTextEdit*>("fpgaDiagnostics")->toPlainText().contains("fixture configuration rejected"),
        "failed configuration is explicitly reported");
    click(false);registers[QString::number(0x6004)]=0;complete();
    require(waitUntil([&]{return button->isEnabled()&&baud->isEnabled()&&!button->property("connected").toBool();}),
        "successful stop restores editing from actual readback");
    baud->setCurrentText("19200");
    require(click(true).value("baud").toInt()==19200&&requests.last().request_id!=firstId,
        "repeated configuration has a fresh request identity and new staged values");
    const int beforeLate=replies;
    send(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(firstResponse));publish();
    require(replies==beforeLate&&button->property("state")=="connecting"&&page->uiTestHardwareValues().value(0x6004)==0,
        "late old result cannot complete or overwrite a newer in-flight configuration");
    registers[QString::number(0x6004)]=1;registers[QString::number(0x6010)]=19200;complete();
    require(waitUntil([&]{return button->isEnabled()&&button->property("connected").toBool()&&baud->currentText()=="19200";}),
        "newer configuration is confirmed by its own returned registers");
    const int completedReplies=replies;
    send(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(firstResponse));publish();
    require(replies==completedReplies&&page->uiTestHardwareValues().value(0x6010)==19200&&baud->currentText()=="19200"
        &&confirmed().value(QString::number(0x6010)).toInt()==19200,
        "late duplicate cannot roll back confirmed UI or register state");
    stack->setCurrentWidget(page);
    const int beforeRead=requests.size();page->findChild<QAbstractButton*>("fpgaRefresh")->click();
    require(waitUntil([&]{return requests.size()==beforeRead+1;}),"explicit configuration readback sends a request");
    QJsonObject read;require(FpgaRemote::parseControl(requests.last().payload,read)&&read.value("op")=="refresh","readback uses existing refresh operation");
    const int beforeReadReply=replies;complete();
    require(waitUntil([&]{return replies==beforeReadReply+1;})&&page->uiTestHardwareValues().value(0x6010)==19200
        &&button->property("connected").toBool(),"reread and UI agree with the last confirmed configuration");
    stack->setCurrentWidget(configurationPage);
    registers[QString::number(0x6100)]=0x00410106;
    registers[QString::number(0x614c)]=0x48434432;
    registers[QString::number(0x6104)]=0;
    registers[QString::number(0x6110)]=38400;
    registers[QString::number(0x6118)]=0;
    publish();
    auto *hc2aBaud=window.findChild<QComboBox*>("deviceHumidityBaudComboFpgaEditor");
    auto *hc2aAction=action(SkyDeviceId::Hc2a);
    require(hc2aBaud&&hc2aAction&&waitUntil([&]{return hc2aBaud->isEnabled()&&hc2aBaud->currentText()=="38400";}),
        "HC2A inline baud displays the actual 0x6110 readback instead of a fixed default");
    const auto *hc2aValidator=qobject_cast<const QIntValidator*>(hc2aBaud->validator());
    require(hc2aValidator&&hc2aValidator->bottom()==1200&&hc2aValidator->top()==115200,
        "HC2A FPGA baud editing uses its 1200..115200 interface range");
    for(const auto &entry:{qMakePair("1200",true),qMakePair("115200",true),
            qMakePair("1199",false),qMakePair("115201",false)}) {
        QString text=QString::fromLatin1(entry.first);int position=text.size();
        require((hc2aValidator->validate(text,position)==QValidator::Acceptable)==entry.second,
            "HC2A baud editor accepts its range boundaries and rejects outside values");
    }
    const int beforeHc2a=requests.size();
    hc2aBaud->setCurrentText("57600");publish();
    require(requests.size()==beforeHc2a&&hc2aBaud->currentText()=="57600",
        "HC2A baud edit is staged and survives periodic actual-value readback");
    hc2aAction->click();
    require(waitUntil([&]{return requests.size()==beforeHc2a+1;}),"HC2A connection submits one staged request");
    QJsonObject hc2aRequest;
    require(FpgaRemote::parseControl(requests.last().payload,hc2aRequest)&&hc2aRequest.value("op")=="sensor"
        &&hc2aRequest.value("source").toInt()==0x41&&hc2aRequest.value("enabled").toBool()
        &&hc2aRequest.value("baud").toInt()==57600,
        "HC2A connection carries the legal baud through the normal FPGA transaction");
    registers[QString::number(0x6110)]=57600;registers[QString::number(0x6104)]=1;complete();
    require(waitUntil([&]{return hc2aAction->isEnabled()&&hc2aAction->property("connected").toBool()
        &&!hc2aBaud->isEnabled()&&hc2aBaud->currentText()=="57600";})
        &&page->uiTestHardwareValues().value(0x6118)==0,
        "confirmed HC2A baud locks editing and preserves its 8N1 format");
    click(false);
    auto closingResponse=firstResponse;closingResponse.request_id=requests.last().request_id;
    auto closingStatus=status;auto closingRegisters=registers;
    closingRegisters[QString::number(0x6004)]=0;closingStatus["registers"]=closingRegisters;
    closingResponse.payload=FpgaRemote::encodeStatus(closingStatus);
    send(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(closingResponse));
    QPointer<MainWindow> weak=owner.get();QPointer<QThread> worker=window.testFpgaController()->thread();
    QElapsedTimer closing;closing.start();
    owner.reset();
    require(weak.isNull()&&worker.isNull()&&closing.elapsed()<5000,"closing during configuration destroys UI and joins its worker");
    bool lateDelivered=false;
    QTimer::singleShot(0,&server,[&]{server.writeBytes(encoder.encodeFrame(MsgType::DeviceOperationResponse,
        TelemetryCodec::serializeDeviceOperationResponse(closingResponse),sequence++,1));lateDelivered=true;});
    require(waitUntil([&]{return lateDelivered;}),"late transport activity after UI destruction is safe");
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
    require(waitUntil([&]{return epsilon->isEnabled()&&epsilon->property("state")=="disconnected";}),
        "an enabled FPGA channel without an online sensor offers Connect");
    const int beforeRetry = requests.size();
    epsilon->click();
    require(waitUntil([&]{return requests.size() == beforeRetry + 1;}) &&
        FpgaRemote::parseControl(requests.last().payload,enableControl) && enableControl.value("enabled").toBool(),
        "clicking an offline sensor with its run bit set requests enable");
    connectAck.command_seq=commandSequence;send(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(connectAck));
    connected.request_id=requests.last().request_id;
    send(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(connected));
    require(!homeAction(window,QStringLiteral("原位 TEC103"))->isEnabled(),"unsupported RD105 remains unavailable");
    auto *pressureRate=window.findChild<PtbPanel*>()->findChild<QLabel*>("rateLabel");
    auto *humidityRate=window.findChild<Hc2aPanel*>()->findChild<QLabel*>("rateLabel");
    auto *distanceRate=window.findChild<LidarPanel*>()->findChild<QLabel*>("rateLabel");
    auto *epsilonRate=window.findChild<QWidget*>("epsilonPanel")->parentWidget()->findChild<QLabel*>("rateLabel");
    require(pressureRate&&humidityRate&&distanceRate&&epsilonRate,"home sensor frequency labels exist");
    const bool ratesShown=waitUntil([&]{QMetaObject::invokeMethod(&window,"onRefreshTimer");
        return pressureRate->text().contains("1.0")&&humidityRate->text().contains("--")
            &&distanceRate->text().contains("1000.0")&&epsilonRate->text().contains("750.0");});
    if(!ratesShown)std::cerr<<"FPGA rates: "<<qPrintable(pressureRate->text())<<", "<<qPrintable(humidityRate->text())
        <<", "<<qPrintable(distanceRate->text())<<", "<<qPrintable(epsilonRate->text())<<'\n';
    require(ratesShown,
        "remote FPGA rates come from Sky raw sensors and fixed PTB, with HC2A unavailable until HC2A data arrives");
    {
        const auto summary = MainWindowRtcmTestAccess::summary(window);
        const QStringList expected{QStringLiteral("基础遥测"), QStringLiteral("遥测状态"),
            QStringLiteral("原始波形预览（H1/H2）"), QStringLiteral("归一化二次谐波预览"),
            QStringLiteral("波形特征值"), QStringLiteral("采集周期频率")};
        require(summary.rateItems.size() == 6, "FPGA summary has exactly six rates");
        MainWindowRtcmTestAccess::refreshSummary(window);
        for (int i = 0; i < expected.size(); ++i) {
            require(summary.rateItems[i].label == expected[i] && !summary.rateItems[i].tooltip.isEmpty(),
                    "FPGA labels, order and explanations match specification");
            bool found = false;
            for (auto *label : window.findChildren<QLabel*>()) {
                if (label->text() != expected[i] || label->toolTip().isEmpty()) continue;
                found = true;
                bool matchingValue = false;
                for (auto *sibling : label->parentWidget()->findChildren<QLabel*>())
                    if (sibling != label && sibling->toolTip() == label->toolTip()) matchingValue = true;
                require(matchingValue, "rate name and value share the tooltip");
            }
            require(found, "actual summary labels are not overwritten by legacy titles");
        }
        require(summary.rateItems[5].value == QStringLiteral("0.0 Hz"),
                "acquisition rate does not substitute received feature rate");
        const QSize original = window.size();
        const bool wasVisible = window.isVisible();
        window.show();
        auto& state = MainWindowRtcmTestAccess::state(window);
        auto *columnLayout = qobject_cast<QBoxLayout*>(state.home_sensor_column_->layout());
        require(columnLayout, "navigation and motion share a responsive layout");
        for (const QSize size : {QSize(2500, 1200), QSize(1100, 850), QSize(2500, 1200)}) {
            window.resize(size); processFor(150);
            const bool environmentOnTop = state.home_overview_splitter_->indexOf(state.env_group_) >= 0;
            if (size.width() == 2500) {
                require(environmentOnTop && columnLayout->direction() == QBoxLayout::LeftToRight,
                        "wide home places navigation and motion on the same row");
                require(state.epsilon_group_->geometry().right() < state.home_motion_card_->geometry().left(),
                        "navigation and motion do not overlap");
            } else {
                require(columnLayout->direction() == QBoxLayout::TopToBottom,
                        "narrow home restores navigation and motion stacking");
            }
        }
        window.resize(original);
        if (!wasVisible) window.hide();
        processFor(100);
    }
    auto *internal=window.findChild<QWidget*>("homeInternalEnvironment");
    require(internal,"home internal metrics exist");
    FpgaSensor::Reading bmp;bmp.source=0x43;bmp.kind=FpgaSensor::SensorKind::Bmp390;
    bmp.validity={true,true,true,true};bmp.pressurePa=100000;
    FpgaSensor::Reading sht;sht.source=0x44;sht.kind=FpgaSensor::SensorKind::Sht45;
    sht.validity={true,true,true,true};sht.temperatureC=22;sht.humidityPct=33;
    send(MsgType::FpgaSensor,FpgaRemote::encodeSensor(bmp,1));
    send(MsgType::FpgaSensor,FpgaRemote::encodeSensor(sht,2));
    require(waitUntil([&]{return internalEnvironmentText(internal).contains("1000.00") && internalEnvironmentText(internal).contains("22.00") && internalEnvironmentText(internal).contains("33.00");}),
        "remote internal metrics use typed FPGA sensors independently of basic environmental telemetry");
    sht.validity.measurement=false;send(MsgType::FpgaSensor,FpgaRemote::encodeSensor(sht,2));
    require(waitUntil([&]{return internalEnvironmentText(internal).count(QStringLiteral("—"))==2 && internalEnvironmentText(internal).contains("1000.00");}),
        "invalid SHT clears temperature and humidity without clearing independent BMP pressure");
    registers[QString::number(0x6304)]=0;status["registers"]=registers;send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));
    require(waitUntil([&]{return internalEnvironmentText(internal).count(QStringLiteral("—"))==3;}),"disabled BMP cannot retain a live internal value");
    registers[QString::number(0x6304)]=1;status["registers"]=registers;send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));
    const QStringList names{"EPSILON","PTB210","SDFM300","AI-8288",QStringLiteral("波形源")};
    const QList<quint16> sources{0x42,0x40,0x45,0x46,0};
    auto *waveEnabled=window.findChild<QCheckBox*>("deviceFpgaAcquisitionEnabledCheck");
    require(waitUntil([&]{return window.findChild<QCheckBox*>("deviceInternalEnabledBMP390")->isChecked();}),
        "remote BMP enable readback arrives before indicator verification");
    require(waveEnabled&&waveEnabled->isEnabled()&&waveEnabled->isChecked(),"remote acquisition selection is editable independently of registers");
    verifyRelayIndicators(window,{"Epsilon","Pressure","Lidar","Ai8","Wave","Bmp390","Sht45"});
    auto *epsilonSelection=window.findChild<QCheckBox*>("deviceRemoteEpsilonEnabledCheck");
    const int beforeSelection=requests.size();
    epsilonSelection->click();processFor(50);
    require(!epsilonSelection->isChecked()&&requests.size()==beforeSelection
        &&!homeAction(window,"EPSILON")->isEnabled(),
        "remote selection changes persist without sending hardware commands or stopping the channel");
    send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));processFor(50);
    require(!epsilonSelection->isChecked(),"remote hardware readback never overwrites a cleared selection");
    epsilonSelection->click();
    require(deviceWaveRate(window)&&deviceWaveRate(window)->text().contains("250000")
        &&deviceWaveRate(window)->toolTip().contains("2000"),"remote ADC and DLIA rates use actual register readback");
    for (int index=0;index<names.size();++index) {
        auto *button=homeAction(window,names[index]);
        if (sources[index]) {
            FpgaSensor::Reading reading;reading.source=sources[index];reading.validity={true,true,true,true};
            send(MsgType::FpgaSensor,FpgaRemote::encodeSensor(reading,1));
            require(waitUntil([&]{return button->property("connected").toBool();}),"online sensor offers Disconnect");
        }
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
            if(!sources[index])require(waveEnabled->isEnabled()&&waveEnabled->isChecked(),
                "remote acquisition selection does not change on ACK");
            if(sources[index]!=0x42) {
                status["busy"]=true;send(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));processFor(20);
                navigation->refreshStatus();
                require(navigationPanel->snapshot().navigationDataAvailable&&navigationPanel->snapshot().gnssFixCode==6,
                    "a pending remote sensor command preserves EPSILON navigation");
                require(epsilon->isEnabled(),"unrelated EPSILON action stays available during the remote transaction");
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
            if(!sources[index])require(waveEnabled->isEnabled()&&waveEnabled->isChecked(),
                "remote acquisition readback does not overwrite enable selection");
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
    for (bool enabled : {false, true}) {
        const int count = requests.size();
        auto *pressure = homeAction(window,"PTB210");
        auto *distance = homeAction(window,"SDFM300");
        pressure->click();
        require(waitUntil([&]{return requests.size()==count+1;}),"first queued remote operation starts immediately");
        require(epsilon->isEnabled()&&distance->isEnabled(),"busy FPGA leaves other device buttons available");
        epsilon->click();distance->click();
        require(!epsilon->isEnabled()&&!distance->isEnabled(),"only selected queued devices block duplicate clicks");
        epsilon->click();distance->click();processFor(60);
        require(requests.size()==count+1,"queued actions cannot reach the transport before final readback");
        for (int source : {0x40,0x42,0x45}) {
            QJsonObject control;
            require(FpgaRemote::parseControl(requests.last().payload,control)&&control.value("source").toInt()==source
                &&control.value("enabled").toBool()==enabled,"remote actions preserve click order and intent");
            registers[QString::number(0x6004+(source-0x40)*0x100)]=enabled?1:0;
            const int before=requests.size();complete();
            if(source!=0x45)require(waitUntil([&]{return requests.size()==before+1;}),"final readback dispatches the next queued device");
            else require(waitUntil([&]{return distance->isEnabled();}),"last queued device unlocks after confirmation");
        }
        require(waitUntil([&]{return epsilon->isEnabled()&&pressure->isEnabled();}),"completed queue releases each selected control");
    }
    const int beforeFailedQueue=requests.size();
    homeAction(window,"PTB210")->click();
    require(waitUntil([&]{return requests.size()==beforeFailedQueue+1;}),"queue failure fixture starts one transaction");
    epsilon->click();
    require(!epsilon->isEnabled(),"next device is queued before failure");
    complete(CommandErrorCode::ConfigApplyFailed);
    require(waitUntil([&]{return epsilon->isEnabled()&&homeAction(window,"PTB210")->isEnabled();}),"failed operation cancels queued actions and releases controls");
    processFor(60);require(requests.size()==beforeFailedQueue+1,"failure never sends or retries the queued write");
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
    require(!baud->isEnabled()&&epsilon->property("state")=="connecting",
        "submitted remote settings lock immediately while final readback is pending");
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
static void simulatorRefreshBudget()
{
    for (bool uiTest : {false, true}) {
        MainWindow window;
        window.resize(1600, 1000);
        QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 0));
        auto *path = window.findChild<QComboBox *>("deviceSensorTransportPathCombo");
        path->setCurrentIndex(path->findData("fpga_relay"));
        auto *page = window.findChild<FpgaControlPage *>("fpgaControlPage");
        auto *mode = window.findChild<QAction *>("uiTestModeAction");
        if (uiTest) mode->setChecked(true);
        else {
            auto *backend = page->findChild<QComboBox *>("fpgaBackend");
            backend->setCurrentIndex(backend->findData("simulator"));
        }
        window.show();
        require(QMetaObject::invokeMethod(&window, "onConnectClicked", Qt::DirectConnection),
                "refresh budget connects all simulated devices");
        auto *grid = window.findChild<QWidget *>("homeOverviewDeviceGrid");
        auto actions = grid->findChildren<QToolButton *>("homeDeviceActionButton");
        actions.removeAll(MainWindowRtcmTestAccess::state(window).home_motion_action_btn_);
        require(actions.size() == 10, "refresh budget includes both TEC103 devices");
        const bool connected=waitUntil([&] {
            return std::all_of(actions.cbegin(), actions.cend(), [](auto *button) {
                return button->property("state") == "connected";
            });
        }, 20000);
        if(!connected) {
            const auto& current=MainWindowRtcmTestAccess::state(window);
            std::cerr<<"Refresh startup "<<(uiTest?"UI simulator":"Local simulator")
                <<": queued="<<current.fpga_action_queue_.size()<<" busy="<<(uiTest?current.fpga_sim_busy_:current.fpga_busy_)
                <<" activeDevice="<<int(current.fpga_home_action_device_)<<'\n';
            for(auto* button:actions)std::cerr<<"  "<<button->toolTip().toStdString()
                <<" state="<<button->property("state").toString().toStdString()
                <<" enabled="<<button->isEnabled()<<'\n';
        }
        require(connected,"refresh budget waits for every supported current-firmware device");
        const auto& state = MainWindowRtcmTestAccess::state(window);
        const QPoint inSitu = state.home_temperature_status_lbl_->mapTo(grid, QPoint());
        const QPoint remote = state.home_tec_remote_lbl_->mapTo(grid, QPoint());
        require(inSitu.y() == remote.y() && inSitu.x() < remote.x(),
                "in-situ and remote TEC103 capsules appear side by side in that order");
        processFor(500);

        int measurements = 0, viewUpdates = 0;
        auto *ai8 = window.findChild<VaporView::Ground::Widgets::Ai8TemperatureControllerPanel *>();
        require(ai8, "refresh budget observes the AI8 view");
        const auto viewConnection = QObject::connect(ai8,
            &VaporView::Ground::Widgets::Ai8TemperatureControllerPanel::outputStatusChanged,
            &window, [&] { ++viewUpdates; });
        auto *controller = uiTest ? page->uiTestController() : window.testFpgaController();
        const auto measurementConnection = QObject::connect(controller,
            &VaporView::Ground::Devices::FpgaDeviceController::measurementUpdated,
            &window, [&](const auto &) { ++measurements; });
        QList<QPointer<QFrame>> summaryPills;
        for (auto *pill : window.findChildren<QFrame *>("homeTelemetrySummaryPill"))
            summaryPills.append(pill);
        require(!summaryPills.isEmpty(), "refresh budget observes visible and hidden summary fields");
        QVector<qint64> delays;
        QElapsedTimer clock;
        clock.start();
        qint64 previous = 0;
        QTimer heartbeat;
        heartbeat.setTimerType(Qt::PreciseTimer);
        QObject::connect(&heartbeat, &QTimer::timeout, &window, [&] {
            const qint64 now = clock.elapsed();
            delays.append(now - previous);
            previous = now;
        });
        heartbeat.start(10);
        processFor(2500);
        heartbeat.stop();
        std::sort(delays.begin(), delays.end());
        require(!delays.isEmpty(), "GUI heartbeat produced samples");
        const qint64 p95 = delays.at((delays.size() - 1) * 95 / 100);
        std::cout << (uiTest ? "UI simulator" : "Local simulator")
                  << ": measurements=" << measurements << " viewUpdates=" << viewUpdates
                  << " heartbeatSamples=" << delays.size() << " p95Ms=" << p95
                  << " maxMs=" << delays.last() << std::endl;
        require(measurements > 20,
                "responsiveness measurement runs under sustained FPGA data");

        VaporView::FpgaSensor::AdaptedMeasurements batch;
        batch.reading.source = 0x46;
        batch.reading.kind = VaporView::FpgaSensor::SensorKind::Ai8;
        batch.reading.validity = {true, true, true, true};
        batch.ai8 = VaporView::FpgaSensor::Ai8Sample{};
        batch.ai8Live = VaporView::Ai8TemperatureControllerProtocol::LiveData{};
        batch.ai8Live->valid = true;
        batch.ai8Live->setpointsValid = true;
        const int before = viewUpdates;
        for (int channel = 1; channel <= 8; ++channel) {
            batch.ai8->channel = channel;
            batch.ai8Live->measuredC[channel - 1] = -20.0 + channel;
            batch.ai8Live->setpointsC[channel - 1] = 20.0 + channel;
            page->simulationMeasurementsUpdated(batch);
        }
        require(viewUpdates == before,
                "a burst of eight channel packets must not synchronously rebuild the AI8 view");
        require(QMetaObject::invokeMethod(&window, "onRefreshTimer", Qt::DirectConnection),
                "display tick presents the accumulated AI8 channels");
        require(viewUpdates == before + 1, "eight channel packets produce one AI8 view update");
        const auto measured = window.findChildren<QLabel *>("ai8TemperatureOverviewValueLabel");
        const auto targets = window.findChildren<QLabel *>("ai8TemperatureOverviewTargetLabel");
        require(measured.size() == 8 && targets.size() == 8, "all eight channels remain visible");
        for (auto *label : measured)
            require(label->text().contains(QString::number(-19.0 + label->property("channelIndex").toInt(), 'f', 1)),
                    "coalescing preserves every channel's latest measured temperature");
        for (auto *label : targets)
            require(label->text().contains(QString::number(21.0 + label->property("channelIndex").toInt(), 'f', 1)),
                    "coalescing preserves every channel's latest target temperature");
        for (const auto &pill : summaryPills)
            require(!pill.isNull(), "unchanged summaries on hidden pages must not be rebuilt per display tick");
        QObject::disconnect(viewConnection);
        QObject::disconnect(measurementConnection);
        require(p95 < 100 && delays.last() < 250,
                "connected simulator keeps the GUI event loop responsive");
        QMetaObject::invokeMethod(&window, "onDisconnectClicked", Qt::DirectConnection);
        if (uiTest) mode->setChecked(false);
        window.close();
    }
}

static QDialog *recordingCalibrationDialog(MainWindow& window)
{
    for (auto *dialog : window.findChildren<QDialog*>(QStringLiteral("recordingCalibrationDialog")))
        if (dialog->isVisible()) return dialog;
    return nullptr;
}

static void confirmRecordingCalibration(MainWindow& window)
{
    auto *dialog = recordingCalibrationDialog(window);
    require(dialog, "formal recording presents the calibration confirmation");
    auto *confirm = dialog->findChild<QAbstractButton*>(QStringLiteral("recordingCalibrationConfirm"));
    require(confirm && confirm->isEnabled(), "valid current calibration can be confirmed");
    confirm->click();
}

static void localSimulatorRouting()
{
    const auto check=[](bool ok,const char *message){if(!ok)throw std::runtime_error(message);};
    QTemporaryDir output;check(output.isValid(),"isolated local recording directory");
    VaporView::LogService logService("FpgaLocalHomeActionsTest",nullptr,output.filePath("logs"),output.filePath("logs"));
    auto settings=VaporView::applicationConfigSettings();
    QSettings native("VaporView","MainWindow");
    native.setValue("recording_directory",output.path());
    FpgaControlConfig oldConfiguration;oldConfiguration.ai8.scan=false;
    settings.setValue("Fpga/desiredConfiguration",QJsonDocument(oldConfiguration.toJson()).toJson(QJsonDocument::Compact));
    settings.setValue("Fpga/remoteDesiredConfiguration",QJsonDocument(oldConfiguration.toJson()).toJson(QJsonDocument::Compact));
    settings.remove("Fpga/ai8EightChannelDefaultApplied");
    QMap<quint32,quint32> lastRegisters;
    IndependentCalibrationTest::Rig calibration;const auto calibrationFile=calibration.complete();
    calibration.service->selectFile(calibrationFile.filename);calibration.service->synchronize();
    MainWindow window(nullptr,calibration.service);
    QObject::connect(window.testFpgaController(), &VaporView::Ground::Devices::FpgaDeviceController::hardwareValuesChanged,
        &window, [&](const QMap<quint32,quint32>& values) { lastRegisters = values; });
#ifdef Q_OS_WIN
    auto *focusStyle=qobject_cast<QProxyStyle*>(qApp->style());
    check(focusStyle&&focusStyle->baseStyle()->objectName()=="fusion",
          "simulator integration retains Fusion after installing the application focus style");
#endif
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
    auto *applyWms = page->findChild<QAbstractButton*>("fpgaApplyWms");
    check(applyWms && applyWms->isEnabled(), "WMS parameters can be submitted");
    const auto generation = MainWindowRtcmTestAccess::state(window).fpga_local_dispatch_generation_;
    const auto beforePollError = generation->load();
    VaporView::LogRecord pollError;
    pollError.level = VaporView::LogLevel::Error;
    pollError.message = QStringLiteral("UART response deadline expired; the command was not retried");
    pollError.fields.insert("ready", true);
    emit window.testFpgaController()->logRecordGenerated(pollError);
    applyWms->click();
    check(!applyWms->isEnabled(), "pending WMS submission prevents double clicks");
    check(waitUntil([&]{ return applyWms->isEnabled() && disconnect->isEnabled(); }, 12000),
          "WMS and connection controls recover after a concurrent polling diagnostic");
    check(generation->load() == beforePollError, "polling diagnostic preserves queued WMS submission");
    check(waitUntil([&]{return connectionSuccesses.contains(0x40);})&&connectionSuccesses.size()==1,
        "only the requested simulated sensor reports a connection result");
    {
        auto *run=window.findChild<QAbstractButton*>("fpgaMotorRun");
        auto *stop=window.findChild<QAbstractButton*>("fpgaMotorStop");
        auto *enable=window.findChild<QAbstractButton*>("fpgaMotorEnable");
        auto *disable=window.findChild<QAbstractButton*>("fpgaMotorDisable");
        auto *steps=window.findChild<QDoubleSpinBox*>("fpgaMotorSteps");
        auto *progress=window.findChild<QLabel*>("fpgaMotorProgress");
        check(run&&stop&&enable&&disable&&steps&&progress,"local simulator exposes the motor command controls");
        check(page->configuration().backend=="simulator"&&devices->currentData().toString()=="simulation://fpga",
            "motor integration uses only the selected protocol simulator, never hardware USB");
        const auto moves=std::make_shared<int>(0);
        const auto observer=QObject::connect(window.testFpgaController(),&VaporView::Ground::Devices::FpgaDeviceController::rawCommand,
            &window,[moves](quint64,const QByteArray& bytes) {
                VaporView::FpgaVlp1::StreamParser parser;
                for(const auto& frame:parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),bytes.size()))
                    if(frame.header.message==12&&frame.header.source==0x47&&frame.payload.size()==8
                        &&qFromLittleEndian<quint32>(frame.payload.data())==1)++*moves;
            },Qt::QueuedConnection);
        const auto cleanup=qScopeGuard([observer]{QObject::disconnect(observer);});
        enable->click();
        check(waitUntil([&]{return run->isEnabled()&&lastRegisters.value(0x6704)==1;}),"simulated motor enable is confirmed by the production controller");
        steps->setValue(200);run->click();
        check(!run->isEnabled()&&stop->isEnabled()&&progress->text().contains(QStringLiteral("等待指令结果")),
            "local MOVE locks immediately before the worker acknowledges it");
        check(QMetaObject::invokeMethod(run,"clicked",Qt::DirectConnection,Q_ARG(bool,false)),
            "force duplicate local click through the page command guard");
        check(waitUntil([&]{return *moves==1&&progress->text().contains(QStringLiteral("预计进度"));}),
            "one local MOVE reaches VLP and its own final result starts the estimate");
        check(!run->isEnabled()&&stop->isEnabled(),"local command completion does not unlock an active movement");
        stop->click();stop->click();
        check(waitUntil([&]{return run->isEnabled()&&progress->text().contains(QStringLiteral("已停止"));}),
            "replacement STOP ignores the old cancellation and unlocks only from its own result");
        check(*moves==1,"STOP replacement does not retry or re-enable a move");
        steps->setValue(100);run->click();
        check(waitUntil([&]{return *moves==2&&progress->text().contains(QStringLiteral("预计进度"));}),
            "a second local MOVE starts only after confirmed STOP");
        check(waitUntil([&]{return run->isEnabled()&&progress->text().contains(QStringLiteral("设备已确认运动结束"));},8000),
            "natural simulated completion and matching motion ID unlock the movement");
        disable->click();
        check(waitUntil([&]{return run->isEnabled()&&lastRegisters.value(0x6704)==0;}),"motor test restores disabled drive state");
        steps->setValue(40);
    }
    homeAction(window,"HC2A")->click();
    check(waitUntil([&]{return homeAction(window,"HC2A")->isEnabled()&&homeAction(window,"HC2A")->property("state")=="connected";})
        &&pressureAction->property("state")=="connected","connecting a second home device preserves the first");
    check(waitUntil([&]{return connectionSuccesses.contains(0x41);}),"individual HC2A connection confirms valid data before reconnect");
    homeAction(window,"HC2A")->click();
    check(waitUntil([&]{return homeAction(window,"HC2A")->property("state")=="disconnected";}),"stop HC2A before testing an idle FPGA batch");
    pressureAction->click();
    check(waitUntil([&]{return pressureAction->property("state")=="disconnected";}),"stop PTB before testing an idle FPGA batch");
    disconnect->click();check(waitUntil([&]{return connectButton->isEnabled();}),"individual connection releases USB before full-connect regression");
    const QStringList relaySelectionNames{"deviceRemoteEpsilonEnabledCheck","deviceRemotePressureEnabledCheck",
        "deviceRemoteHumidityEnabledCheck","deviceRemoteLidarEnabledCheck","deviceRemoteTemperatureEnabledCheck",
        "deviceRemoteAi8TemperatureEnabledCheck","deviceFpgaAcquisitionEnabledCheck","deviceInternalEnabledBMP390","deviceInternalEnabledSHT45"};
    for(const auto &name:relaySelectionNames) window.findChild<QCheckBox*>(name)->setChecked(false);
    connectionSuccesses.clear();
    QMetaObject::invokeMethod(&window,"onConnectClicked");processFor(100);
    check(connectButton->isEnabled()&&connectionSuccesses.isEmpty(),"empty selection does not open or start FPGA devices");
    auto *pressureSelected=window.findChild<QCheckBox*>("deviceRemotePressureEnabledCheck");
    pressureSelected->setChecked(true);
    QMetaObject::invokeMethod(&window,"onConnectClicked");
    check(waitUntil([&]{return connectionSuccesses.contains(0x40)&&pressureAction->isEnabled();},15000),
        "bulk connect starts the selected PTB210");
    check(connectionSuccesses==QSet<quint16>{0x40}&&lastRegisters.value(0x6204)==0
        &&lastRegisters.value(0x6304)==0&&lastRegisters.value(0x6404)==0&&lastRegisters.value(0x4004)==0,
        "bulk connect skips unchecked serial, internal and ADC/DLIA devices");
    pressureSelected->setChecked(false);processFor(100);
    check(lastRegisters.value(0x6004)==1&&pressureAction->isEnabled()&&pressureAction->property("connected").toBool(),
        "clearing selection leaves the running channel and its disconnect action intact");
    pressureAction->click();
    const bool stopped=waitUntil([&]{return lastRegisters.value(0x6004)==0&&disconnect->isEnabled();});
    if(!stopped)std::cerr<<"PTB stop: state="<<pressureAction->property("state").toString().toStdString()
        <<" control="<<lastRegisters.value(0x6004)<<" disconnect="<<disconnect->isEnabled()<<"\n";
    check(stopped,"unselected running PTB remains stoppable and completes before USB disconnect");
    disconnect->click();check(waitUntil([&]{return connectButton->isEnabled();}),"filtered batch releases USB");
    connectionSuccesses.clear();
    window.findChild<QCheckBox*>("deviceRemoteTemperatureEnabledCheck")->setChecked(true);
    QMetaObject::invokeMethod(&window,"onConnectClicked");
    check(waitUntil([&]{return connectionSuccesses==QSet<quint16>{0x48,0x49}&&homeAction(window,QStringLiteral("原位 TEC103"))->isEnabled();}),
        "RD105-only selection connects the independent source");
    check(lastRegisters.value(0x6a04)==0&&lastRegisters.value(0x6804)==1&&lastRegisters.value(0x6904)==1,
        "TEC103 selective connection uses both physical UARTs and leaves legacy RS485 disabled");
    if(disconnect->isEnabled())disconnect->click();
    check(waitUntil([&]{return connectButton->isEnabled();}),"RD105 connection leaves the USB connection releasable");
    auto& drafts = MainWindowRtcmTestAccess::state(window).fpga_sensor_drafts_[0];
    const auto savedDraft = drafts.value(0x46);
    drafts[0x46]["baud"] = QStringLiteral("invalid");
    window.findChild<QCheckBox*>("deviceRemoteAi8TemperatureEnabledCheck")->setChecked(true);
    window.findChild<QCheckBox*>("deviceInternalEnabledSHT45")->setChecked(true);
    connectionSuccesses.clear();
    QMetaObject::invokeMethod(&window,"onConnectClicked");
    check(waitUntil([&]{return connectionSuccesses==QSet<quint16>{0x44,0x48,0x49};},15000),
        "a rejected AI8 draft leaves SHT45 and the two-port temperature controller in the title connection queue");
    if (savedDraft.isEmpty()) drafts.remove(0x46); else drafts[0x46] = savedDraft;
    disconnect->click();
    check(waitUntil([&]{return connectButton->isEnabled();}),"filtered batch after AI8 rejection releases USB");
    for(const auto &name:relaySelectionNames) window.findChild<QCheckBox*>(name)->setChecked(true);
    QMetaObject::invokeMethod(&window,"onConnectClicked");
    check(waitUntil([&]{auto *wave=homeAction(window,QStringLiteral("波形源"));
        return wave->isEnabled()&&wave->property("state")=="connected";},20000),
        "title connection opens FPGA then starts every supported sensor and shared acquisition");
    check(waitUntil([&]{return connectionSuccesses==QSet<quint16>{0,0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x48,0x49};}),
        "all eight current sensors and shared acquisition report confirmed connections");
    verifyRelayIndicators(window,{"Epsilon","Pressure","Humidity","Lidar","Temperature","Ai8","Wave","Bmp390","Sht45"});
    check(connectionSuccesses.contains(0x41),"HC2A has its own confirmed connection log");
    {
        int rdReads=0,aiReads=0,asciiStarts=0;
        const auto observer=QObject::connect(window.testFpgaController(),&VaporView::Ground::Devices::FpgaDeviceController::rawCommand,
            &window,[&](quint64,const QByteArray& bytes) {
                VaporView::FpgaVlp1::StreamParser parser;
                for(const auto& frame:parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),bytes.size()))if(frame.header.message==3 && frame.payload.size()>=12) {
                    const auto address=qFromLittleEndian<quint32>(frame.payload.data());rdReads+=address==0x6818||address==0x6918;aiReads+=address==0x6680;asciiStarts+=address==0x6240;
                }
            },Qt::QueuedConnection);
        auto* readRd=window.findChild<QToolButton*>("temperatureFpgaReadParameters");
        check(readRd&&readRd->isEnabled(),"RD105 capability enables the existing configuration read");
        auto *bmpAction=homeAction(window,"BMP390");
        bmpAction->click();
        check(waitUntil([&]{return bmpAction->property("state")=="disconnected";}),"stop BMP390 before interleaved parameter read");
        connectionSuccesses.remove(0x43);
        readRd->click();
        auto& state=MainWindowRtcmTestAccess::state(window);
        check(waitUntil([&]{return bool(state.fpga_instrument_reply_);}),"parameter read owns the FPGA reply");
        state.fpga_home_connect_queue_.append(VaporView::SkyDeviceId::Bmp390);
        check(waitUntil([&]{return connectionSuccesses.contains(0x43);},15000),
            "instrument completion resumes pending BMP390 connection and confirms a valid sample");
        const bool rdReadFinished=waitUntil([&]{return rdReads>5&&readRd->isEnabled();},45000);
        if(!rdReadFinished) {
            const auto& current=MainWindowRtcmTestAccess::state(window);
            std::cerr<<"RD105 parameter read: starts="<<rdReads<<" queue="<<current.fpga_action_queue_.size()
                <<" reply="<<bool(current.fpga_instrument_reply_)<<" busy="<<current.fpga_busy_
                <<" device="<<int(current.fpga_home_action_device_)<<" enabled="<<readRd->isEnabled()<<'\n';
        }
        check(rdReadFinished,"RD105 configuration page reads through the independent FPGA transaction interface");
        {
            auto& live=MainWindowRtcmTestAccess::state(window).current_temperature_controller_;
            const auto before=live;
            VaporView::Ground::Devices::Rd105SessionResult delayed;
            delayed.command=VaporView::CommandId::QueryDeviceStatus;
            delayed.outcome=VaporView::Ground::Devices::Rd105OperationOutcome::Success;
            delayed.has_latest_data=true;delayed.latest_data=before;
            delayed.latest_data.timestamp-=std::chrono::seconds(1);
            delayed.latest_data.channels[0].kp=12345;delayed.latest_data.channels[0].measured_temperature_c=-123;
            MainWindowRtcmTestAccess::temperatureResult(window,delayed);
            check(live.channels[0].kp==12345&&live.channels[0].measured_temperature_c==before.channels[0].measured_temperature_c,
                "a newer live sample preserves its measurement while the completed parameter read still updates PID");
            live=before;
        }
        for(const auto& name:{"temperatureDeviceAddressSpin","temperatureRs485BaudCombo","temperatureFactoryResetButton"}) {
            auto* control=window.findChild<QWidget*>(name);
            check(control&&!control->isEnabled(),"FPGA mode disables unsupported RD105 communication maintenance");
        }
        auto* readAi=window.findChild<QAbstractButton*>("ai8ReadParametersButton");
        check(readAi&&!readAi->isEnabled(),"current AI8 firmware keeps unsupported parameter transactions disabled");
        check(aiReads==0,"RD105 configuration reads never access unsupported AI8 parameter commands");
        auto* readEpsilon=window.findChild<QAbstractButton*>("epsilonSettingsReadButton");
        check(readEpsilon&&readEpsilon->isEnabled(),"EPSILON existing settings page is enabled by extension capabilities");readEpsilon->click();
        check(waitUntil([&]{return asciiStarts>5&&readEpsilon->isEnabled();},12000),"EPSILON existing parameter page uses the mailbox through its production worker");
        auto* packetRates=window.findChild<QAbstractButton*>("deviceEpsilonPacketRatesButton");
        check(packetRates&&packetRates->isEnabled(),"new firmware enables the shared EPSILON packet-rate entry");
        QObject::disconnect(observer);
    }
    for(const auto &entry: {qMakePair("deviceEpsilonBaudComboFpgaEditor","921600"),
            qMakePair("devicePressureBaudComboFpgaEditor","9600"),
            qMakePair("deviceLidarBaudComboFpgaEditor","460800"),
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
    auto *distanceBaud=window.findChild<QComboBox*>("deviceLidarBaudComboFpgaEditor");
    check(distanceBaud && !distanceBaud->isEditable() && distanceBaud->count()==7 &&
        distanceBaud->findText("9600")>=0 && distanceBaud->findText("256000")>=0 &&
        distanceBaud->findText("500000")<0,"FPGA SDFM300 editor exposes exactly the documented baud list");
    check(window.testLocalDeviceConfigSnapshot()==presets,"FPGA parameter writes preserve all Direct presets");
    check(!page->findChild<QLabel*>("fpgaConnectionStatus")->text().contains(QStringLiteral("修改 BMP 地址")),"already-enabled BMP does not receive a duplicate address-setting start");
    check(stack->currentIndex()==0,"local auto-connect keeps the home page selected");
    const QStringList homeNames{"EPSILON","PTB210","SDFM300","AI-8288",QStringLiteral("波形源")};
    auto *waveEnabled=window.findChild<QCheckBox*>("deviceFpgaAcquisitionEnabledCheck");
    check(waveEnabled&&deviceWaveRate(window)&&deviceWaveRate(window)->text().contains("1000000"),
        "local ADC actual sampling rate is displayed");
    for(const auto &name:homeNames) {
        auto *button=homeAction(window,name);
        for(bool enabled:{false,true}) {
            button->click();
            const bool completed=waitUntil([&]{return button->isEnabled()&&button->property("state")== (enabled?"connected":"disconnected");},10000);
            if(!completed) {
                const auto& current=MainWindowRtcmTestAccess::state(window);
                std::cerr<<"Local home action "<<name.toStdString()<<" enabled="<<enabled
                    <<" state="<<button->property("state").toString().toStdString()
                    <<" available="<<button->isEnabled()<<" queue="<<current.fpga_action_queue_.size()
                    <<" busy="<<current.fpga_busy_<<" activeDevice="<<int(current.fpga_home_action_device_)<<'\n';
            }
            check(completed,"local home action confirms sensor or acquisition enable through actual VLP1 readback");
            check(disconnect->isEnabled()&&stack->currentIndex()==0,"individual stop keeps USB open and stays on home");
            if(name==QStringLiteral("波形源"))check(waveEnabled->isEnabled()&&waveEnabled->isChecked(),
                "local VLP1 start and stop preserve desired acquisition selection");
        }
    }
    for (const auto& entry : QList<QPair<QString,VaporView::SkyDeviceId>>{
             {"AI-8288",VaporView::SkyDeviceId::Ai8TemperatureController},
             {QStringLiteral("波形源"),VaporView::SkyDeviceId::WaveTcp}}) {
        auto* action = homeAction(window,entry.first);
        QToolButton* configAction = nullptr;
        for (auto* candidate : window.findChildren<QToolButton*>("homeDeviceActionButton"))
            if (candidate->property("deviceConfigAction").toBool()
                && candidate->property("deviceConfigRemoteDevice").toInt() == static_cast<int>(entry.second))
                configAction = candidate;
        check(configAction,"device configuration has a matching device action");
        check(waitUntil([&]{return action->isEnabled();}),"device action is idle before reconnect");
        action->click();
        check(waitUntil([&]{return action->isEnabled()&&!action->property("connected").toBool();}),"device disconnect completes");
        action->click();
        check(waitUntil([&]{return action->property("connected").toBool();}),"reconnection completes before minimum animation ends");
        check(configAction->property("state")=="connecting","configuration displays the minimum connection animation");
        // Deliver register updates before timer events after the GUI was busy.
        QThread::msleep(1100);
        const auto confirmed = lastRegisters;
        emit window.testFpgaController()->hardwareValuesChanged(confirmed);
        QCoreApplication::sendPostedEvents(&window,QEvent::MetaCall);
        check(configAction->isEnabled()&&configAction->property("state")=="connected"
            &&!configAction->toolTip().contains(QStringLiteral("连接中")),
            "home refresh consuming spinner expiry also restores device configuration actions");
    }
    check(homeAction(window,QStringLiteral("原位 TEC103"))->isEnabled(),"current local relay advertises the independent RD105 capability");
    auto *navigation=window.findChild<VaporView::Ground::Navigation::CombinationNavigationPage*>("combinationNavigationPage");
    auto *navigationPanel=static_cast<VaporView::Ground::Navigation::NavigationStatusPanel*>(window.findChild<QWidget*>("navigationStatusPanel"));
    check(navigation&&navigationPanel,"local navigation status widgets exist");
    check(waitUntil([&]{navigation->refreshStatus();return navigationPanel->snapshot().navigationDataAvailable
        &&navigationPanel->snapshot().gnssFixCode==6;}),"local simulator supplies a fresh GNSS fix");
    auto *epsilonPanel=window.findChild<QWidget*>("epsilonPanel");
    check(waitUntil([&]{
        navigation->refreshStatus();const auto& sample=navigationPanel->snapshot();
        bool latitude=false,longitude=false;
        for(auto *label:epsilonPanel->findChildren<QLabel*>("valueLabel")) {
            bool numeric=false;const double value=label->text().toDouble(&numeric);
            latitude|=numeric&&value>30.229&&value<30.263;longitude|=numeric&&value>120.129&&value<120.159;
        }
        return sample.positionAvailable&&sample.speedAvailable&&sample.attitudeAvailable
            &&sample.latitudeDeg>30.229&&sample.latitudeDeg<30.263&&sample.heightM>=15&&sample.heightM<=215
            &&latitude&&longitude;
    }),"coalesced simulator packets populate home LLH plus navigation position, speed and attitude");
    int navigationChanges=0;
    QObject::connect(&logService,&VaporView::LogService::recordPublished,&window,[&](const VaporView::LogRecord& record) {
        if(record.fields.value("event")=="navigation_correction_status_changed")++navigationChanges;
    });
    for(bool enabled:{false,true}) {
        pressureAction->click();navigation->refreshStatus();
        check(homeAction(window,"EPSILON")->isEnabled(),"EPSILON action stays available during the local PTB transaction");
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
    auto *humidityRate=window.findChild<Hc2aPanel*>()->findChild<QLabel*>("rateLabel");
    auto *distanceRate=window.findChild<LidarPanel*>()->findChild<QLabel*>("rateLabel");
    auto *epsilonRate=window.findChild<QWidget*>("epsilonPanel")->parentWidget()->findChild<QLabel*>("rateLabel");
    check(pressureRate&&humidityRate&&distanceRate&&epsilonRate,"local home sensor frequency labels exist");
    const auto rateHz=[](QLabel *label){return QRegularExpression("([0-9]+\\.[0-9]+)\\s*Hz")
        .match(label->text().simplified()).captured(1).toDouble();};
    check(waitUntil([&]{return rateHz(pressureRate)>0.0&&rateHz(humidityRate)>0.0
        &&rateHz(distanceRate)>0.0&&rateHz(epsilonRate)>20.0;}),
        "local FPGA simulator supplies visible rates without serial collectors");
    auto *internal=window.findChild<QWidget*>("homeInternalEnvironment");
    check(internal && waitUntil([&]{return !internalEnvironmentText(internal).contains(QStringLiteral("—"));}),
        "both internal sensors reach the home capsules through real VLP frames");
    auto *environmentPressure=window.findChild<PtbPanel*>()->findChild<QLabel*>("highlightedValue");
    const double pressureHpa=QRegularExpression("([0-9]+\\.[0-9]+)").match(environmentPressure->text()).captured(1).toDouble();
    check(pressureHpa>=1012.97&&pressureHpa<=1013.53,"BMP pressure never replaces environmental PTB pressure across the PTB simulation cycle");
    check(homeAction(window,"HC2A")->isEnabled(),"HC2A firmware channel is available through the common home connection action");

    QWidget *deviceConfig=path;
    while(stack->indexOf(deviceConfig)<0)deviceConfig=deviceConfig->parentWidget();
    stack->setCurrentWidget(deviceConfig);processFor(100);
    verifyRelayIndicators(window,{"Epsilon","Pressure","Humidity","Lidar","Temperature","Ai8","Wave","Bmp390","Sht45"});
    check(deviceWaveRate(window)->width()>=deviceWaveRate(window)->sizeHint().width(),"actual ADC sampling rate fits its device configuration cell");
    stack->setCurrentIndex(0);processFor(100);

    auto *ai8Action=homeAction(window,"AI-8288");
    auto *ai8Channel=page->findChild<QDoubleSpinBox*>("fpgaAi8Channel");
    auto *ai8Scan=page->findChild<QCheckBox*>("fpgaAi8Scan");
    auto *apply=page->findChild<QAbstractButton*>("fpgaApplyAi8");
    QLabel *channel8=nullptr;
    for(auto *label:window.findChildren<QLabel*>("ai8TemperatureOverviewValueLabel"))
        if(label->property("channelIndex").toInt()==7)channel8=label;
    check(ai8Channel&&ai8Scan&&apply&&channel8&&!ai8Scan->isChecked()&&!ai8Scan->isEnabled(),"current firmware keeps automatic eight-channel scanning disabled");
    check(!FpgaControlConfig::fromJson(QJsonDocument::fromJson(settings.value("Fpga/remoteDesiredConfiguration").toByteArray()).object()).ai8.scan,
        "remote FPGA preferences preserve supported single-channel defaults");
    check(waitUntil([&]{
        const auto measured=window.findChildren<QLabel*>("ai8TemperatureOverviewValueLabel");
        const auto targets=window.findChildren<QLabel*>("ai8TemperatureOverviewTargetLabel");
        if(measured.size()!=8||targets.size()!=8)return false;
        for(auto *label:measured)if(label->text().contains("°C")!=(label->property("channelIndex").toInt()==0))return false;
        for(auto *label:targets)if(label->text().contains("°C")!=(label->property("channelIndex").toInt()==0))return false;
        return true;
    }),"home connection populates only the selected AI8 channel and does not fabricate other channels");
    ai8Action->click();
    check(waitUntil([&]{return ai8Action->isEnabled()&&ai8Channel->isEnabled();}),
        "AI8 disconnect unlocks channel selection");
    ai8Scan->setChecked(false);ai8Channel->setValue(8);apply->click();
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
    calibration.service->selectFile(calibrationFile.filename,true);calibration.service->synchronize();
    auto *record = page->findChild<QAbstractButton*>("fpgaRecord");
    record->click();
    auto *confirmation = recordingCalibrationDialog(window);
    check(confirmation, "local recording asks which calibration to use");
    auto *identity = confirmation->findChild<QCheckBox*>(QStringLiteral("recordingCalibrationIdentityConfirmed"));
    check(identity, "local confirmation provides an explicit device identity override");
    identity->setChecked(true); // The synthetic fixture and the local FPGA simulator report different identities.
    confirmRecordingCalibration(window);
    if (!waitUntil([&] { return record->text().contains(QStringLiteral("停止")); })) {
        const auto *dialog = recordingCalibrationDialog(window);
        const auto *status = dialog ? dialog->findChild<QLabel*>(QStringLiteral("recordingCalibrationStatus")) : nullptr;
        throw std::runtime_error(QStringLiteral("Local recording failed to start: %1")
                                     .arg(status ? status->text() : calibration.service->view().message).toStdString());
    }
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
    QTemporaryDir logs;
    VaporView::LogService logService("FpgaRd105HomeTest",nullptr,logs.path(),logs.path());
    QMap<int,bool> confirmedOutputs;
    MainWindow window;
    QObject::connect(&logService,&VaporView::LogService::recordPublished,&window,[&](const VaporView::LogRecord& record) {
        if(record.fields.value("event")=="temperature_command_completed"
            &&record.fields.value("command")=="SetTemperatureOutputEnabled")
            confirmedOutputs[record.fields.value("channel").toInt()]=record.fields.value("output_enabled").toBool();
    });
    window.resize(1600,1000);window.show();
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));
    auto *mode=window.findChild<QAction*>("uiTestModeAction");
    auto *page=window.findChild<FpgaControlPage*>();
    auto *monitor=window.findChild<TcpWavePanel*>();
    auto *raw=monitor->findChild<QAbstractButton*>("fpgaMonitorRawButton");
    auto *harmonic=monitor->findChild<QAbstractButton*>("fpgaMonitorHarmonicButton");
    require(mode&&page&&raw&&harmonic,"simulated monitor controls exist");
    // Exercise the production RD105/AI8 scan profile, without historical AI8 parameter support.
    mode->trigger();
    require(waitUntil([&]{return page->uiTestController()!=nullptr;}),"UI test installs an isolated protocol controller");
    require(!page->uiTestConnected()&&!raw->isEnabled(),"UI test mode does not fabricate an FPGA connection");
    auto *configPage=window.findChild<QWidget*>("deviceConfigPage");
    auto *stack=window.findChild<QStackedWidget*>("mainPageStack");
    auto *sensorPath=window.findChild<QComboBox*>("deviceSensorTransportPathCombo");
    require(configPage&&stack&&sensorPath,"device configuration page and path selector exist");
    QWidget *stackPage=configPage;
    while(stackPage && stack->indexOf(stackPage)<0)stackPage=stackPage->parentWidget();
    require(stackPage,"device configuration is in the page stack");
    stack->setCurrentWidget(stackPage);
    const auto displayedLabel=[&](const QString& name) -> QLabel * {
        for(auto *pill:configPage->findChildren<QFrame*>("homeTelemetrySummaryPill")) {
            auto *label=pill->findChild<QLabel*>("homeTelemetrySummaryNameLabel");
            auto *value=pill->findChild<QLabel*>("homeTelemetrySummaryValueLabel");
            if(label&&value&&value->isVisible()&&label->text()==name)return value;
        }
        return nullptr;
    };
    const auto displayedValue=[&](const QString& name) {
        auto *label=displayedLabel(name);
        return label ? label->text() : QString();
    };
    const auto setDemoElapsed=[&](qint64 elapsedMs) {
        MainWindowRtcmTestAccess::state(window).ui_test_started_ms_=QDateTime::currentMSecsSinceEpoch()-elapsedMs;
        MainWindowRtcmTestAccess::refreshSummary(window);
    };
    const qint64 originalStart=MainWindowRtcmTestAccess::state(window).ui_test_started_ms_;
    require(waitUntil([&]{return displayedLabel(QStringLiteral("基础遥测")) != nullptr;}),
            "the FPGA telemetry summary is visible before sampling the demo clock");
    setDemoElapsed(200);
    const auto previewShown=[&] {
        const QString frequency = sensorPath->currentData()==QStringLiteral("fpga_relay")
            ? QStringLiteral("基础遥测") : QStringLiteral("基础");
        return displayedValue(frequency)==QStringLiteral("88.8 Hz")
            && displayedValue(QStringLiteral("天→地"))==QStringLiteral("234.5 Mbps")
            && displayedValue(QStringLiteral("地→天"))==QStringLiteral("123.4 Mbps")
            && displayedValue(QStringLiteral("磁盘"))==QStringLiteral("128.0 GB")
            && displayedValue(QStringLiteral("记录"))==QStringLiteral("记录中")
            && displayedValue(QStringLiteral("CRC"))==QStringLiteral("0")
            && displayedValue(QStringLiteral("EPSILON"))==QStringLiteral("有");
    };
    require(previewShown(),"visible FPGA configuration pills show demo values before connection");
    setDemoElapsed(1200);
    require(displayedValue(QStringLiteral("基础遥测"))==QStringLiteral("89.8 Hz")
        && displayedValue(QStringLiteral("天→地"))!=QStringLiteral("234.5 Mbps"),
        "demo frequency and link rate change over time");
    setDemoElapsed(6200);
    auto *errorRate=displayedLabel(QStringLiteral("基础遥测"));
    auto *errorDevice=displayedLabel(QStringLiteral("EPSILON"));
    require(errorRate && errorRate->text()==QStringLiteral("0.0 Hz")
        && errorRate->property("telemetryError").toBool()
        && displayedValue(QStringLiteral("CRC"))==QStringLiteral("127")
        && displayedValue(QStringLiteral("记录"))==QStringLiteral("异常")
        && errorDevice && errorDevice->text()==QStringLiteral("异常")
        && errorDevice->property("telemetryError").toBool(),
        "demo shows error values and error styling");
    setDemoElapsed(12200);
    auto *missingRate=displayedLabel(QStringLiteral("基础遥测"));
    auto *missingDevice=displayedLabel(QStringLiteral("EPSILON"));
    require(missingRate && missingRate->text()==QStringLiteral("-- Hz")
        && !missingRate->property("telemetryAvailable").toBool()
        && !missingRate->property("telemetryError").toBool()
        && displayedValue(QStringLiteral("CRC"))==QStringLiteral("--")
        && missingDevice && missingDevice->text()==QStringLiteral("无"),
        "demo shows unavailable values separately from errors");
    sensorPath->setCurrentIndex(sensorPath->findData("direct_devices"));
    require(waitUntil([&]{return displayedLabel(QStringLiteral("基础")) != nullptr;}),
            "switching to direct devices shows the direct telemetry summary");
    setDemoElapsed(200);
    require(previewShown(),"visible direct configuration pills retain demo values");
    sensorPath->setCurrentIndex(sensorPath->findData("fpga_relay"));
    require(waitUntil([&]{return displayedLabel(QStringLiteral("基础遥测")) != nullptr;}),
            "switching back to FPGA relay shows the FPGA telemetry summary");
    setDemoElapsed(200);
    require(previewShown(),"visible FPGA configuration pills restore demo values after path switch");
    MainWindowRtcmTestAccess::state(window).ui_test_started_ms_=originalStart;
    {
        const auto summary = MainWindowRtcmTestAccess::summary(window);
        require(!summary.rateItems[0].hasData && !summary.linkItems[0].hasData,
                "disconnected FPGA demo does not show active summary rates");
    }
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
    require(waitUntil([&]{
        const auto summary = MainWindowRtcmTestAccess::summary(window);
        return summary.rateItems[0].hasData && summary.rateItems[0].value != QStringLiteral("0.0 Hz")
            && summary.linkItems[0].hasData;
    }), "connected FPGA demo supplies representative summary rates");
    verifyRelayIndicators(window,{"Epsilon","Pressure","Humidity","Lidar","Temperature","Ai8","Wave","Bmp390","Sht45"});
    require(raw->text().contains(QStringLiteral("开始")),"connecting all devices does not start RAW upload");
    require(waitUntil([&]{
        auto *percent=window.findChild<QLabel*>("temperatureOverviewOutputPercentPill");
        int populatedPlots=0;
        for(auto *plot:window.findChildren<QWidget*>("temperatureTrendPlot"))
            if(plot->property("temperatureOverviewPlot").toBool()&&plot->property("sampleCount").toInt()>=2)++populatedPlots;
        return percent&&percent->property("channel1OutputPercentText").toString()=="0.00%"
            &&percent->property("channel2OutputPercentText").toString()=="0.00%" && percent->text().contains(QStringLiteral("原：")) && percent->text().contains(QStringLiteral("遥："))&&populatedPlots==2;
    },6000),"FPGA overview displays both instruments' TC1 trends");
    const auto outputs=window.findChildren<QAbstractButton*>("temperatureOverviewOutputSwitch");
    require(outputs.size()==2,"simulated RD105 overview exposes both output controls");
    auto *tc1 = window.findChild<QAbstractButton*>("temperatureChannelSelectorButton1");
    auto *tc2 = window.findChild<QAbstractButton*>("temperatureChannelSelectorButton2");
    auto *title = window.findChild<QWidget*>("temperatureTitleStatusStrip");
    auto *device = window.findChild<QAbstractButton*>("tec103DeviceSwitch");
    auto *pages = window.findChild<QStackedWidget*>("temperatureChannelStack");
    require(tc1 && tc2 && tc1->text() == QStringLiteral("通道1") && tc2->isHidden() &&
            device && !device->isHidden() && pages, "Separate TEC103 device and channel selectors exist");
    QWidget *temperaturePage = title;
    while (temperaturePage && stack->indexOf(temperaturePage) < 0) temperaturePage = temperaturePage->parentWidget();
    require(temperaturePage, "Temperature title belongs to the page stack");
    auto *previousPage = stack->currentWidget();
    stack->setCurrentWidget(temperaturePage);
    QCoreApplication::processEvents();
    auto *navigation = tc1->parentWidget();
    require(device->parentWidget() == navigation->parentWidget() &&
            device->geometry().right() < navigation->geometry().left() &&
            std::abs(device->geometry().center().y() - navigation->geometry().center().y()) <= 1,
            "TEC103 device selector precedes the temperature navigation bar");
    device->click();
    require(tc1->isChecked() && pages->currentIndex() == 1, "Remote TC1 uses the remote instrument page");
    device->click();
    require(tc1->isChecked() && pages->currentIndex() == 0, "In situ TC1 restores its own controls");
    auto capsules=window.findChildren<QLabel*>("homeDeviceStatusCapsule");
    capsules.removeAll(MainWindowRtcmTestAccess::state(window).home_motion_status_lbl_);
    QList<QObject*> watched;
    struct OutputStateObserver : QObject {
        bool capsuleChanged=false, actionsLocked=false;
        bool eventFilter(QObject *object,QEvent *event) override {
            if(event->type()==QEvent::DynamicPropertyChange&&object->objectName()=="homeDeviceStatusCapsule")
                capsuleChanged |= !object->property("connected").toBool() ||
                    (object->property("state") != "connected" && object->property("state") != "warning");
            if(event->type()==QEvent::EnabledChange&&object->objectName()=="homeDeviceActionButton")
                actionsLocked|=!static_cast<QWidget*>(object)->isEnabled();
            return false;
        }
    } outputState;
    require(capsules.size()==10&&waitUntil([&]{
        for(auto *capsule:capsules)if(capsule->property("state")!="connected")return false;
        for(auto *button:window.findChildren<QToolButton*>("homeDeviceActionButton"))
            if(!button->property("deviceConfigAction").toBool()&&!button->property("motionDeviceAction").toBool()&&!button->isEnabled())return false;
        return true;
    }),"all nine device capsules are connected before testing RD105 output commands");
    for(auto *capsule:capsules)watched.append(capsule);
    for(auto *button:window.findChildren<QToolButton*>("homeDeviceActionButton"))
        if(!button->property("deviceConfigAction").toBool()&&!button->property("motionDeviceAction").toBool())watched.append(button);
    for(auto *object:watched)object->installEventFilter(&outputState);
    auto *homeDevice = window.findChild<QAbstractButton*>("tec103OverviewDeviceSwitch");
    require(!homeDevice && !outputs[1]->isHidden(), "Home overview shows both TC1 output switches");
    auto *output = outputs[0];
    for (int instrument = 1; instrument <= 2; ++instrument) {
        output = outputs[instrument - 1];
        require(output->isEnabled(),"simulated RD105 output control is available");
        QTimer confirm;
        QObject::connect(&confirm,&QTimer::timeout,&window,[]{
            if(auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                if(auto *yes=box->button(QMessageBox::Yes))yes->click();
        });
        confirm.start(10);output->click();confirm.stop();
        require(waitUntil([&]{return output->isChecked()&&confirmedOutputs.value(instrument,false);},6000),
            "simulated output enable reaches confirmed telemetry and command completion");
    }
    auto *percent=window.findChild<QLabel*>("temperatureOverviewOutputPercentPill");
    const auto outputPercent=[&](int channel) {
        return percent->property(channel==1?"channel1OutputPercentText":"channel2OutputPercentText").toString().remove('%').toDouble();
    };
    require(waitUntil([&]{return outputPercent(2)<0;}), "Remote TC1 displays its own PWM");
    output = outputs[0];
    require(outputPercent(1)>0, "In situ TC1 displays its own PWM");
    output->click();
    require(waitUntil([&]{return !output->isChecked() && !confirmedOutputs.value(1,true);},6000), "In situ TC1 stops");
    output = outputs[1];
    require(output->isChecked() && outputPercent(2)<0 && outputPercent(1)==0, "Remote TC1 remains enabled independently");
    output->click();
    require(waitUntil([&]{return !output->isChecked() && !confirmedOutputs.value(2,true);},6000), "Remote TC1 stops");
    for(auto *object:watched)object->removeEventFilter(&outputState);
    require(!outputState.actionsLocked,"RD105 output commands never disable unrelated home connection buttons");
    require(!outputState.capsuleChanged,"RD105 output enable and disable never flash connected device capsules to unavailable");
    auto *queuedPressure=homeAction(window,"PTB210");
    queuedPressure->click();
    require(!queuedPressure->isEnabled()&&homeAction(window,"HC2A")->isEnabled(),"local device action locks only its own control");
    QTimer confirmQueuedOutput;
    QObject::connect(&confirmQueuedOutput,&QTimer::timeout,&window,[]{
        if(auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            if(auto *yes=box->button(QMessageBox::Yes))yes->click();
    });
    confirmQueuedOutput.start(10);outputs[0]->click();confirmQueuedOutput.stop();
    require(waitUntil([&]{return confirmedOutputs.value(1,false)&&queuedPressure->isEnabled()
        &&queuedPressure->property("state")=="disconnected";},10000),"RD105 command waits behind a local sensor transaction and completes");
    auto *queuedHumidity=homeAction(window,"HC2A");
    bool queuedDuringOutput=false;
    const auto queueObserver=QObject::connect(page->uiTestController(),&VaporView::Ground::Devices::FpgaDeviceController::connectionChanged,
        &window,[&](bool ready,bool busy,const QString&) {
            if(!ready||!busy||queuedDuringOutput)return;
            queuedDuringOutput=true;
            require(queuedHumidity->isEnabled()&&homeAction(window,"EPSILON")->isEnabled(),"RD105 transaction leaves other device controls clickable");
            queuedHumidity->click();queuedHumidity->click();
            homeAction(window,QStringLiteral("原位 TEC103"))->click();
        });
    outputs[0]->click();
    require(waitUntil([&]{return queuedDuringOutput&&!confirmedOutputs.value(1,true)&&queuedHumidity->isEnabled()
        &&queuedHumidity->property("state")=="disconnected"&&homeAction(window,QStringLiteral("原位 TEC103"))->isEnabled()
        &&homeAction(window,QStringLiteral("原位 TEC103"))->property("state")=="disconnected"
        &&homeAction(window,QStringLiteral("遥测 TEC103"))->property("state")=="connected";},10000),"queued sensor and RD105 disconnect finish after the full output command readback");
    QObject::disconnect(queueObserver);
    mode->trigger();mode->trigger();
    require(waitUntil([&]{return page->uiTestController()!=nullptr&&!page->uiTestConnected();}),"reset protocol board for selective connection regression");
    require(QMetaObject::invokeMethod(&window,"onDisconnectClicked",Qt::DirectConnection),"disconnect layout simulation before individual connection");
    auto *pressure=homeAction(window,"PTB210");
    require(waitUntil([&]{return pressure->isEnabled()&&pressure->property("state")=="disconnected";}),"layout simulation is disconnected");
    pressure->click();
    require(waitUntil([&]{return pressure->isEnabled()&&pressure->property("state")=="connected";}),"layout simulation connects the requested device");
    require(onlyPressureConnected(window),"layout simulation leaves all other sensors and waveform acquisition disconnected");
    verifyRelayIndicators(window,{"Pressure"});
    require(waitUntil([&]{return onlyPressureDataAvailable(window, true);}),"UI summary reports only actual PTB device data");
    require(raw->text().contains(QStringLiteral("开始"))&&harmonic->text().contains(QStringLiteral("开始")),"individual simulation connection leaves waveform acquisition stopped");
    require(QMetaObject::invokeMethod(&window,"onConnectClicked",Qt::DirectConnection),"batch simulation connection is available");
    require(waitUntil([&]{return harmonic->isEnabled()&&harmonic->text().contains(QStringLiteral("停止"));},20000),
        "UI test title connection starts all devices through the production VLP1 controller");
    verifyRelayIndicators(window,{"Epsilon","Pressure","Humidity","Lidar","Temperature","Ai8","Wave","Bmp390","Sht45"});
    for(const auto &entry:{qMakePair("deviceEpsilonBaudComboFpgaEditor","921600"),
            qMakePair("devicePressureBaudComboFpgaEditor","9600"),
            qMakePair("deviceLidarBaudComboFpgaEditor","460800"),
            qMakePair("deviceInternalRateSHT45FpgaEditor","1")}) {
        auto *value=window.findChild<QComboBox*>(entry.first);
        require(value&&value->currentText()==QString::fromUtf8(entry.second),"UI test sensor register values update inline fields immediately");
    }
    auto *rate=window.findChild<QComboBox*>("deviceInternalRateSHT45FpgaEditor");
    auto *humidity=homeAction(window,"SHT45");
    require(rate&&humidity&&!rate->isEnabled(),"UI test connected sensor locks inline frequency");
    require(waitUntil([&]{return humidity->isEnabled();}),"title connection releases individual controls after their action spinner settles");humidity->click();
    require(waitUntil([&]{return rate->isEnabled();}),"UI test individual disconnect enables frequency");rate->setCurrentText("2.");
    processFor(150);require(rate->currentText()=="2.","refresh preserves partially typed decimal frequency");
    rate->setCurrentText("2.5");
    processFor(150);require(rate->currentText()=="2.5","simulation refresh preserves staged frequency");
    require(waitUntil([&]{return humidity->isEnabled();}),"individual disconnect releases the sensor action");
    humidity->click();
    require(waitUntil([&]{return !rate->isEnabled()&&humidity->isEnabled();}),
        "UI test connect applies staged frequency");
    processFor(150);require(rate->currentText()=="2.5","periodic simulation retains applied frequency");
    auto *internalPressure=homeAction(window,"BMP390");
    require(waitUntil([&]{return internalPressure->isEnabled()&&internalPressure->property("state")=="connected";}),
        "home overview reports the connected BMP390 independently");
    internalPressure->click();
    require(waitUntil([&]{return internalPressure->isEnabled()&&internalPressure->property("state")=="disconnected";}),
        "home BMP390 action stops its own sensor");
    require(pressure->property("state")=="connected"&&humidity->property("state")=="connected",
        "stopping BMP390 preserves PTB210 and SHT45");
    internalPressure->click();
    require(waitUntil([&]{return internalPressure->isEnabled()&&internalPressure->property("state")=="connected";}),
        "home BMP390 action restarts its own sensor");
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
    require(waitUntil([&]{return displayedValue(QStringLiteral("磁盘"))!=QStringLiteral("128.0 GB");}),
        "leaving UI test removes demo values from visible configuration pills");
    {
        const auto summary = MainWindowRtcmTestAccess::summary(window);
        require(!summary.rateItems[0].hasData && !summary.linkItems[0].hasData,
                "leaving FPGA demo clears simulated summary rates");
    }
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
        page->setConnectionState(true,false);
        page->updateWaveform(wave);
        require(monitor->findChild<QWidget *>("waterConcentrationPlot")->isVisibleTo(monitor) &&
            monitor->findChild<QWidget *>("waterProfilePlot")->isVisibleTo(monitor) &&
            !monitor->findChild<QWidget *>("tcpWaveRawPlot")->isVisibleTo(monitor),
            "home FPGA monitor starts in the water measurement view");
        auto *all = monitor->findChild<QAction *>("tcpWaveDisplayShowAllMenuAction");
        require(all, "home monitor provides the all-graphs view");
        all->trigger();
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
    auto preferences=VaporView::applicationConfigSettings();
    const auto oldLocal=preferences.value("Fpga/localDisabledHomeSources");
    const auto oldRemote=preferences.value("Fpga/remoteDisabledHomeSources");
    const auto restoreSelection=qScopeGuard([&] {
        for(const auto &item:{qMakePair(QStringLiteral("Fpga/localDisabledHomeSources"),oldLocal),
                qMakePair(QStringLiteral("Fpga/remoteDisabledHomeSources"),oldRemote)}) {
            if(item.second.isValid()) preferences.setValue(item.first,item.second); else preferences.remove(item.first);
        }
    });
    preferences.remove("Fpga/localDisabledHomeSources");preferences.remove("Fpga/remoteDisabledHomeSources");
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
    require(waveEnabled->isVisible()&&waveEnabled->isEnabled()&&deviceWaveRate(window)->text().contains(QStringLiteral("—")),
        "FPGA selection is editable before connection while actual sampling remains unknown");
    auto *epsilonEnabled=window.findChild<QCheckBox*>(presetNames[0]);
    epsilonEnabled->click();waveEnabled->click();
    require(!epsilonEnabled->isChecked()&&!waveEnabled->isChecked(),"FPGA selections can be cleared without a connection");
    require(!homeAction(window,"EPSILON")->isEnabled(),"unselected disconnected devices cannot be started until enabled");
    path->setCurrentIndex(path->findData("direct_devices"));
    require(epsilonEnabled->isChecked()==enabledPresets[0],"FPGA selection does not overwrite Direct selection");
    path->setCurrentIndex(path->findData("fpga_relay"));
    require(!epsilonEnabled->isChecked()&&!waveEnabled->isChecked(),"returning to FPGA restores its own selections");
    const bool originalDark=VaporView::isDarkThemeEnabled();
    for(bool dark:{true,false}) {
        if(VaporView::isDarkThemeEnabled()!=dark) {
            QMetaObject::invokeMethod(&window,"onToggleTheme");processFor(80);
        }
        for(const auto *name:{"deviceRemoteEpsilonEnabledCheck","deviceRemotePressureEnabledCheck",
                "deviceRemoteHumidityEnabledCheck","deviceRemoteLidarEnabledCheck",
                "deviceRemoteTemperatureEnabledCheck","deviceRemoteAi8TemperatureEnabledCheck"}) {
            auto *check=window.findChild<QCheckBox*>(name);
            require(check&&check->isEnabled(),"FPGA enable configuration is editable before connection");
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
                require(themePixels>8,"editable enable checkboxes render the active theme accent");
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
    require(monitor->testPeakValues()==QVector<float>({99999.f/1000}),"cycle-normalized peak uses full resolution before display decimation");
    require(monitor->testHarmonicSamples().size()>3&&monitor->testHarmonicSamples().first()==-0.005f&&monitor->testHarmonicSamples()[3]==-0.005f,
        "home waveform retains H2 shape across local reference minima");
    page->setLanguage(true);page->setLanguage(false);page->updateWaveform(full);
    require(monitor->testPeakValues()==QVector<float>({99999.f/1000}),"FPGA rerender and repeated cycle do not duplicate trend entries");
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
    remote.epsilon.port="REMOTE-COM7";remote.ptb.port="REMOTE-COM8";remote.hc2a.port="REMOTE-COM9";
    remote.lidar.port="REMOTE-COM10";remote.temperature_controller.port="REMOTE-COM11";
    remote.ai8_temperature_controller.port="REMOTE-COM12";
    window.testInjectRemoteSkyConfig(remote.toJson());processFor(50);
    for(const auto &device:devices) {
        auto *port=window.findChild<QComboBox*>("device"+device+"PortCombo");
        require(!port->isEnabled()&&!port->isVisible(),"remote config refresh cannot enable relay serial ports");
    }
    auto *remoteEnable=window.findChild<QCheckBox*>(presetNames[0]);
    require(remoteEnable->isChecked(),"remote FPGA selection is independent of local selection");
    remoteEnable->click();
    require(!remoteEnable->isChecked(),"remote FPGA selection can be edited without changing Direct presets");
    QString error;
    const auto saved=window.testRemoteSkyConfigFromDeviceConfigUi(&error);
    require(error.isEmpty()&&saved.value("epsilon").toObject().value("port")=="REMOTE-COM7",
        "FPGA presentation preserves the remote serial configuration too");
    for(const auto *key:{"epsilon","ptb","hc2a","lidar","temperature_controller","ai8_temperature_controller","bmp390","sht45"})
        require(saved.value(key).toObject().value("enabled")==remote.toJson().value(key).toObject().value("enabled"),
            "FPGA readback never overwrites remote Direct startup presets");
    QMetaObject::invokeMethod(&window,"onDataSourceModeChanged",Q_ARG(int,0));
    path->setCurrentIndex(path->findData("fpga_relay"));
    window.close();
    }
    const auto stored=VaporView::applicationConfigSettings();
    const QStringList presetKeys{"local_enabled/epsilon","local_enabled/ptb","local_enabled/hc2a","local_enabled/lidar",
        "local_enabled/temperature","local_enabled/ai8_temperature","local_enabled/wave_tcp","internal/bmp390/enabled","internal/sht45/enabled"};
    for(int i=0;i<presetKeys.size();++i)
        require(stored.value("MainWindow/"+presetKeys[i]).toBool()==enabledPresets[i],
            "saving settings on the relay path preserves every Direct startup preset");
    MainWindow restored;
    auto *path=restored.findChild<QComboBox*>("deviceSensorTransportPathCombo");
    require(path->currentData()=="fpga_relay","restarted window retains the FPGA path");
    require(!restored.findChild<QCheckBox*>(presetNames[0])->isChecked()
        &&!restored.findChild<QCheckBox*>("deviceFpgaAcquisitionEnabledCheck")->isChecked(),
        "restart restores local FPGA selections independently of Direct configuration");
    QMetaObject::invokeMethod(&restored,"onDataSourceModeChanged",Q_ARG(int,1));
    auto remote=VaporView::SkyConfig::defaults();remote.sensor_transport_path=VaporView::SensorTransportPath::FpgaRelay;
    restored.testInjectRemoteSkyConfig(remote.toJson());
    require(!restored.findChild<QCheckBox*>(presetNames[0])->isChecked(),"restart restores remote FPGA selection");
    QMetaObject::invokeMethod(&restored,"onDataSourceModeChanged",Q_ARG(int,0));
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
    sky.ptb.source="ptb210"; sky.hc2a.source="hc2a";
    window.testInjectRemoteSkyConfig(sky.toJson());
    TelemetryCodec decoder, encoder; quint16 sequence=1; int applies=0; bool reject=false;
    QObject::connect(&server,&TelemetryLink::bytesReceived,&window,[&](const QByteArray &bytes) {
        for (const auto &frame:decoder.feedBytes(bytes)) {
            CommandMessage command;
            if(frame.type!=MsgType::Command||!TelemetryCodec::parseCommand(frame.payload,command))continue;
            if(command.command_id!=CommandId::GetSkyConfig&&command.command_id!=CommandId::SetSkyConfig)continue;
            CommandAck ack;ack.command_id=command.command_id;ack.command_seq=command.command_seq;
            ack.correlation_id=command.correlation_id;ack.session_id=command.session_id;
            ack.extended_ack_context_allowed=command.extended_ack_context_requested;
            if(command.command_id==CommandId::SetSkyConfig) {
                ++applies;
                ack.config_result_context_expected=true;
                if(!reject) require(SkyConfig::fromJson(QJsonDocument::fromJson(command.payload).object(),sky),"parse submitted Sky configuration");
            }
            server.writeBytes(encoder.encodeFrame(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack),sequence++,1));
            const bool read=command.command_id==CommandId::GetSkyConfig;
            const auto payload=read?QJsonDocument(sky.toJson()).toJson(QJsonDocument::Compact)
                :TelemetryCodec::serializeSkyConfigApplyResultContext({{"success",!reject},{"error",reject?"rejected test":""}},
                    command.correlation_id,command.session_id,command.command_seq,command.extended_result_context_requested);
            server.writeBytes(encoder.encodeFrame(read?MsgType::SkyConfig:MsgType::SkyConfigApplyResultContext,payload,sequence++,1));
            if(!read)server.writeBytes(encoder.encodeFrame(MsgType::SkyConfigApplyResult,
                QJsonDocument(QJsonObject{{"success",!reject},{"error",reject?"rejected test":""}}).toJson(QJsonDocument::Compact),sequence++,1));
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
    require(sky.fpga.configuration.pressureSource==0x40 && sky.ptb.source=="ptb210" && sky.hc2a.source=="hc2a",
        "remote environmental device identity stays fixed");
    QMetaObject::invokeMethod(&window,"onRtkConfigClicked");
    auto *rtk=window.findChild<RtkConfigDialog*>();
    auto *start=rtk->findChild<QAbstractButton*>("rtkStartButton");
    require(start&&!start->isEnabled()&&start->toolTip().contains("VLP1"),"FPGA RTK start is disabled with a protocol capability reason");
    QMetaObject::invokeMethod(rtk,"onStartClicked");
    require(!rtk->isRunning(),"direct slot invocation cannot start a bypass RTCM stream");
    auto *readAi8=window.findChild<QAbstractButton*>("ai8ReadParametersButton");
    auto *readEpsilon=window.findChild<QAbstractButton*>("epsilonSettingsReadButton");
    require(readAi8&&!readAi8->isEnabled()&&readAi8->toolTip().contains("AI8") && readAi8->toolTip().contains(QStringLiteral("固件")),"FPGA AI8 page commands explain unsupported capability");
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
    QTcpServer waveServer;
    require(waveServer.listen(QHostAddress::LocalHost, 0), "Local direct waveform server listens");
    auto *wavePanel = window.findChild<TcpWavePanel *>();
    wavePanel->setConnectionEndpoint("127.0.0.1", waveServer.serverPort());
    wavePanel->toggleConnection();
    require(waitUntil([&] { return wavePanel->isConnected() && waveServer.hasPendingConnections(); }), "Direct waveform opens without a remote Sky connection");
    auto *waveSocket = waveServer.nextPendingConnection();
    const auto frame = [](int count, bool harmonic) {
        QByteArray bytes(4 + count * 4, '\0');
        qToLittleEndian<qint32>(count * 4, bytes.data());
        for (int i = 0; i < count; ++i)
        {
            const float value = harmonic ? float(std::exp(-std::pow((i - count * 0.45) / (count * 0.08), 2)))
                : float((i % 80) / 79.0 * 2.0 - 1.0);
            quint32 bits;
            std::memcpy(&bits, &value, sizeof(bits));
            qToLittleEndian<quint32>(bits, bytes.data() + 4 + i * 4);
        }
        return bytes;
    };
    QLabel *waveCapsule = nullptr;
    for (auto *label : window.findChildren<QLabel *>("homeDeviceStatusCapsule"))
    {
        if (label->text().contains(QStringLiteral("波形")))
            waveCapsule = label;
    }
    require(waveCapsule, "Home waveform capsule exists");
    const auto hasState = [&](const QString &state) { return waveCapsule->property("state") == state; };
    MainWindowRtcmTestAccess::refreshCapsules(window);
    require(hasState(QStringLiteral("warning")), "Connected waveform warns until its first valid frame");
    const int normalWidth = waveCapsule->minimumSizeHint().width();
    auto &history = MainWindowRtcmTestAccess::state(window).home_device_data_state_[SkyDeviceId::WaveTcp];
    history.connectedSinceMs -= 4000;
    MainWindowRtcmTestAccess::refreshCapsules(window);
    require(hasState(QStringLiteral("warning")) && waveCapsule->property("connected").toBool() &&
            waveCapsule->toolTip().contains(QStringLiteral("尚未收到有效测量数据")),
            "Connected waveform without a first frame displays a warning and its reason");
    require(waveCapsule->minimumSizeHint().width() == normalWidth &&
            homeAction(window, QStringLiteral("波形源"))->property("state") == QStringLiteral("connected"),
            "Warning keeps the capsule width and the disconnect action");
    const QByteArray data = frame(2000, false) + frame(1800, true);
    waveSocket->write(data);
    require(waitUntil([&] { return wavePanel->hasRecentData(3000) && hasState(QStringLiteral("connected")); }),
            "A valid waveform restores the normal capsule");
    require(waitUntil([&] { return hasState(QStringLiteral("warning")); }, 5000) && wavePanel->isConnected() &&
            waveCapsule->toolTip().contains(QStringLiteral("测量数据超时")),
            "A stalled waveform displays a timeout warning while still connected");
    waveSocket->write(data);
    require(waitUntil([&] { return hasState(QStringLiteral("connected")); }),
            "The warning clears when waveform frames resume");
    wavePanel->toggleConnection();
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
    for (const auto &name : {QStringLiteral("BMP390"), QStringLiteral("SHT45")})
        require(homeAction(window,name)->isEnabled()&&homeAction(window,name)->property("state")=="disconnected",
            "home internal sensor actions follow the independent direct serial presets");
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
    auto *label=window.findChild<QWidget*>("homeInternalEnvironment");
    require(waitUntil([&]{return remote->deviceState(SkyDeviceId::Sht45)==DeviceState::Connected
        &&homeAction(window,"BMP390")->property("state")=="connected"
        &&homeAction(window,"SHT45")->property("state")=="connected";}),"home internal sensor actions reflect independent remote device IDs");
    const auto environmentBefore=window.findChild<PtbPanel*>()->findChild<QLabel*>("highlightedValue")->text();
    FpgaSensor::Reading reading;reading.source=0x43;reading.kind=FpgaSensor::SensorKind::Bmp390;reading.pressurePa=100000;reading.validity={true,true,true,true};
    send(MsgType::InternalEnvironment,FpgaRemote::encodeSensor(reading,1));
    reading.source=0x44;reading.kind=FpgaSensor::SensorKind::Sht45;reading.pressurePa.reset();reading.temperatureC=30;reading.humidityPct=70;
    send(MsgType::InternalEnvironment,FpgaRemote::encodeSensor(reading,2));
    require(waitUntil([&]{return internalEnvironmentText(label).contains("1000.00")&&internalEnvironmentText(label).contains("30.00")&&internalEnvironmentText(label).contains("70.00");}),
        "direct Sky internal telemetry reaches the three home internal metrics");
    require(window.findChild<PtbPanel*>()->findChild<QLabel*>("highlightedValue")->text()==environmentBefore,
        "internal serial telemetry never overwrites the environmental PTB panel");
    status.devices[0].state=DeviceState::Disconnected;send(MsgType::TelemetryStatus,TelemetryCodec::serializeTelemetryStatus(status));
    require(waitUntil([&]{return internalEnvironmentText(label).contains(QStringLiteral("—"))&&internalEnvironmentText(label).contains("30.00")
        &&homeAction(window,"BMP390")->property("state")=="disconnected"
        &&homeAction(window,"SHT45")->property("state")=="connected";}),
        "individual BMP disconnect invalidates pressure while preserving SHT values");
    require(waitUntil([&]{return internalEnvironmentText(label).count(QStringLiteral("—"))==3;},6000),"stale direct internal data expires without a disconnect");
    remote->close();window.close();
}

static void remoteAi8FaultMetadata()
{
    using namespace VaporView;
    namespace Ai8 = Ai8TemperatureControllerProtocol;
    TcpTelemetryLink server;
    require(server.listen("127.0.0.1", 0), "AI8 fault fixture listens");
    MainWindow window;
    QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 1));
    auto *remote = window.testRemoteSkyController();
    require(remote->openTcp("127.0.0.1", server.localPort()) &&
                waitUntil([&] { return remote->isOpen(); }), "AI8 fault fixture opens remote telemetry");
    auto *overview = static_cast<Ground::Widgets::Ai8TemperatureOverviewPanel *>(
        window.findChild<QWidget *>(QStringLiteral("ai8TemperatureOverviewPanel")));
    auto *panel = window.findChild<Ground::Widgets::Ai8TemperatureControllerPanel *>();
    require(overview && panel, "AI8 remote diagnostics expose both temperature panels");
    overview->setEnglish(false);
    panel->setEnglish(false);
    panel->findChild<QSpinBox *>("ai8ChannelSpin")->setValue(1);
    const auto statuses = overview->findChildren<QLabel *>("ai8TemperatureOverviewStatusLabel");
    const auto values = overview->findChildren<QLabel *>("ai8TemperatureOverviewValueLabel");
    auto *alarm = panel->findChild<QLineEdit *>("ai8ChannelAlarmStatusEdit");
    require(statuses.size() == 8 && values.size() == 8 && alarm, "AI8 fault labels are available");
    TelemetryCodec encoder;
    quint16 sequence = 1;
    auto send = [&](MsgType type, const QByteArray& payload) {
        server.writeBytes(encoder.encodeFrame(type, payload, sequence++, 1));
    };
    TelemetryStatus status;
    DeviceStatusItem item;
    item.device_id = SkyDeviceId::Ai8TemperatureController;
    item.state = DeviceState::Connected;
    status.devices.append(item);
    send(MsgType::TelemetryStatus, TelemetryCodec::serializeTelemetryStatus(status));
    require(waitUntil([&] { return remote->deviceState(item.device_id) == DeviceState::Connected; }),
            "AI8 fault fixture receives initial device status");
    Ai8::LiveData fault;
    fault.measuredC.fill(std::numeric_limits<double>::quiet_NaN());
    fault.detailedChannel = 1;
    fault.setpointC = 25.0;
    fault.displayedSetpointC = 25.0;
    fault.controlRaw = 1;
    fault.alarmRaw = 1;
    fault.mainStatusValid = true;
    fault.mainStatusRaw = 0x214;
    send(MsgType::Ai8TemperatureControllerStatus, TelemetryCodec::serializeAi8TemperatureControllerStatus(fault));
    require(waitUntil([&] { return statuses[0]->text() == QStringLiteral("输入错误") &&
                alarm->text().contains(QStringLiteral("0x01")); }), "confirmed remote input fault is displayed");
    status.devices[0].state = DeviceState::Error;
    send(MsgType::TelemetryStatus, TelemetryCodec::serializeTelemetryStatus(status));
    require(waitUntil([&] { return remote->deviceState(item.device_id) == DeviceState::Error; }),
            "AI8 remote status reports its measurement error");
    require(statuses[0]->text() == QStringLiteral("输入错误") &&
                values[0]->text() == QStringLiteral("---") && alarm->text().contains(QStringLiteral("0x01")),
            "Error device status preserves newly confirmed input diagnostics without restoring a fault PV");
    Ai8::LiveData unknown;
    unknown.measuredC.fill(std::numeric_limits<double>::quiet_NaN());
    send(MsgType::Ai8TemperatureControllerStatus, TelemetryCodec::serializeAi8TemperatureControllerStatus(unknown));
    require(waitUntil([&] { return statuses[0]->text() == QStringLiteral("—") &&
                alarm->text() == QStringLiteral("---"); }), "unknown failed poll clears older confirmed fault metadata");
    fault.alarmRaw = 0;
    fault.mainStatusRaw = 0x100;
    send(MsgType::Ai8TemperatureControllerStatus, TelemetryCodec::serializeAi8TemperatureControllerStatus(fault));
    require(waitUntil([&] { return statuses[0]->text() == QStringLiteral("系统故障") &&
                statuses[0]->toolTip().contains(QStringLiteral("仪表系统故障")); }),
            "confirmed remote system fault is independent of channel input alarms");
    status.devices[0].state = DeviceState::Disconnected;
    send(MsgType::TelemetryStatus, TelemetryCodec::serializeTelemetryStatus(status));
    require(waitUntil([&] { return statuses[0]->text() == QStringLiteral("—"); }),
            "disconnect clears remote AI8 fault metadata");
    remote->close();
    window.close();
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
    QTemporaryDir calibrationRoot;check(calibrationRoot.isValid(),"isolated simulator calibration directory");
    QSettings native("VaporView","MainWindow");native.setValue("recording_directory",output.path());
    VaporView::applicationConfigSettings().remove("Fpga/desiredConfiguration");
    auto calibration=std::make_shared<VaporView::Ground::Calibration::CalibrationService>(calibrationRoot.path());
    calibration->synchronize();
    check(calibration->view().files.isEmpty()&&!calibration->view().selected.valid,
          "simulator recording starts with an empty calibration installation");
    MainWindow window(nullptr,calibration);window.resize(1600,1000);
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
    check(waitUntil([&]{
        const auto view=calibration->view();
        return view.files.size()==1&&view.selected.valid&&view.selected.project.origin==QStringLiteral("software_test");
    },20000),"simulator connection creates and selects a software calibration file");
    check(QFileInfo(calibration->view().selected.filename).absolutePath()==QFileInfo(calibrationRoot.filePath("calibrations")).absoluteFilePath(),
          "simulator calibration is stored in the isolated installation directory");
    acquisition->click();
    check(waitUntil([&]{return acquisition->isEnabled()&&page->findChild<QWidget*>("fpgaWavePlot48")->property("sampleCount").toInt()>0;}),"DLIA acquisition reaches the page");
    page->findChild<QAbstractButton*>("fpgaRawStart")->click();
    check(waitUntil([&]{return page->findChild<QWidget*>("fpgaWavePlot32")->property("sampleCount").toInt()>0;}),"RAW upload is active during recording verification");
    const auto originalFile = calibration->view().selected.filename;
    calibration->copyFile(originalFile, QStringLiteral("switched-for-recording"));
    calibration->synchronize();
    const auto available = calibration->view().files;
    check(available.size() == 2 && available.contains(originalFile), "alternate calibration is installed");
    const auto alternateFile = available[0] == originalFile ? available[1] : available[0];
    auto *record = page->findChild<QAbstractButton*>("fpgaRecord");
    record->click();
    auto *confirmation = recordingCalibrationDialog(window);
    check(confirmation, "new formal recording shows a calibration confirmation");
    auto *files = confirmation->findChild<QComboBox*>(QStringLiteral("recordingCalibrationFile"));
    auto *cancel = confirmation->findChild<QAbstractButton*>(QStringLiteral("recordingCalibrationCancel"));
    check(files && cancel, "confirmation has file selection and cancel controls");
    check(files->currentData().toString() == originalFile, "current calibration is selected in the dialog");
    const int alternateIndex = files->findData(alternateFile);
    check(alternateIndex >= 0, "confirmation lists alternate calibration");
    files->setCurrentIndex(alternateIndex);
    check(calibration->view().selected.filename == originalFile && !calibration->view().locked,
          "choosing an alternate file does not immediately select it or lock calibration");
    cancel->click();
    check(!recordingCalibrationDialog(window) && !calibration->view().locked &&
          calibration->view().selected.filename == originalFile &&
          !QDir(output.filePath("simulated_sessions")).exists(),
          "cancel leaves recording stopped, calibration unlocked and the original file selected");
    record->click();
    confirmation = recordingCalibrationDialog(window);
    check(confirmation, "the recording confirmation can be reopened after cancel");
    files = confirmation->findChild<QComboBox*>(QStringLiteral("recordingCalibrationFile"));
    check(files && files->findData(alternateFile) >= 0, "reopened confirmation lists alternate calibration");
    files->setCurrentIndex(files->findData(alternateFile));
    confirmRecordingCalibration(window);
    check(waitUntil([&]{return record->text().contains(QStringLiteral("停止"));}),"UI starts common session recording");
    check(calibration->view().selected.filename == alternateFile && calibration->view().locked,
          "recording locks the calibration file selected in the dialog");
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
    auto *homeRecording = static_cast<VaporView::Ground::Main::RecordingStatusView *>(window.findChild<QWidget *>("recordingStatusView"));
    check(homeRecording && homeRecording->statusText().contains(QStringLiteral("归一化峰值记录：%1").arg(metadata.metadata.waveformFrames)) &&
        homeRecording->statusText().contains(QStringLiteral("H1/H2 周期：%1").arg(metadata.metadata.waveformFrames)) &&
        !homeRecording->statusText().contains(QStringLiteral("仅远程有效")), "Home recording summary exposes real harmonic and normalized feature counts");
    window.close();
}

static void remoteRecordingCalibrationUi()
{
    using namespace VaporView;
    IndependentCalibrationTest::Rig calibration;
    const auto file = calibration.complete();
    TcpTelemetryLink server;
    require(server.listen(QStringLiteral("127.0.0.1"), 0), "manual recording fake Sky listens");
    MainWindow window(nullptr, calibration.service);
    require(QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 1)), "manual recording remote mode");
    window.show();
    auto *remote = window.testRemoteSkyController();
    require(remote && remote->openTcp(QStringLiteral("127.0.0.1"), server.localPort()) &&
            waitUntil([&] { return remote->isOpen(); }), "manual recording fake Sky connection");
    auto differentDevice = calibration.identity;
    differentDevice.insert(QStringLiteral("serial"), QStringLiteral("OTHER_TEST_DEVICE"));
    calibration.bus.setDevice(true, differentDevice);
    calibration.service->synchronize();
    QVector<CommandMessage> starts;
    TelemetryCodec codec;
    QObject::connect(&server, &TelemetryLink::bytesReceived, &window, [&](const QByteArray& bytes) {
        for (const auto& frame : codec.feedBytes(bytes)) {
            CommandMessage command;
            if (frame.type == MsgType::Command && TelemetryCodec::parseCommand(frame.payload, command) &&
                command.command_id == CommandId::StartRecording)
                starts.push_back(command);
        }
    });
    auto requestStart = [&] {
        require(QMetaObject::invokeMethod(&window, "onStartRecordingClicked", Qt::DirectConnection),
                "manual recording start is available");
    };
    requestStart();
    auto *dialog = recordingCalibrationDialog(window);
    require(dialog, "remote recording asks for confirmation");
    requestStart();
    require(window.findChildren<QDialog*>(QStringLiteral("recordingCalibrationDialog")).size() == 1,
            "repeated start does not open a second confirmation");
    auto *files = dialog->findChild<QComboBox*>(QStringLiteral("recordingCalibrationFile"));
    require(files && files->findData(file.filename) >= 0, "remote recording lists installed calibration");
    files->setCurrentIndex(files->findData(file.filename));
    processFor(30);
    require(starts.isEmpty() && !calibration.service->view().locked,
            "remote confirmation does not send or lock before acceptance");
    confirmRecordingCalibration(window);
    auto *status = dialog->findChild<QLabel*>(QStringLiteral("recordingCalibrationStatus"));
    auto *identity = dialog->findChild<QCheckBox*>(QStringLiteral("recordingCalibrationIdentityConfirmed"));
    require(status && identity && waitUntil([&] { return status->text().contains(QStringLiteral("身份不一致")); }) &&
            starts.isEmpty() && !calibration.service->view().selected.valid && !calibration.service->view().locked,
            "unconfirmed device mismatch keeps the popup open and recording stopped");
    identity->setChecked(true);
    confirmRecordingCalibration(window);
    require(waitUntil([&] { return starts.size() == 1; }), "remote start is sent once after confirmation");
    requestStart();
    require(!recordingCalibrationDialog(window), "remote start in flight cannot open another confirmation");
    const auto command = starts.front();
    const auto payload = QJsonDocument::fromJson(command.payload).object();
    require(payload.value("file_sha256").toString() == file.sha256 && calibration.service->view().locked,
            "remote StartRecording carries and locks the selected calibration snapshot");
    CommandAck ack;
    ack.command_id = command.command_id;
    ack.command_seq = command.command_seq;
    ack.correlation_id = command.correlation_id;
    ack.error_code = CommandErrorCode::InvalidPayload;
    server.writeBytes(codec.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack), 1, 1));
    require(waitUntil([&] { return !calibration.service->view().locked; }) && starts.size() == 1,
            "rejected remote start releases its calibration lock without a second command");
    remote->close();
}

static void scheduledRecordingCorrelation()
{
    using namespace VaporView;
    QTemporaryDir output;
    require(output.isValid(), "scheduled recording log directory");
    LogService logs(QStringLiteral("ScheduledRecordingTest"), nullptr, output.path(), output.path());
    IndependentCalibrationTest::Rig calibration;
    const auto file = calibration.complete();
    calibration.service->selectFile(file.filename);
    calibration.service->synchronize();
    TcpTelemetryLink server;
    require(server.listen(QStringLiteral("127.0.0.1"), 0), "scheduled fake Sky listens");
    MainWindow window(nullptr, calibration.service);
    require(QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 1)), "scheduled remote mode");
    auto *remote = window.testRemoteSkyController();
    require(remote && remote->openTcp(QStringLiteral("127.0.0.1"), server.localPort()) &&
                waitUntil([&] { return remote->isOpen(); }), "scheduled fake Sky connection");
    QVector<LogRecord> records;
    QVector<CommandMessage> commands;
    TelemetryCodec decoder;
    QObject::connect(&logs, &LogService::recordPublished, &window,
                     [&](const LogRecord& record) { records.push_back(record); }, Qt::DirectConnection);
    QObject::connect(&server, &TelemetryLink::bytesReceived, &window, [&](const QByteArray& bytes) {
        for (const auto& frame : decoder.feedBytes(bytes)) {
            CommandMessage command;
            if (frame.type == MsgType::Command && TelemetryCodec::parseCommand(frame.payload, command) &&
                (command.command_id == CommandId::StartRecording || command.command_id == CommandId::StopRecording))
                commands.push_back(command);
        }
    });
    require(MainWindowRecordingTestAccess::startScheduled(window), "scheduled remote recording starts");
    require(MainWindowRecordingTestAccess::stopScheduled(window), "scheduled remote recording stops");
    require(waitUntil([&] { return commands.size() == 2; }), "scheduled commands reach Sky TCP");
    for (int i = 0; i < 2; ++i) {
        const QString event = i == 0 ? QStringLiteral("scheduled_recording_start_command_sent")
                                     : QStringLiteral("scheduled_recording_stop_command_sent");
        bool matched = false;
        for (const auto& record : records) {
            if (record.fields.value(QStringLiteral("event")) == event)
                matched = record.fields.value(QStringLiteral("command_seq")).toUInt() == commands[i].command_seq &&
                          !record.correlation_id.isEmpty() && record.correlation_id == commands[i].correlation_id &&
                          record.session_id.isEmpty();
        }
        require(matched, "scheduled log retains transmitted command identity without inventing a session");
    }
    require(commands[0].correlation_id != commands[1].correlation_id, "scheduled operations use independent identities");
    remote->close();
}

// Opt in with --xsi-port <port> against an isolated VirtualLab instance.
static void liveVirtualFpga(quint16 port)
{
    using namespace VaporView::Ground::Devices;
    constexpr quint16 ai8 = 0x0046;
    constexpr quint32 shadow = 0x6650;
    {
        bool ready = false;
        MainWindow window(nullptr, {}, port);
        auto *controller = window.testFpgaController();
        auto *page = window.findChild<FpgaControlPage *>("fpgaControlPage");
        require(controller && page, "virtual FPGA window has a live controller and page");
        QObject::connect(controller, &FpgaDeviceController::connectionChanged, &window,
            [&](bool isReady, bool busy, const QString &detail) {
                ready = isReady && !busy;
                if (ready) std::cout << "VaporView ready: " << detail.toStdString() << '\n';
            });
        window.show();
        require(waitUntil([&] { return ready; }, 30000),
                "VaporView confirms FPGA ready after protocol and module validation");
        auto *backend = page->findChild<QComboBox *>("fpgaBackend");
        require(backend && !backend->isEnabled() && backend->currentData().toString() == QStringLiteral("virtual-tcp"),
                "ready window remains locked to the loopback backend");
    }

    FpgaDeviceSession session(makeFpgaVirtualTcpTransport(port));
    require(session.open(), "reconnect using only the virtual FPGA transport");
    quint32 completed = 0;
    bool ok = false;
    QByteArray response;
    QObject::connect(&session, &FpgaDeviceSession::commandCompleted, &session,
        [&](quint32 sequence, quint16 message, bool success, quint32 status, const QByteArray &payload) {
            completed = sequence;
            ok = success && status == 0;
            response = payload;
            std::cout << "VLP1 RX seq=" << sequence << " msg=0x" << std::hex << message << std::dec
                      << " status=" << status << " payload=" << payload.toHex(' ').constData() << '\n';
        });
    const auto acknowledged = [&](quint32 sequence) {
        return sequence && waitUntil([&] { return completed == sequence; }, 12000) && ok;
    };
    const auto read = [&](quint32 address, quint32 &value) {
        if (!acknowledged(session.readRegisters(address, 1, ai8)) || response.size() != 16) return false;
        value = qFromLittleEndian<quint32>(response.constData() + 12);
        return true;
    };
    require(acknowledged(session.ping("VaporView XSI")) && response.mid(4) == "VaporView XSI",
            "verify successful PING and exact echo");
    require(acknowledged(session.requestCapabilities()) && session.capabilities().valid,
            "verify capabilities response");
    quint32 id = 0, original = 0, readback = 0, restored = 0;
    require(read(0x6600, id) && id == 0x00460200 && read(shadow, original), "verify AI-8 identity and read original shadow");
    require(qint32(original) >= -9990 && qint32(original) <= 32000, "verify shadow raw range");
    const quint32 candidate = original == 400 ? 401 : 400;
    // The shadow write uses zero flags and leaves the apply/commit register untouched.
    const bool changed = acknowledged(session.writeRegisters(shadow, {candidate}, 0, ai8)) &&
        read(shadow, readback) && readback == candidate;
    const bool recovered = acknowledged(session.writeRegisters(shadow, {original}, 0, ai8)) &&
        read(shadow, restored) && restored == original;
    require(recovered, "verify shadow restoration even if the candidate check failed");
    require(changed, "verify the changed shadow readback");
    std::cout << "AI-8 0x6650: " << qint32(original) << " -> " << readback << " -> " << qint32(restored) << '\n';
}

static void liveVirtualFpgaAcquisition(quint16 port)
{
    using VaporView::Ground::Devices::FpgaDeviceController;
    using VaporView::FpgaWave::CompletedStream;
    constexpr int operationTimeoutMs = 60000;
    const auto check = [](bool ok, const char *message) {
        if (!ok) throw std::runtime_error(message);
    };
    QTemporaryDir output(QDir::temp().filePath("vaporview-xsi-acquisition-XXXXXX"));
    check(output.isValid(), "create isolated XSI recording directory");
    output.setAutoRemove(false);
    std::cout << "XSI artifacts " << output.path().toStdString() << std::endl;
    QSettings native("VaporView", "MainWindow");
    native.setValue("recording_directory", output.path());

    unsigned rawLanes = 0, dliaLanes = 0;
    QJsonObject deviceIdentity;
    QMap<quint32, quint32> registers;
    bool ready = false, busy = true;
    auto calibration = std::make_shared<VaporView::Ground::Calibration::CalibrationService>(output.path());
    MainWindow window(nullptr, calibration, port);
    auto *controller = window.testFpgaController();
    auto *page = window.findChild<FpgaControlPage *>("fpgaControlPage");
    check(controller && page, "find native FPGA controller and page");
    auto *backend = page->findChild<QComboBox *>("fpgaBackend");
    check(backend && !backend->isEnabled() && backend->currentData().toString() == "virtual-tcp",
          "require pinned virtual transport before sending commands");
    QObject::connect(controller, &FpgaDeviceController::connectionChanged, &window,
                     [&](bool isReady, bool isBusy, const QString &detail) {
        ready = isReady;
        busy = isBusy;
        std::cout << "XSI controller ready=" << ready << " busy=" << busy << ' '
                  << detail.toStdString() << std::endl;
    });
    QObject::connect(controller, &FpgaDeviceController::hardwareValuesChanged, &window,
                     [&](const QMap<quint32, quint32> &values) { registers = values; });
    QObject::connect(controller, &FpgaDeviceController::snapshot, &window,
                     [&](quint64, const QJsonObject &snapshot) {
        if (snapshot.value("event") == "config" && snapshot.value("ready").toBool()) {
            const auto diagnostic = snapshot.value("usb").toObject();
            deviceIdentity = {{"transport", "fpga_virtual_tcp"}, {"backend", diagnostic.value("mode")},
                              {"locator", QString()}, {"usb", diagnostic}};
        }
    });
    QObject::connect(controller, &FpgaDeviceController::waveformUpdated, &window,
                     [&](const CompletedStream &stream) {
        if (stream.source < 0x20 || stream.source > 0x21 || !stream.totalPoints ||
            (stream.signed32Samples.isEmpty() && stream.unsigned32Samples.isEmpty())) return;
        const unsigned lane = 1u << (stream.source - 0x20);
        if (!(rawLanes & lane))
            std::cout << "RTL RAW source=" << stream.source << " points=" << stream.totalPoints << std::endl;
        rawLanes |= lane;
    });
    QObject::connect(controller, &FpgaDeviceController::harmonicCycleReady, &window,
                     [&](quint64, const CompletedStream &stream) {
        if (stream.source < 0x30 || stream.source > 0x31 || !stream.complete || stream.dliaPoints.isEmpty() ||
            !stream.dliaPoints.front().hasHarmonics) return;
        const unsigned lane = 1u << (stream.source - 0x30);
        if (!(dliaLanes & lane))
            std::cout << "RTL DLIA source=" << stream.source << " points=" << stream.totalPoints << std::endl;
        dliaLanes |= lane;
    });
    window.show();
    const auto idle = [&] { return ready && !busy; };
    const auto reg = [&](quint32 address) { return registers.value(address); };
    check(waitUntil(idle, operationTimeoutMs), "wait for native FPGA handshake");
    QMetaObject::invokeMethod(controller, &FpgaDeviceController::snapshotConfiguration, Qt::QueuedConnection);
    check(waitUntil([&] { return !deviceIdentity.isEmpty(); }), "read virtual endpoint diagnostic identity");
    check(waitUntil([&] { return calibration->view().connected; }), "calibration service sees virtual FPGA");
    check(deviceIdentity.value("backend") == "virtual-tcp", "verify reported calibration device is virtual TCP");
    // Recording requires a valid two-wavelength calibration. Only the fixture's
    // calibration coefficients are synthetic. All recorded waveforms come from RTL.
    IndependentCalibrationTest::Rig fixture;
    auto project = fixture.complete().project;
    project.deviceIdentity = deviceIdentity;
    for (auto &stage : project.stages)
        for (auto &laneSamples : stage.samples)
            for (auto &sample : laneSamples) sample.deviceIdentity = deviceIdentity;
    VaporView::Ground::Calibration::CalibrationFile file;
    QString calibrationError;
    const bool generated = VaporView::Ground::Calibration::CalibrationFileStore(output.path()).generate(
        project, QStringLiteral("XSI TEST ONLY"), file, &calibrationError);
    if (!generated) throw std::runtime_error(calibrationError.toStdString());
    calibration->selectFile(file.filename);
    calibration->synchronize();
    check(calibration->view().selected.valid, "select isolated test calibration for native recording");

    // Match the short continuous scan exercised by the Lab's RTL regression.
    auto config = page->configuration();
    for (int lane = 0; lane < 2; ++lane) {
        config.wms[lane].scanMilliHz = 2000000;
        config.dlia[lane].outputRateHz = 100000;
        config.dlia[lane].fragmentPoints = 17;
    }
    emit page->applyWmsRequested(config);
    check(waitUntil([&] { return idle() && reg(0x2010) == 2000000 && reg(0x2110) == 2000000; }, operationTimeoutMs),
          "verify WMS scan frequency readback");
    emit page->applyDliaRequested(config);
    check(waitUntil([&] { return idle() && reg(0x504c) == 100000 && reg(0x514c) == 100000; }, operationTimeoutMs),
          "verify active DLIA rate readback");
    // The native acquisition action leaves independent DAC enables untouched.
    for (int lane = 0; lane < 2; ++lane) {
        const quint32 control = 0x3004 + 0x100 * lane;
        emit page->dacRequested(lane, true);
        check(waitUntil([&] { return idle() && (reg(control) & 1); }, operationTimeoutMs),
              "enable virtual DAC for native WMS cycle timing");
    }
    emit page->acquisitionRequested(true);
    check(waitUntil([&] { return idle() && (reg(0x4004) & 1) && (reg(0x4104) & 1); }, operationTimeoutMs),
          "start native acquisition and verify both ADCs enabled");

    auto &state = MainWindowRtcmTestAccess::state(window);
    check(bool(state.recording_service_), "find native session recording service");
    const auto status = [&] { return state.recording_service_->status(); };
    QTimer recordingDialogWatch;
    QObject::connect(&recordingDialogWatch, &QTimer::timeout, &window, [] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            std::cerr << "XSI recording warning: " << box->text().toStdString() << std::endl;
            box->reject();
        }
    });
    recordingDialogWatch.start(100);
    emit page->recordingRequested(true);
    check(waitUntil([&] { return recordingCalibrationDialog(window) != nullptr; }, 5000),
          "open native recording calibration confirmation");
    auto *confirmation = recordingCalibrationDialog(window);
    const auto *files = confirmation->findChild<QComboBox*>(QStringLiteral("recordingCalibrationFile"));
    check(files && files->currentData() == file.filename,
          "recording confirmation selects the isolated calibration file");
    confirmRecordingCalibration(window);
    if (!waitUntil([&] { return status().active; }, 5000)) {
        if (const auto *dialog = recordingCalibrationDialog(window)) {
            if (const auto *message = dialog->findChild<QLabel*>(QStringLiteral("recordingCalibrationStatus")))
                std::cerr << "XSI recording confirmation: " << message->text().toStdString() << std::endl;
        }
        throw std::runtime_error("start native recording");
    }
    recordingDialogWatch.stop();
    const auto directory = status().sessionDirectory;
    std::cout << "XSI recording " << directory.toStdString() << std::endl;
    auto finishRecording = qScopeGuard([&] {
        if (status().sessionOpen) emit page->recordingRequested(false);
    });
    dliaLanes = 0;
    emit page->rawRequested(true);
    check(waitUntil([&] { return rawLanes == 3 && dliaLanes == 3 && status().waveformFrames >= 2; }, operationTimeoutMs),
          "receive RAW and complete DLIA on both lanes and record harmonic cycles");
    emit page->recordingRequested(false);
    check(waitUntil([&] { return !status().sessionOpen; }, operationTimeoutMs), "stop and drain native recording");
    finishRecording.dismiss();

    QFile calibrationSnapshot(QDir(directory).filePath(QStringLiteral("calibration_snapshot.json")));
    check(calibrationSnapshot.open(QIODevice::ReadOnly), "read persisted recording calibration snapshot");
    const auto snapshotJson = QJsonDocument::fromJson(calibrationSnapshot.readAll()).object();
    check(snapshotJson.value(QStringLiteral("file_sha256")).toString() == file.sha256,
          "recording persists the confirmed calibration digest");
    const auto metadata = VaporView::Ground::SessionLoader::loadMetadata(directory);
    check(metadata.success && metadata.metadata.waveformFrames >= 2, "load persisted harmonic session metadata");
    const auto catalog = VaporView::Ground::SessionWaveformRepository::loadCatalog(metadata.metadata);
    check(catalog.success && VaporView::Ground::SessionWaveformRepository::readFrame(catalog.catalog, 0).success,
          "decode persisted harmonic waveform");
    std::cout << "XSI persisted waveform frames=" << metadata.metadata.waveformFrames << std::endl;

    check(waitUntil(idle, operationTimeoutMs), "wait for RAW enable to settle before stopping acquisition");
    emit page->acquisitionRequested(false);
    check(waitUntil([&] { return idle() && !(reg(0x4004) & 1) && !(reg(0x4104) & 1); }, operationTimeoutMs),
          "stop native acquisition and verify both ADCs disabled");
    for (int lane = 0; lane < 2; ++lane) {
        const quint32 offset = 0x100 * lane;
        emit page->waveformRequested(lane, false);
        check(waitUntil([&] { return idle() && !(reg(0x2004 + offset) & 1); }, operationTimeoutMs),
              "disable the virtual WMS lane");
        emit page->dacRequested(lane, false);
        check(waitUntil([&] { return idle() && !(reg(0x3004 + offset) & 1); }, operationTimeoutMs),
              "disable the virtual DAC lane");
    }
}

static void tec103PidLayout()
{
    QSettings native("VaporView", "MainWindow");
    native.setValue("font_scale_percent", 80);
    MainWindow window;
    auto *stack = window.findChild<QStackedWidget *>("mainPageStack");
    auto *page = window.findChild<QWidget *>("temperaturePage");
    auto *transport = window.findChild<QComboBox *>("deviceSensorTransportPathCombo");
    require(stack && page && transport, "TEC103 temperature page and transport selector exist");
    QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 0));
    const int relayIndex = transport->findData("fpga_relay");
    require(relayIndex >= 0, "FPGA relay transport is available");
    transport->setCurrentIndex(relayIndex);
    auto *device = page->findChild<QAbstractButton *>("tec103DeviceSwitch");
    auto *pages = page->findChild<QStackedWidget *>("temperatureChannelStack");
    require(device && pages, "TEC103 device selector and panels exist");
    window.resize(1600, 1000);
    window.show();
    stack->setCurrentWidget(page);
    QApplication::setActiveWindow(&window);

    const auto checkPidEditors = [&](int deviceIndex) {
        QCoreApplication::processEvents();
        require(device->isVisible() && pages->currentIndex() == deviceIndex, "Expected TEC103 device page is visible");
        const auto editor = [&](const char *name) {
            return pages->currentWidget()->findChild<QWidget *>(QString::fromLatin1(name).arg(deviceIndex + 1));
        };
        auto *p = qobject_cast<QSpinBox *>(editor("temperaturePidKpSpinChannel%1"));
        require(p && p->isVisible(), "TEC103 P editor is visible");
        const QList<QWidget *> editors{p, editor("temperaturePidKiSpinChannel%1"),
                                      editor("temperaturePidKdSpinChannel%1"), editor("temperatureOutputModeComboChannel%1"),
                                      editor("temperatureTargetSpinChannel%1"), editor("temperatureMaxOutputSpinChannel%1")};
        const int height = p->height();
        const auto sameHeight = [&] {
            return std::all_of(editors.cbegin(), editors.cend(), [height](QWidget *field) {
                return field && field->isVisible() && field->height() == height;
            });
        };
        require(sameHeight(), "All TEC103 common parameter editors start with the same height");
        auto *common = editor("temperatureChannelCommonParamsPageChannel%1");
        auto *maxOutput = editors.last();
        require(common && common->isAncestorOf(maxOutput), "The maximum output editor belongs to common parameters");
        for (QWidget *ancestor = maxOutput->parentWidget(); ancestor && ancestor != page;
             ancestor = ancestor->parentWidget())
        {
            const QRect editorRect(maxOutput->mapTo(ancestor, QPoint()), maxOutput->size());
            const bool controlsCard = ancestor->objectName() == QStringLiteral("temperatureControllerControlsCard");
            if (controlsCard || ancestor == common || qobject_cast<QStackedWidget *>(ancestor))
            {
                const QRect bounds = controlsCard ? ancestor->rect().adjusted(0, 0, 0, -2) : ancestor->rect();
                require(bounds.contains(editorRect), "The maximum output editor is fully visible inside the card and stacks");
            }
        }
        auto *mode = page->findChild<QComboBox *>("temperatureControllerModeCombo");
        auto *view = page->findChild<QToolButton *>("tec103PlotViewToggle");
        require(mode && mode->isVisible() && view && view->isVisible() && mode->parentWidget() == view->parentWidget() &&
                    mode->geometry().right() < view->geometry().left() &&
                    std::abs(mode->geometry().center().y() - view->geometry().center().y()) <= 1,
                "TEC103 plot toggle sits beside the controller mode combo");
        p->setFocus(Qt::TabFocusReason);
        QCoreApplication::processEvents();
        require(p->hasFocus() && sameHeight(), "Focusing P preserves the TEC103 PID editor heights");

        QStyleOptionSpinBox option;
        option.initFrom(p);
        option.subControls = QStyle::SC_All;
        option.buttonSymbols = p->buttonSymbols();
        option.frame = p->hasFrame();
        const QPoint arrow = p->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, p).center();
        QMouseEvent move(QEvent::MouseMove, QPointF(arrow), QPointF(p->mapToGlobal(arrow)),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(p, &move);
        QCoreApplication::processEvents();
        require(p->property("spinArrowHover").toString() == QStringLiteral("up") && sameHeight(),
                "Hovering the P arrow preserves the TEC103 PID editor heights");

        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(p, &leave);
        p->clearFocus();
        QCoreApplication::processEvents();
        require(sameHeight(), "Leaving the P editor preserves the TEC103 PID editor heights");
        return height;
    };

    const int compactHeight = checkPidEditors(0);
    device->click();
    require(checkPidEditors(1) == compactHeight, "Both TEC103 devices use the same height at 80% scale");
    device->click();
    MainWindowRtcmTestAccess::state(window).font_normal_action_->trigger();
    const int normalHeight = checkPidEditors(0);
    device->click();
    require(checkPidEditors(1) == normalHeight && normalHeight > compactHeight,
            "Both TEC103 devices use the same larger editor height at 100% scale");
    device->click();
}

int main(int argc,char **argv)
{
    require(VaporViewTest::SettingsSandbox::initialized,"isolated native/INI application settings");
#ifdef Q_OS_WIN
    bool localSimulator=false;
    for(int i=1;i<argc;++i) localSimulator|=QByteArray(argv[i])=="--local-simulator" || QByteArray(argv[i])=="--tec103-pid-layout";
    // Native theme queries can block on a non-input Windows test desktop. Set
    // the override before QApplication so the app's QProxyStyle also uses Fusion.
    // Other modes retain Windows 11 style coverage, including the font regression.
    if(localSimulator) qputenv("QT_STYLE_OVERRIDE","Fusion");
#endif
    QApplication app(argc,argv);
#ifdef Q_OS_WIN
    auto *windowsStyle=QStyleFactory::create(localSimulator ? "Fusion" : "windows11");
    require(windowsStyle,localSimulator ? "Fusion style is available for the simulator integration test"
                                      : "Windows 11 style is available for font regression");
    QApplication::setStyle(windowsStyle);
#endif
    app.setOrganizationName(QStringLiteral("VaporViewFpgaTest"));
    app.setApplicationName(QStringLiteral("fpga_main_window_test"));
    const auto args = app.arguments();
    const int xsiPortIndex = args.indexOf(QStringLiteral("--xsi-port"));
    const bool xsiAcquisition = args.contains(QStringLiteral("--xsi-acquisition"));
    require(!xsiAcquisition || xsiPortIndex >= 0, "--xsi-acquisition requires --xsi-port");
    if (xsiPortIndex >= 0) {
        bool valid = false;
        const quint16 port = args.value(xsiPortIndex + 1).toUShort(&valid);
        require(valid && port, "--xsi-port requires an isolated VirtualLab loopback port");
        if (xsiAcquisition) {
            try { liveVirtualFpgaAcquisition(port); }
            catch (const std::exception &error) {
                std::cerr << "XSI acquisition failed: " << error.what() << '\n';
                return 1;
            }
        } else {
            liveVirtualFpga(port);
        }
        return 0;
    }
    QSettings native("VaporView","MainWindow");
    native.setValue("developer_options_enabled",true);
    if(app.arguments().contains("--rtcm-context")) { rtcmCompletionContext(); scheduledRecordingCorrelation(); return 0; }
    native.setValue("font_scale_percent",100);
    native.setValue("app_sidebar_width",62);
    if(args.contains("--tec103-pid-layout")) { tec103PidLayout(); return 0; }
    if(app.arguments().contains("--epsilon-packets")) {epsilonMixedPacketPresentation();return 0;}
    if(app.arguments().contains("--internal-direct")) {directInternalEnvironmentRouting();return 0;}
    if(app.arguments().contains("--ai8-fault-status")) {remoteAi8FaultMetadata();return 0;}
    if(app.arguments().contains("--relay-configuration")) {
        relayConfigurationPresentation();std::cout<<"FPGA relay configuration test passed\n";return 0;
    }
    if(app.arguments().contains("--remote-home-actions")) {
        remoteHomeActions();std::cout<<"Remote FPGA home actions test passed\n";return 0;
    }
    if(app.arguments().contains("--configuration-action-readback")) {
        configurationActionReadback();std::cout<<"FPGA configuration action readback test passed\n";return 0;
    }
    if(app.arguments().contains("--remote-page-routing")) {
        remotePageRouting();std::cout<<"FPGA remote page routing test passed\n";return 0;
    }
    if(app.arguments().contains("--transport-capabilities")) {
        remoteTransportAndCapabilities();std::cout<<"Transport capabilities test passed\n";return 0;
    }
    if(app.arguments().contains("--monitor-ui-actions")) {
        simulatedMonitorActions();std::cout<<"Simulated FPGA monitor actions test passed\n";return 0;
    }
    if(app.arguments().contains("--harmonic-recording")) {
        try { harmonicRecordingUi();remoteRecordingCalibrationUi();std::cout<<"Harmonic recording UI test passed\n";return 0; }
        catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
    }
    if(app.arguments().contains("--harmonic-home")) {
        harmonicHomeGraphs();std::cout<<"FPGA harmonic home graphs test passed\n";return 0;
    }
    if(app.arguments().contains("--local-simulator")) {
        try { localSimulatorRouting();std::cout<<"Local FPGA simulator GUI test passed\n";return 0; }
        catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
    }
    if(app.arguments().contains("--refresh-budget")) {
        simulatorRefreshBudget();std::cout<<"FPGA refresh budget test passed\n";return 0;
    }
    QElapsedTimer phases;phases.start();
    const auto phase=[&](const char *name) { std::cout<<"FPGA_TEST_PHASE elapsed_ms="<<phases.elapsed()<<" phase="<<name<<std::endl; };
    phase("epsilon_packets");epsilonMixedPacketPresentation();
    phase("initial_window");
    const auto standardFont=app.font();
    QPointer<QThread> worker;
    {
        MainWindow window;
        auto *stack=window.findChild<QStackedWidget*>(QStringLiteral("mainPageStack"));
        auto *page=window.findChild<FpgaControlPage*>(QStringLiteral("fpgaControlPage"));
        require(stack && stack->count()==6 && page && stack->widget(4)==page,"FPGA remains the fifth standalone persistent page");
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
        window.show();page->findChild<QTabWidget*>("fpgaTabs")->setCurrentIndex(0);processFor(100);
        verifyConnectionCard(page,100);
        verifyCardMargins(window,page,100);
        auto handler=qInstallMessageHandler(captureFont);
        VaporView::FpgaWave::CompletedStream wave;wave.source=0x30;wave.message=0x1001;wave.format=0;wave.bytesPerPoint=8;wave.totalPoints=2;wave.rate=10000;wave.complete=true;wave.pointBytes=QByteArray(16,0);
        page->setConnectionState(true,false);
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
    phase("scaled_windows");
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
    if(app.arguments().contains("--window-layout-only")) {
        require(native.value("font_scale_percent").toInt()==100,"font scale stays at standard 100 percent");
        std::cout<<"FPGA main window layout tests passed\n";return 0;
    }
    phase("relay_presentation");relayConfigurationPresentation();
    phase("remote_page");remotePageRouting();
    phase("remote_ai8_fault");remoteAi8FaultMetadata();
    phase("remote_home");remoteHomeActions();
    phase("transport_capabilities");remoteTransportAndCapabilities();
    phase("finished");
    require(native.value("font_scale_percent").toInt()==100,"font scale stays at standard 100 percent");
    std::cout<<"fpga main window tests passed\n";
}
