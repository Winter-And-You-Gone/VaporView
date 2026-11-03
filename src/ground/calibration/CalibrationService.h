#pragma once
#include "CalibrationFile.h"
#include "RealtimePeakBus.h"
#include "ground/session/SessionCalibrationPolicy.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <QScopeGuard>

namespace VaporView::Ground::Calibration
{
struct CalibrationView
{
    CalibrationProject project;
    CalibrationFile selected;
    QStringList files, drafts;
    QMap<int, ConcentrationResult> live;
    QMap<int, double> recentPeaks;
    QString message, identityWarning, recoveredSession;
    QJsonObject inspectedDocument;
    quint64 inspection = 0;
    bool connected = false, acquiring = false, locked = false, error = false;
    int activeStage = -1;
};
// Owns acquisition and file work independently of any page. One worker drains a
// bounded peak mailbox and serializes commands; no file/fit work runs in UI slots.
class CalibrationService final : public QObject, public Session::SessionCalibrationPolicy
{
    Q_OBJECT
public:
    using Clock = std::function<quint64()>;
    explicit CalibrationService(QString installationDirectory = {}, Clock clock = {}, QObject *parent = nullptr);
    ~CalibrationService() override;
    CalibrationView view() const;
    QJsonObject diagnostics() const;
    void offer(const PeakRecord&);
    void setDevice(bool connected, const QJsonObject& identity);
    void setExternalSessionOpen(bool open);
    void setEnvironment(const Environment&, const QString& alignment);
    void noteSensor(const QString& source, const QJsonObject& reading);
    void createProject(const QString& name, const QString& unit);
    void addStage(double concentration, quint64 durationMs = 600000, const QString& note = {});
    void editStage(int index, double concentration, quint64 durationMs, const QString& note);
    void moveStage(int index, int delta);
    void removeStage(int index);
    void startStage(int index, bool identityConfirmed = false);
    void pauseStage(bool stopped = false);
    void exclude(const QString& stage, int wavelength, const QSet<QString>& ids, bool excluded, const QString& reason);
    void fitProject();
    void generateFile(const QString& name);
    void openProject(const QString& filename, bool draft);
    void selectFile(const QString& filename, bool identityConfirmed = false);
    void copyFile(const QString& filename, const QString& name);
    void removeFile(const QString& filename, bool archive);
    void importLegacy(const QString& libraryFilename, const QString& fit816, const QString& fit1368);
    void refreshFiles();
    void inspectFile(const QString& filename);
    void finishRecoveredSession();
    // Flushes all earlier work; deterministic tests inject a monotonic clock.
    void synchronize();
    bool prepare(QJsonObject& snapshot, QString *error) override;
    bool bindSession(const QString& directory, QString *error) override;
    void finish() override;
    QJsonObject concentration(quint64 host, quint64 sequence,
        const VaporView::Session::HarmonicLayout&, const FpgaWave::NormalizedFeature&) override;
signals:
    void changed();
private:
    struct PendingPeak { PeakRecord record; quint64 time = 0, offeredNs = 0, serial = 0; QString stageId; };
    struct Command { std::function<void()> work; quint64 throughPeak = 0; };
    CalibrationFileStore store_;
    Clock clock_;
    mutable std::recursive_mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<PendingPeak> peaks_;
    std::deque<Command> commands_;
    std::thread worker_;
    bool stopping_ = false, dirty_ = false, overflowed_ = false, externalSessionOpen_ = false;
    CalibrationView state_;
    QJsonObject device_;
    QSet<QString> recordedIds_;
    QMap<int, quint64> latestValid_;
    quint64 cursor_ = 0, wallCursor_ = 0, lastSave_ = 0, lastNotify_ = 0;
    Environment environment_;
    QString alignment_ = QStringLiteral("unavailable");
    QJsonObject sensorReadings_;
    QJsonObject lockSnapshot_;
    quint64 inputCount_ = 0, processedCount_ = 0, droppedCount_ = 0, duplicateCount_ = 0;
    quint64 admittedSerial_ = 0;
    quint64 validCount_ = 0, invalidCount_ = 0, recordedCount_ = 0, abandonedCount_ = 0;
    quint64 queuePeak_ = 0, maximumLatencyNs_ = 0, totalLatencyNs_ = 0, maximumAdmissionNs_ = 0;
    quint64 saveCount_ = 0, saveFailureCount_ = 0, maximumSaveNs_ = 0;
    quint64 lastSaveAttempt_ = 0, autoPauseCount_ = 0;
    quint64 draftBudgetBytes_ = 0, capacityRejectedCount_ = 0;
    QString lastAutoPauseReason_;
    QMap<int, quint64> laneMaximumLatencyNs_, laneProcessedCount_;
    QString sessionDirectory_;
    void submit(std::function<void()>);
    void run();
    void process(const PendingPeak&);
    void tick();
    bool persist(bool acquisitionProgress = false);
    void pause(const QString&, bool stopped = false, bool automatic = true);
    void message(const QString&, bool error = false);
    bool editable();
    QString preflight(bool identityConfirmed) const;
    bool saveLock(QString *error);
    void restoreLock();
    void listFiles();
    bool prepareImpl(QJsonObject&,QString *error);
    template<class Work> auto outsideStateLock(Work work)
    {
        mutex_.unlock();const auto relock=qScopeGuard([this]{mutex_.lock();});return work();
    }
};
}
