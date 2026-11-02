#pragma once
#include "shared/session/HarmonicWaveform.h"

namespace VaporView::Ground::Session
{
// The recorder enforces the policy; UI enabled states are only presentation.
class SessionCalibrationPolicy
{
public:
    virtual ~SessionCalibrationPolicy() = default;
    virtual bool prepare(QJsonObject& snapshot, QString *error) = 0;
    virtual bool bindSession(const QString& directory, QString *error) = 0;
    virtual void finish() = 0;
    virtual QJsonObject concentration(quint64 host, quint64 sequence,
        const VaporView::Session::HarmonicLayout&, const FpgaWave::NormalizedFeature&) = 0;
};
}
