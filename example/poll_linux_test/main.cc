#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <string>

#include <log.hpp>
#include <platform/linux/eventloop/poll_backend.h>

class EventLoop { // 并非一个真正的调度器，只是简单封装 RopEventloop::IEventLoopCore
public:
    explicit EventLoop()
        : core_(std::make_unique<RopEventloop::Linux::PollEventLoopCore>()) {}

    void run() {
        exit_requested_ = false;
        core_->applyInitialChanges();
        while (!exit_requested_) {
            core_->runOnce(timeout_ms_);
        }
    }

    void addSource(std::unique_ptr<RopEventloop::IEventSource> source) {
        core_->addSource(std::move(source));
    }

    void requestExit() {
        exit_requested_ = true;
        // 后面会加 wakeup
    }

    void setTimeout(int timeout_ms) {
        timeout_ms_ = timeout_ms;
    }

private:
    std::unique_ptr<RopEventloop::IEventLoopCore> core_;
    bool exit_requested_ = false;
    int timeout_ms_ = -1; // 默认永久阻塞
};


/* ---------- 工具函数 ---------- */

static void make_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ---------- 测试主程序 ---------- */

int main() {
    EventLoop loop;

    logger::setMinLevel(LogLevel::INFO);

    // 两对“对等”的 socket
    int pairA[2];
    int pairB[2];

    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pairA) < 0 ||
        ::socketpair(AF_UNIX, SOCK_STREAM, 0, pairB) < 0) {
        perror("socketpair");
        return 1;
    }

    // 全部设为非阻塞
    make_nonblocking(pairA[0]);
    make_nonblocking(pairA[1]);
    make_nonblocking(pairB[0]);
    make_nonblocking(pairB[1]);

    LOG(INFO)("sockets ready, starting event loop");

    /* ---------- Socket A Source（慢处理版） ---------- */
    auto srcA = std::make_unique<RopEventloop::Linux::SocketEventSource>(
        pairA[0],
        POLLIN,
        [&](short revents) {
            LOG(DEBUG)("[A] event received: revents=0x%04x", revents);

            if (revents & POLLIN) {
                char ch;
                int n = ::read(pairA[0], &ch, 1);  // 故意只读 1 字节
                if (n > 0) {
                    LOG(INFO)("[A] recv byte: %c", ch);

                    // 模拟“业务处理耗时”，但不 sleep
                    auto start = std::chrono::steady_clock::now();
                    while (std::chrono::steady_clock::now() - start <
                           std::chrono::milliseconds(50)) {
                        // busy work
                    }
                }
            }
        });


    /* ---------- Socket B Source ---------- */
    auto srcB = std::make_unique<RopEventloop::Linux::SocketEventSource>(
        pairB[0],
        POLLIN,
        [&](short revents) {
            LOG(DEBUG)("Socket B event triggered");
            if (revents & POLLIN) {
                char buf[256];
                int n = ::read(pairB[0], buf, sizeof(buf));
                if (n > 0) {
                    LOG(INFO)("[B] recv: %s", std::string(buf, n).c_str());
                }
            }
        });

    loop.addSource(std::move(srcA));
    loop.addSource(std::move(srcB));

    /* ---------- 发送线程 ---------- */
    std::thread sender([&] {
        for (int i = 0; i < 5; ++i) {
            std::string msgA =
                "hello from socket A " + std::to_string(i);
            std::string msgB =
                "hello from socket B " + std::to_string(i);

            LOG(DEBUG)("sending messages %d", i);

            ::send(pairA[1], msgA.data(), msgA.size(), 0);
            ::send(pairB[1], msgB.data(), msgB.size(), 0);

            std::this_thread::sleep_for(
                std::chrono::milliseconds(300));
        }

        // 给 loop 一点时间处理完
        std::this_thread::sleep_for(
            std::chrono::seconds(3));
        LOG(INFO)("sender thread requesting event loop exit");
        // 请求退出事件循环
        loop.requestExit();
    });

    /* ---------- 启动事件循环 ---------- */
    loop.run();

    sender.join();

    ::close(pairA[0]);
    ::close(pairA[1]);
    ::close(pairB[0]);
    ::close(pairB[1]);

    LOG(INFO)("done");
    return 0;
}
