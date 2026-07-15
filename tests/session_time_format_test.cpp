#include "ground/session/SessionTimeFormat.h"

#include <QCoreApplication>
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

void testBeijingSummaryTime()
{
    const QString start = VaporView::formatSessionMetadataTimeBeijing(QStringLiteral("2026-06-16T02:50:09.689Z"));
    const QString end = VaporView::formatSessionMetadataTimeBeijing(QStringLiteral("2026-06-16T02:52:42Z"));

    require(start == QStringLiteral("2026-06-16 10:50:09 UTC+8"), "start time converted to Beijing time");
    require(end == QStringLiteral("2026-06-16 10:52:42 UTC+8"), "end time converted to Beijing time");
}

void testDurationStillUsesUtcInstant()
{
    const QString duration = VaporView::formatSessionDurationText(
        QStringLiteral("2026-06-16T02:50:09.689Z"),
        QStringLiteral("2026-06-16T02:52:42.942Z"),
        false);

    require(duration == QStringLiteral("2分 33秒"), "duration ignores display timezone");
}

void testFallbacks()
{
    require(VaporView::formatSessionMetadataTimeBeijing(QString()) == QStringLiteral("---"), "empty time fallback");
    require(VaporView::formatSessionMetadataTimeBeijing(QStringLiteral("bad-time")) == QStringLiteral("bad-time"), "invalid time fallback");
    require(VaporView::formatSessionDurationText(QStringLiteral("bad-time"), QStringLiteral("2026-06-16T02:52:42Z"), false) == QStringLiteral("---"), "invalid duration fallback");
}

}  // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    testBeijingSummaryTime();
    testDurationStillUsesUtcInstant();
    testFallbacks();

    std::cout << "session_time_format_test passed\n";
    return 0;
}
