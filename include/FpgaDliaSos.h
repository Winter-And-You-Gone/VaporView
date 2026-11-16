#pragma once

#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <array>
#include <cmath>

namespace VaporView::FpgaDliaSos {
constexpr quint32 Api = 0x534f5301;
constexpr quint32 PpsApi = 0x50505301;
constexpr qint64 One = qint64(1) << 46;
using Section = std::array<qint64,5>; // b0,b1,b2,a1,a2; denominator signs unchanged

inline qint64 roundEven(double x)
{
    const double low=std::floor(x),fraction=x-low;
    return qint64(low)+(fraction>0.5 || (fraction==0.5 && (qint64(low)&1)));
}

// Bilinear transform of the analog Butterworth poles. Match the firmware's
// reference generator: low-Q sections first, odd-order single pole last,
// per-section unity DC gain after Q2.46 quantization.
inline QVector<quint32> design(int order, quint32 fs, quint32 fc)
{
    if(order<2 || order>8 || !fc || quint64(fc)*2>=fs)return {};
    const double pi=std::acos(-1.0),k=std::tan(pi*double(fc)/fs);
    QVector<Section> sections;
    for(int i=0;i<order/2;++i) {
        const double damping=std::sin(pi*(2*i+1)/(2*order));
        const double denominator=1+2*damping*k+k*k;
        const qint64 a1=roundEven(2*(k*k-1)/denominator*One);
        const qint64 a2=roundEven((1-2*damping*k+k*k)/denominator*One);
        const qint64 dc=One+a1+a2,b0=roundEven(double(dc)/4);
        sections.append({b0,dc-2*b0,b0,a1,a2});
    }
    std::sort(sections.begin(),sections.end(),[](const auto& a,const auto& b){return a[4]<b[4];});
    if(order&1) {
        const qint64 a1=roundEven((k-1)/(k+1)*One),dc=One+a1,b0=roundEven(double(dc)/2);
        sections.append({b0,dc-b0,0,a1,0});
    }
    while(sections.size()<4)sections.append({One,0,0,0,0});
    QVector<quint32> words(30,0);
    int byte=0;
    for(const auto& s:sections) {
        if(One+s[3]+s[4]<=0 || One-s[3]+s[4]<=0 || One-s[4]<=0)return {};
        for(qint64 coefficient:s) {
            if(coefficient<-(qint64(1)<<47) || coefficient>=(qint64(1)<<47))return {};
            for(int n=0;n<6;++n,++byte)
                words[byte/4]|=quint32((quint64(coefficient)>>(8*n))&255)<<(8*(byte%4));
        }
    }
    return words;
}
}
