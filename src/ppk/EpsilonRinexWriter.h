#pragma once
#include <QString>
#include <atomic>
#include <cstdint>

namespace VaporView::Ppk
{
struct RinexSignal
{
    int system = 0;
    int headerIndex = 0;
    const char *code = nullptr;
};
// One policy for the manual's zero-based, receiver-specific frequency indices.
RinexSignal epsilonRinexSignal(std::uint8_t constellation, std::uint8_t frequency);
QString epsilonRinexMappingPolicy();
struct RinexWriteResult
{
    bool success = false;
    QString error;
    quint64 epochs = 0;
    quint64 observations = 0;
    quint64 unmappedObservations = 0;
};
class EpsilonRinexWriter final
{
  public:
    static RinexWriteResult write(const QString &observationStore, const QString &rinexFile, std::uint8_t receiver = 1,
                                  const std::atomic_bool *cancel = nullptr);
};
} // namespace VaporView::Ppk
