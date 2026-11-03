#pragma once
#include "CalibrationProject.h"

namespace VaporView::Ground::Calibration
{
struct CalibrationFile
{
    QString id, createdUtc, filename, sha256;
    CalibrationProject project;
    QJsonObject document;
    bool valid = false;
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
    CalibrationFile read(const QString& filename, bool draft, QString *error) const;
    static CalibrationFile decode(const QByteArray&, bool draft, QString *error);
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
