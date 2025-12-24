#include "platform/schedule/eventloop_core.h"
#include <chrono>
#include <log.hpp>
#include <platform/schedule/eventloop.h>

void more_test_code();

int main() {
    logger::setMinLevel(LogLevel::DEBUG);

    LOG(INFO)("This is an info message.");
    LOG(DEBUG)("This is a debug message.");
    LOG(WARN)("This is a warning message.");
    LOG(ERROR)("This is an error message.");
    LOG(FATAL)("This is a fatal message");
    int value = 42;
    LOG(INFO)("The answer to life, the universe, and everything is %d", value);

    more_test_code();

    return 0;
}

void more_test_code() {
    using namespace RopEventloop;

    EventLoop loop(BackendType::MACOS_POLL);

    loop.postDelayed([&](){
        LOG(INFO)("This is an info message. 1");
        loop.postDelayed([&](){
            LOG(INFO)("This is an info message. 2");
            loop.postDelayed([&](){
                LOG(INFO)("This is an info message. 3");
                loop.requestExit();
            }, std::chrono::seconds(1));
        }, std::chrono::seconds(1));
    }, std::chrono::seconds(1));
    
    logger::setMinLevel(LogLevel::INFO);

    loop.run();
    LOG(INFO)("Logging from more_test_code function.");
}