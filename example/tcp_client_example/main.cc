#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include <platform/schedule/eventloop.h>
#include <log.hpp>

using namespace RopEventloop;
using namespace RopEventloop::Linux;

/*
 * SimpleHttpClient
 *
 * - One-shot HTTP GET client
 * - Correct non-blocking connect state machine
 */
class SimpleHttpClient final : public IWatcher {
public:
    using ResponseCallback = std::function<void(const std::string&)>;

    SimpleHttpClient(EventLoop& loop,
                     std::string host,
                     int port,
                     std::string path,
                     ResponseCallback cb)
        : IWatcher(loop),
          host_(std::move(host)),
          port_(port),
          path_(std::move(path)),
          cb_(std::move(cb)) {}

    ~SimpleHttpClient() override {
        stop();
    }

    void start() override {
        createSocket();
        startConnect();
        createSource();
        attachSource(source_.get());
        attached_ = true;
        checkConnectResult();
    }

    void stop() override {
        if (attached_) {
            detachSource(source_.get());
            attached_ = false;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    enum class State {
        Connecting,
        Sending,
        Reading,
        Done,
    };

    void createSocket() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) {
            perror("socket");
            std::abort();
        }

        int flags = ::fcntl(fd_, F_GETFL, 0);
        ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    }

    void startConnect() {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);

        if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
            std::cerr << "invalid address\n";
            std::abort();
        }

        int rc = ::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (rc == 0) {
            LOG(INFO)("connect completed immediately");
            state_ = State::Sending;
        } else if (errno == EINPROGRESS) {
            LOG(INFO)("connect in progress");
            state_ = State::Connecting;
        } else {
            perror("connect");
            std::abort();
        }
    }

    void createSource() {
        source_ = std::make_unique<EpollReadinessEventSource>(
            fd_,
            EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP,
            [this](uint32_t events) {
                onReady(events);
            });
    }

    void checkConnectResult() {
        if (state_ != State::Connecting) return;

        int err = 0;
        socklen_t len = sizeof(err);
        if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
            perror("getsockopt");
            finish();
            return;
        }

        if (err == 0) {
            LOG(DEBUG)("connect completed (SO_ERROR == 0)");
            loop_.post([=](){ 
                this->state_ = State::Sending;
                sendRequest();
            });
        }
    }

    void onReady(uint32_t events) {
        if (events & (EPOLLERR | EPOLLHUP)) {
            LOG(DEBUG)("EPOLLERR | EPOLLHUP");
            finish();
            return;
        }

        if (state_ == State::Connecting && (events & EPOLLOUT)) {
            LOG(DEBUG)("EPOLLOUT");
            checkConnectResult();
        }

        if (state_ == State::Sending && (events & EPOLLOUT)) {
            LOG(DEBUG)("EPOLLOUT");
            sendRequest();
            state_ = State::Reading;
        }

        if (state_ == State::Reading && (events & EPOLLIN)) {
            LOG(DEBUG)("EPOLLIN");
            readResponse();
        }
    }

    void sendRequest() {
        std::string req =
            "GET " + path_ + " HTTP/1.1\r\n"
            "Host: " + host_ + "\r\n"
            "Connection: close\r\n\r\n";

        LOG(INFO)("send request:\n%s", req.c_str());
        ::send(fd_, req.data(), req.size(), 0);

        state_ = State::Reading;
    }

    void readResponse() {
        char buf[4096];
        for (;;) {
            ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
            LOG(DEBUG)("data number %d", n);
            if (n > 0) {
                response_.append(buf, n);
            } else if (n == 0) {
                finish();
                return;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    return;
                perror("recv");
                finish();
                return;
            }
        }
    }

    void finish() {
        LOG(DEBUG)("finish()");
        if (state_ == State::Done) return;
        state_ = State::Done;
        if (cb_) cb_(response_);
        stop();
    }

private:
    std::string host_;
    int port_;
    std::string path_;
    ResponseCallback cb_;

    int fd_{-1};
    bool attached_{false};

    State state_{State::Connecting};
    std::unique_ptr<IEventSource> source_;

    std::string response_;
};

/* ---------------- demo main ---------------- */

int main() {
    logger::setMinLevel(LogLevel::DEBUG);

    EventLoop loop(BackendType::LINUX_EPOLL);

    SimpleHttpClient client(
        loop,
        "127.0.0.1",
        8080,
        "/",
        [&](const std::string& resp) {
            std::cout << "=== response ===\n";
            std::cout << resp << "\n";
            loop.requestExit();
        });

    client.start();
    loop.run();
    return 0;
}
