#pragma once

// Historical files for reader compatibility tests. Production recorders no longer
// write VLP1/USB archives, so fixtures construct that old format explicitly.
#include "ground/session/GroundRecordingService.h"
#include "shared/session/FpgaSessionArchive.h"
#include "shared/session/SessionManifest.h"
#include <QDir>
#include <QJsonDocument>

class LegacyFpgaArchiveFixture
{
public:
    bool start(const VaporView::Ground::Session::GroundRecordingOptions& options)
    {
        const bool resume = service_.isPaused();
        if (!service_.start(options)) return false;
        if (!resume) { raw_.setFileName(VaporView::Session::FpgaSessionArchive::rawPath(service_.status().sessionDirectory));
            count_ = 0; statistics_.clear(); }
        if (resume) return recordFpgaSnapshot(0, {{"event", "resume"}});
        return true;
    }
    bool recordFpgaFrame(quint64 t, const QByteArray& b) { return record(t, VaporView::Session::FpgaArchiveKind::Frame, b); }
    bool recordFpgaUsbBytes(quint64 t, const QByteArray& b) { return record(t, VaporView::Session::FpgaArchiveKind::UsbBytes, b); }
    bool recordFpgaCommand(quint64 t, const QByteArray& b) { return record(t, VaporView::Session::FpgaArchiveKind::Command, b); }
    bool recordFpgaSnapshot(quint64 t, const QJsonObject& o) {
        return record(t, VaporView::Session::FpgaArchiveKind::Snapshot, QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
    bool pause() { return recordFpgaSnapshot(0, {{"event", "pause"}}) && raw_.flush() && service_.pause(); }
    VaporView::Ground::Session::GroundRecordingStopSummary stop()
    {
        if (raw_.isOpen()) { raw_.flush(); raw_.close(); }
        auto result = service_.stop();
        QFile file(QDir(result.sessionDirectory).filePath(QStringLiteral("session.json")));
        if (!file.open(QIODevice::ReadOnly)) { result.writeFailed = true; return result; }
        auto manifest = VaporView::Session::sessionManifestFromJson(QJsonDocument::fromJson(file.readAll()).object());
        file.close();
        manifest.manifest.fpgaRecords = count_;
        manifest.manifest.counts.waveformFrames = statistics_.status().value("waveform_frames").toString().toULongLong();
        result.writeFailed |= !VaporView::Session::writeSessionManifestAtomically(file.fileName(), manifest.manifest);
        return result;
    }
    VaporView::Ground::Session::GroundRecordingStatus status() const {
        auto status = service_.status(); status.rawFpgaRecords = count_; status.fpgaStatistics = statistics_.status();
        status.waveformFrames = status.fpgaStatistics.value("waveform_frames").toString().toLongLong(); return status;
    }
private:
    bool record(quint64 t, VaporView::Session::FpgaArchiveKind kind, const QByteArray& bytes)
    {
        if (!service_.isActive()) return false;
        if (!raw_.isOpen()) {
            if (!raw_.open(QIODevice::WriteOnly) || !VaporView::SessionRawDat::writeFileHeader(raw_, 8)) return false;
        }
        VaporView::SessionRawDat::RawRecordHeader header;
        header.hostTimestampUs = t; header.sourceId = 8; header.recordType = quint16(kind); header.sequence = count_;
        if (!VaporView::SessionRawDat::writeRecord(raw_, header, bytes)) return false;
        ++count_; statistics_.record(kind, bytes); return true;
    }
    VaporView::Ground::Session::GroundRecordingService service_;
    QFile raw_;
    quint64 count_ = 0;
    VaporView::Session::FpgaRecordingStatistics statistics_;
};
