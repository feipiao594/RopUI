#include "schedule/sched_trace.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include <chrono>

#include <log.hpp>

namespace RopHive::SchedTrace {

static uint64_t nowNs() noexcept {
    using clock = std::chrono::steady_clock;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        clock::now().time_since_epoch())
                        .count();
    return ns < 0 ? 0ULL : static_cast<uint64_t>(ns);
}

// -1: auto (read env lazily)
//  0: disabled
//  1: enabled
static int g_enabled_state{-1};

bool enabled() noexcept {
    if (g_enabled_state != -1) {
        return g_enabled_state == 1;
    }
    const char* env = std::getenv("ROP_SCHED_TRACE");
    const bool on = (env && env[0] == '1');
    g_enabled_state = on ? 1 : 0;
    return on;
}

void setEnabled(bool on) noexcept {
    g_enabled_state = on ? 1 : 0;
}

void emitImpl(const char* file,
              int line,
              const char* func,
              size_t worker_id,
              const char* event,
              const char* fmt,
              ...) {
    if (!enabled()) {
        return;
    }

    char kv_buf[768];
    kv_buf[0] = '\0';

    if (fmt && fmt[0] != '\0') {
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(kv_buf, sizeof(kv_buf), fmt, ap);
        va_end(ap);
    }

    const uint64_t ts = nowNs();
    logger::logImpl(LogLevel::DEBUG,
                    file,
                    line,
                    func,
                    "SCHED|%llu|%zu|%s|%s",
                    static_cast<unsigned long long>(ts),
                    worker_id,
                    event ? event : "",
                    kv_buf);
}

void emitImpl(const char* file,
              int line,
              const char* func,
              size_t worker_id,
              Event event,
              const char* fmt,
              ...) {
    if (!enabled()) {
        return;
    }

    char kv_buf[768];
    kv_buf[0] = '\0';

    if (fmt && fmt[0] != '\0') {
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(kv_buf, sizeof(kv_buf), fmt, ap);
        va_end(ap);
    }

    const uint64_t ts = nowNs();
    logger::logImpl(LogLevel::DEBUG,
                    file,
                    line,
                    func,
                    "SCHED|%llu|%zu|%s|%s",
                    static_cast<unsigned long long>(ts),
                    worker_id,
                    toString(event),
                    kv_buf);
}

} // namespace RopHive::SchedTrace
