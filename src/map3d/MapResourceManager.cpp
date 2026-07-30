#include "map3d/MapResourceManager.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QSettings>
#include "shared/config/SettingsWriteBarrier.h"
#include "shared/config/ApplicationConfig.h"
#include <QStandardPaths>

#include <algorithm>
#include <limits>

namespace VaporView::Map3D {
namespace {

QString normalizedRelativePath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}

bool isSafeRelativePath(const QString& path)
{
    const QString normalized = normalizedRelativePath(path);
    if (normalized.isEmpty() || normalized == QStringLiteral(".") || QDir::isAbsolutePath(normalized))
    {
        return false;
    }

    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return std::none_of(parts.cbegin(), parts.cend(), [](const QString& part) {
        return part == QStringLiteral("..") || part.contains(QLatin1Char(':'));
    });
}

bool isSha256Hex(const QString& value)
{
    if (value.size() != 64)
    {
        return false;
    }
    return std::all_of(value.cbegin(), value.cend(), [](QChar character) {
        return (character >= QLatin1Char('0') && character <= QLatin1Char('9')) ||
               (character >= QLatin1Char('a') && character <= QLatin1Char('f')) ||
               (character >= QLatin1Char('A') && character <= QLatin1Char('F'));
    });
}

bool parseString(const QJsonObject& object, const char* key, QString* value, bool required, QString* error)
{
    const QJsonValue jsonValue = object.value(QLatin1String(key));
    if (!jsonValue.isString())
    {
        if (required && error)
        {
            *error = QStringLiteral("Manifest field '%1' must be a string.").arg(QString::fromLatin1(key));
        }
        return !required;
    }
    *value = jsonValue.toString().trimmed();
    if (required && value->isEmpty() && error)
    {
        *error = QStringLiteral("Manifest field '%1' must not be empty.").arg(QString::fromLatin1(key));
        return false;
    }
    return true;
}

bool parseSize(const QJsonObject& object, const char* key, qint64* value, QString* error)
{
    const QJsonValue jsonValue = object.value(QLatin1String(key));
    if (jsonValue.isUndefined() || jsonValue.isNull())
    {
        return true;
    }
    if (!jsonValue.isDouble())
    {
        if (error) *error = QStringLiteral("Manifest field '%1' must be a number.").arg(QString::fromLatin1(key));
        return false;
    }
    const double number = jsonValue.toDouble();
    if (number < 0.0 || number > static_cast<double>(std::numeric_limits<qint64>::max()))
    {
        if (error) *error = QStringLiteral("Manifest field '%1' is out of range.").arg(QString::fromLatin1(key));
        return false;
    }
    *value = static_cast<qint64>(number);
    return true;
}

QUrl resolveUrl(const QUrl& baseUrl, const QString& value)
{
    const QUrl url(value);
    return url.isRelative() ? baseUrl.resolved(url) : url;
}

bool parseFileObject(const QJsonObject& object,
                     const QUrl& baseUrl,
                     MapResourceFile* result,
                     QString* error)
{
    QString path;
    QString url;
    if (!parseString(object, "relativePath", &path, true, error) &&
        !parseString(object, "path", &path, true, error))
    {
        return false;
    }
    if (!isSafeRelativePath(path))
    {
        if (error) *error = QStringLiteral("Unsafe map resource path: %1").arg(path);
        return false;
    }
    if (!parseString(object, "url", &url, true, error))
    {
        return false;
    }
    result->relativePath = normalizedRelativePath(path);
    result->url = resolveUrl(baseUrl, url);
    if (!result->url.isValid() || (result->url.scheme() != QStringLiteral("http") &&
                                  result->url.scheme() != QStringLiteral("https")))
    {
        if (error) *error = QStringLiteral("Map resource URL must use http or https: %1").arg(result->url.toString());
        return false;
    }
    if (!parseSize(object, "sizeBytes", &result->sizeBytes, error))
    {
        return false;
    }
    if (!parseString(object, "sha256", &result->sha256, false, error))
    {
        return false;
    }
    result->sha256 = result->sha256.toLower();
    if (!result->sha256.isEmpty() && !isSha256Hex(result->sha256))
    {
        if (error) *error = QStringLiteral("sha256 must contain 64 hexadecimal characters.");
        return false;
    }
    return true;
}

} // namespace

