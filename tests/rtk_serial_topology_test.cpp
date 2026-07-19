#include "ground/rtk/RtkStreamService.h"

#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    require(serialPortNamesReferToSamePort(QStringLiteral("COM3"), QStringLiteral("com3")),
            "Windows COM names compare case-insensitively");
    require(serialPortNamesReferToSamePort(QStringLiteral("COM12"), QStringLiteral("\\\\.\\COM12")),
            "Win32 device prefix resolves to the same COM port");
    require(!serialPortNamesReferToSamePort(QStringLiteral("COM3"), QStringLiteral("COM4")),
            "different COM ports remain distinct");
    require(!serialPortNamesReferToSamePort(QString(), QStringLiteral("COM3")),
            "an empty port never conflicts");

    std::cout << "rtk_serial_topology_test passed\n";
    return 0;
}
