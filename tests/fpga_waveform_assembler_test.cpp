#include "FpgaWaveformAssembler.h"

#include <cassert>
#include <iostream>

using namespace VaporView::FpgaWave;

namespace
{
void put16(QByteArray& bytes, quint16 value)
{
    bytes.append(char(value & 0xFF));
    bytes.append(char(value >> 8));
}

void put32(QByteArray& bytes, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        bytes.append(char((value >> (8 * i)) & 0xFF));
}

QByteArray rawFragment(quint16 index, quint16 count, quint32 first, quint32 points,
                       const QVector<qint32>& values)
{
    QByteArray payload;
    put16(payload, 2);
    put16(payload, 24);
    put32(payload, 1000000);
    put32(payload, 4);
    put32(payload, 0);
    put16(payload, index);
    put16(payload, count);
    put32(payload, first);
    put32(payload, points);
    put32(payload, 0);
    for (qint32 value : values)
        put32(payload, static_cast<quint32>(value));
    return payload;
}
}

int main()
{
    Assembler assembler;
    const auto first = rawFragment(0, 2, 0, 2, {10, 11});
    const auto second = rawFragment(1, 2, 2, 2, {12, 13});
    assert(!assembler.accept({0x0020, 0x1000, (1u << 3) | (1u << 5), 5, 100, first}));
    const auto result = assembler.accept({0x0020, 0x1000, (1u << 3) | (1u << 5), 5, 100, second});
    assert(result.has_value());
    assert(!result->complete && !result->continuityError);
    assert(result->partial && result->overflow);
    assert(result->signed32Samples == QVector<qint32>({10, 11, 12, 13}));
    assert(result->rate == 1000000 && result->totalPoints == 4);
    std::cout << "fpga_waveform_assembler_test passed\n";
}
