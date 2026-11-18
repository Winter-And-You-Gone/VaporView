#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <functional>
#include <limits>
#include <memory>
#include <optional>

#if defined(VAPORVIEW_SESSION_SDK_BUILD)
#  define VAPORVIEW_SESSION_API Q_DECL_EXPORT
#elif defined(VAPORVIEW_SESSION_SDK_STATIC)
#  define VAPORVIEW_SESSION_API
#else
#  define VAPORVIEW_SESSION_API Q_DECL_IMPORT
#endif

namespace VaporView::SessionSdk
{

enum class Category {
    Sensors, NavigationRaw, PressureRaw, TemperatureHumidityRaw, DistanceRaw,
    Waveforms, CycleFeatures, TcpPeaks, Concentrations
};

enum class Status { Ok, Missing, InvalidArgument, Corrupt, IoError, Cancelled };

struct Result {
    Status status = Status::Ok;
    QString message;
    quint64 records = 0;
    explicit operator bool() const { return status == Status::Ok; }
};

struct Range {
    quint64 firstUs = 0;
    quint64 lastUs = std::numeric_limits<quint64>::max();
};

struct Record {
    quint64 ordinal = 0;
    quint64 timestampUs = 0;
    std::optional<quint64> sequence;
    QJsonObject fields;
};

struct Waveform {
    Record record;
    // FPGA H1/H2 or TCP RAW/harmonic. A valid incomplete cycle has empty vectors.
    QVector<double> h1;
    QVector<double> h2;
    QVector<float> raw;
    QVector<float> harmonic;
};

VAPORVIEW_SESSION_API QString version();
VAPORVIEW_SESSION_API QString categoryName(Category category);

class VAPORVIEW_SESSION_API Reader final
{
public:
    using Visitor = std::function<bool(const Record&)>;

    Reader();
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    Result open(const QString& sessionDirectory);
    QJsonObject metadata() const;
    QList<Category> categories() const;
    Result visit(Category category, Range range, const Visitor& visitor) const;
    Result readWaveform(quint64 ordinal, Waveform& frame) const;
    // Each category has one atomically written output in the supplied directory.
    Result exportTo(Category category, Range range, const QString& directory) const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace VaporView::SessionSdk
