#include "test_settings_sandbox.h"
#include "IndependentCalibrationFixture.h"
#include "SkyConfig.h"
#include "ground/devices/RemoteSkyController.h"
#include "ground/calibration/RecordedConcentrations.h"
#include "shared/session/CalibrationRuntime.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QTcpServer>
#include <QThread>
#include <stdexcept>

using namespace IndependentCalibrationTest;
namespace {
void require(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
struct SkyProcess : QProcess {
    ~SkyProcess() {if(state()!=NotRunning){kill();waitForFinished(5000);}}
};
template<class Predicate> bool waitFor(Predicate predicate,SkyProcess& sky,int timeout=8000) {
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents,10);
        if(predicate())return true;
        if(sky.state()==QProcess::NotRunning)break;
        QThread::msleep(1);
    }
    return predicate();
}
void exercise() {
    Rig ground;const auto file=ground.complete();QString error;
    ground.service->selectFile(file.filename);ground.service->synchronize();
    QJsonObject full;require(ground.service->prepare(full,&error),qPrintable(error));
    QJsonObject second;require(!ground.service->prepare(second,&error),"prepared-only Ground lock rejects another Session");
    auto remote=full;remote.remove("vcal_document");
    QTemporaryDir root;require(root.isValid(),"isolated Sky installation");
    const auto configPath=root.filePath(QStringLiteral("中文天空配置.json"));
    require(SkyConfig::defaults().saveToFile(configPath,&error),qPrintable(error));
    QTcpServer reservation;require(reservation.listen(QHostAddress::LocalHost,0),"reserve local port");
    const auto port=reservation.serverPort();reservation.close();
    SkyProcess sky;
    sky.setProgram(QCoreApplication::applicationDirPath()+"/VaporViewSkyCore.exe");
    sky.setArguments({"--fpga-simulate-data","--sky-config",configPath,"--telemetry-transport","tcp",
        "--telemetry-host","127.0.0.1","--telemetry-tcp-port",QString::number(port),"--sky-wave-port","9","--no-ipc"});
    sky.setProcessChannelMode(QProcess::MergedChannels);
    QByteArray output;
    QObject::connect(&sky,&QProcess::readyRead,[&]{output+=sky.readAll();if(output.size()>65536)output=output.right(65536);});
    Ground::Devices::RemoteSkyController controller;QHash<quint16,CommandAck> acks;bool discardAck=false;
    TelemetryStatus status;
    QObject::connect(&controller,&Ground::Devices::RemoteSkyController::commandAckReceived,
        [&](const CommandAck& ack){if(!discardAck)acks.insert(ack.command_seq,ack);});
    QObject::connect(&controller,&Ground::Devices::RemoteSkyController::statusUpdated,[&](const TelemetryStatus& value){status=value;});
    auto launch=[&]{sky.start();require(sky.waitForStarted(5000),"launch independent SkyCore process");};
    auto connect=[&]{require(waitFor([&]{if(!controller.isOpen())controller.openTcp("127.0.0.1",port);return controller.isOpen();},sky),"TCP telemetry connects");};
    auto command=[&](CommandId id,const QJsonObject& snapshot=QJsonObject{}) {
        const auto seq=controller.sendCommand(id,snapshot.isEmpty()?QByteArray{}:QJsonDocument(snapshot).toJson(QJsonDocument::Compact));
        require(waitFor([&]{return acks.contains(seq);},sky),"command receives ACK");return acks.take(seq).error_code;
    };
    const auto base=root.filePath("simulated_sessions");const auto lock=QDir(base).filePath(".calibration-session-lock.json");
    launch();connect();
    require(command(CommandId::StartRecording)!=CommandErrorCode::Ok,"Sky independently rejects missing calibration while Ground is prepared");
    require(ground.service->view().locked&&!QFile::exists(lock),"one-end preparation remains explicit and never creates a Sky Session");
    // Drop the Start ACK at the receiving endpoint, then interrupt the transport
    // after the persistent Sky lock proves the command was committed.
    discardAck=true;controller.sendCommand(CommandId::StartRecording,QJsonDocument(remote).toJson(QJsonDocument::Compact));
    require(waitFor([&]{return QFile::exists(lock);},sky),"Sky commits Session before simulated ACK loss");
    controller.close();discardAck=false;
    const auto locked=QJsonDocument::fromJson(read(lock)).object();const auto session=locked["session_directory"].toString();
    require(ground.service->view().locked,"lost ACK and transport cannot unlock Ground");
    require(ground.service->bindSession(QDir(ground.root.path()).filePath("remote_session"),&error),"Ground binds its own Session without changing Sky identity");
    connect();require(waitFor([&]{return status.recording_state==1;},sky),"reconnect observes original Sky recording");
    require(command(CommandId::PauseRecording)==CommandErrorCode::Ok,"pause over real transport");
    auto alternate=remote;auto runtime=alternate["runtime"].toObject();auto channels=runtime["channels"].toObject();
    auto lane=channels["816"].toObject();lane["a"]=lane["a"].toDouble()*2;channels["816"]=lane;runtime["channels"]=channels;
    runtime.remove("sha256");runtime["sha256"]=QString::fromLatin1(Session::runtimeCalibrationDigest(runtime));alternate["runtime"]=runtime;
    require(command(CommandId::StartRecording,alternate)==CommandErrorCode::InvalidPayload,"paused Sky refuses alternate validly hashed coefficients");
    require(command(CommandId::StartRecording,remote)==CommandErrorCode::Ok,"resume preserves original parameters");
    require(QJsonDocument::fromJson(read(lock)).object()==locked,"pause and resume preserve persistent lock identity");
    // Abrupt process loss, not a recorder destructor or a graceful Stop command.
    controller.close();sky.kill();require(sky.waitForFinished(5000),"killed SkyCore is reaped");
    require(QFile::exists(lock)&&ground.service->view().locked,"both persistent locks survive Sky process failure");
    status={};launch();connect();require(waitFor([&]{return status.recording_state==2;},sky),"restarted Sky reports recovered pause");
    require(command(CommandId::StartRecording,remote)!=CommandErrorCode::Ok,"restart cannot silently bridge missing writer interval");
    require(command(CommandId::StopRecording)==CommandErrorCode::Ok&&!QFile::exists(lock),"explicit Stop clears recovered Sky lock");
    require(ground.service->view().locked,"Sky Stop alone does not silently clear Ground lock");
    ground.service->finish();require(!ground.service->view().locked,"explicit Ground completion releases its lock");
    require(command(CommandId::StartRecording)!=CommandErrorCode::Ok,"new Session requires fresh parameters");
    require(QFile::remove(file.filename),"delete original selected file");
    const auto history=Ground::Calibration::readRecordedConcentrations(QDir(session).filePath("calibration_snapshot.json"));
    require(history.success,"Sky history validates independently of deleted original and current Ground selection");
    write(file.filename,QJsonDocument(file.document).toJson(QJsonDocument::Indented));
    QJsonObject changed;require(!ground.service->prepare(changed,&error)&&error.contains(QStringLiteral("变化")),"external byte-only file modification fails selected SHA check");
    controller.close();sky.write("quit\n");sky.closeWriteChannel();require(sky.waitForFinished(5000),"SkyCore exits after fault tests");
    require(sky.exitStatus()==QProcess::NormalExit&&sky.exitCode()==0,"clean final Sky process exit");
    std::cout<<"SOFTWARE_SIMULATION_ONLY: independent SkyCore/TCP lock, ACK loss, pause replacement, process restart, SHA and history faults passed.\n";
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    try {exercise();return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
