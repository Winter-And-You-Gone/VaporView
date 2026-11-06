#include "LogContext.h"
#include "data_collector.h"
#include <QString>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

static void require(bool value, const char *message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

int main()
{
    using namespace VaporView;
    require(!currentLogContext(), "initial context is empty");
    {
        ScopedLogContext outer({QStringLiteral("outer"), QStringLiteral("session")});
        require(currentLogContext()->correlationId == QStringLiteral("outer"), "outer context active");
        try {
            ScopedLogContext nested({QStringLiteral("nested"), {}});
            require(currentLogContext()->correlationId == QStringLiteral("nested"), "nested context active");
            throw 1;
        } catch (int) {}
        require(currentLogContext()->correlationId == QStringLiteral("outer"), "exception restores outer context");
        QString first, second, secondSession;
        const LogContext captured = *currentLogContext();
        std::thread a([&] {
            require(!currentLogContext(), "worker does not implicitly inherit context");
            { ScopedLogContext scope(captured); first = currentLogContext()->correlationId; }
            require(!currentLogContext(), "worker context cleared on completion");
        });
        std::thread b([&] {
            require(!currentLogContext(), "independent worker starts empty");
            { ScopedLogContext scope({QStringLiteral("other"), {}});
              second = currentLogContext()->correlationId; secondSession = currentLogContext()->sessionId; }
            require(!currentLogContext(), "independent worker context cleared");
        });
        a.join(); b.join();
        require(first == QStringLiteral("outer") && second == QStringLiteral("other") && secondSession.isEmpty(),
                "explicit concurrent operation contexts remain isolated");
        require(currentLogContext()->sessionId == QStringLiteral("session"), "main thread session remains intact");
    }
    require(!currentLogContext(), "scope exit clears operation and session");
    EpsilonCollector collector;
    std::vector<LogContext> observations;
    collector.setLogCallback([&](const std::string&) {
        const auto *context = currentLogContext();
        observations.push_back(context ? *context : LogContext{});
    });
    {
        ScopedLogContext operation({QStringLiteral("epsilon-connect"), QStringLiteral("real-session")});
        require(!collector.configureRtcmPort(2, 115200), "unopened collector rejects configuration");
    }
    require(!collector.configureRtcmPort(2, 115200), "unopened collector rejects later independent configuration");
    require(observations.size() == 2 && observations[0].correlationId == QStringLiteral("epsilon-connect") &&
            observations[0].sessionId == QStringLiteral("real-session") && observations[1].correlationId.isEmpty() &&
            observations[1].sessionId.isEmpty(), "real synchronous collector log callback sees the active context only");
}
