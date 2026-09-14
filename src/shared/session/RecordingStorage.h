#pragma once

#include <QFile>
#include <QIODevice>

namespace VaporView
{
// Narrow storage boundary shared by both recorders. Tests can reproduce real
// short writes and flush failures without changing the parser or recorder.
class RecordingStorage
{
public:
    virtual ~RecordingStorage() = default;
    virtual qint64 write(QFile& file, const char *data, qint64 size)
    {
        return file.write(data, size);
    }
    virtual bool flush(QFile& file) { return file.flush(); }
};

class RecordingOutputDevice final : public QIODevice
{
public:
    RecordingOutputDevice(RecordingStorage& storage, QFile& file)
        : storage_(storage), file_(file) { open(WriteOnly | Unbuffered); }
    bool isSequential() const override { return true; }
protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *data, qint64 size) override
    {
        return storage_.write(file_, data, size);
    }
private:
    RecordingStorage& storage_;
    QFile& file_;
};
}
