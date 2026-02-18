#include <cerrno>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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
        LOG(INFO)("accepted peer=%s local=%s",
                  ipPortToString(stream.peer).c_str(),
                  ipPortToString(stream.local).c_str());
    }

    void onData(std::string_view data) {
        LOG(DEBUG)("recv %zu bytes", data.size());
        LOG(INFO)("client payload:");
        LOG(INFO)("=== data begin ===\n%s", data.data());
        LOG(INFO)("=== data end ===");

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
            if (res.err != 0) {
                return;
            }
            if (res.n > 0) {
                pending_out_.erase(0, res.n);
                continue;
            }
            if (res.would_block) {
                return;
            }
            return;
        }
    }

    IOWorker& worker_;
    std::weak_ptr<ITcpConnectionWatcher> watcher_wp_;
    std::string pending_out_;
};

int main() {
#if defined(_WIN32)
    int ret = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (ret != 0) {
        LOG(ERROR)("WSAStartup failed: %d\n", ret);
        return 1;
    }
#endif
    logger::setMinLevel(LogLevel::DEBUG);
    Hive::Options opt;
    opt.io_backend = DEFAULT_BACKEND;

    Hive hive(opt);
    auto worker = std::make_shared<IOWorker>(opt);
    hive.attachIOWorker(worker);

    hive.postToWorker(0, [worker]() {
        auto* self = IOWorker::currentWorker();
        if (!self) return;

        TcpAcceptOption acc_opt;
        acc_opt.local = parseIpEndpoint("0.0.0.0:8080").value();
        acc_opt.fill_endpoints = true;
        acc_opt.set_close_on_exec = true;
        acc_opt.reuse_addr = true;
        acc_opt.tcp_no_delay = true;

        LOG(INFO)("tcp_server_example listening on %s", ipPortToString(acc_opt.local).c_str());
        auto accept = createTcpAcceptWatcher(
            *worker,
            acc_opt,
            [worker](std::unique_ptr<ITcpStream> stream) {
                auto* self = IOWorker::currentWorker();
                if (!self) return;
                if (!stream) return;

                auto watcher_wp_box =
                    std::make_shared<std::weak_ptr<ITcpConnectionWatcher>>();
                auto session = std::make_shared<EchoSession>(*worker);
                session->start(*stream);

                TcpConnectionOption conn_opt;

                auto watcher = createTcpConnectionWatcher(
                    *worker,
                    conn_opt,
                    std::move(stream),
                    [session](std::string_view data) { session->onData(data); },
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

        self->adoptWatcher(accept);
        accept->start();
        LOG(INFO)("accept watcher started");
    });

    hive.run();
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}
