#include "shared/session/FpgaSessionArchive.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QFileInfo>
#include <QtEndian>
#include <exception>

namespace VaporView::Session {
namespace {
QByteArray parserFrameBytes(QByteArrayView bytes)
{
    QByteArray result(bytes.data(),bytes.size());
    // The shared codec's Frame::bytes deliberately excludes zero word padding.
    // Normalize only complete decoded-frame records; exact USB evidence is never changed.
    if(bytes.size()>=44 && bytes.first(4)==QByteArrayView("VLP1",4)) {
        const auto total=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.data())+8);
        if(total==quint64(bytes.size()) && total<=FpgaVlp1::kMaxFrame) {
            while(result.size()%4)result.append('\0');
        }
    }
    return result;
}
QJsonObject payloadDescription(const FpgaVlp1::Frame& frame)
{
    QJsonObject decoded;
    const auto& payload=frame.payload;
    if(payload.size()<4)return decoded;
    const auto *p=reinterpret_cast<const uchar*>(payload.data());
    const quint16 schema=qFromLittleEndian<quint16>(p);
    decoded.insert(QStringLiteral("schema"),int(schema));
    if(frame.header.message==0x1100 && frame.header.source>=0x40 && frame.header.source<=0x46) {
        const quint16 count=qFromLittleEndian<quint16>(p+2);
        QJsonArray fields;
        size_t offset=4;
        for(quint16 i=0;i<count;++i) {
            if(offset+4>payload.size()){decoded.insert(QStringLiteral("malformed"),true);break;}
            const auto tag=qFromLittleEndian<quint16>(p+offset);
            const quint8 type=p[offset+2],length=p[offset+3];
            const size_t padded=(size_t(length)+3)&~size_t(3);
            if(offset+4+padded>payload.size()){decoded.insert(QStringLiteral("malformed"),true);break;}
            QJsonObject field{{QStringLiteral("tag"),int(tag)},{QStringLiteral("type"),int(type)},
                              {QStringLiteral("bytes"),QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(p+offset+4),length).toHex())}};
            if(length==4 && (type==3 || type==7)) {
                const quint32 raw=qFromLittleEndian<quint32>(p+offset+4);
                const qint64 value=type==7?qint64(qint32(raw)):qint64(raw);
                field.insert(QStringLiteral("raw_value"),QString::number(value));
                if(tag==0x10){field.insert(QStringLiteral("temperature_c"),double(value)/1000.0);}
                if(tag==0x11){field.insert(QStringLiteral("humidity_percent"),double(value)/1000.0);}
                if(tag==0x12){field.insert(QStringLiteral("pressure_pa"),double(value)/1000.0);}
                if(tag==0x13){field.insert(QStringLiteral("distance_mm"),double(value));}
            }
            fields.append(field);offset+=4+padded;
        }
        decoded.insert(QStringLiteral("tlv"),fields);
    } else if((frame.header.message==0x1000 || frame.header.message==0x1001) && payload.size()>=20) {
        const bool adc=frame.header.message==0x1000;
        decoded.insert(adc?QStringLiteral("adc_bits"):QStringLiteral("format"),int(qFromLittleEndian<quint16>(p+2)));
        decoded.insert(QStringLiteral("rate_hz"),QString::number(qFromLittleEndian<quint32>(p+4)));
        decoded.insert(QStringLiteral("total_points"),QString::number(qFromLittleEndian<quint32>(p+8)));
        if(adc)decoded.insert(QStringLiteral("format"),int(qFromLittleEndian<quint32>(p+12)));
        if(schema==2 && payload.size()>=32) {
            const int fragment=adc?16:12;
            decoded.insert(QStringLiteral("fragment_index"),int(qFromLittleEndian<quint16>(p+fragment)));
            decoded.insert(QStringLiteral("fragment_count"),int(qFromLittleEndian<quint16>(p+fragment+2)));
            decoded.insert(QStringLiteral("first_point"),QString::number(qFromLittleEndian<quint32>(p+(adc?20:16))));
            decoded.insert(QStringLiteral("fragment_points"),QString::number(qFromLittleEndian<quint32>(p+(adc?24:20))));
        }
    }
    return decoded;
}
}
QString FpgaSessionArchive::rawPath(const QString& directory)
{ return QDir(directory).filePath(QStringLiteral("raw/fpga_vlp1.dat")); }

