#include "TcpWaveEncoding.h"

#include <QByteArray>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{

QByteArray encodeValues(const std::vector<float>& values, VaporView::TcpFloatEncoding encoding)
{
    QByteArray payload;
    payload.reserve(static_cast<int>(values.size() * sizeof(float)));
    for (float value : values)
    {
        quint32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        const char little[4] = {
            static_cast<char>(bits & 0xFFu),
            static_cast<char>((bits >> 8) & 0xFFu),
            static_cast<char>((bits >> 16) & 0xFFu),
            static_cast<char>((bits >> 24) & 0xFFu),
        };
        const char big[4] = {
            little[3],
            little[2],
            little[1],
            little[0],
        };
        const char wordSwapped[4] = {
            little[0],
            little[1],
            little[3],
            little[2],
        };

        switch (encoding)
        {
        case VaporView::TcpFloatEncoding::LittleEndian:
            payload.append(little, sizeof(little));
            break;
        case VaporView::TcpFloatEncoding::BigEndian:
            payload.append(big, sizeof(big));
            break;
        case VaporView::TcpFloatEncoding::WordSwappedLittleEndian:
            payload.append(wordSwapped, sizeof(wordSwapped));
            break;
        case VaporView::TcpFloatEncoding::Unknown:
        default:
            payload.append(little, sizeof(little));
            break;
        }
    }
    return payload;
}

bool almostEqual(float lhs, float rhs)
{
    return std::fabs(static_cast<double>(lhs) - static_cast<double>(rhs)) < 1.0e-5;
}

bool checkPayload(const std::vector<float>& expected, VaporView::TcpFloatEncoding encoding)
{
    const QByteArray payload = encodeValues(expected, encoding);
    const QVector<float> decoded = VaporView::decodeTcpFloatPayload(payload, encoding);
    if (decoded.size() != static_cast<int>(expected.size()))
    {
        std::cerr << "decoded size mismatch\n";
        return false;
    }
    for (int i = 0; i < decoded.size(); ++i)
    {
        if (!almostEqual(decoded[i], expected[static_cast<size_t>(i)]))
        {
            std::cerr << "decoded value mismatch at " << i << ": " << decoded[i]
                      << " != " << expected[static_cast<size_t>(i)] << "\n";
            return false;
        }
    }

    const VaporView::TcpFloatEncoding detected = VaporView::autoDetectTcpFloatEncoding(payload);
    if (detected != encoding)
    {
        std::cerr << "auto-detect mismatch\n";
        return false;
    }
    return true;
}

}  // namespace

int main()
{
    std::vector<float> values;
    values.reserve(128);
    for (int i = 0; i < 128; ++i)
    {
        values.push_back(static_cast<float>(0.25 + std::sin(i * 0.07) * 0.4 + i * 0.002));
    }

    int failures = 0;
    failures += checkPayload(values, VaporView::TcpFloatEncoding::LittleEndian) ? 0 : 1;
    failures += checkPayload(values, VaporView::TcpFloatEncoding::BigEndian) ? 0 : 1;
    failures += checkPayload(values, VaporView::TcpFloatEncoding::WordSwappedLittleEndian) ? 0 : 1;

    const quint32 littleFlags = VaporView::tcpFloatEncodingToRawDatFlags(VaporView::TcpFloatEncoding::LittleEndian);
    const quint32 bigFlags = VaporView::tcpFloatEncodingToRawDatFlags(VaporView::TcpFloatEncoding::BigEndian);
    const quint32 swappedFlags = VaporView::tcpFloatEncodingToRawDatFlags(VaporView::TcpFloatEncoding::WordSwappedLittleEndian);
    const quint32 unknownFlags = VaporView::tcpFloatEncodingToRawDatFlags(VaporView::TcpFloatEncoding::Unknown);
    failures += VaporView::tcpFloatEncodingFromRawDatFlags(littleFlags) == VaporView::TcpFloatEncoding::LittleEndian ? 0 : 1;
    failures += VaporView::tcpFloatEncodingFromRawDatFlags(bigFlags) == VaporView::TcpFloatEncoding::BigEndian ? 0 : 1;
    failures += VaporView::tcpFloatEncodingFromRawDatFlags(swappedFlags) == VaporView::TcpFloatEncoding::WordSwappedLittleEndian ? 0 : 1;
    failures += VaporView::tcpFloatEncodingFromRawDatFlags(unknownFlags) == VaporView::TcpFloatEncoding::Unknown ? 0 : 1;

    const QByteArray legacyPayload = encodeValues(values, VaporView::TcpFloatEncoding::BigEndian);
    failures += VaporView::autoDetectTcpFloatEncoding(legacyPayload) == VaporView::TcpFloatEncoding::BigEndian ? 0 : 1;

    return failures == 0 ? 0 : 1;
}
