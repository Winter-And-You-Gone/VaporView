#pragma once
#include "CalibrationProject.h"

namespace VaporView::Ground::Calibration
{
struct CalibrationFile
{
    QString id, createdUtc, filename, sha256;
    CalibrationProject project;
    // Keep the immutable verified source compact. Materialize its complete JSON
    // only for copy/export or a self-contained Session snapshot.
    QJsonObject summary;
    QJsonObject document() const;
    bool valid = false;
private:
    QByteArray compressedDocument_;
    friend class CalibrationFileStore;
};
class CalibrationFileStore final
{
public:
    // Explicit root injection is for isolated tests. Production always uses applicationDirPath.
    explicit CalibrationFileStore(QString installationDirectory = {});
    QString directory() const;
    QString draftsDirectory() const;
    QStringList files() const;
    QStringList drafts() const;
    bool saveDraft(const CalibrationProject&, QString *error) const;
    // Append-only acquisition saves. Editing raw records/revisions uses saveDraft.
    // Each atomic part is chained to the current full checkpoint's file ID.
    bool saveAcquisitionProgress(const CalibrationProject&, QString *error) const;
    quint64 draftStorageBytes() const;
    bool generate(const CalibrationProject&, const QString& name, CalibrationFile&, QString *error) const;
    // Optional diagnostics observe phases without retaining file/project copies.
    CalibrationFile read(const QString& filename, bool draft, QString *error, const CalibrationReadObserver& observer = {}) const;
    static CalibrationFile decode(QByteArray bytes, bool draft, QString *error, const CalibrationReadObserver& observer = {});
    bool copy(const QString& source, const QString& name, CalibrationFile&, QString *error) const;
    bool remove(const QString& filename, const QString& lockedSha256, QString *error) const;
    bool archive(const QString& filename, const QString& lockedSha256, QString *error) const;
private:
    struct DraftProgress {
        QString taskId, filename, fileId, stamp, head;
        quint64 sequence = 0, bytes = 0;
        QMap<QString, QMap<int, int>> counts;
    };
    QString root_;
    mutable DraftProgress progress_;
    bool write(const QString&, const QJsonObject&, bool replace, QString *error, bool acquisition = false) const;
    bool checkpoint(const CalibrationProject&, QString *error, bool acquisition) const;
    QString uniqueFilename(const QString& name) const;
};
}
