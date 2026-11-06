#pragma once

#include <QString>
#include <utility>

namespace VaporView {

// Explicitly copy context into each asynchronous work item. This scope only
// exposes that copy to synchronous callbacks on the current worker thread.
struct LogContext {
    QString correlationId;
    QString sessionId;
};

inline thread_local const LogContext *current_log_context = nullptr;

inline const LogContext *currentLogContext() noexcept { return current_log_context; }

class ScopedLogContext final {
public:
    explicit ScopedLogContext(LogContext context)
        : context_(std::move(context)), previous_(current_log_context) { current_log_context = &context_; }
    ~ScopedLogContext() { current_log_context = previous_; }
    ScopedLogContext(const ScopedLogContext&) = delete;
    ScopedLogContext& operator=(const ScopedLogContext&) = delete;
private:
    LogContext context_;
    const LogContext *previous_;
};

} // namespace VaporView
