#ifndef _ROP_PLATFORM_SCHEDULE_SCHED_TRACE_H
#define _ROP_PLATFORM_SCHEDULE_SCHED_TRACE_H

#include <cstddef>

namespace RopHive::SchedTrace {

enum class Event {
    UserBlockBegin,
    UserBlockEnd,

    WorkerBind,

    InboundDrained,
    WakeupNotify,

    LocalPush,
    LocalDegradeGlobal,
    LocalDone,
    GlobalHarvestDone,

    PrivateDone,
    TimerDone,

    WorkerRunStart,
    WorkerRunStop,

    StealFail,
    LoopTick,
    RunOnceBegin,
	SleepEnter,
	SleepExit,
	RunOnceEnd,

	StealAttempt,
	StealSuccess,
};

inline constexpr const char* toString(Event e) noexcept {
    switch (e) {
        case Event::UserBlockBegin:
            return "user_block_begin";
        case Event::UserBlockEnd:
            return "user_block_end";
        case Event::WorkerBind:
            return "worker_bind";
        case Event::InboundDrained:
            return "inbound_drained";
        case Event::WakeupNotify:
            return "wakeup_notify";
        case Event::LocalPush:
            return "local_push";
        case Event::LocalDegradeGlobal:
            return "local_degrade_global";
        case Event::LocalDone:
            return "local_done";
        case Event::GlobalHarvestDone:
            return "global_harvest_done";
        case Event::PrivateDone:
            return "private_done";
        case Event::TimerDone:
            return "timer_done";
        case Event::WorkerRunStart:
            return "worker_run_start";
        case Event::WorkerRunStop:
            return "worker_run_stop";
        case Event::StealFail:
            return "steal_fail";
        case Event::LoopTick:
            return "loop_tick";
        case Event::RunOnceBegin:
            return "runonce_begin";
        case Event::SleepEnter:
            return "sleep_enter";
	    case Event::SleepExit:
	        return "sleep_exit";
	    case Event::RunOnceEnd:
	        return "runonce_end";
	    case Event::StealAttempt:
	        return "steal_attempt";
	    case Event::StealSuccess:
	        return "steal_success";
    }
    return "";
}

// Enable by setting env var `ROP_SCHED_TRACE=1`.
bool enabled() noexcept;

// Overrides env-based behavior at runtime (e.g., driven by config/argv).
// This setting is process-global.
void setEnabled(bool on) noexcept;

// Emits a single line in the form:
//   SCHED|<ts_ns>|<worker_id>|<event>|k=v k=v ...
void emitImpl(const char* file,
              int line,
              const char* func,
              size_t worker_id,
              const char* event,
              const char* fmt,
              ...);

void emitImpl(const char* file,
              int line,
              const char* func,
              size_t worker_id,
              Event event,
              const char* fmt,
              ...);

} // namespace RopHive::SchedTrace

#define SCHED_TRACE(worker_id, event, fmt, ...)                                  \
    do {                                                                         \
        if (::RopHive::SchedTrace::enabled()) {                                  \
            ::RopHive::SchedTrace::emitImpl(__FILE__, __LINE__, __func__,        \
                                            (worker_id), (event), (fmt),         \
                                            ##__VA_ARGS__);                      \
        }                                                                        \
    } while (0)

#define SCHED_TRACE0(worker_id, event) SCHED_TRACE((worker_id), (event), "%s", "")

#define SCHED_TRACE_E(worker_id, event, fmt, ...)                                \
    SCHED_TRACE((worker_id), ::RopHive::SchedTrace::toString((event)), (fmt), ##__VA_ARGS__)

#define SCHED_TRACE_E0(worker_id, event) SCHED_TRACE_E((worker_id), (event), "%s", "")

#endif // _ROP_PLATFORM_SCHEDULE_SCHED_TRACE_H
