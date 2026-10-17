#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace VaporView::FpgaExtensions {
constexpr quint32 RawSchema2 = 1u;
constexpr quint32 Ai8Scan = 2u;
constexpr quint32 Ai8Parameters = 4u;
constexpr quint32 EpsilonMailbox = 8u;
constexpr quint32 Rd105 = 16u;
constexpr quint32 DliaCutoff = 32u;
inline bool sensor(quint16 source) { return (source >= 0x40 && source <= 0x46) || source == 0x48; }
inline bool cutoff(quint32 hz) {
    return hz==100 || hz==250 || hz==500 || hz==1000 || hz==2000 || hz==5000 || hz==10000;
}
inline bool rtcmFrame(const QByteArray& bytes) {
    if(bytes.size()<6 || bytes.size()>1029 || quint8(bytes[0])!=0xd3 || (quint8(bytes[1])&0xfc)
        || (((quint8(bytes[1])&3)<<8)|quint8(bytes[2]))+6!=bytes.size())return false;
    quint32 crc=0;
    for(int i=0;i<bytes.size()-3;++i) {
        crc^=quint32(quint8(bytes[i]))<<16;
        for(int j=0;j<8;++j) {crc<<=1;if(crc&0x1000000)crc^=0x1864cfb;}
    }
    const int n=int(bytes.size());
    return (crc&0xffffff)==((quint32(quint8(bytes[n-3]))<<16)|(quint32(quint8(bytes[n-2]))<<8)|quint8(bytes[n-1]));
}
}