bool MapResourceManifest::parse(const QByteArray& payload,
                                const QUrl& manifestUrl,
                                QVector<MapResourcePackage>* packages,
                                QString* errorMessage)
{
    if (!packages)
    {
        if (errorMessage) *errorMessage = QStringLiteral("Output package list is null.");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (errorMessage) *errorMessage = QStringLiteral("Map manifest is not valid JSON: %1").arg(parseError.errorString());
        return false;
    }

    const QJsonArray resources = document.object().value(QStringLiteral("resources")).toArray();
    if (resources.isEmpty())
    {
        if (errorMessage) *errorMessage = QStringLiteral("Map manifest does not contain a non-empty resources array.");
        return false;
    }

    QVector<MapResourcePackage> parsed;
    for (const QJsonValue& value : resources)
    {
        if (!value.isObject())
        {
            if (errorMessage) *errorMessage = QStringLiteral("Every map resource entry must be an object.");
            return false;
        }

        const QJsonObject object = value.toObject();
        MapResourcePackage package;
        if (!parseString(object, "id", &package.id, true, errorMessage) ||
            !parseString(object, "displayName", &package.displayName, true, errorMessage) ||
            !parseString(object, "version", &package.version, true, errorMessage))
        {
            return false;
        }
        if (!isSafeRelativePath(package.id) || package.id.contains(QLatin1Char('/')))
        {
            if (errorMessage) *errorMessage = QStringLiteral("Unsafe map resource id: %1").arg(package.id);
            return false;
        }
        if (!parseSize(object, "sizeBytes", &package.sizeBytes, errorMessage) ||
            !parseString(object, "sha256", &package.sha256, false, errorMessage))
        {
            return false;
        }
        package.sha256 = package.sha256.toLower();
        if (!package.sha256.isEmpty() && !isSha256Hex(package.sha256))
        {
            if (errorMessage) *errorMessage = QStringLiteral("sha256 must contain 64 hexadecimal characters.");
            return false;
        }

        const QJsonValue downloadUrlValue = object.value(QStringLiteral("downloadUrl"));
        if (downloadUrlValue.isString())
        {
            package.downloadUrl = resolveUrl(manifestUrl, downloadUrlValue.toString());
        }
        parseString(object, "installPath", &package.installPath, false, errorMessage);
        if (!package.installPath.isEmpty() && !isSafeRelativePath(package.installPath))
        {
            if (errorMessage) *errorMessage = QStringLiteral("Unsafe map install path: %1").arg(package.installPath);
            return false;
        }

        const QJsonArray requiredFiles = object.value(QStringLiteral("requiredFiles")).toArray();
        for (const QJsonValue& required : requiredFiles)
        {
            if (!required.isString() || !isSafeRelativePath(required.toString()))
            {
                if (errorMessage) *errorMessage = QStringLiteral("Manifest contains an unsafe required file path.");
                return false;
            }
            package.requiredFiles.push_back(normalizedRelativePath(required.toString()));
        }

        const QJsonArray files = object.value(QStringLiteral("files")).toArray();
        for (const QJsonValue& fileValue : files)
        {
            if (!fileValue.isObject())
            {
                if (errorMessage) *errorMessage = QStringLiteral("Map resource files must be objects.");
                return false;
            }
            MapResourceFile file;
            if (!parseFileObject(fileValue.toObject(), manifestUrl, &file, errorMessage))
            {
                return false;
            }
            package.files.push_back(file);
        }

        if (package.files.isEmpty() && package.downloadUrl.isValid() && !package.installPath.isEmpty())
        {
            MapResourceFile file;
            file.relativePath = package.installPath;
            file.url = package.downloadUrl;
            file.sizeBytes = package.sizeBytes;
            file.sha256 = package.sha256;
            package.files.push_back(file);
        }
        if (package.files.isEmpty())
        {
            if (errorMessage) *errorMessage = QStringLiteral("Map resource '%1' has no downloadable files.").arg(package.id);
            return false;
        }
        if (package.requiredFiles.isEmpty())
        {
            for (const MapResourceFile& file : package.files)
            {
                package.requiredFiles.push_back(file.relativePath);
            }
        }
        if (std::any_of(parsed.cbegin(), parsed.cend(), [&package](const MapResourcePackage& existing) {
                return existing.id == package.id;
            }))
        {
            if (errorMessage) *errorMessage = QStringLiteral("Duplicate map resource id: %1").arg(package.id);
            return false;
        }
        parsed.push_back(package);
    }

    *packages = parsed;
    return true;
}

