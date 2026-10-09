#include "FpgaVlp1.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace VaporView::FpgaVlp1;
static void check(bool x,const char *m){if(!x){std::cerr<<"FAIL: "<<m<<'\n';std::exit(1);}}
int main(){
    check(crc32(reinterpret_cast<const std::uint8_t*>("123456789"),9)==0xcbf43926u,"CRC vector");
    auto ping=buildPing(1,{ 'P','I','N','G' });
    check(ping.size()==48 && ping[8]==48 && ping[40]=='P' && ping[43]=='G',"PING layout and padding");
    auto read=buildReadReg(2,0,5); check(read.size()==52 && read[18]==2 && read[40]==0 && read[44]==5,"READ layout");
    auto write=buildWriteReg(3,0x6004,{1}); check(write.size()==56 && write[18]==3 && write[44]==1,"WRITE layout");
    StreamParser parser; std::vector<Frame> frames; for(std::size_t i=0;i<ping.size();i+=3){auto n=parser.feed(ping.data()+i,std::min<std::size_t>(3,ping.size()-i));frames.insert(frames.end(),n.begin(),n.end());} check(frames.size()==1 && frames[0].header.sequence==1 && frames[0].payload.size()==4,"incremental parse");
    auto bad=ping; bad[12]^=1; auto good=buildGetCapabilities(9); std::vector<std::uint8_t> stream={0xaa,0x55}; stream.insert(stream.end(),bad.begin(),bad.end()); stream.insert(stream.end(),good.begin(),good.end()); auto got=parser.feed(stream); check(got.size()==1 && got[0].header.sequence==9,"bad frame one-byte resync");
    auto padded=buildPing(4,{1,2,3},1); padded.back()=7; StreamParser p2; check(p2.feed(padded).empty(),"reject nonzero padding");
    ResponseMatch key{9,1,1};
    auto response = buildFrame(FrameType::Response, 9, 1, 1, {0, 0, 0, 0});
    auto responseFrames = parser.feed(response);
    check(responseFrames.size() == 1 && matchesResponse(responseFrames[0],key),"response match type/tuple");
    bool threw=false; try{buildReadReg(1,2,1);}catch(const std::invalid_argument&){threw=true;} check(threw,"reject unaligned register");
    std::cout<<"fpga_vlp1_test passed\n";
}
