#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
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
        quint32 outputRateHz = 10000, mode = 3;
    };
    struct Frontend { double voltsPerCode = 10.0 / 8388608.0; int polarity = 1; bool calibrated = false; };
    struct Dac {
        quint32 deviceCtrl = 0x312, clearCode = 0x80000, minCode = 0, maxCode = 0xfffff;
        quint32 spiClockHz = 20000000, gain = 0x40000000;
        qint32 offset = 0;
    };
    struct Ai8 { quint32 slaveAddress = 1, channel = 1, pollIntervalMs = 1000, timeoutMs = 200, retryLimit = 1; };
    quint32 adcRateHz = 1000000;
    QString backend = QStringLiteral("auto");
    std::array<Wms, 2> wms;
    std::array<Dlia, 2> dlia;
    std::array<Frontend, 2> frontend;
    std::array<Dac, 2> dac;
    Ai8 ai8;
    quint16 pressureSource = 0x0040;
    int rawWindowSeconds = 10;
    bool recordSensors = true, recordDlia = true, recordRaw = false;

    QJsonObject toJson() const {
        QJsonArray channels;
        for (int i = 0; i < 2; ++i) {
            const auto &w = wms[i]; const auto &d = dlia[i]; const auto &f = frontend[i]; const auto &a = dac[i];
            channels.append(QJsonObject{{"scanMilliHz", double(w.scanMilliHz)}, {"sineMilliHz", double(w.sineMilliHz)},
                {"updateRateHz", double(w.updateRateHz)}, {"scanAmplitude", w.scanAmplitude}, {"sineAmplitude", w.sineAmplitude},
                {"bias", w.bias}, {"phase", double(w.phase)}, {"phaseMode", double(w.phaseMode)},
                {"referenceMilliHz", double(d.referenceMilliHz)}, {"phase1", double(d.phase1)}, {"phase2", double(d.phase2)},
                {"outputRateHz", double(d.outputRateHz)}, {"mode", double(d.mode)}, {"voltsPerCode", f.voltsPerCode},
                {"polarity", f.polarity}, {"calibrated", f.calibrated},
                {"dac", QJsonObject{{"deviceCtrl",double(a.deviceCtrl)},{"clearCode",double(a.clearCode)},
                    {"minCode",double(a.minCode)},{"maxCode",double(a.maxCode)},{"spiClockHz",double(a.spiClockHz)},
                    {"gain",double(a.gain)},{"offset",a.offset}}}});
        }
        return {{"backend", backend}, {"adcRateHz", double(adcRateHz)}, {"pressureSource", pressureSource}, {"rawWindowSeconds", rawWindowSeconds},
            {"recordSensors", recordSensors}, {"recordDlia", recordDlia}, {"recordRaw", recordRaw}, {"channels", channels},
            {"ai8",QJsonObject{{"slaveAddress",double(ai8.slaveAddress)},{"channel",double(ai8.channel)},{"pollIntervalMs",double(ai8.pollIntervalMs)},
                {"timeoutMs",double(ai8.timeoutMs)},{"retryLimit",double(ai8.retryLimit)}}}};
    }
    static FpgaControlConfig fromJson(const QJsonObject &o) {
        FpgaControlConfig c;
        c.backend = o.value("backend").toString(QStringLiteral("auto"));
        auto u = [](const QJsonObject &v, const char *key, quint32 fallback) { return quint32(v.value(QLatin1String(key)).toDouble(fallback)); };
        c.adcRateHz = u(o, "adcRateHz", c.adcRateHz);
        c.pressureSource = quint16(u(o, "pressureSource", c.pressureSource));
        c.rawWindowSeconds = qBound(1, o.value("rawWindowSeconds").toInt(10), 60);
        c.recordSensors = o.value("recordSensors").toBool(true); c.recordDlia = o.value("recordDlia").toBool(true);
        c.recordRaw = o.value("recordRaw").toBool(false);
        const auto ai = o.value("ai8").toObject();c.ai8.slaveAddress=u(ai,"slaveAddress",1);c.ai8.channel=u(ai,"channel",1);c.ai8.pollIntervalMs=u(ai,"pollIntervalMs",1000);
        c.ai8.timeoutMs=u(ai,"timeoutMs",200);c.ai8.retryLimit=u(ai,"retryLimit",1);
        const auto a = o.value("channels").toArray();
        for (int i = 0; i < qMin(2, int(a.size())); ++i) {
            const auto v = a[i].toObject(); auto &w = c.wms[i]; auto &d = c.dlia[i]; auto &f = c.frontend[i];
            w.scanMilliHz = u(v,"scanMilliHz",w.scanMilliHz); w.sineMilliHz = u(v,"sineMilliHz",w.sineMilliHz);
            w.updateRateHz = u(v,"updateRateHz",w.updateRateHz); w.scanAmplitude = v.value("scanAmplitude").toInt(w.scanAmplitude);
            w.sineAmplitude = v.value("sineAmplitude").toInt(w.sineAmplitude); w.bias = v.value("bias").toInt(w.bias);
            w.phase = u(v,"phase",w.phase); w.phaseMode = u(v,"phaseMode",w.phaseMode);
            d.referenceMilliHz = u(v,"referenceMilliHz",d.referenceMilliHz); d.phase1 = u(v,"phase1",d.phase1);
            d.phase2 = u(v,"phase2",d.phase2); d.outputRateHz = u(v,"outputRateHz",d.outputRateHz); d.mode = u(v,"mode",d.mode);
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
