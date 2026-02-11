#include "tcp_connection_watcher.h"

#ifdef __linux__

#include <cerrno>
#include <string_view>
#include <vector>

#include "../../schedule/epoll_backend.h"
#include "../../schedule/poll_backend.h"

#include "tcp_socket_common.h"

namespace RopHive::Linux {
namespace {

using namespace RopHive::Network;

static int consumeLinuxStream(std::unique_ptr<ITcpStream> stream) {
  // stream is a middle value, it should be recycled after consume
  auto *linux_stream = dynamic_cast<LinuxTcpStream *>(stream.get());
  if (!linux_stream) {
    throw std::runtime_error(
        "createTcpConnectionWatcher(linux): stream type mismatch");
  }
  const int fd = linux_stream->releaseFd();
  stream.reset();
  return fd;
}

class EpollTcpConnectionWatcher final : public ITcpConnectionWatcher {
public:
  EpollTcpConnectionWatcher(IOWorker &worker, TcpConnectionOption option,
                            int fd, OnRecv on_recv, OnClose on_close,
                            OnError on_error, OnSendReady on_send_ready)
      : ITcpConnectionWatcher(worker), option_(std::move(option)), fd_(fd),
        on_recv_(std::move(on_recv)), on_close_(std::move(on_close)),
        on_error_(std::move(on_error)),
        on_send_ready_(std::move(on_send_ready)) {
    in_buf_.resize(64 * 1024);
    TcpDetail::applyTcpNoDelayIfConfigured(fd_, option_.tcp_no_delay);
    TcpDetail::applyKeepAliveIfConfigured(
        fd_, option_.keep_alive, option_.keep_alive_idle_sec,
        option_.keep_alive_interval_sec, option_.keep_alive_count);
    TcpDetail::applyBufSizeIfConfigured(fd_, option_.recv_buf_bytes,
                                        option_.send_buf_bytes);
    TcpDetail::applyLingerIfConfigured(fd_, option_.linger_sec);

    source_ = std::make_shared<RopHive::Linux::EpollReadinessEventSource>(
        fd_, EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP,
        [this](uint32_t events) { onReady(events); });
    armed_events_ = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
  }

  ~EpollTcpConnectionWatcher() override {
    stop();
    source_.reset();
    TcpDetail::closeFd(fd_);
  }

  void start() override {
    if (attached_)
      return;
    attachSource(source_);
    attached_ = true;
  }

  void stop() override {
    if (!attached_)
      return;
    detachSource(source_);
    attached_ = false;
  }

  TrySendResult trySend(std::string_view data) override {
    TrySendResult res;
    if (fd_ < 0) {
      res.err = EBADF;
      return res;
    }
    if (data.empty())
      return res;

    const size_t limit =
        std::min(option_.max_write_bytes_per_tick, data.size());
    size_t total = 0;
    while (total < limit) {
      const ssize_t n =
          ::send(fd_, data.data() + total, limit - total, MSG_NOSIGNAL);
      if (n > 0) {
        total += static_cast<size_t>(n);
        continue;
      }
      if (n < 0 && errno == EINTR)
        continue;
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        want_send_ready_ = true;
        armWritable();
        res.would_block = true;
        break;
      }
      res.err = (n < 0) ? errno : EIO;
      if (on_error_)
        on_error_(res.err);
      closeNow();
      res.n = total;
      return res;
    }
    res.n = total;
    return res;
  }

  void shutdownWrite() override {
    if (fd_ < 0)
      return;
    ::shutdown(fd_, SHUT_WR);
  }

  void close() override { closeNow(); }

private:
  void armWritable() {
    if (!source_)
      return;
    const uint32_t want =
        armed_events_ | EPOLLOUT | EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    if (want == armed_events_)
      return;
    armed_events_ = want;
    source_->setEvents(armed_events_);
  }

  void disarmWritable() {
    if (!source_)
      return;
    const uint32_t want = (armed_events_ & ~EPOLLOUT) | EPOLLIN | EPOLLERR |
                          EPOLLHUP | EPOLLRDHUP;
    if (want == armed_events_)
      return;
    armed_events_ = want;
    source_->setEvents(armed_events_);
  }

  void onReady(uint32_t events) {
    if (fd_ < 0)
      return;

    if (events & EPOLLERR) {
      int err = 0;
      socklen_t len = sizeof(err);
      if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
        err = errno;
      }
      if (on_error_)
        on_error_(err != 0 ? err : EIO);
      closeNow();
      return;
    }

