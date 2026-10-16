#include "ground/devices/RemoteConfigSession.h"

namespace VaporView::Ground::Devices
{
quint16 RemoteConfigSession::beginRead(RemoteSkyController& remote)
{
    if (pending()) return 0;
    readGeneration = remote.linkGeneration();
    readSequence = remote.isOpen() && !pending() ? remote.requestSkyConfig() : 0;
    reading = readSequence != 0;
    if (!reading) readGeneration = 0;
    return readSequence;
}

quint16 RemoteConfigSession::beginApply(RemoteSkyController& remote, const SkyConfig& config)
{
    if (!remote.isOpen() || pending() || !config.validate()) return 0;
    desired = config;
    applyGeneration = remote.linkGeneration();
    applySequence = remote.telemetryService()->setSkyConfig(config.toJson());
    applying = applySequence != 0;
    if (!applying) applyGeneration = 0;
    return applySequence;
}

quint16 RemoteConfigSession::beginSave(RemoteSkyController& remote)
{
    if (pending()) return 0;
    saveSequence = remote.isOpen() && !pending() ? remote.telemetryService()->saveSkyConfig() : 0;
    saving = saveSequence != 0;
    return saveSequence;
}

bool RemoteConfigSession::currentRead(const RemoteSkyController* remote) const
{
    return remote && remote->isOpen() && remote->linkGeneration() == readGeneration;
}

bool RemoteConfigSession::currentApply(const RemoteSkyController* remote) const
{
    return remote && remote->isOpen() && remote->linkGeneration() == applyGeneration;
}

void RemoteConfigSession::acceptRead(const SkyConfig& config, quint64 generation, bool migrated)
{
    desired = baseline = config;
    loaded = true;
    reading = false;
    loadedGeneration = generation;
    dirty = migrated;
}

bool RemoteConfigSession::completeApply(bool success, quint64 generation)
{
    applying = false;
    const bool pathChanged = baseline.sensor_transport_path != desired.sensor_transport_path;
    if (success) {
        baseline = desired;
        loaded = true;
        loadedGeneration = generation;
        dirty = false;
    } else {
        desired.sensor_transport_path = baseline.sensor_transport_path;
        desired.fpga.configuration.pressureSource = baseline.fpga.configuration.pressureSource;
        dirty = true;
    }
    return success && pathChanged;
}

bool RemoteConfigSession::timeout(CommandId command, quint16 sequence)
{
    if (command == CommandId::GetSkyConfig && sequence == readSequence && reading) reading = false;
    else if (command == CommandId::SetSkyConfig && sequence == applySequence && applying) { applying = false; dirty = true; }
    else if (command == CommandId::SaveSkyConfig && sequence == saveSequence && saving) saving = false;
    else return false;
    return true;
}
} // namespace VaporView::Ground::Devices
