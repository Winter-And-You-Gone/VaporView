#pragma once

#include "FpgaSensorDecoder.h"
#include "FpgaWaveformAssembler.h"
#include <QJsonObject>
#include <QString>

namespace VaporView::FpgaRemote
{
// Eight little-endian u32 words, schema 1; these are not sensor TLVs.
QJsonObject decodeManagementEvent(quint8 type, quint16 source, quint16 message,
                                 quint64 tick, const QByteArray& payload);
bool validateControl(const QJsonObject& control, QString *error = nullptr);
QByteArray encodeControl(const QJsonObject& control);
bool parseControl(const QByteArray& payload, QJsonObject& control, QString *error = nullptr);
// Registers use decimal address keys and uint32 numeric values. All 64-bit
// counters/timestamps use decimal strings to avoid JSON precision loss.
QByteArray encodeStatus(const QJsonObject& status);
bool parseStatus(const QByteArray& payload, QJsonObject& status);
QByteArray encodeSensor(const FpgaSensor::Reading& reading, quint64 hostTimestampUs);
bool parseSensor(const QByteArray& payload, FpgaSensor::Reading& reading,
                 quint64 *hostTimestampUs = nullptr);
QByteArray encodeWaveformPreview(const FpgaWave::CompletedStream& stream);
bool parseWaveformPreview(const QByteArray& payload, FpgaWave::CompletedStream& stream);
}
