#pragma once

#include "data_types.h"
#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>
#include <array>
#include <functional>

namespace VaporView::Tec103GpioProtocol {

using Call = std::function<QJsonObject(const QJsonObject&)>;
using Values = QMap<QByteArray, qint64>;

struct Result {
    bool success = false;
    bool unverified = false;
    QString message;
    TemperatureControllerData data;
    std::array<bool, 2> channelValid{{false, false}};
    int logicalChannel = 0;
};

struct Setting {
    QByteArray key;
    qint64 value = 0;
};

// The two logical channels select two physical UARTs. Every instrument uses TC1.
bool parseValues(const QByteArray& response, Values& values, QString* error = nullptr);
Result read(const Call& call, int logicalChannel, bool full);
inline Result readData(const Call& call, int logicalChannel, bool full) { return read(call, logicalChannel, full); }
Result writeVerified(const Call& call, int logicalChannel, const QVector<Setting>& settings);

}
