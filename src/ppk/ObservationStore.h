#pragma once

#include "EpsilonRawSatellite.h"
#include <QByteArray>
#include <QFile>
#include <QString>
#include <functional>

namespace VaporView::Ppk
{
QByteArray encodeEpoch(const RawSatelliteEpoch &epoch);
bool decodeEpoch(const QByteArray &bytes, RawSatelliteEpoch &epoch);

struct ObservationReadResult
{
    bool success = false;
    bool recoveredTail = false;
    quint64 epochs = 0;
    QString error;
};

class ObservationStore final
{
  public:
    ~ObservationStore()
    {
        close();
    }
    bool append(const QString &sessionDirectory, const RawSatelliteEpoch &epoch, QString *error = nullptr);
    void close();
    static QString filename(const QString &sessionDirectory);
    static ObservationReadResult read(const QString &filename,
                                      const std::function<bool(const RawSatelliteEpoch &)> &consume);

  private:
    QFile file_;
    quint64 count_ = 0;
    quint64 firstUtcNs_ = 0;
    quint64 lastUtcNs_ = 0;
};
} // namespace VaporView::Ppk
