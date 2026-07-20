#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace VaporView::Session
{

struct SessionPackageLayout
{
    QString manifestPath;
    QString devicesCsvPath;
    QString temperatureControllerCsvPath;
    QString waveformFeaturesCsvPath;
    QString epsilonRawPath;
    QString ptbRawPath;
    QString hmpRawPath;
    QString lidarRawPath;
    QString tcpWaveRawPath;
    QString tcpWavePeaksCsvPath;
    QString eventLogPath;
    QString errorLogPath;
    QString deviceConfigPath;
    QString rawFormatDocumentPath;
};

struct RawFileDefinition
{
    QString key;
    QString relativePath;
    quint16 sourceId = 0;
};

const SessionPackageLayout& standardSessionPackageLayout();
QStringList standardSessionDirectories();
QStringList standardSessionFiles();
QVector<RawFileDefinition> standardRawFileDefinitions();

QString sessionPackageFilePath(const QString& sessionDirectory, const QString& relativePath);

QString waveformFeaturesCsvHeader();
QString temperatureControllerCsvHeader();
QString tcpWavePeaksCsvHeader();
QString eventLogCsvHeader();
QString rawDatFormatDocumentText();

}  // namespace VaporView::Session