MapResourceManager::MapResourceManager(QObject* parent)
    : QObject(parent)
    , network_manager_(new QNetworkAccessManager(this))
{
    const QString environmentUrl = qEnvironmentVariable("VAPORVIEW_MAP_MANIFEST_URL");
    VaporView::migrateLegacyApplicationConfig();
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("Map3D"));
    manifest_url_ = environmentUrl.isEmpty()
        ? settings.value(QStringLiteral("mapManifestUrl")).toString()
        : environmentUrl;
}

QString MapResourceManager::defaultDownloadRoot()
{
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath();
#else
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return appData.isEmpty() ? QCoreApplication::applicationDirPath() : appData;
#endif
}

QString MapResourceManager::manifestUrl() const
{
    return manifest_url_;
}

void MapResourceManager::setManifestUrl(const QString& url)
{
    manifest_url_ = url.trimmed();
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("Map3D"));
    VaporView::setPersistentSetting(settings, QStringLiteral("mapManifestUrl"), manifest_url_);
}

QString MapResourceManager::downloadRoot() const
{
    return defaultDownloadRoot();
}

const QVector<MapResourcePackage>& MapResourceManager::packages() const
{
    return packages_;
}

const MapResourcePackage* MapResourceManager::findPackage(const QString& packageId) const
{
    const auto it = std::find_if(packages_.cbegin(), packages_.cend(), [&packageId](const MapResourcePackage& package) {
        return package.id == packageId;
    });
    return it == packages_.cend() ? nullptr : &(*it);
}

bool MapResourceManager::validateRelativePath(const QString& path) const
{
    return isSafeRelativePath(path);
}

QString MapResourceManager::filePath(const QString& relativePath) const
{
    return QDir(downloadRoot()).absoluteFilePath(normalizedRelativePath(relativePath));
}

bool MapResourceManager::packageInstalled(const MapResourcePackage& package, QString* reason) const
{
    for (const MapResourceFile& resource : package.files)
    {
        if (!validateRelativePath(resource.relativePath))
        {
            if (reason) *reason = QStringLiteral("Unsafe resource path: %1").arg(resource.relativePath);
            return false;
        }
        if (!verifyFile(filePath(resource.relativePath), resource, reason))
        {
            return false;
        }
    }
    for (const QString& required : package.requiredFiles)
    {
        if (!validateRelativePath(required))
        {
            if (reason) *reason = QStringLiteral("Unsafe required path: %1").arg(required);
            return false;
        }
        if (!QFileInfo(filePath(required)).isFile())
        {
            if (reason) *reason = QStringLiteral("Missing %1").arg(required);
            return false;
        }
    }
    if (reason) reason->clear();
    return true;
}

