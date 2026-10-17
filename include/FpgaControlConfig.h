#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QMap>
#include <QString>
#include <array>

// Desired configuration only. Live register values and enable states belong to
// the device session, never to these persisted defaults.
struct FpgaControlConfig
{
    struct Wms {
        quint32 scanMilliHz = 100000, sineMilliHz = 20000000, updateRateHz = 500000;
        qint32 scanAmplitude = 107374182, sineAmplitude = 21474836, bias = 0;
        quint32 phase = 0, phaseMode = 2;
    };
    struct Dlia {
        quint32 referenceMilliHz = 20000000, phase1 = 0, phase2 = 0;
        quint32 outputRateHz = 10000, mode = 3, fragmentPoints = 256, cutoffHz = 1000;
    };
    struct Frontend { double voltsPerCode = 10.0 / 8388608.0; int polarity = 1; bool calibrated = false; };
    struct Dac {
        quint32 deviceCtrl = 0x312, clearCode = 0x80000, minCode = 0, maxCode = 0xfffff;
        quint32 spiClockHz = 20000000, gain = 0x40000000;
        qint32 offset = 0;
    };
    struct Ai8 { quint32 slaveAddress = 1, channel = 1, pollIntervalMs = 1000, timeoutMs = 200, retryLimit = 1; bool scan = false; };
    quint32 adcRateHz = 1000000, adcCnvTicks = 2, adcBusyTimeoutTicks = 100;
    std::array<quint32,2> adcExpectedPoints{{0,0}};
    QMap<quint16,QJsonObject> sensorParameters;
    QString backend = QStringLiteral("auto");
    std::array<Wms, 2> wms;
    std::array<Dlia, 2> dlia;
    std::array<Frontend, 2> frontend;
    std::array<Dac, 2> dac;
    Ai8 ai8;
    // Legacy settings compatibility. Environmental pressure is always PTB210.
    quint16 pressureSource = 0x0040;
    bool recordSensors = true, recordDlia = true, recordRaw = false;

