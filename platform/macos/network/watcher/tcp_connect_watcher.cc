#include "tcp_connect_watcher.h"

#ifdef __APPLE__

#include <cerrno>

#include <sys/event.h>

#include "../../schedule/kqueue_backend.h"
#include "../../schedule/poll_backend.h"

#include "tcp_socket_common.h"

namespace RopHive::MacOS {
namespace {

using namespace RopHive::Network;

static int getSoErrorOr(int fd, int fallback) {
  int err = 0;
  socklen_t len = sizeof(err);
  if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
    return errno != 0 ? errno : fallback;
  }
  return err != 0 ? err : fallback;
}

class KqueueTcpConnectWatcher final : public ITcpConnectWatcher {
public:
  KqueueTcpConnectWatcher(IOWorker &worker, IpEndpoint remote,
                          TcpConnectOption option, OnConnected on_connected,
                          OnError on_error)
      : ITcpConnectWatcher(worker), remote_(std::move(remote)),
        option_(std::move(option)), on_connected_(std::move(on_connected)),
        on_error_(std::move(on_error)) {}

  ~KqueueTcpConnectWatcher() override {
    stop();
    source_.reset();
    TcpDetail::closeFd(fd_);
  }

  void start() override {
    if (attached_ || canceled_)
      return;
    createSocket();
    if (fd_ < 0)
      return;
    startConnect();
    if (fd_ < 0)
      return;
    source_ = std::make_shared<RopHive::MacOS::KqueueReadinessEventSource>(
        fd_, EVFILT_WRITE,
        [this](const RopHive::MacOS::KqueueRawEvent &) { onReady(); });
    attachSource(source_);
    attached_ = true;
  }

  void stop() override {
    if (!attached_)
      return;
    detachSource(source_);
    attached_ = false;
    TcpDetail::closeFd(fd_);
  }

  void cancel() override {
    if (canceled_)
      return;
    canceled_ = true;
    stop();
    source_.reset();
  }

private:
  void createSocket() {
    sockaddr_storage ss{};
    socklen_t ss_len = 0;
    if (!TcpDetail::toSockaddr(remote_, ss, ss_len)) {
      fail(EINVAL);
      return;
    }

    const int family = reinterpret_cast<sockaddr *>(&ss)->sa_family;
    fd_ = ::socket(family, SOCK_STREAM, 0);
    if (fd_ < 0) {
      fail(errno);
      return;
    }

    if (!TcpDetail::setNonBlocking(fd_)) {
      fail(errno);
      return;
    }

    TcpDetail::applyCloseOnExecIfConfigured(fd_, option_.set_close_on_exec);
    TcpDetail::applyNoSigPipeIfAvailable(fd_);
    TcpDetail::applyTcpNoDelayIfConfigured(fd_, option_.tcp_no_delay);
    TcpDetail::applyKeepAliveIfConfigured(
        fd_, option_.keep_alive, option_.keep_alive_idle_sec,
        option_.keep_alive_interval_sec, option_.keep_alive_count);
    TcpDetail::applyBufSizeIfConfigured(fd_, option_.recv_buf_bytes,
                                        option_.send_buf_bytes);

    if (option_.local_bind.has_value() || option_.local_port != 0) {
      IpEndpoint bind_ep{};
      if (option_.local_bind.has_value()) {
        bind_ep = *option_.local_bind;
      } else {
        if (family == AF_INET) {
          IpEndpointV4 v4;
          v4.ip = {0, 0, 0, 0};
          v4.port = option_.local_port;
          bind_ep = v4;
        } else if (family == AF_INET6) {
          IpEndpointV6 v6;
          v6.ip.fill(0);
          v6.port = option_.local_port;
          v6.scope_id = 0;
          bind_ep = v6;
        } else {
          fail(EAFNOSUPPORT);
          return;
        }
      }

      sockaddr_storage bind_ss{};
      socklen_t bind_len = 0;
      if (!TcpDetail::toSockaddr(bind_ep, bind_ss, bind_len)) {
        fail(EINVAL);
        return;
      }
      if (::bind(fd_, reinterpret_cast<sockaddr *>(&bind_ss), bind_len) != 0) {
        fail(errno);
        return;
      }
    }
  }