void MapResourceManager::refreshManifest()
{
    if (VaporView::settingsWritesSuspended())
    {
        emit operationFinished(QString(), true, QStringLiteral("[界面测试] 已返回固定地图资源状态；未访问网络。"));
        return;
    }
    if (active_reply_)
    {
        emit operationFinished(QString(), false, QStringLiteral("已有地图资源操作正在进行。"));
        return;
    }
    const QUrl url(manifest_url_);
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")))
    {
        last_manifest_error_ = QStringLiteral("请填写有效的 HTTP/HTTPS 地图资源清单地址。\n环境变量 VAPORVIEW_MAP_MANIFEST_URL 也可提供默认地址。");
        emit operationFinished(QString(), false, last_manifest_error_);
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VaporView/%1").arg(QCoreApplication::applicationVersion()));
    active_reply_ = network_manager_->get(request);
    connect(active_reply_, &QNetworkReply::finished, this, &MapResourceManager::finishManifestReply);
}

void MapResourceManager::finishManifestReply()
{
    QNetworkReply* reply = active_reply_;
    active_reply_ = nullptr;
    if (!reply)
    {
        return;
    }
    const QByteArray payload = reply->readAll();
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (reply->error() != QNetworkReply::NoError || (status.isValid() && status.toInt() >= 400))
    {
        last_manifest_error_ = reply->errorString();
        if (status.isValid() && status.toInt() >= 400)
        {
            last_manifest_error_ = QStringLiteral("HTTP %1").arg(status.toInt());
        }
        if (last_manifest_error_.isEmpty())
        {
            last_manifest_error_ = QStringLiteral("网络请求失败");
        }
        emit operationFinished(QString(), false, QStringLiteral("读取地图资源清单失败: %1").arg(last_manifest_error_));
        reply->deleteLater();
        return;
    }

    QVector<MapResourcePackage> parsed;
    QString error;
    if (!MapResourceManifest::parse(payload, QUrl(manifest_url_), &parsed, &error))
    {
        last_manifest_error_ = error;
        emit operationFinished(QString(), false, QStringLiteral("地图资源清单无效: %1").arg(error));
        reply->deleteLater();
        return;
    }
    packages_ = parsed;
    last_manifest_error_.clear();
    emit manifestUpdated();
    emit operationFinished(QString(), true, QStringLiteral("已读取 %1 个地图资源包。\n资源目录：%2").arg(packages_.size()).arg(downloadRoot()));
    reply->deleteLater();
}

void MapResourceManager::downloadPackage(const QString& packageId)
{
    if (VaporView::settingsWritesSuspended())
    {
        emit operationFinished(packageId, true, QStringLiteral("[界面测试] 模拟地图资源安装完成；未访问网络或写入文件。"));
        return;
    }
    if (active_reply_)
    {
        emit operationFinished(packageId, false, QStringLiteral("已有地图资源操作正在进行。"));
        return;
    }
    const MapResourcePackage* package = findPackage(packageId);
    if (!package)
    {
        emit operationFinished(packageId, false, QStringLiteral("未找到地图资源包：%1").arg(packageId));
        return;
    }
    active_package_ = *package;
    active_package_id_ = packageId;
    active_file_index_ = 0;
    active_error_.clear();
    active_installed_files_.clear();
    active_backups_.clear();
    active_temp_root_ = QDir(downloadRoot()).filePath(QStringLiteral(".vaporview-downloads/%1").arg(packageId));
    QDir(active_temp_root_).removeRecursively();
    if (!QDir().mkpath(active_temp_root_))
    {
        failOperation(QStringLiteral("无法创建地图临时目录：%1").arg(active_temp_root_));
        return;
    }
    downloadNextFile();
}

void MapResourceManager::downloadNextFile()
{
    if (active_file_index_ >= active_package_.files.size())
    {
        QString reason;
        if (!packageInstalled(active_package_, &reason))
        {
            failOperation(QStringLiteral("地图资源安装后校验失败：%1").arg(reason));
            return;
        }
        for (const auto& backup : active_backups_)
        {
            QFile::remove(backup.second);
        }
        QDir(active_temp_root_).removeRecursively();
        const QString packageId = active_package_id_;
        const QString displayName = active_package_.displayName;
        clearActiveOperation();
        emit operationProgress(packageId, 1, 1);
        emit operationFinished(packageId, true, QStringLiteral("地图资源已安装：%1").arg(displayName));
        emit resourcesChanged();
        return;
    }

    const MapResourceFile& resource = active_package_.files.at(active_file_index_);
    if (!validateRelativePath(resource.relativePath))
    {
        failOperation(QStringLiteral("地图资源路径不安全：%1").arg(resource.relativePath));
        return;
    }
    const QString tempFile = QDir(active_temp_root_).filePath(resource.relativePath + QStringLiteral(".part"));
    active_file_.setFileName(tempFile);
    active_error_.clear();
    if (!QDir().mkpath(QFileInfo(tempFile).absolutePath()) ||
        !active_file_.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        failOperation(QStringLiteral("无法写入地图临时文件：%1").arg(tempFile));
        return;
    }

    QNetworkRequest request(resource.url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VaporView/%1").arg(QCoreApplication::applicationVersion()));
    active_reply_ = network_manager_->get(request);
    connect(active_reply_, &QNetworkReply::readyRead, this, [this]() {
        if (active_reply_)
        {
            const QByteArray payload = active_reply_->readAll();
            if (active_file_.write(payload) != payload.size() && active_error_.isEmpty())
            {
                active_error_ = QStringLiteral("无法写入地图临时文件：磁盘空间不足或文件不可写。");
            }
        }
    });
    connect(active_reply_, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        emit operationProgress(active_package_id_, received, total);
    });
    connect(active_reply_, &QNetworkReply::finished, this, &MapResourceManager::finishFileReply);
}

void MapResourceManager::finishFileReply()
{
    QNetworkReply* reply = active_reply_;
    active_reply_ = nullptr;
    if (!reply)
    {
        return;
    }
    const QByteArray tail = reply->readAll();
    if (active_file_.write(tail) != tail.size() && active_error_.isEmpty())
    {
        active_error_ = QStringLiteral("无法写入地图临时文件：磁盘空间不足或文件不可写。");
    }
    active_file_.flush();
    active_file_.close();
    const MapResourceFile resource = active_package_.files.value(active_file_index_);
    const QString tempFile = QDir(active_temp_root_).filePath(resource.relativePath + QStringLiteral(".part"));
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!active_error_.isEmpty())
    {
        const QString error = active_error_;
        failOperation(error);
        reply->deleteLater();
        return;
    }
    if (reply->error() != QNetworkReply::NoError || (status.isValid() && status.toInt() >= 400))
    {
        QString reason = reply->errorString();
        if (status.isValid() && status.toInt() >= 400)
        {
            reason = QStringLiteral("HTTP %1").arg(status.toInt());
        }
        if (reason.isEmpty())
        {
            reason = QStringLiteral("网络请求失败");
        }
        failOperation(QStringLiteral("下载地图资源失败：%1").arg(reason));
        reply->deleteLater();
        return;
    }
    QString error;
    if (!verifyFile(tempFile, resource, &error))
    {
        failOperation(error);
        reply->deleteLater();
        return;
    }
    const QString destination = filePath(resource.relativePath);
    if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
    {
        failOperation(QStringLiteral("无法安装地图文件：%1").arg(destination));
        reply->deleteLater();
        return;
    }
    if (QFileInfo::exists(destination))
    {
        const QString backup = tempFile + QStringLiteral(".old");
        QFile::remove(backup);
        if (!QFile::rename(destination, backup))
        {
            failOperation(QStringLiteral("无法替换地图文件：%1").arg(destination));
            reply->deleteLater();
            return;
        }
        active_backups_.push_back({destination, backup});
    }
    if (!QFile::rename(tempFile, destination))
    {
        failOperation(QStringLiteral("无法安装地图文件：%1").arg(destination));
        reply->deleteLater();
        return;
    }
    active_installed_files_.push_back(destination);
    ++active_file_index_;
    emit operationProgress(active_package_id_, active_file_index_, active_package_.files.size());
    reply->deleteLater();
    downloadNextFile();
}

