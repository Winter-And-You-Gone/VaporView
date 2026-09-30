#pragma once
#include "ppk/PpkNavigation.h"
#include <QFile>

namespace VaporView::Ppk
{
// 0x50 carries UTC and attitude in the same device packet, independently of host time.
class AttitudeStore final
{
  public:
    ~AttitudeStore()
    {
        close();
    }
    bool appendSystemState(const QString &session, quint64 hostUs, const QByteArray &frame);
    void close();
    static bool read(const QString &session, std::vector<AttitudeSample> &samples, QString *error = nullptr);

  private:
    QFile file_;
};
} // namespace VaporView::Ppk