    if (events & (EPOLLHUP | EPOLLRDHUP)) {
      peer_closed_ = true;
    }

    if ((events & EPOLLIN) || peer_closed_) {
      handleRead();
    }
    if (peer_closed_) {
      closeNow();
      return;
    }

    if ((events & EPOLLOUT) && want_send_ready_) {
      want_send_ready_ = false;
      disarmWritable();
      if (on_send_ready_)
        on_send_ready_();
    }
  }

  void handleRead() {
    size_t remaining = option_.max_read_bytes_per_tick;
    while (remaining > 0) {
      const size_t to_read = std::min(remaining, in_buf_.size());
      const ssize_t n = ::recv(fd_, in_buf_.data(), to_read, 0);
      if (n > 0) {
        remaining -= static_cast<size_t>(n);
        if (on_recv_)
          on_recv_(std::string_view(in_buf_.data(), static_cast<size_t>(n)));
        continue;
      }
      if (n == 0) {
        peer_closed_ = true;
        return;
      }
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
      if (on_error_)
        on_error_(errno);
      closeNow();
      return;
    }
  }

  void closeNow() {
    if (closed_)
      return;
    closed_ = true;
    stop();
    TcpDetail::closeFd(fd_);
    if (on_close_)
      on_close_();
  }

private:
  TcpConnectionOption option_;
  int fd_{-1};

  OnRecv on_recv_;
  OnClose on_close_;
  OnError on_error_;
  OnSendReady on_send_ready_;

  bool attached_{false};
  bool peer_closed_{false};
  bool want_send_ready_{false};
  bool closed_{false};

  uint32_t armed_events_{0};
  std::vector<char> in_buf_;
  std::shared_ptr<RopHive::Linux::EpollReadinessEventSource> source_;
};

class PollTcpConnectionWatcher final : public ITcpConnectionWatcher {
public:
  PollTcpConnectionWatcher(IOWorker &worker, TcpConnectionOption option, int fd,
                           OnRecv on_recv, OnClose on_close, OnError on_error,
                           OnSendReady on_send_ready)
      : ITcpConnectionWatcher(worker), option_(std::move(option)), fd_(fd),
        on_recv_(std::move(on_recv)), on_close_(std::move(on_close)),
        on_error_(std::move(on_error)),
        on_send_ready_(std::move(on_send_ready)) {
    in_buf_.resize(64 * 1024);
    TcpDetail::applyTcpNoDelayIfConfigured(fd_, option_.tcp_no_delay);
    TcpDetail::applyKeepAliveIfConfigured(
        fd_, option_.keep_alive, option_.keep_alive_idle_sec,
        option_.keep_alive_interval_sec, option_.keep_alive_count);
    TcpDetail::applyBufSizeIfConfigured(fd_, option_.recv_buf_bytes,
                                        option_.send_buf_bytes);
    TcpDetail::applyLingerIfConfigured(fd_, option_.linger_sec);

    source_ = std::make_shared<RopHive::Linux::PollReadinessEventSource>(
        fd_, POLLIN | POLLERR | POLLHUP,
        [this](short revents) { onReady(revents); });
    armed_events_ = POLLIN | POLLERR | POLLHUP;
  }

  ~PollTcpConnectionWatcher() override {
    stop();
    source_.reset();
    TcpDetail::closeFd(fd_);
  }

  void start() override {
    if (attached_)
      return;
    attachSource(source_);
    attached_ = true;
  }

  void stop() override {
    if (!attached_)
      return;
    detachSource(source_);
    attached_ = false;
  }

  TrySendResult trySend(std::string_view data) override {
    TrySendResult res;
    if (fd_ < 0) {
      res.err = EBADF;
      return res;
    }
    if (data.empty())
      return res;

    const size_t limit =
        std::min(option_.max_write_bytes_per_tick, data.size());
    size_t total = 0;
    while (total < limit) {
      const ssize_t n =
          ::send(fd_, data.data() + total, limit - total, MSG_NOSIGNAL);
      if (n > 0) {
        total += static_cast<size_t>(n);
        continue;
      }
      if (n < 0 && errno == EINTR)
        continue;
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        want_send_ready_ = true;
        armWritable();
        res.would_block = true;
        break;
      }
      res.err = (n < 0) ? errno : EIO;
      if (on_error_)
        on_error_(res.err);
      closeNow();
      res.n = total;
      return res;
    }
    res.n = total;
    return res;
  }

  void shutdownWrite() override {
    if (fd_ < 0)
      return;
    ::shutdown(fd_, SHUT_WR);
  }

  void close() override { closeNow(); }