bool MapResourceManager::verifyFile(const QString& filename, const MapResourceFile& resource, QString* errorMessage) const
{
    const QFileInfo info(filename);
    if (!info.isFile())
    {
        if (errorMessage) *errorMessage = QStringLiteral("地图临时文件不存在：%1").arg(filename);
        return false;
    }
    if (resource.sizeBytes >= 0 && info.size() != resource.sizeBytes)
    {
        if (errorMessage) *errorMessage = QStringLiteral("地图文件大小校验失败：%1").arg(resource.relativePath);
        return false;
    }
    if (!resource.sha256.isEmpty())
    {
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly))
        {
            if (errorMessage) *errorMessage = QStringLiteral("无法读取地图文件进行校验：%1").arg(filename);
            return false;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!file.atEnd())
        {
            const QByteArray chunk = file.read(1024 * 1024);
            if (chunk.isEmpty() && file.error() != QFile::NoError)
            {
                if (errorMessage) *errorMessage = QStringLiteral("读取地图文件进行校验时失败：%1").arg(filename);
                return false;
            }
            hash.addData(chunk);
        }
        const QString actual = QString::fromLatin1(hash.result().toHex());
        if (actual.compare(resource.sha256, Qt::CaseInsensitive) != 0)
        {
            if (errorMessage) *errorMessage = QStringLiteral("地图文件 SHA-256 校验失败：%1").arg(resource.relativePath);
            return false;
        }
    }
    return true;
}

