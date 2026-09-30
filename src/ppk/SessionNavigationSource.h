#pragma once
#include "ppk/PpkNavigation.h"
#include <QHash>
#include <QStringList>
#include <QObject>

namespace VaporView::Ppk
{
enum class NavigationSource
{
    Original,
    Ppk
};
NavigationSource sessionNavigationSource(const QString &sessionDirectory);
bool setSessionNavigationSource(const QString &sessionDirectory, NavigationSource source, QString *error = nullptr);

class SessionNavigationEvents final : public QObject
{
    Q_OBJECT
  public:
    static SessionNavigationEvents *instance();
    void notify(const QString &session)
    {
        emit changed(session);
    }
  signals:
    void changed(const QString &session);
};

// Shared coordinate boundary for Session readers, rendering, heat and export.
// Original source files are never modified. Uncovered PPK rows have no position.
class SessionNavigationResolver final
{
  public:
    explicit SessionNavigationResolver(const QString &sessionDirectory);
    bool usesPpk() const
    {
        return source_ == NavigationSource::Ppk;
    }
    bool available() const
    {
        return !usesPpk() || !trajectory_.empty();
    }
    QString error() const
    {
        return error_;
    }
    std::optional<PpkSample> atTimestamp(std::uint64_t sessionUs) const;
    void prepareCsv(const QStringList &headers);
    void applyCsvRow(QStringList &row) const;

  private:
    NavigationSource source_ = NavigationSource::Original;
    PpkTrajectory trajectory_;
    QString error_;
    QHash<QString, int> columns_;
};
} // namespace VaporView::Ppk