    QJsonObject toJson() const {
        QJsonObject sensors;for(auto it=sensorParameters.begin();it!=sensorParameters.end();++it)sensors[QString::number(it.key())]=it.value();
        QJsonArray channels;
        for (int i = 0; i < 2; ++i) {
            const auto &w = wms[i]; const auto &d = dlia[i]; const auto &f = frontend[i]; const auto &a = dac[i];
            channels.append(QJsonObject{{"scanMilliHz", double(w.scanMilliHz)}, {"sineMilliHz", double(w.sineMilliHz)},
                {"updateRateHz", double(w.updateRateHz)}, {"scanAmplitude", w.scanAmplitude}, {"sineAmplitude", w.sineAmplitude},
                {"bias", w.bias}, {"phase", double(w.phase)}, {"phaseMode", double(w.phaseMode)},
                {"referenceMilliHz", double(d.referenceMilliHz)}, {"phase1", double(d.phase1)}, {"phase2", double(d.phase2)},
                {"outputRateHz", double(d.outputRateHz)}, {"mode", double(d.mode)}, {"cutoffHz",double(d.cutoffHz)}, {"fragmentPoints",double(d.fragmentPoints)}, {"expectedPoints",double(adcExpectedPoints[i])}, {"voltsPerCode", f.voltsPerCode},
                {"polarity", f.polarity}, {"calibrated", f.calibrated},
                {"dac", QJsonObject{{"deviceCtrl",double(a.deviceCtrl)},{"clearCode",double(a.clearCode)},
                    {"minCode",double(a.minCode)},{"maxCode",double(a.maxCode)},{"spiClockHz",double(a.spiClockHz)},
                    {"gain",double(a.gain)},{"offset",a.offset}}}});
        }
        return {{"backend", backend}, {"sensorParameters",sensors}, {"adcRateHz", double(adcRateHz)}, {"adcCnvTicks",double(adcCnvTicks)}, {"adcBusyTimeoutTicks",double(adcBusyTimeoutTicks)}, {"pressureSource", 0x0040},
            {"recordSensors", recordSensors}, {"recordDlia", recordDlia}, {"recordRaw", recordRaw}, {"channels", channels},
            {"ai8",QJsonObject{{"slaveAddress",double(ai8.slaveAddress)},{"channel",double(ai8.channel)},{"pollIntervalMs",double(ai8.pollIntervalMs)},
                {"timeoutMs",double(ai8.timeoutMs)},{"retryLimit",double(ai8.retryLimit)},{"scan",ai8.scan}}}};
    }
    static FpgaControlConfig fromJson(const QJsonObject &o) {
        FpgaControlConfig c;
        const auto sensors=o.value("sensorParameters").toObject();for(auto it=sensors.begin();it!=sensors.end();++it)c.sensorParameters[quint16(it.key().toUInt())]=it.value().toObject();
        c.backend = o.value("backend").toString(QStringLiteral("auto"));
        auto u = [](const QJsonObject &v, const char *key, quint32 fallback) { return quint32(v.value(QLatin1String(key)).toDouble(fallback)); };
        c.adcRateHz = u(o, "adcRateHz", c.adcRateHz);
        c.adcCnvTicks=u(o,"adcCnvTicks",2);c.adcBusyTimeoutTicks=u(o,"adcBusyTimeoutTicks",100);
        c.recordSensors = o.value("recordSensors").toBool(true); c.recordDlia = o.value("recordDlia").toBool(true);
        c.recordRaw = o.value("recordRaw").toBool(false);
        const auto ai = o.value("ai8").toObject();c.ai8.slaveAddress=u(ai,"slaveAddress",1);c.ai8.channel=u(ai,"channel",1);c.ai8.pollIntervalMs=u(ai,"pollIntervalMs",1000);
        c.ai8.timeoutMs=u(ai,"timeoutMs",200);c.ai8.retryLimit=u(ai,"retryLimit",1);
        c.ai8.scan=ai.value("scan").toBool(false);
        const auto a = o.value("channels").toArray();
        for (int i = 0; i < qMin(2, int(a.size())); ++i) {
            const auto v = a[i].toObject(); auto &w = c.wms[i]; auto &d = c.dlia[i]; auto &f = c.frontend[i];
            w.scanMilliHz = u(v,"scanMilliHz",w.scanMilliHz); w.sineMilliHz = u(v,"sineMilliHz",w.sineMilliHz);
            w.updateRateHz = u(v,"updateRateHz",w.updateRateHz); w.scanAmplitude = v.value("scanAmplitude").toInt(w.scanAmplitude);
            w.sineAmplitude = v.value("sineAmplitude").toInt(w.sineAmplitude); w.bias = v.value("bias").toInt(w.bias);
            w.phase = u(v,"phase",w.phase); w.phaseMode = u(v,"phaseMode",w.phaseMode);
            d.referenceMilliHz = u(v,"referenceMilliHz",d.referenceMilliHz); d.phase1 = u(v,"phase1",d.phase1);
            d.phase2 = u(v,"phase2",d.phase2); d.outputRateHz = u(v,"outputRateHz",d.outputRateHz); d.mode = u(v,"mode",d.mode);
            d.fragmentPoints=u(v,"fragmentPoints",256);c.adcExpectedPoints[i]=u(v,"expectedPoints",0);
            d.cutoffHz=u(v,"cutoffHz",1000);
            f.voltsPerCode = v.value("voltsPerCode").toDouble(f.voltsPerCode); f.polarity = v.value("polarity").toInt(1) < 0 ? -1 : 1;
            f.calibrated = v.value("calibrated").toBool(false);
            const auto dv=v.value("dac").toObject();auto &dac=c.dac[i];
            dac.deviceCtrl=u(dv,"deviceCtrl",dac.deviceCtrl);dac.clearCode=u(dv,"clearCode",dac.clearCode);dac.minCode=u(dv,"minCode",dac.minCode);
            dac.maxCode=u(dv,"maxCode",dac.maxCode);dac.spiClockHz=u(dv,"spiClockHz",dac.spiClockHz);dac.gain=u(dv,"gain",dac.gain);dac.offset=dv.value("offset").toInt(0);
        }
        return c;
    }
};
Q_DECLARE_METATYPE(FpgaControlConfig)
