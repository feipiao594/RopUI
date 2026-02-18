#include <iostream>
#include <memory>
#include <string>
#include <string_view>
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

/*
 * SimpleHttpClient
 *
 * - One-shot HTTP GET client
 * - Correct non-blocking connect state machine
 */
class SimpleHttpClient final
    : public std::enable_shared_from_this<SimpleHttpClient> {
public:
  using ResponseCallback = std::function<void(const std::string &)>;

  SimpleHttpClient(IOWorker &worker, std::string host, int port,
                   std::string path, ResponseCallback cb)
      : worker_(worker), host_(std::move(host)), port_(port),
        path_(std::move(path)), cb_(std::move(cb)) {}

  void start() {
    auto self = shared_from_this();

    auto remote_parse = parseIpEndpoint(host_ + ":" + std::to_string(port_));

    if (!remote_parse.has_value()) {
      LOG(FATAL)("error ip: %s:%d", host_.c_str(), port_);
      exit(1);
    }
    IpEndpoint remote = *remote_parse;
    TcpConnectOption opt;
    opt.fill_endpoints = true;

    connect_ = createTcpConnectWatcher(
        worker_, remote, opt,
        [self](std::unique_ptr<ITcpStream> stream) {
          self->onConnected(std::move(stream));
        },
        [self](int err) { self->finishWithError(err); });
    connect_->start();
  }

  void mannualFinish() {
    finish();
  }

private:
  void onConnected(std::unique_ptr<ITcpStream> stream) {
    auto self = shared_from_this();
    conn_ = createTcpConnectionWatcher(
        worker_, TcpConnectionOption{}, std::move(stream),
        [self](std::string_view chunk) { self->onRecv(chunk); },
        [self]() { self->finish(); },
        [self](int err) { self->finishWithError(err); },
        [self]() { self->onSendReady(); });
    conn_->start();
    sendRequest();
  }

  void sendRequest() {
    std::string req = 
          "GET " + path_ + " HTTP/1.1\r\n"
          "Host: " + host_ + "\r\n"
          "User-Agent: curl/8.17.0\r\n"
          "Accept: */*\r\n\r\n";

    LOG(INFO)("send request:\n%s", req.c_str());

    pending_out_.assign(req.data(), req.size());
    flushSend();
  }

  void onRecv(std::string_view chunk) {
    response_.append(chunk.data(), chunk.size());
  }

  void onSendReady() { flushSend(); }

  void flushSend() {
    if (pending_out_.empty())
      return;
    if (!conn_)
      return;

    while (!pending_out_.empty()) {
      auto res = conn_->trySend(pending_out_);
      if (res.err != 0) {
        finishWithError(res.err);
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

    if (conn_)
      conn_->shutdownWrite();
  }

  void finish() {
    LOG(DEBUG)("finish()");
    if (done_)
      return;
    done_ = true;
    if (cb_)
      cb_(response_);
    connect_.reset();
    conn_.reset();
  }

  void finishWithError(int err) {
    LOG(WARN)("http client error: %d", err);
    finish();
  }

private:
  IOWorker &worker_;
  std::string host_;
  int port_;
  std::string path_;
  ResponseCallback cb_;

  bool done_{false};
  std::shared_ptr<ITcpConnectWatcher> connect_;
  std::shared_ptr<ITcpConnectionWatcher> conn_;
  std::string response_;
  std::string pending_out_;
};

/* ---------------- demo main ---------------- */

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

  hive.postToWorker(0, [worker, &hive]() {
    auto client = std::make_shared<SimpleHttpClient>(
        // baidu dns ip: 36.152.44.132
        *worker, "36.152.44.132", 80, "/", [&](const std::string &resp) {
          std::cout << "=== response ===\n";
          std::cout << resp << "\n";
          hive.requestExit();
        });
    client->start();
  });

  hive.run();
#if defined(_WIN32)
    WSACleanup();
#endif
  return 0;
}
