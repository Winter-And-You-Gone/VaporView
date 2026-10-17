#pragma once

#include "FpgaVlp1.h"
#include "FpgaWaveformAssembler.h"
#include "shared/session/UnifiedRawDat.h"
#include <QJsonObject>
#include <QMutex>
#include <QMap>
#include <functional>

namespace VaporView::Session {
// Independent source: FPGA bytes must never masquerade as PTB/serial records.
inline constexpr quint16 kFpgaRawSource = SessionRawDat::kSourceFpga;
enum class FpgaArchiveKind : quint16 { Frame = 1, Command = 2, Snapshot = 3, UsbBytes = 4 };

// Counts only validated input data, using USB evidence in preference to its
// duplicate decoded-frame records. A waveform frame is a complete cycle.
class FpgaRecordingStatistics final {
public:
    void record(FpgaArchiveKind kind, QByteArrayView bytes);
    void clear();
    QJsonObject status() const;
private:
    mutable QMutex mutex_;
    bool usb_ = false;
    FpgaVlp1::StreamParser parser_;
    FpgaWave::Assembler assembler_;
    QMap<quint16, quint64> records_, cycles_;
    QMap<quint16, quint32> points_;
};

class FpgaSessionArchive final {
public:
    using Visitor = std::function<bool(const SessionRawDat::RawRecordHeader&, const QByteArray&)>;
    using FrameVisitor = std::function<bool(quint64 hostTimestampUs, const FpgaVlp1::Frame&)>;
    using ResetVisitor = std::function<void(quint64 hostTimestampUs, const QJsonObject& snapshot)>;
    using WaveformVisitor = std::function<bool(quint64 hostTimestampUs, const FpgaWave::CompletedStream&)>;
    enum class ExportFormat { Csv, Json, Bin };
    static QString rawPath(const QString& sessionDirectory);
    static QJsonObject describe(quint64 hostTimestampUs, FpgaArchiveKind kind, QByteArrayView bytes);
    // Index scan recovers an interrupted final record; visit reads one payload at a time.
    static SessionRawDat::RawScanResult scan(const QString& sessionDirectory,
                                             const SessionRawDat::RawScanOptions& options = {});
    static bool visit(const QString& sessionDirectory, const Visitor& visitor, QString *error = nullptr);
    // Reuses the live codec. USB chunks take precedence over duplicate decoded IN records.
    // Snapshots with event=connected/disconnected/reconnect reset the partial-frame buffer.
    static bool replay(const QString& sessionDirectory, const FrameVisitor& visitor,
                       QString *error = nullptr, const ResetVisitor& resetVisitor = {},
                       const std::function<bool()>& isCancelled = {}, const ResetVisitor& snapshotVisitor = {});
    static bool waveforms(const QString& sessionDirectory, const WaveformVisitor& visitor,
                          QString *error = nullptr, const std::function<bool()>& isCancelled = {});
    // BIN exports exact IN USB bytes (or IN frames in older FPGA sessions); JSON/CSV include all kinds.
    // Cancellation aborts scanning/writing and preserves an existing destination.
    static bool exportTo(const QString& sessionDirectory, const QString& filename,
                         ExportFormat format, QString *error = nullptr,
                         const std::function<bool()>& isCancelled = {});
};
}
