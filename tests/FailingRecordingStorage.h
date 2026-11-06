#pragma once
#include "shared/session/RecordingStorage.h"
#include <atomic>
#include <algorithm>
#include <mutex>

class FailingRecordingStorage final : public VaporView::RecordingStorage
{
public:
    std::atomic_bool failWrites{false};
    std::atomic_bool failFlush{false};
    std::atomic_bool shortWriteObserved{false};
    void setFailWriteFile(const QString& fileName)
    {
        std::lock_guard<std::mutex> lock(failWriteFileMutex_);
        failWriteFile_ = fileName;
    }
    qint64 write(QFile& file, const char *data, qint64 size) override
    {
        bool shouldFail = failWrites.load();
        if (shouldFail) {
            std::lock_guard<std::mutex> lock(failWriteFileMutex_);
            shouldFail = failWriteFile_.isEmpty() || file.fileName() == failWriteFile_;
        }
        const qint64 written = file.write(data, shouldFail ? std::min<qint64>(size, 1) : size);
        if (shouldFail && written >= 0 && written < size) shortWriteObserved.store(true);
        return written;
    }
    bool flush(QFile& file) override
    {
        return failFlush.load() ? false : file.flush();
    }

private:
    std::mutex failWriteFileMutex_;
    QString failWriteFile_;
};
