#include "Tec103GpioProtocol.h"
#include <QCoreApplication>
#include <QList>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace VaporView;
namespace Protocol = VaporView::Tec103GpioProtocol;

static void require(bool condition,const char* detail)
{
    if(!condition) {std::cerr<<detail<<'\n';std::exit(1);}
}

struct Instrument {
    Protocol::Values values{{"TEC",103},{"FPV",4370},{"ADDRESS",1},{"TC1:TG",2730000},
        {"TC1:PWMDUTY",-40000},{"TC1:ENABLE",1},{"TC1:MODE",0},{"ERRORCODE",0},
        {"TC1:LIMITED",18},{"TC1:KP",200},{"TC1:KI",100},{"TC1:KD",0},
        {"TC1:RP",10000},{"TC1:BX",393000},{"TC1:PT1000RP",1000000},
        {"TC1:PTA",3908300},{"TC1:PTB",-5775000},{"TC1:PTC",-4183000},
        {"TC1:OVERTEMPUP",500000000},{"TC1:OVERTEMPLOWER",-300000000},{"TC1:SPEED",0},
        {"TC1:POLYOMIAL",0},{"TC1:AUTOPID",0},{"TC1:STARTUPDELAY",3},{"CONTMODE",0},{"OVERTTEMP",1}};
    QList<QPair<int,QByteArray>> requests;
    qint64 actual=2512345;
    bool ignoreWrite=false,failWrite=false,badBase64=false,echoWrite=false,errorWrite=false,nackWrite=false;