QJsonObject FpgaSessionArchive::describe(quint64 timestamp, FpgaArchiveKind kind, QByteArrayView bytes)
{
    QJsonObject result{{QStringLiteral("host_timestamp_us"), QString::number(timestamp)},
                       {QStringLiteral("kind"), int(kind)},
                       {QStringLiteral("wire_hex"), QString::fromLatin1(QByteArray(bytes.data(), bytes.size()).toHex())}};
    if (kind == FpgaArchiveKind::Snapshot) {
        result.insert(QStringLiteral("snapshot"), QJsonDocument::fromJson(QByteArray(bytes.data(),bytes.size())).object());
        return result;
    }
    if (kind == FpgaArchiveKind::UsbBytes) return result;
    bool valid = false;
    try {
        FpgaVlp1::StreamParser parser;
        const QByteArray parserBytes=parserFrameBytes(bytes);
        auto frames = parser.feed(reinterpret_cast<const std::uint8_t*>(parserBytes.data()), parserBytes.size());
        auto tail = parser.finish();
        frames.insert(frames.end(),tail.begin(),tail.end());
        valid = frames.size()==1 && parser.discardedBytes()==0;
        if (valid) result.insert(QStringLiteral("payload_hex"), QString::fromLatin1(
            QByteArray(reinterpret_cast<const char*>(frames[0].payload.data()), frames[0].payload.size()).toHex()));
        if (valid) result.insert(QStringLiteral("decoded_payload"),payloadDescription(frames[0]));
    } catch (const std::exception&) { valid = false; }
    result.insert(QStringLiteral("crc_valid"),valid);
    // Retain header metadata for corrupt packets as evidence, without treating them as samples.
    if (bytes.size() >= 40 && bytes.first(4) == QByteArrayView("VLP1",4)) {
        const auto *p = reinterpret_cast<const uchar*>(bytes.data());
        result.insert(QStringLiteral("sequence"), QString::number(qFromLittleEndian<quint32>(p+12)));
        result.insert(QStringLiteral("source"), int(qFromLittleEndian<quint16>(p+16)));
        result.insert(QStringLiteral("message"), int(qFromLittleEndian<quint16>(p+18)));
        result.insert(QStringLiteral("flags"), QString::number(qFromLittleEndian<quint32>(p+20)));
        result.insert(QStringLiteral("fpga_tick"), QString::number(qFromLittleEndian<quint64>(p+24)));
        result.insert(QStringLiteral("cycle"), QString::number(qFromLittleEndian<quint32>(p+32)));
        result.insert(QStringLiteral("frame_type"), int(p[6]));
    }
    return result;
}

SessionRawDat::RawScanResult FpgaSessionArchive::scan(const QString& directory,
                                                     const SessionRawDat::RawScanOptions& requested)
{
    QFile file(rawPath(directory));
    if (!file.open(QIODevice::ReadOnly)) {
        SessionRawDat::RawScanResult result; result.error=file.errorString(); return result;
    }
    auto options=requested; options.expectedSourceId=kFpgaRawSource;
    return SessionRawDat::scan(file,options);
}

namespace {
bool visitIndex(const QString& directory, const SessionRawDat::RawScanResult& index,
                const FpgaSessionArchive::Visitor& visitor, QString *error)
{
    QFile file(FpgaSessionArchive::rawPath(directory));
    if (!file.open(QIODevice::ReadOnly)) { if(error)*error=file.errorString(); return false; }
    for (const auto& record:index.records) {
        if (!file.seek(record.payloadOffset)) { if(error)*error=file.errorString(); return false; }
        const QByteArray bytes=file.read(record.header.payloadSize);
        if (bytes.size()!=record.header.payloadSize) { if(error)*error=QStringLiteral("Incomplete FPGA record"); return false; }
        if (!visitor(record.header,bytes)) { if(error)*error=QStringLiteral("FPGA operation cancelled"); return false; }
    }
    return true;
}

}

bool FpgaSessionArchive::visit(const QString& directory,const Visitor& visitor,QString *error)
{
    const auto index=scan(directory);
    if (!index.success()) { if(error)*error=index.error; return false; }
    return visitIndex(directory,index,visitor,error);
}