  void startConnect() {
    sockaddr_storage ss{};
    socklen_t ss_len = 0;
    if (!TcpDetail::toSockaddr(remote_, ss, ss_len)) {
      fail(EINVAL);
      return;
    }
    const int rc = ::connect(fd_, reinterpret_cast<sockaddr *>(&ss), ss_len);
    if (rc == 0) {
      checkConnectResult();
      return;
    }
    if (errno == EINPROGRESS)
      return;
    fail(errno);
  }

  void onReady() {
    if (canceled_)
      return;
    checkConnectResult();
  }

  void checkConnectResult() {
    if (canceled_ || fd_ < 0)
      return;
    int err = 0;
    socklen_t len = sizeof(err);
    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
      fail(errno);
      return;
    }
    if (err != 0) {
      fail(err);
      return;
    }

    auto stream = std::make_unique<MacTcpStream>(TcpStreamKind::MacKqueue, fd_);
    fd_ = -1;
    stop();
    if (option_.fill_endpoints) {
      TcpDetail::bestEffortFillEndpoints(stream->fd(), *stream);
    }
    if (on_connected_)
      on_connected_(std::move(stream));
  }

  void fail(int err) {
    stop();
    if (canceled_)
      return;
    if (on_error_)
      on_error_(err);
  }

private:
  IpEndpoint remote_;
  TcpConnectOption option_;
  OnConnected on_connected_;
  OnError on_error_;

  int fd_{-1};
  bool attached_{false};
  bool canceled_{false};
  std::shared_ptr<RopHive::MacOS::KqueueReadinessEventSource> source_;
};

class PollTcpConnectWatcher final : public ITcpConnectWatcher {
public:
  PollTcpConnectWatcher(IOWorker &worker, IpEndpoint remote,
                        TcpConnectOption option, OnConnected on_connected,
                        OnError on_error)
      : ITcpConnectWatcher(worker), remote_(std::move(remote)),
        option_(std::move(option)), on_connected_(std::move(on_connected)),
        on_error_(std::move(on_error)) {}

  ~PollTcpConnectWatcher() override {
    stop();
    source_.reset();
    TcpDetail::closeFd(fd_);
  }

  void start() override {
    if (attached_ || canceled_)
      return;
    createSocket();
    if (fd_ < 0)
      return;
    startConnect();
    if (fd_ < 0)
      return;

    source_ = std::make_shared<RopHive::MacOS::PollReadinessEventSource>(
        fd_, POLLOUT, [this](short revents) { onReady(revents); });
    attachSource(source_);
    attached_ = true;
  }

  void stop() override {
    if (!attached_)
      return;
    detachSource(source_);
    attached_ = false;
    TcpDetail::closeFd(fd_);
  }

