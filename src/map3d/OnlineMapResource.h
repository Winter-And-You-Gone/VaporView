#pragma once

#include "shared/config/ApplicationConfig.h"
#include "shared/config/SettingsWriteBarrier.h"

#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

namespace VaporView::Map3D::Detail {

enum class OnlineMapProvider { Tianditu, Esri, Cesium };

struct OnlineMapResource
{
    QString requestUrl;
    QString credential;

    bool operator==(const OnlineMapResource& other) const
    {
        return requestUrl == other.requestUrl && credential == other.credential;
    }
};

inline QSettings onlineMapSettings()
{
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                     QStringLiteral("VaporView"), QStringLiteral("Map3D"));
}

inline QString requestUrlSetting(OnlineMapProvider provider)
{
    switch (provider)
    {
    case OnlineMapProvider::Tianditu: return QStringLiteral("tianditu/request_url");
    case OnlineMapProvider::Esri: return QStringLiteral("esri/request_url");
    case OnlineMapProvider::Cesium: return QStringLiteral("cesium/request_url");
    }
    return {};
}

inline QString defaultRequestUrl(OnlineMapProvider provider)
{
    switch (provider)
    {
    case OnlineMapProvider::Tianditu:
        return QStringLiteral(
            "https://t[01234567].tianditu.gov.cn/img_w/wmts"
            "?SERVICE=WMTS&REQUEST=GetTile&VERSION=1.0.0"
            "&LAYER=img&STYLE=default&TILEMATRIXSET=w&FORMAT=tiles"
            "&TILEMATRIX={z}&TILEROW={y}&TILECOL={x}&tk={key}");
    case OnlineMapProvider::Esri:
        return QStringLiteral("https://ibasemaps-api.arcgis.com/arcgis/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}");
    case OnlineMapProvider::Cesium:
        return QStringLiteral("https://api.cesium.com/v1/assets/1/endpoint");
    }
    return {};
}

inline QSettings tiandituKeySettings()
{
    migrateLegacyApplicationConfig();
    return applicationConfigSettings();
}

inline OnlineMapResource configuredOnlineMapResource(OnlineMapProvider provider)
{
    const auto settings = onlineMapSettings();
    OnlineMapResource config;
    config.requestUrl = settings.value(requestUrlSetting(provider), defaultRequestUrl(provider)).toString().trimmed();
    switch (provider)
    {
    case OnlineMapProvider::Tianditu:
        config.credential = tiandituKeySettings().value(QStringLiteral("TrajectoryViewer/map/tianditu_key")).toString();
        break;
    case OnlineMapProvider::Esri:
        config.credential = settings.value(QStringLiteral("esriApiKey")).toString();
        break;
    case OnlineMapProvider::Cesium:
        // An explicit edit can replace the environment default without changing its legacy precedence.
        config.credential = settings.value(QStringLiteral("cesium/editor_token")).toString().trimmed();
        if (config.credential.isEmpty()) config.credential = qEnvironmentVariable("VAPORVIEW_CESIUM_ION_TOKEN").trimmed();
        if (config.credential.isEmpty()) config.credential = settings.value(QStringLiteral("cesium/ion_token")).toString();
        break;
    }
    config.credential = config.credential.trimmed();
    return config;
}

inline void saveOnlineMapResource(OnlineMapProvider provider, const OnlineMapResource& config)
{
    auto settings = onlineMapSettings();
    setPersistentSetting(settings, requestUrlSetting(provider), config.requestUrl);
    switch (provider)
    {
    case OnlineMapProvider::Tianditu:
    {
        auto appSettings = tiandituKeySettings();
        setPersistentSetting(appSettings, QStringLiteral("TrajectoryViewer/map/tianditu_key"), config.credential);
        break;
    }
    case OnlineMapProvider::Esri:
        setPersistentSetting(settings, QStringLiteral("esriApiKey"), config.credential);
        break;
    case OnlineMapProvider::Cesium:
        if (config.credential != configuredOnlineMapResource(provider).credential)
            setPersistentSetting(settings, QStringLiteral("cesium/editor_token"), config.credential);
        break;
    }
}

inline QString expandTileUrl(QString url, unsigned z, unsigned x, unsigned y, const QString& key = {})
{
    // Preserve Tianditu's numbered servers using the same bracket syntax as the default template.
    static const QRegularExpression serverSet(QStringLiteral(R"(^https://[^/?#\[\]]*\[([0-9]+)\])"),
                                              QRegularExpression::CaseInsensitiveOption);
    const auto match = serverSet.match(url);
    if (match.hasMatch())
    {
        const QString servers = match.captured(1);
        url.replace(match.capturedStart(1) - 1, servers.size() + 2,
                    servers.mid((quint64(x) + y) % servers.size(), 1));
    }
    url.replace(QStringLiteral("{z}"), QString::number(z));
    url.replace(QStringLiteral("{x}"), QString::number(x));
    url.replace(QStringLiteral("{y}"), QString::number(y));
    url.replace(QStringLiteral("{key}"), QString::fromLatin1(QUrl::toPercentEncoding(key)));
    return url;
}

inline bool validOnlineMapUrl(OnlineMapProvider provider, const QString& text)
{
    const auto endOfAuthority = text.indexOf(QRegularExpression(QStringLiteral("[/?#]")), 8);
    const QString authority = endOfAuthority < 0 ? text.mid(8) : text.mid(8, endOfAuthority - 8);
    if (!text.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive) || authority.isEmpty()
        || authority.contains(QLatin1Char('@')) || authority.contains(QLatin1Char('{'))
        || text.contains(QLatin1Char('#')))
        return false;
    if (provider != OnlineMapProvider::Cesium
        && (!text.contains(QStringLiteral("{z}")) || !text.contains(QStringLiteral("{x}"))
            || !text.contains(QStringLiteral("{y}"))))
        return false;
    const QString keyPlaceholder = QStringLiteral("{key}");
    if (provider == OnlineMapProvider::Tianditu)
    {
        static const QRegularExpression queryKey(QStringLiteral(R"([?&][^?&=]+=[^&]*\{key\})"));
        const auto queryStart = text.indexOf(QLatin1Char('?'));
        if (queryStart < 0 || text.count(keyPlaceholder) != 1 || !queryKey.match(text.mid(queryStart)).hasMatch())
            return false;
    }
    else if (text.contains(keyPlaceholder))
        return false;

    const QString expanded = provider == OnlineMapProvider::Cesium ? text : expandTileUrl(text, 1, 0, 0, QStringLiteral("key"));
    const QUrl url(expanded, QUrl::StrictMode);
    return url.isValid() && !url.host().isEmpty() && !expanded.contains(QLatin1Char('{'))
        && !expanded.contains(QLatin1Char('}'));
}

inline bool validOnlineMapCredential(const QString& credential)
{
    static const QRegularExpression invalid(QStringLiteral("[\\s\\x00-\\x1f\\x7f]"));
    return !credential.isEmpty() && !invalid.match(credential).hasMatch();
}

inline bool isOnlineMapResourceConfigured(OnlineMapProvider provider, const OnlineMapResource& config)
{
    return validOnlineMapCredential(config.credential) && validOnlineMapUrl(provider, config.requestUrl);
}

} // namespace VaporView::Map3D::Detail
