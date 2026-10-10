#include "FpgaUsbTransport.h"
#include "FpgaCyApiTransport.h"
#include <QCoreApplication>
#include <cstdlib>
#include <iostream>
using namespace VaporView::Ground::Devices;
static void require(bool v,const char *why) { if(!v) { std::cerr<<why<<'\n'; std::exit(1); } }
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    auto replay=makeFpgaUsbReplayTransport(QByteArray::fromHex("0102030405060708"));
    QByteArray b;
    require(replay->read(b,4)==-1,"closed replay read");
    require(replay->open(),"replay opens");
    require(replay->read(b,3)==3 && b.toHex()=="010203","USB read boundaries may split VLP headers");
    require(replay->read(b,99)==5 && b.toHex()=="0405060708","remaining read bytes preserved");
    require(replay->write(QByteArray::fromHex("010203"))==-1,"bulk write alignment");
    require(replay->write(QByteArray::fromHex("01020304"))==4,"aligned command write");
    require(replay->diagnostic().bytesWritten==4 && replay->diagnostic().bytesRead==8,"transfer accounting");
    require(!replay->readDiagnostic(0xb1,b) && b.isEmpty(),"replay never fabricates GP01 identity");
    replay->close(); require(!replay->isOpen(),"replay closes");
    auto unknown=makeFpgaUsbHardwareTransport(QStringLiteral("unsupported"));
    require(!unknown->open() && !unknown->isOpen(),"unknown backend rejected");
    require(!unknown->diagnostic().hardwareBackendAvailable,"unknown backend cannot claim hardware availability");
#if !defined(Q_OS_WIN) || !defined(VAPORVIEW_HAS_CYAPI)
    auto cypress=makeFpgaUsbHardwareTransport(QStringLiteral("cypress"));
    require(!cypress->diagnostic().hardwareBackendAvailable,"SDK-free Cypress unavailable diagnostic");
    require(!cypress->open() && cypress->read(b,32)==-1 && cypress->write(QByteArray(4,char(0)))==-1,"SDK-free Cypress never sends fake data");
    require(!cypress->readDiagnostic(0xb1,b) && b.isEmpty(),"SDK-free identity is not fabricated");
#endif
    std::cout<<"fpga USB transport tests passed\n";
}