void MapResourceManager::removePackage(const QString& packageId)
{
    if (VaporView::settingsWritesSuspended())
    {
        emit operationFinished(packageId, true, QStringLiteral("[界面测试] 模拟地图资源移除完成；未删除文件。"));
        return;
    }
    if (active_reply_)
    {
        emit operationFinished(packageId, false, QStringLiteral("已有地图资源操作正在进行。"));
        return;
    }
    const MapResourcePackage* package = findPackage(packageId);
    if (!package)
    {
        emit operationFinished(packageId, false, QStringLiteral("未找到地图资源包：%1").arg(packageId));
        return;
    }
    QStringList paths = package->requiredFiles;
    for (const MapResourceFile& resource : package->files)
    {
        paths.push_back(resource.relativePath);
    }
    paths.removeDuplicates();
    bool removed = false;
    for (const QString& relativePath : paths)
    {
        if (!validateRelativePath(relativePath))
        {
            emit operationFinished(packageId, false, QStringLiteral("地图资源路径不安全：%1").arg(relativePath));
            return;
        }
        const QString target = filePath(relativePath);
        if (QFileInfo::exists(target))
        {
            removed = QFile::remove(target) || removed;
        }
    }
    emit operationFinished(packageId, true, removed ? QStringLiteral("已删除地图资源：%1").arg(package->displayName)
                                                    : QStringLiteral("地图资源未安装：%1").arg(package->displayName));
    if (removed)
    {
        emit resourcesChanged();
    }
}

void MapResourceManager::failOperation(const QString& message)
{
    const QString packageId = active_package_id_;
    rollbackActiveFiles();
    QDir(active_temp_root_).removeRecursively();
    clearActiveOperation();
    emit operationFinished(packageId, false, message);
}

void MapResourceManager::rollbackActiveFiles()
{
    for (const QString& installed : active_installed_files_)
    {
        QFile::remove(installed);
    }
    for (auto it = active_backups_.crbegin(); it != active_backups_.crend(); ++it)
    {
        if (QFileInfo::exists(it->first))
        {
            QFile::remove(it->first);
        }
        QFile::rename(it->second, it->first);
    }
}

void MapResourceManager::clearActiveOperation()
{
    if (active_reply_)
    {
        active_reply_->deleteLater();
        active_reply_ = nullptr;
    }
    active_file_.close();
    active_package_id_.clear();
    active_package_ = {};
    active_file_index_ = -1;
    active_temp_root_.clear();
    active_error_.clear();
    active_installed_files_.clear();
    active_backups_.clear();
}

} // namespace VaporView::Map3D
