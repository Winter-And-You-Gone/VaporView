#pragma once

#include "SimulatorSounding.h"
#include <QCommandLineParser>
#include <QTextStream>

namespace VaporView {
inline void addSimulatorModeOptions(QCommandLineParser &parser)
{
    parser.addOption({"simulate-mode", QStringLiteral("模拟数据模式（默认 realistic）"), "realistic|random", "realistic"});
    parser.addOption({"simulate-random", QStringLiteral("等价于 --simulate-mode random")});
}

inline bool readSimulatorMode(const QCommandLineParser &parser, SimulatorDataMode &mode)
{
    const QString value = parser.value("simulate-mode");
    if (value != "realistic" && value != "random") {
        QTextStream(stderr) << QStringLiteral("--simulate-mode 必须为 realistic 或 random，收到：") << value << Qt::endl;
        return false;
    }
    mode = value == "random" || parser.isSet("simulate-random") ? SimulatorDataMode::Random : SimulatorDataMode::Realistic;
    return true;
}

inline void printSimulatorMode(SimulatorDataMode mode)
{
    QTextStream(stdout) << QStringLiteral("数据模式：") << (mode == SimulatorDataMode::Random ? "random" : "realistic") << Qt::endl;
}
}
