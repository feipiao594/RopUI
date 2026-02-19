#pragma once

#include "asyncnet.h"
#include "server_runner.h"

#ifndef ROPUI_COROUTINE_TRUE_RUN
#error "ROPUI_COROUTINE_TRUE_RUN not defined"
#endif

#define rophive_async_main int main(int argc, char** argv) { \
        return ::tcp_server_async_example::run(argc, argv); \
    } \
    asyncnet::Task<void>

// Business entry coroutine (keep this file clean).
asyncnet::Task<void> async_main(asyncnet::Executor& accept_exec,
                          ::RopHive::Hive& hive,
                          int worker_n,
                          std::shared_ptr<std::vector<std::shared_ptr<asyncnet::Executor>>> execs);