  void cancel() override {
    if (canceled_)
      return;
    canceled_ = true;
    stop();
    source_.reset();
  }

private:
  void createSocket() {
    sockaddr_storage ss{};
    socklen_t ss_len = 0;
    if (!TcpDetail::toSockaddr(remote_, ss, ss_len)) {
      fail(EINVAL);
      return;
    }

    const int family = reinterpret_cast<sockaddr *>(&ss)->sa_family;
    fd_ = ::socket(family, SOCK_STREAM, 0);
    if (fd_ < 0) {
      fail(errno);
      return;
    }

    if (!TcpDetail::setNonBlocking(fd_)) {
      fail(errno);
      return;
    }

    TcpDetail::applyCloseOnExecIfConfigured(fd_, option_.set_close_on_exec);
    TcpDetail::applyNoSigPipeIfAvailable(fd_);
    TcpDetail::applyTcpNoDelayIfConfigured(fd_, option_.tcp_no_delay);
    TcpDetail::applyKeepAliveIfConfigured(
        fd_, option_.keep_alive, option_.keep_alive_idle_sec,
        option_.keep_alive_interval_sec, option_.keep_alive_count);
    TcpDetail::applyBufSizeIfConfigured(fd_, option_.recv_buf_bytes,
                                        option_.send_buf_bytes);

    if (option_.local_bind.has_value() || option_.local_port != 0) {
      IpEndpoint bind_ep{};
      if (option_.local_bind.has_value()) {
        bind_ep = *option_.local_bind;
      } else {
        if (family == AF_INET) {
          IpEndpointV4 v4;
          v4.ip = {0, 0, 0, 0};
          v4.port = option_.local_port;
          bind_ep = v4;
        } else if (family == AF_INET6) {
          IpEndpointV6 v6;
          v6.ip.fill(0);
          v6.port = option_.local_port;
          v6.scope_id = 0;
          bind_ep = v6;
        } else {
          fail(EAFNOSUPPORT);
          return;
        }
      }

      sockaddr_storage bind_ss{};
      socklen_t bind_len = 0;
      if (!TcpDetail::toSockaddr(bind_ep, bind_ss, bind_len)) {
        fail(EINVAL);
        return;
      }
      if (::bind(fd_, reinterpret_cast<sockaddr *>(&bind_ss), bind_len) != 0) {
        fail(errno);
        return;
      }
    }
  }

  void startConnect() {
    sockaddr_storage ss{};
    socklen_t ss_len = 0;
    if (!TcpDetail::toSockaddr(remote_, ss, ss_len)) {
      fail(EINVAL);
      return;
    }
    const int rc = ::connect(fd_, reinterpret_cast<sockaddr *>(&ss), ss_len);
    if (rc == 0) {
      checkConnectResult();
      return;
    }
    if (errno == EINPROGRESS)
      return;
    fail(errno);
  }

  void onReady(short revents) {
    if (canceled_)
      return;
    if (revents & (POLLOUT | POLLERR | POLLHUP)) {
      checkConnectResult();
    }
  }

  void checkConnectResult() {
    if (canceled_ || fd_ < 0)
      return;
    int err = 0;
    socklen_t len = sizeof(err);
    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
      fail(errno);
      return;
    }
    if (err != 0) {
      fail(err);
      return;
    }

    auto stream = std::make_unique<MacTcpStream>(TcpStreamKind::MacPoll, fd_);
    fd_ = -1;
    stop();
    if (option_.fill_endpoints) {
      TcpDetail::bestEffortFillEndpoints(stream->fd(), *stream);
    }
    if (on_connected_)
      on_connected_(std::move(stream));
  }

  void fail(int err) {
    stop();
    if (canceled_)
      return;
    if (on_error_)
      on_error_(err);
  }

private:
  IpEndpoint remote_;
  TcpConnectOption option_;
  OnConnected on_connected_;
  OnError on_error_;

  int fd_{-1};
  bool attached_{false};
  bool canceled_{false};
  std::shared_ptr<RopHive::MacOS::PollReadinessEventSource> source_;
};

} // namespace

std::shared_ptr<RopHive::Network::ITcpConnectWatcher>
createKqueueTcpConnectWatcher(IOWorker &worker, IpEndpoint remote,
                              TcpConnectOption option,
                              ITcpConnectWatcher::OnConnected on_connected,
                              ITcpConnectWatcher::OnError on_error) {
  return std::make_shared<KqueueTcpConnectWatcher>(
      worker, std::move(remote), std::move(option), std::move(on_connected),
      std::move(on_error));
}

std::shared_ptr<RopHive::Network::ITcpConnectWatcher>
createPollTcpConnectWatcher(IOWorker &worker, IpEndpoint remote,
                            TcpConnectOption option,
                            ITcpConnectWatcher::OnConnected on_connected,
                            ITcpConnectWatcher::OnError on_error) {
  return std::make_shared<PollTcpConnectWatcher>(
      worker, std::move(remote), std::move(option), std::move(on_connected),
      std::move(on_error));
}

} // namespace RopHive::MacOS

#endif // __APPLE__

