#pragma once

#include <QFile>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <utility>

class QNetworkAccessManager;
class QNetworkReply;

namespace VaporView::Map3D {

struct MapResourceFile
{
    QString relativePath;
    QUrl url;
    qint64 sizeBytes = -1;
    QString sha256;
};

struct MapResourcePackage
{
    QString id;
    QString displayName;
    QString version;
    qint64 sizeBytes = -1;
    QString sha256;
    QUrl downloadUrl;
    QString installPath;
    QStringList requiredFiles;
    QVector<MapResourceFile> files;
};

class MapResourceManifest final
{
public:
    static bool parse(const QByteArray& payload,
                      const QUrl& manifestUrl,
                      QVector<MapResourcePackage>* packages,
                      QString* errorMessage);
};

class MapResourceManager final : public QObject
{
    Q_OBJECT

public:
    explicit MapResourceManager(QObject* parent = nullptr);

    static QString defaultDownloadRoot();

    QString manifestUrl() const;
    void setManifestUrl(const QString& url);
    QString downloadRoot() const;
    const QVector<MapResourcePackage>& packages() const;
    bool packageInstalled(const MapResourcePackage& package, QString* reason = nullptr) const;

public slots:
    void refreshManifest();
    void downloadPackage(const QString& packageId);
    void removePackage(const QString& packageId);

signals:
    void manifestUpdated();
    void operationProgress(const QString& packageId, qint64 received, qint64 total);
    void operationFinished(const QString& packageId, bool success, const QString& message);
    void resourcesChanged();

private slots:
    void finishManifestReply();
    void finishFileReply();

private:
    void failOperation(const QString& message);
    void downloadNextFile();
    bool validateRelativePath(const QString& path) const;
    QString filePath(const QString& relativePath) const;
    const MapResourcePackage* findPackage(const QString& packageId) const;
    bool verifyFile(const QString& filename, const MapResourceFile& resource, QString* errorMessage) const;
    void clearActiveOperation();
    void rollbackActiveFiles();

    QNetworkAccessManager* network_manager_ = nullptr;
    QNetworkReply* active_reply_ = nullptr;
    QFile active_file_;
    QVector<MapResourcePackage> packages_;
    QString manifest_url_;
    QString last_manifest_error_;
    QString active_package_id_;
    MapResourcePackage active_package_;
    int active_file_index_ = -1;
    QString active_temp_root_;
    QString active_error_;
    QStringList active_network_ssl_errors_;
    QStringList active_installed_files_;
    QVector<std::pair<QString, QString>> active_backups_;
};

} // namespace VaporView::Map3D
