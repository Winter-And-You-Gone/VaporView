#pragma once
#include "ground/devices/LocalDeviceConnectionController.h"
#include <QJsonObject>
#include <QObject>
#include <functional>

namespace VaporView::Ground::Devices {
using FpgaInstrumentCall=std::function<QJsonObject(const QJsonObject&)>;
using FpgaInstrumentReply=std::function<void(const QJsonObject&)>;
using FpgaInstrumentSubmit=std::function<void(const QJsonObject&,FpgaInstrumentReply)>;
// Call only from an instrument worker. USB and the GUI keep their own event loops.
FpgaInstrumentCall makeFpgaInstrumentCall(QObject* owner,FpgaInstrumentSubmit submit);
LocalAi8OperationResult readFpgaAi8Page(const FpgaInstrumentCall& call,
    Ai8TemperatureControllerProtocol::Page page,const Ai8TemperatureControllerProtocol::Selection& selection);
LocalAi8OperationResult writeFpgaAi8Page(const FpgaInstrumentCall& call,
    const Ai8TemperatureControllerProtocol::PageData& data);
LocalTemperatureCommandResult sendFpgaTemperatureCommand(const FpgaInstrumentCall& call,
    CommandId command,const TemperatureControllerCommand& payload);
}
