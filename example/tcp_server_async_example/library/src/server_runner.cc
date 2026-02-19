#include "app.h"

#include <csignal>
#include <exception>
#include <memory>
#include <string>

#include <log.hpp>
#include <platform/schedule/hive.h>
#include <platform/schedule/io_worker.h>

#ifdef __linux__
#define DEFAULT_BACKEND BackendType::LINUX_EPOLL
#endif
#ifdef __APPLE__ 
#define DEFAULT_BACKEND BackendType::MACOS_KQUEUE
#endif
#ifdef _WIN32
#include <ws2tcpip.h>
#define DEFAULT_BACKEND BackendType::WINDOWS_IOCP
WSADATA wsaData;
#endif

namespace tcp_server_async_example {

static int parseInt(const char* s, int fallback) {
    if (!s) return fallback;
    try {
        return std::stoi(std::string(s));
    } catch (...) {
        return fallback;
    }
}

// 简单的信号捕获，只记录崩溃类型
static void installCrashHandler() {
    auto handler = [](int sig) {
        LOG(FATAL)("Caught fatal signal: %d. Program will exit.", sig);
        std::_Exit(sig); 
    };
    
    std::signal(SIGSEGV, handler);
    std::signal(SIGFPE, handler);
    std::signal(SIGILL, handler);
    #ifdef SIGBUS
    std::signal(SIGBUS, handler);
    #endif
}

// 异常中止处理：将 std::terminate 转化为 LOG 输出
static void installTerminateHandler() {
    std::set_terminate([]() {
        if (auto eptr = std::current_exception()) {
            try {
                std::rethrow_exception(eptr);
            } catch (const std::system_error& e) {
                LOG(FATAL)("Uncaught std::system_error: [%d] %s (what: %s)", 
                           e.code().value(), e.code().message().c_str(), e.what());
            } catch (const std::exception& e) {
                LOG(FATAL)("Uncaught std::exception: %s", e.what());
            } catch (...) {
                LOG(FATAL)("Uncaught unknown exception type.");
            }
        } else {
            LOG(FATAL)("std::terminate called (no active exception).");
        }
        std::abort();
    });
}

int run(int argc, char** argv) {
#if defined(_WIN32)
    WSADATA wsaData;
    int ret = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (ret != 0) {
        LOG(ERROR)("WSAStartup failed with error code: %d", ret);
        return 1;
    }
#endif

    // 初始化配置
    logger::setMinLevel(LogLevel::INFO);
    installCrashHandler();
    installTerminateHandler();

    ::RopHive::Hive::Options opt;
    opt.io_backend = ::RopHive::DEFAULT_BACKEND;

    const int worker_n = std::max(1, parseInt(argc > 1 ? argv[1] : nullptr, 4));

    ::RopHive::Hive hive(opt);
    for (int i = 0; i < worker_n; ++i) {
        hive.attachIOWorker(std::make_shared<::RopHive::IOWorker>(opt));
    }

    auto execs = std::make_shared<std::vector<std::shared_ptr<asyncnet::Executor>>>();
    execs->resize(static_cast<size_t>(worker_n));

    // 绑定线程执行器
    for (int i = 0; i < worker_n; ++i) {
        hive.postToWorker(static_cast<size_t>(i), [execs, i]() {
            auto* self = ::RopHive::IOWorker::currentWorker();
            if (!self) return;
            (*execs)[static_cast<size_t>(i)] = std::make_shared<asyncnet::Executor>(*self);
        });
    }

    // 在主 Worker 上拉起业务代码
    hive.postToWorker(0, [&hive, execs, worker_n]() {
        auto* self = ::RopHive::IOWorker::currentWorker();
        if (!self) return;
        
        auto exec = (*execs)[0];
        if (!exec) {
            exec = std::make_shared<asyncnet::Executor>(*self);
            (*execs)[0] = exec;
        }
        
        LOG(INFO)("Starting async_main on Worker 0...");
        asyncnet::spawn(*exec, async_main(*exec, hive, worker_n, execs));
    });

    hive.run();

#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}

} // namespace tcp_server_async_example