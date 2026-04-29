#include "VaporViewBackends.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>

namespace
{
QIcon loadApplicationIcon()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList iconCandidates = {
        QDir(appDir).filePath(QStringLiteral("resources/app.ico")),
        QDir(appDir).filePath(QStringLiteral("../resources/app.ico")),
        QDir(appDir).filePath(QStringLiteral("../../resources/app.ico")),
    };

    for (const QString& path : iconCandidates)
    {
        if (!QFileInfo::exists(path))
        {
            continue;
        }
        QIcon icon(path);
        if (!icon.isNull())
        {
            return icon;
        }
    }
    return {};
}
}  // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("VaporView"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));
    app.setOrganizationName(QStringLiteral("VaporView"));

    const QIcon icon = loadApplicationIcon();
    if (!icon.isNull())
    {
        app.setWindowIcon(icon);
    }

    qRegisterMetaType<VaporView::TcpFloatEncoding>("VaporView::TcpFloatEncoding");
    qRegisterMetaType<QVector<float>>("QVector<float>");

    AppBackend appBackend;
    DeviceBackend deviceBackend;
    WaveformBackend waveformBackend;
    RecordingBackend recordingBackend(&deviceBackend, &waveformBackend);
    RtkBackend rtkBackend(&deviceBackend);
    SessionBackend sessionBackend;
    RawParserBackend rawParserBackend;
    SettingsBackend settingsBackend(&appBackend, &recordingBackend);

    QObject::connect(&appBackend, &AppBackend::languageChanged, &deviceBackend, [&]() {
        deviceBackend.setEnglish(appBackend.english());
    });
    deviceBackend.setEnglish(appBackend.english());

    QObject::connect(&waveformBackend, &WaveformBackend::connectedChanged, &deviceBackend, [&]() {
        deviceBackend.setWaveformDeviceState(
            waveformBackend.connected(),
            QStringLiteral("%1:%2").arg(waveformBackend.host()).arg(waveformBackend.port()),
            waveformBackend.frameRate());
    });
    QObject::connect(&waveformBackend, &WaveformBackend::frameRateChanged, &deviceBackend, [&]() {
        deviceBackend.setWaveformDeviceState(
            waveformBackend.connected(),
            QStringLiteral("%1:%2").arg(waveformBackend.host()).arg(waveformBackend.port()),
            waveformBackend.frameRate());
    });
    QObject::connect(&recordingBackend, &RecordingBackend::sessionCompleted, &sessionBackend, [&](const QString&) {
        sessionBackend.setRecordingDirectory(recordingBackend.recordingDirectory());
        sessionBackend.refreshSessions();
    });

    auto appendNotificationToLog = [&deviceBackend](const QString& level, const QString& message) {
        deviceBackend.appendLogLine(message, level);
    };
    QObject::connect(&waveformBackend, &WaveformBackend::notificationRequested, &deviceBackend, appendNotificationToLog);
    QObject::connect(&recordingBackend, &RecordingBackend::notificationRequested, &deviceBackend, appendNotificationToLog);
    QObject::connect(&rtkBackend, &RtkBackend::notificationRequested, &deviceBackend, appendNotificationToLog);
    QObject::connect(&sessionBackend, &SessionBackend::notificationRequested, &deviceBackend, appendNotificationToLog);
    QObject::connect(&rawParserBackend, &RawParserBackend::notificationRequested, &deviceBackend, appendNotificationToLog);
    QObject::connect(&settingsBackend, &SettingsBackend::notificationRequested, &deviceBackend, appendNotificationToLog);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appBackend"), &appBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("deviceBackend"), &deviceBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("recordingBackend"), &recordingBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("waveformBackend"), &waveformBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("rtkBackend"), &rtkBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("sessionBackend"), &sessionBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("rawParserBackend"), &rawParserBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &settingsBackend);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() {
        QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("VaporView"), QStringLiteral("Main"));

    return app.exec();
}