bool FpgaSessionArchive::replay(const QString& directory,const FrameVisitor& visitor,QString *error,
                              const ResetVisitor& resetVisitor,const std::function<bool()>& isCancelled)
{
    SessionRawDat::RawScanOptions scanOptions;
    scanOptions.isCancelled=isCancelled;
    const auto index=scan(directory,scanOptions);
    if(!index.success()){if(error)*error=index.error;return false;}
    bool usb=false;
    for(const auto& r:index.records) if(r.header.recordType==quint16(FpgaArchiveKind::UsbBytes)) usb=true;
    FpgaVlp1::StreamParser parser;
    try {
        return visitIndex(directory,index,[&](const auto& h,const QByteArray& bytes){
            if(isCancelled && isCancelled())return false;
            if(h.recordType==quint16(FpgaArchiveKind::Snapshot)) {
                const auto snapshot=QJsonDocument::fromJson(bytes).object();
                const QString event=snapshot.value(QStringLiteral("event")).toString();
                if(event==QStringLiteral("connected") || event==QStringLiteral("disconnected") ||
                   event==QStringLiteral("reconnect") || event==QStringLiteral("pause") || event==QStringLiteral("resume")) {
                    parser=FpgaVlp1::StreamParser();
                    if(resetVisitor)resetVisitor(h.hostTimestampUs,snapshot);
                }
                return true;
            }
            if(h.recordType!=quint16(usb?FpgaArchiveKind::UsbBytes:FpgaArchiveKind::Frame)) return true;
            const QByteArray parserBytes=usb?bytes:parserFrameBytes(bytes);
            const auto frames=parser.feed(reinterpret_cast<const std::uint8_t*>(parserBytes.constData()),parserBytes.size());
            for(const auto& frame:frames) if(!visitor(h.hostTimestampUs,frame)) return false;
            return true;
        },error);
    } catch(const std::exception& e) { if(error)*error=QString::fromUtf8(e.what());return false; }
}

bool FpgaSessionArchive::exportTo(const QString& directory,const QString& filename,ExportFormat format,QString *error,
                                const std::function<bool()>& isCancelled)
{
    if (QFileInfo(filename).absoluteFilePath().compare(QFileInfo(rawPath(directory)).absoluteFilePath(), Qt::CaseInsensitive)==0) {
        if(error)*error=QStringLiteral("Export must not overwrite the source archive"); return false;
    }
    SessionRawDat::RawScanOptions scanOptions;
    scanOptions.isCancelled=isCancelled;
    const auto index=scan(directory,scanOptions);
    if(!index.success()){
        if(error)*error=index.status==SessionRawDat::RawReadStatus::Cancelled
            ?QStringLiteral("FPGA export cancelled"):index.error;
        return false;
    }
    bool usb=false; for(const auto& r:index.records) if(r.header.recordType==quint16(FpgaArchiveKind::UsbBytes))usb=true;
    QSaveFile output(filename);
    if(!output.open(QIODevice::WriteOnly)){if(error)*error=output.errorString();return false;}
    if(format==ExportFormat::Csv && output.write("host_timestamp_us,kind,source,message,sequence,fpga_tick,cycle,flags,crc_valid,wire_hex,decoded_payload,snapshot_json\n")<0)return false;
    if(format==ExportFormat::Json && output.write("[\n")<0)return false;
    bool first=true;
    bool cancelled=false;
    const bool ok=visitIndex(directory,index,[&](const auto& h,const QByteArray& bytes){
        if(isCancelled && isCancelled()){cancelled=true;return false;}
        QByteArray row;
        if(format==ExportFormat::Bin) {
            if(h.recordType!=quint16(usb?FpgaArchiveKind::UsbBytes:FpgaArchiveKind::Frame))return true;
            row=bytes;
        } else {
            const auto object=describe(h.hostTimestampUs,FpgaArchiveKind(h.recordType),bytes);
            if(format==ExportFormat::Json) { row=(first?QByteArray():QByteArray(",\n"))+QJsonDocument(object).toJson(QJsonDocument::Compact);first=false; }
            else {
                for(const QString key:{QStringLiteral("host_timestamp_us"),QStringLiteral("kind"),QStringLiteral("source"),QStringLiteral("message"),QStringLiteral("sequence"),QStringLiteral("fpga_tick"),QStringLiteral("cycle"),QStringLiteral("flags"),QStringLiteral("crc_valid"),QStringLiteral("wire_hex"),QStringLiteral("decoded_payload"),QStringLiteral("snapshot")}) {
                    if(!row.isEmpty())row+=',';
                    const auto value=object.value(key);
                    QByteArray cell=value.isObject()?QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact):value.toVariant().toString().toUtf8();
                    cell.replace("\"","\"\"");row+='\"';row+=cell;row+='\"';
                } row+='\n';
            }
        }
        return output.write(row)==row.size();
    },error);
    if(!ok){
        if(cancelled){output.cancelWriting();if(error)*error=QStringLiteral("FPGA export cancelled");}
        return false;
    }
    if(format==ExportFormat::Json && output.write("\n]\n")!=3)return false;
    // A late interruption must not atomically replace an existing destination.
    if(isCancelled && isCancelled()){
        output.cancelWriting();if(error)*error=QStringLiteral("FPGA export cancelled");return false;
    }
    if(!output.commit()){if(error)*error=output.errorString();return false;}
    return true;
}
}