private:
  void armWritable() {
    if (!source_)
      return;
    const int want = armed_events_ | POLLOUT | POLLIN | POLLERR | POLLHUP;
    if (want == armed_events_)
      return;
    armed_events_ = want;
    source_->setEvents(static_cast<short>(armed_events_));
  }

  void disarmWritable() {
    if (!source_)
      return;
    const int want = (armed_events_ & ~POLLOUT) | POLLIN | POLLERR | POLLHUP;
    if (want == armed_events_)
      return;
    armed_events_ = want;
    source_->setEvents(static_cast<short>(armed_events_));
  }

  void onReady(short revents) {
    if (fd_ < 0)
      return;

    if (revents & POLLERR) {
      int err = 0;
      socklen_t len = sizeof(err);
      if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
        err = errno;
      }
      if (on_error_)
        on_error_(err != 0 ? err : EIO);
      closeNow();
      return;
    }

    if (revents & POLLHUP) {
      peer_closed_ = true;
    }

    if ((revents & POLLIN) || peer_closed_) {
      handleRead();
    }
    if (peer_closed_) {
      closeNow();
      return;
    }

    if ((revents & POLLOUT) && want_send_ready_) {
      want_send_ready_ = false;
      disarmWritable();
      if (on_send_ready_)
        on_send_ready_();
    }
  }

  void handleRead() {
    size_t remaining = option_.max_read_bytes_per_tick;
    while (remaining > 0) {
      const size_t to_read = std::min(remaining, in_buf_.size());
      const ssize_t n = ::recv(fd_, in_buf_.data(), to_read, 0);
      if (n > 0) {
        remaining -= static_cast<size_t>(n);
        if (on_recv_)
          on_recv_(std::string_view(in_buf_.data(), static_cast<size_t>(n)));
        continue;
      }
      if (n == 0) {
        peer_closed_ = true;
        return;
      }
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
      if (on_error_)
        on_error_(errno);
      closeNow();
      return;
    }
  }

  void closeNow() {
    if (closed_)
      return;
    closed_ = true;
    stop();
    TcpDetail::closeFd(fd_);
    if (on_close_)
      on_close_();
  }

private:
  TcpConnectionOption option_;
  int fd_{-1};

  OnRecv on_recv_;
  OnClose on_close_;
  OnError on_error_;
  OnSendReady on_send_ready_;

  bool attached_{false};
  bool peer_closed_{false};
  bool want_send_ready_{false};
  bool closed_{false};

  int armed_events_{0};
  std::vector<char> in_buf_;
  std::shared_ptr<RopHive::Linux::PollReadinessEventSource> source_;
};

} // namespace

std::shared_ptr<RopHive::Network::ITcpConnectionWatcher>
createEpollTcpConnectionWatcher(
    IOWorker &worker, TcpConnectionOption option,
    std::unique_ptr<ITcpStream> connected_stream,
    ITcpConnectionWatcher::OnRecv on_recv,
    ITcpConnectionWatcher::OnClose on_close,
    ITcpConnectionWatcher::OnError on_error,
    ITcpConnectionWatcher::OnSendReady on_send_ready) {
  const int fd = consumeLinuxStream(std::move(connected_stream));
  return std::make_shared<EpollTcpConnectionWatcher>(
      worker, std::move(option), fd, std::move(on_recv), std::move(on_close),
      std::move(on_error), std::move(on_send_ready));
}

std::shared_ptr<RopHive::Network::ITcpConnectionWatcher>
createPollTcpConnectionWatcher(
    IOWorker &worker, TcpConnectionOption option,
    std::unique_ptr<ITcpStream> connected_stream,
    ITcpConnectionWatcher::OnRecv on_recv,
    ITcpConnectionWatcher::OnClose on_close,
    ITcpConnectionWatcher::OnError on_error,
    ITcpConnectionWatcher::OnSendReady on_send_ready) {
  const int fd = consumeLinuxStream(std::move(connected_stream));
  return std::make_shared<PollTcpConnectionWatcher>(
      worker, std::move(option), fd, std::move(on_recv), std::move(on_close),
      std::move(on_error), std::move(on_send_ready));
}

} // namespace RopHive::Linux

#endif // __linux__
