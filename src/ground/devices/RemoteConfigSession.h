#pragma once

#include "SkyConfig.h"
#include "ground/devices/RemoteSkyController.h"

namespace VaporView::Ground::Devices
{

// Widget-free desired/confirmed configuration and connection-scoped transactions.
class RemoteConfigSession
{
public:
    SkyConfig desired = SkyConfig::defaults();
    SkyConfig baseline = SkyConfig::defaults();
    bool loaded = false, dirty = false, reading = false, applying = false, saving = false;
    quint16 readSequence = 0, applySequence = 0, saveSequence = 0;
    quint64 readGeneration = 0, applyGeneration = 0, loadedGeneration = 0;

    bool pending() const { return reading || applying || saving; }
    quint16 beginRead(RemoteSkyController& remote);
    quint16 beginApply(RemoteSkyController& remote, const SkyConfig& config);
    quint16 beginSave(RemoteSkyController& remote);
    bool currentRead(const RemoteSkyController* remote) const;
    bool currentApply(const RemoteSkyController* remote) const;
    void acceptRead(const SkyConfig& config, quint64 generation, bool migrated = false);
    bool completeApply(bool success, quint64 generation);
    bool timeout(CommandId command, quint16 sequence);
};

} // namespace VaporView::Ground::Devices
