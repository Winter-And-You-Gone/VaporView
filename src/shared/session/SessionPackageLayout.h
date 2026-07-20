#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace VaporView::Session
{

enum class SessionFileKind
{
    SensorSummaryCsv,
    TemperatureControllerCsv,
    WaveformFeaturesCsv,
    NavigationRaw,
    PressureRaw,
    TemperatureHumidityRaw,
    DistanceRaw,
    WaveformRaw,
    WaveformPeaksCsv
};

struct SessionPackageLayout
{
    QString manifestPath;
    QString sensorSummaryCsvPath;
    QString temperatureControllerCsvPath;
    QString waveformFeaturesCsvPath;
    QString navigationRawPath;
    QString pressureRawPath;
    QString temperatureHumidityRawPath;
    QString distanceRawPath;
    QString waveformRawPath;
    QString waveformPeaksCsvPath;
    QString eventLogPath;
    QString errorLogPath;
    QString deviceConfigPath;
    QString rawFormatDocumentPath;
};

struct RawFileDefinition
{
    QString key;
    QString relativePath;
    SessionFileKind kind = SessionFileKind::NavigationRaw;
    quint16 sourceId = 0;
};

struct SessionPathAliases
{
    QString preferredPath;
    QStringList legacyPaths;
    QStringList manifestPathKeys;
    QStringList manifestRawFileKeys;
};

const SessionPackageLayout& standardSessionPackageLayout();
QStringList standardSessionDirectories();
QStringList standardSessionFiles();
QVector<RawFileDefinition> standardRawFileDefinitions();
const SessionPathAliases& sessionPathAliases(SessionFileKind kind);

QString sessionPackageFilePath(const QString& sessionDirectory, const QString& relativePath);

QString waveformFeaturesCsvHeader();
QString temperatureControllerCsvHeader();
QString tcpWavePeaksCsvHeader();
QString eventLogCsvHeader();
QString rawDatFormatDocumentText();

}  // namespace VaporView::Session
