#pragma once
#include <QVector>
#include <QString>
#include <QJsonObject>
#include <cmath>

namespace VaporView::FpgaRemote {
struct SensorParameter { quint16 source; const char *key; quint32 address, minimum, maximum, fallback; const char *label; };
inline const QVector<SensorParameter>& sensorParameters() {
    static const QVector<SensorParameter> specs{
        {0x40,"format",0x6014,0,255,72,"UART 格式码 (数据位/校验/停止位)"},
        {0x40,"mode",0x601c,0,1,0,"输出模式 (0 轮询 / 1 连续)"},
        {0x40,"timeout",0x602c,1,3600000,1000,"响应超时 / ms"},
        {0x41,"address",0x6114,1,247,240,"Modbus 地址"},
        {0x41,"measurements",0x6120,1,3,3,"测量掩码 (1 湿度 / 2 温度)"},
        {0x42,"filterLow",0x6214,0,0xffffffff,0xffffffff,"消息过滤掩码低 32 位"},
        {0x42,"filterHigh",0x6218,0,0xffffffff,0xffffffff,"消息过滤掩码高 32 位"},
        {0x42,"rawForward",0x621c,0,1,1,"转发原始观测"},
        {0x43,"address",0x6310,0x76,0x77,0x77,"I²C 地址 (118=0x76 / 119=0x77)"},
        {0x43,"oversampling",0x6318,0,45,0,"OSR 码 (压力低 3 位 / 温度高 3 位)"},
        {0x43,"odr",0x631c,0,17,0,"ODR 码"},
        {0x43,"iir",0x6320,0,14,0,"IIR 码 (偶数)"},
        {0x43,"power",0x6324,0,51,3,"电源模式码"},
        {0x44,"repeatability",0x6418,0,2,0,"重复精度 (0 高 / 1 中 / 2 低)"},
        {0x44,"heater",0x641c,0,6,0,"加热模式 (0 关闭)"},
        {0x45,"mode",0x6514,0,2,1,"测距模式 (0 待机 / 1 LF / 2 HF)"},
        {0x45,"timeout",0x653c,1,0xffffffff,100000000,"接收超时 / 10 ns"},
        {0x45,"silence",0x6544,1,0xffffffff,10000000,"静默时间 / 10 ns"},
        {0x45,"gap",0x6548,1,0xffffffff,1000000,"帧间隔 / 10 ns"},
        {0x45,"scale",0x654c,1,1000,1,"LF 毫米 / 计数"},
        {0x45,"invalidMask",0x6550,0,255,0,"LF 无效状态掩码"},
        {0x45,"versionCommand",0x6558,0,255,0,"版本查询命令码"},
        {0x48,"address",0x6814,1,247,2,"RD105 Modbus 地址 (共用 RS485 默认 2)"},
        {0x48,"channel",0x681c,1,2,1,"RD105 测量通道"}
    };return specs;
}
inline bool validSensorParameters(quint16 source,const QJsonObject& values) {
    for(auto it=values.begin();it!=values.end();++it) {
        const SensorParameter *spec=nullptr;
        for(const auto& s:sensorParameters())if(s.source==source && it.key()==QLatin1String(s.key)){spec=&s;break;}
        if(!spec || !it.value().isDouble())return false;
        const double v=it.value().toDouble();
        if(!std::isfinite(v) || v!=std::floor(v) || v<spec->minimum || v>spec->maximum)return false;
        const quint32 u=quint32(v);
        if(source==0x40 && it.key()=="format" && !((u%16==7 || u%16==8) && ((u>>4)&3)<=2 && ((u>>6)==1 || (u>>6)==2)))return false;
        if(source==0x43 && ((it.key()=="oversampling" && ((u&7)>5 || (u>>3)>5)) || (it.key()=="iir" && (u&1)) || (it.key()=="power" && (u&~0x33u))))return false;
    }return true;
}
}
