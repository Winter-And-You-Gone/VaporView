#pragma once
#include "shared/session/RecordingStorage.h"
#include <atomic>
#include <algorithm>

class FailingRecordingStorage final : public VaporView::RecordingStorage
{
public:
    std::atomic_bool failWrites{false};
    std::atomic_bool failFlush{false};
    qint64 write(QFile& file, const char *data, qint64 size) override
    {
        return file.write(data, failWrites.load() ? std::min<qint64>(size, 1) : size);
    }
    bool flush(QFile& file) override
    {
        return failFlush.load() ? false : file.flush();
    }
};
