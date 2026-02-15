#include <cerrno>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <log.hpp>
#include <platform/network/ip_endpoint.h>
#include <platform/network/watcher/tcp_watchers.h>
#include <platform/schedule/hive.h>
#include <platform/schedule/io_worker.h>

using namespace RopHive;
using namespace RopHive::Network;

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

class EchoSession : public std::enable_shared_from_this<EchoSession> {
public:
    explicit EchoSession(IOWorker& worker)
        : worker_(worker) {}

    void bind(std::weak_ptr<ITcpConnectionWatcher> watcher_wp) {
        watcher_wp_ = std::move(watcher_wp);
    }

    void start(const ITcpStream& stream) {
        LOG(INFO)("accepted peer=%s local=%s worker=%zu",
                  ipPortToString(stream.peer).c_str(),
                  ipPortToString(stream.local).c_str(),
                  worker_.id());
    }

    void onRecv(std::string_view data) {
        LOG(DEBUG)("recv %zu bytes", data.size());
        pending_out_.append(data.data(), data.size());
        flushSend();
    }

    void onClose() {
        LOG(INFO)("connection closed");
    }

    void onError(int err) {
        LOG(WARN)("connection error=%d (%s)", err, std::strerror(err));
    }

    void onSendReady() {
        flushSend();
    }

private:
    void flushSend() {
        if (pending_out_.empty()) return;
        auto watcher = watcher_wp_.lock();
        if (!watcher) return;

        while (!pending_out_.empty()) {
            auto res = watcher->trySend(pending_out_);
            if (res.err != 0) return;
            if (res.n > 0) {
                pending_out_.erase(0, res.n);
                continue;
            }
            if (res.would_block) return;
            return;
        }
    }

    IOWorker& worker_;
    std::weak_ptr<ITcpConnectionWatcher> watcher_wp_;
    std::string pending_out_;
};

static int parseInt(const char* s, int fallback) {
    if (!s) return fallback;
    try {
        return std::stoi(std::string(s));
    } catch (...) {
        return fallback;
    }
}

int main(int argc, char** argv) {
#if defined(_WIN32)
    int ret = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (ret != 0) {
        LOG(ERROR)("WSAStartup failed: %d\n", ret);
        return 1;
    }
#endif
    logger::setMinLevel(LogLevel::INFO);

    constexpr int kPort = 8080;
    const int worker_n = std::max(1, parseInt(argc > 1 ? argv[1] : nullptr, 20));

    LOG(INFO)("tcp_server_multithread_example listening on 0.0.0.0:%d (workers=%d)", kPort, worker_n);

    Hive::Options opt;
    opt.io_backend = DEFAULT_BACKEND;

    Hive hive(opt);
    for (int i = 0; i < worker_n; ++i) {
        hive.attachIOWorker(std::make_shared<IOWorker>(opt));
    }

    auto rr = std::make_shared<size_t>(0);

    // Accept on worker 0, dispatch each connection to a worker (round-robin).
    hive.postToWorker(0, [&hive, rr, worker_n]() {
        auto* accept_worker = IOWorker::currentWorker();
        if (!accept_worker) return;

        TcpAcceptOption acc_opt;
        IpEndpointV4 bind_v4;
        bind_v4.ip = {0, 0, 0, 0};
        bind_v4.port = kPort;
        acc_opt.local = bind_v4;
        acc_opt.fill_endpoints = true;
        acc_opt.reuse_addr = true;
        acc_opt.set_close_on_exec = true;
        acc_opt.tcp_no_delay = true;

        auto accept = createTcpAcceptWatcher(
            *accept_worker,
            acc_opt,
            [&hive, rr, worker_n](std::unique_ptr<ITcpStream> stream) {
                auto* aw = IOWorker::currentWorker();
                if (!aw) return;
                if (!stream) return;

                const size_t accept_worker_id = aw->id();
                const size_t target_worker_id = ((*rr)++) % static_cast<size_t>(worker_n);
                LOG(INFO)("accept peer=%s on_worker=%zu -> postToWorker(%zu)",
                         ipPortToString(stream->peer).c_str(),
                         accept_worker_id,
                         target_worker_id);

                auto stream_box = std::make_shared<std::unique_ptr<ITcpStream>>(std::move(stream));
                hive.postToWorker(target_worker_id, [stream_box]() mutable {
                    auto* self = IOWorker::currentWorker();
                    if (!self) return;

                    auto stream_local = std::move(*stream_box);
                    if (!stream_local) return;

                    const auto tid = static_cast<unsigned long long>(
                        std::hash<std::thread::id>{}(std::this_thread::get_id()));
                    LOG(INFO)("conn assigned_worker=%zu tid=%llu", self->id(), tid);

                    auto watcher_wp_box =
                        std::make_shared<std::weak_ptr<ITcpConnectionWatcher>>();
                    auto session = std::make_shared<EchoSession>(*self);
                    session->start(*stream_local);

                    TcpConnectionOption conn_opt;

                    auto watcher = createTcpConnectionWatcher(
                        *self,
                        conn_opt,
                        std::move(stream_local),
                        [session](std::string_view data) { session->onRecv(data); },
                        [watcher_wp_box, session]() {
                            session->onClose();
                            if (auto* w = IOWorker::currentWorker()) {
                                if (auto watcher = watcher_wp_box->lock()) {
                                    w->releaseWatcher(watcher.get());
                                }
                            }
                        },
                        [watcher_wp_box, session](int err) {
                            session->onError(err);
                            if (auto* w = IOWorker::currentWorker()) {
                                if (auto watcher = watcher_wp_box->lock()) {
                                    w->releaseWatcher(watcher.get());
                                }
                            }
                        },
                        [session]() { session->onSendReady(); });

                    *watcher_wp_box = watcher;
                    session->bind(*watcher_wp_box);

                    self->adoptWatcher(watcher);
                    watcher->start();
                });
            },
            [](int err) { LOG(WARN)("accept error: %d (%s)", err, std::strerror(err)); });

        accept_worker->adoptWatcher(accept);
        accept->start();
        LOG(INFO)("accept watcher started on worker 0");
    });

    hive.run();
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}
