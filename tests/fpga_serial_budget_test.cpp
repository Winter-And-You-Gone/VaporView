#include "FpgaSerialBudget.h"
#include "FpgaTelemetry.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <cstdlib>
#include <iostream>
namespace {
void require(bool condition,const char *message) {
    if(!condition){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    using namespace VaporView;
    FpgaSerialBudget budget;budget.reset(115200,0);
    require(budget.capacity()==2880 && budget.maximumFrameBytes()==1440,"25 percent of 8N1 payload budget");
    require(budget.accept(1440,0) && budget.accept(1440,0) && !budget.accept(1,0),"bounded one second burst");
    require(!budget.accept(289,100) && budget.accept(288,100),"refill obeys theoretical diagnostic bytes per second");
    require(!budget.accept(1441,2000),"single diagnostic frame is bounded independently of elapsed time");
    budget.reset(115200,0);
    require(budget.accept(400,0,0x44) && !budget.accept(400,999,0x44) && budget.accept(400,1000,0x44),"per sensor maximum one Hz");
    budget.reset(115200,0);
    require(budget.accept(1440,0) && budget.accept(1440,0) && !budget.accept(400,0,0x44),"budget exhaustion skips sensor");
    require(budget.accept(400,1000,0x44),"skipped sensor does not poison successful send timestamp");
    require(FpgaSerialBudget::maximumPendingBytes(115200)==23040 &&
        FpgaSerialBudget::maximumPendingBytes(9600)==16384,"serial queue cap derived from baud with minimum");
    require(FpgaSerialBudget::queueFits(115200,23000,40) && !FpgaSerialBudget::queueFits(115200,23000,41) &&
        !FpgaSerialBudget::queueFits(115200,0,23041),"pending plus incoming queue boundary enforced");
    FpgaWave::CompletedStream wave;wave.source=0x30;wave.message=0x1001;wave.schema=2;wave.format=2;wave.bytesPerPoint=24;
    wave.totalPoints=1024;wave.rate=10000;wave.preview=true;wave.previewStride=16;wave.complete=true;
    for(int i=0;i<64;++i) wave.dliaPoints.append({-2147483647,2147483647,-2147483647,2147483647,-2147483647,2147483647,true,true});
    const auto ipcPayload=FpgaRemote::encodeWaveformPreview(wave);
    const auto serialPayload=budget.previewPayload(ipcPayload);
    FpgaWave::CompletedStream serial;
    require(!serialPayload.isEmpty() && serialPayload.size()+21<=1440 &&
        FpgaRemote::parseWaveformPreview(serialPayload,serial),"115200 preview fits single frame budget");
    require(serial.dliaPoints.size()==8 && serial.previewStride==128 && serial.totalPoints==1024 && serial.rate==10000,
        "serial decimation composes stride and preserves native rate and original count");
    FpgaWave::CompletedStream ipc;
    require(FpgaRemote::parseWaveformPreview(ipcPayload,ipc) && ipc.dliaPoints.size()==64 && ipc.previewStride==16 &&
        wave.previewStride==16,"input and IPC full preview remain unchanged");
    budget.reset(921600,0);
    require(budget.previewPayload(ipcPayload)==ipcPayload,"high baud retains 64 point preview");
    budget.reset(9600,0);
    require(budget.previewPayload(ipcPayload).isEmpty(),"skip whole preview when even eight points do not fit");
    std::cout<<"fpga_serial_budget_test passed\n";
}