    QJsonObject operator()(const QJsonObject& request)
    {
        require(request.value("op")=="uart_exchange","protocol uses raw UART exchange only");
        const int channel=request.value("channel").toInt();
        require(channel==1 || channel==2,"physical UART selection is explicit");
        const auto command=QByteArray::fromBase64(request.value("data").toString().toLatin1());
        require(!command.contains("TC2:"),"internal channel two is never queried or written");
        requests.append({channel,command});
        QByteArray response;
        if(command=="DATADEMAND=1@") {
            response="TC1:TCADJTEMP="+QByteArray::number(actual)+"@TC1:RESISTOR=11000000000@TC1:PWM=1800@"
                "TC2:TCADJTEMP=0@TC2:RESISTOR=0@TC2:PWM=0@SINTERIORTEMP=34@\r\n";
        } else if(command=="INQUIRE=1@") {
            for(const auto key:{"TEC","TC1:TG","TC1:ENABLE","TC1:LIMITED","TC1:MODE","TC1:KP","TC1:KI","TC1:KD",
                "TC1:RP","TC1:BX","TC1:PT1000RP","TC1:PTA","TC1:PTB","TC1:PTC","TC1:OVERTEMPUP","TC1:OVERTEMPLOWER","TC1:SPEED"}) {
                response+="OK"+QByteArray(key)+'='+QByteArray::number(values.value(key))+'@';
                if(QByteArray(key).startsWith("TC1:"))response+="TC2:"+QByteArray(key).mid(4)+"=0@";
                response.append('\0');
            }
        } else {
            const auto equals=command.indexOf('=');
            require(equals>0 && command.endsWith('@'),"ASCII command syntax");
            const auto key=command.left(equals),value=command.mid(equals+1,command.size()-equals-2);
            require(values.contains(key),"test instrument supports only explicit fields");
            qint64 number=values.value(key);
            if(value!="?") {
                if(failWrite)return {{"ok",false},{"detail","simulated write response timeout"}};
                bool ok=false;number=value.toLongLong(&ok);require(ok,"integer write value");
                if(!ignoreWrite)values[key]=number;
            }
            response="OK:"+key+'='+QByteArray::number(number)+"@\r\n";
            if(value!="?" && echoWrite)response=command;
            if(value!="?" && errorWrite)response="ERRORCODE=1@";
            if(value!="?" && nackWrite)response+="NACK=1@";
        }
        response.append('\0');
        return {{"ok",true},{"data",badBase64?QStringLiteral("!"):QString::fromLatin1(response.toBase64())}};
    }
};

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    Protocol::Values fields;QString error;
    const char identityReply[]="OKTEC=103@\r\n\0OK:FPV=4370@\r\n\0";
    const QByteArray colonReply(identityReply,qsizetype(sizeof(identityReply)-1));
    require(Protocol::parseValues(colonReply,fields,&error) && fields.value("TEC")==103 && fields.value("FPV")==4370,
        "actual colon/NUL identity format parses");
    require(!Protocol::parseValues("OKTC1:TG=1@TC1:TG=2@",fields,&error),"conflicting or duplicated fields are rejected");
    require(!Protocol::parseValues("TC1:TG=?@",fields,&error),"command echo is not a numeric response");
    require(!Protocol::parseValues("OKFPV=999999999999999999999999@",fields,&error),"integer overflow is rejected");
    require(!Protocol::parseValues("ERRORCODE=bad@",fields,&error),"malformed error code cannot be silently skipped");
    require(!Protocol::parseValues("OKTC1:TG=1@ERRORCODE=bad@",fields,&error),"a valid field cannot mask a malformed field");
    require(!Protocol::parseValues("OKERRORCODE=0@trailing",fields,&error),"unterminated garbage cannot become a successful response");
    require(!Protocol::parseValues("OKTC1:TG=1@NACK=1@",fields,&error),"explicit negative acknowledgment wins over a positive-looking field");

    Instrument instrument;
    const Protocol::Call call=[&](const QJsonObject& request){return instrument(request);};
    const auto full=Protocol::read(call,2,true);
    require(full.success && full.data.valid && full.channelValid[1] && !full.channelValid[0],"logical two maps physical UART two");
    require(std::isnan(full.data.channels[0].measured_temperature_c) && !full.data.channels[0].output_enabled,
        "the other physical instrument is not fabricated from internal TC2 placeholders");
    const auto& channel=full.data.channels[1];
    require(std::abs(channel.target_temperature_c-27.3)<1e-9 && std::abs(channel.measured_temperature_c-25.12345)<1e-9,
        "temperature units preserve five decimal places");
    require(channel.output_percent==-2.0 && channel.sensor_resistance_ohm==11000.0 && channel.output_enabled,
        "duty comes from PWMDUTY instead of PWM and resistance uses native micro-ohms");
    require(channel.kp==200 && channel.ki==100 && channel.max_output_percent==18 && channel.ntc_b==393000 && channel.ntc_r0==10000,
        "full configuration decodes the selected instrument parameters");
    require(channel.overtemp_upper_c==5000 && channel.overtemp_lower_c==-3000 && channel.pt_b==-5775000,
        "protection temperature units and legacy PT coefficient raw values are preserved");
    require(full.data.device_address==1 && full.data.rs485_baud_index==-1,"TTL transport baud does not fabricate physical RS485 settings");
    for(const auto& request:instrument.requests)require(request.first==2 &&
        (request.second.endsWith("=?@") || request.second=="DATADEMAND=1@" || request.second=="INQUIRE=1@"),
        "full snapshot is read-only and targets one physical port");

    instrument.requests.clear();
    auto light=Protocol::read(call,1,false);
    require(light.success && instrument.requests.size()==6 && light.channelValid[0] && !light.channelValid[1],"light poll has a fixed read-only query set");
    instrument.actual=999999999;
    light=Protocol::read(call,1,false);
    require(light.success && !light.data.valid && !light.channelValid[0] && std::isnan(light.data.channels[0].measured_temperature_c)
        && light.data.channels[0].output_enabled,"invalid temperature cannot hide an enabled physical output");
    instrument.actual=2512345;

    instrument.requests.clear();
    auto write=Protocol::writeVerified(call,2,{{"TC1:TG",-1234567}});
    require(write.success && !write.unverified && instrument.values.value("TC1:TG")==-1234567,"explicit user setpoint has independent exact readback");
    int writes=0,readbacks=0;
    for(const auto& request:instrument.requests) {
        require(request.first==2,"write stays on selected physical UART");
        if(request.second=="TC1:TG=-1234567@")++writes;
        if(request.second=="TC1:TG=?@")++readbacks;
    }
    require(writes==1 && readbacks==1,"setpoint is sent once and separately queried once");
    instrument.ignoreWrite=true;instrument.requests.clear();
    write=Protocol::writeVerified(call,1,{{"TC1:TG",3000000}});
    require(!write.success && write.unverified,"ACK without actual readback equality does not confirm a write");
    writes=0;for(const auto& request:instrument.requests)if(request.second=="TC1:TG=3000000@")++writes;
    require(writes==1,"mismatched readback never resends the write");
    instrument.ignoreWrite=false;instrument.failWrite=true;instrument.requests.clear();
    write=Protocol::writeVerified(call,1,{{"TC1:ENABLE",0}});
    require(!write.success && write.unverified && instrument.requests.size()==3,"missing write response remains unknown without retry");
    instrument.failWrite=false;instrument.requests.clear();
    for(const auto& setting:QVector<Protocol::Setting>{{"TC2:TG",2500000},{"ADDRESS",2},{"RESET",1},{"TC1:PWMDUTY",1},{"TC1:TG",100000001},{"CONTMODE",0},{"OVERTTEMP",1}})
        require(!Protocol::writeVerified(call,1,{setting}).success,"unsupported operations reject before transport");
    require(instrument.requests.isEmpty(),"invalid or maintenance writes never reach the instrument");
    require(!Protocol::read(call,3,false).success && instrument.requests.isEmpty(),"invalid logical channel sends no command");
    for(int mode=0;mode<3;++mode) {
        instrument.echoWrite=mode==0;instrument.errorWrite=mode==1;instrument.nackWrite=mode==2;
        instrument.requests.clear();
        write=Protocol::writeVerified(call,1,{{"TC1:TG",2800000}});
        require(!write.success && write.unverified && instrument.requests.size()==3,
            "echo, error-field response and NACK cannot confirm a write or trigger retries");
    }
    instrument.echoWrite=instrument.errorWrite=instrument.nackWrite=false;
    instrument.badBase64=true;
    require(!Protocol::read(call,1,false).success,"malformed UART transport data cannot become a valid measurement");
    std::cout<<"TEC103 GPIO protocol tests passed\n";
}
