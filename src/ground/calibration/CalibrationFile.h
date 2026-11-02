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
    bool generate(const CalibrationProject&, const QString& name, CalibrationFile&, QString *error) const;
    CalibrationFile read(const QString& filename, bool draft, QString *error) const;
    static CalibrationFile decode(const QByteArray&, bool draft, QString *error);
    bool copy(const QString& source, const QString& name, CalibrationFile&, QString *error) const;
    bool remove(const QString& filename, const QString& lockedSha256, QString *error) const;
    bool archive(const QString& filename, const QString& lockedSha256, QString *error) const;
private:
    QString root_;
    bool write(const QString&, const QJsonObject&, bool replace, QString *error) const;
    QString uniqueFilename(const QString& name) const;
};
}
