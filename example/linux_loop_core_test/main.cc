#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <string>
#include <memory>
#include <functional>
#include <cstdlib>

#include <log.hpp>

#include <platform/linux/schedule/poll_backend.h>
#include <platform/linux/schedule/epoll_backend.h>

using namespace RopEventloop;

/* ================= Backend Selection ================= */

enum class BackendType {
    Poll,
    Epoll,
};

static const char* backendName(BackendType type) {
    switch (type) {
    case BackendType::Poll:  return "poll";
    case BackendType::Epoll: return "epoll";
    default: return "unknown";
    }
}

/* ================= EventLoop Wrapper ================= */

static std::unique_ptr<IEventLoopCore>
createEventLoopCore(BackendType type) {
    using namespace RopEventloop::Linux;

    switch (type) {
    case BackendType::Poll:
        return std::make_unique<PollEventLoopCore>();
    case BackendType::Epoll:
        return std::make_unique<EpollEventLoopCore>();
    default:
        std::abort();
    }
}

class EventLoop {
public:
    explicit EventLoop(BackendType type)
        : core_(createEventLoopCore(type)) {}

    void run() {
        exit_requested_ = false;
        core_->applyInitialChanges();
        while (!exit_requested_) {
            core_->runOnce(timeout_ms_);
        }
    }

    void addSource(std::unique_ptr<IEventSource> source) {
        core_->addSource(std::move(source));
    }

    void requestExit() {
        exit_requested_ = true;
    }

private:
    std::unique_ptr<IEventLoopCore> core_;
    bool exit_requested_ = false;
    int timeout_ms_ = -1;
};

/* ================= Source Factory ================= */
/*
 * 关键点：
 * - backend 只负责 raw event
 * - 这里统一把“backend-specific 事件”翻译成 POLL* 语义
 * - 上层 callback 永远只看到 short revents (POLLIN | POLLOUT ...)
 */

static short epollToPoll(uint32_t events) {
    short r = 0;
    if (events & EPOLLIN)  r |= POLLIN;
    if (events & EPOLLOUT) r |= POLLOUT;
    if (events & EPOLLERR) r |= POLLERR;
    if (events & EPOLLHUP) r |= POLLHUP;
    return r;
}

static std::unique_ptr<IEventSource>
createSocketSource(
    BackendType type,
    int fd,
    short interest,
    std::function<void(short)> user_cb) {

    using namespace RopEventloop::Linux;

    switch (type) {
    case BackendType::Poll:
        return std::make_unique<PollReadinessEventSource>(
            fd,
            interest,
            std::move(user_cb));

    case BackendType::Epoll:
        return std::make_unique<EpollReadinessEventSource>(
            fd,
            /* epoll interest */ interest,
            [cb = std::move(user_cb)](uint32_t epoll_events) {
                short revents = epollToPoll(epoll_events);
                if (revents) {
                    cb(revents);
                }
            });

    default:
        std::abort();
    }
}

/* ================= Utilities ================= */

static void make_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ================= Test Program ================= */

int main(int argc, char** argv) {
    BackendType backend = BackendType::Epoll;

    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "poll")  backend = BackendType::Poll;
        if (arg == "epoll") backend = BackendType::Epoll;
    } 

    logger::setMinLevel(LogLevel::INFO);
    LOG(INFO)("using backend: %s", backendName(backend));

    EventLoop loop(backend);

    int pairA[2];
    int pairB[2];

    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pairA) < 0 ||
        ::socketpair(AF_UNIX, SOCK_STREAM, 0, pairB) < 0) {
        perror("socketpair");
        return 1;
    }

    make_nonblocking(pairA[0]);
    make_nonblocking(pairA[1]);
    make_nonblocking(pairB[0]);
    make_nonblocking(pairB[1]);

    /* ---------- Socket A (slow handler) ---------- */

    auto srcA = createSocketSource(
        backend,
        pairA[0],
        POLLIN,
        [&](short revents) {
            if (revents & POLLIN) {
                char ch;
                int n = ::read(pairA[0], &ch, 1);
                if (n > 0) {
                    LOG(INFO)("[A] recv byte: %c", ch);

                    auto start = std::chrono::steady_clock::now();
                    while (std::chrono::steady_clock::now() - start <
                           std::chrono::milliseconds(50)) {
                        // busy work
                    }
                }
            }
        });

    /* ---------- Socket B ---------- */

    auto srcB = createSocketSource(
        backend,
        pairB[0],
        POLLIN,
        [&](short revents) {
            if (revents & POLLIN) {
                char buf[256];
                int n = ::read(pairB[0], buf, sizeof(buf));
                if (n > 0) {
                    LOG(INFO)("[B] recv: %s",
                              std::string(buf, n).c_str());
                }
            }
        });

    loop.addSource(std::move(srcA));
    loop.addSource(std::move(srcB));

    /* ---------- Sender Thread ---------- */

    std::thread sender([&] {
        for (int i = 0; i < 5; ++i) {
            std::string msgA =
                "hello from socket A " + std::to_string(i);
            std::string msgB =
                "hello from socket B " + std::to_string(i);

            ::send(pairA[1], msgA.data(), msgA.size(), 0);
            ::send(pairB[1], msgB.data(), msgB.size(), 0);

            std::this_thread::sleep_for(
                std::chrono::milliseconds(300));
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
        LOG(INFO)("requesting event loop exit");
        loop.requestExit();
    });

    loop.run();
    sender.join();

    ::close(pairA[0]);
    ::close(pairA[1]);
    ::close(pairB[0]);
    ::close(pairB[1]);

    LOG(INFO)("done");
    return 0;
}
