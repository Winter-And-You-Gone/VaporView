#ifndef VAPORVIEW_EPSILON_PROTOCOL_H
#define VAPORVIEW_EPSILON_PROTOCOL_H

#include "data_types.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace VaporView::EpsilonProtocol
{

bool decodeCorePacket(EpsilonData& data,
                      std::uint8_t packetId,
                      const std::uint8_t *payload,
                      std::size_t payloadSize);

void resolveAttitudeState(EpsilonData& data,
                          std::chrono::steady_clock::time_point now);

bool packetRateCommandAccepted(const std::string& response,
                               std::uint8_t packetId,
                               int expectedRateHz);

}  // namespace VaporView::EpsilonProtocol

#endif
