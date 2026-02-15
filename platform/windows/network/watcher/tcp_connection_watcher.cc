#include "tcp_connection_watcher.h"

#if defined(_WIN32) or defined(_WIN64)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <cerrno>
#include <utility>
#include <vector>

#include "../../schedule/iocp_backend.h"
#include "tcp_socket_common.h"

namespace RopHive::Windows {
namespace {

using namespace RopHive::Network;

static SOCKET consumeWindowsStream(std::unique_ptr<ITcpStream> stream) {
  auto *win_stream = dynamic_cast<WindowsTcpStream *>(stream.get());
  if (!win_stream) {
    throw std::runtime_error(
        "createTcpConnectionWatcher(windows): stream type mismatch");
  }
  const SOCKET fd = win_stream->releaseFd();
  stream.reset();
  return fd;
}

class IocpTcpConnectionWatcher final : public ITcpConnectionWatcher {
public:
  IocpTcpConnectionWatcher(IOWorker &worker, TcpConnectionOption option,
                           SOCKET fd, OnRecv on_recv, OnClose on_close,
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

    source_ = std::make_shared<RopHive::Windows::IocpHandleCompletionEventSource>(
        reinterpret_cast<HANDLE>(fd_), fd_,
        [this](const IocpRawEvent& event) { onCompletion(event); });
  }

  ~IocpTcpConnectionWatcher() override {
    stop();
    source_.reset();
    TcpDetail::closeFd(fd_);
  }

  void start() override {
    if (attached_)
      return;
    attachSource(source_);
    attached_ = true;
    startRecv();
  }

  void stop() override {
    if (!attached_)
      return;
    detachSource(source_);
    attached_ = false;
  }

  TrySendResult trySend(std::string_view data) override {
    TrySendResult res;
    if (fd_ == INVALID_SOCKET) {
      res.err = WSAEBADF;
      return res;
    }
    if (data.empty())
      return res;

    const size_t limit =
        std::min(option_.max_write_bytes_per_tick, data.size());

    // best-effort synchronous send (non-overlapped)
    size_t total = 0;
    while (total < limit) {
      const int to_send = static_cast<int>(std::min(size_t(INT_MAX), limit - total));
      const int n = ::send(fd_, data.data() + total, to_send, 0);
      if (n > 0) {
        total += static_cast<size_t>(n);
        continue;
      }
      if (n == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
          want_send_ready_ = true;
          res.would_block = true;
          break;
        }
        res.err = err;
        if (on_error_)
          on_error_(err);
        closeNow();
        res.n = total;
        return res;
      }
    }
    res.n = total;
    return res;
  }

  void shutdownWrite() override {
    if (fd_ == INVALID_SOCKET)
      return;
    ::shutdown(fd_, SD_SEND);
  }

  void close() override { closeNow(); }

private:
  void startRecv() {
    if (fd_ == INVALID_SOCKET || receiving_)
      return;

    recv_overlapped_ = {};
    WSABUF wsabuf;
    wsabuf.buf = in_buf_.data();
    wsabuf.len = static_cast<ULONG>(in_buf_.size());

    DWORD flags = 0;
    const int rc = WSARecv(fd_, &wsabuf, 1, nullptr, &flags, &recv_overlapped_, nullptr);
    if (rc == SOCKET_ERROR) {
      const int err = WSAGetLastError();
      if (err != WSA_IO_PENDING) {
        if (on_error_)
          on_error_(err);
        closeNow();
        return;
      }
    }
    receiving_ = true;
  }

  void onCompletion(const IocpRawEvent& event) {
    if (fd_ == INVALID_SOCKET)
      return;

    if (event.error) {
      if (on_error_)
        on_error_((int)event.error);
      closeNow();
      return;
    }

    // Check which overlapped operation completed
    if (&recv_overlapped_ == event.overlapped) {
      handleRecvComplete(event.bytes);
      receiving_ = false;
      if (!closed_)
        startRecv();
    }
  }

  void handleRecvComplete(DWORD bytes) {
    if (bytes == 0) {
      peer_closed_ = true;
      closeNow();
      return;
    }

    if (on_recv_)
      on_recv_(std::string_view(in_buf_.data(), static_cast<size_t>(bytes)));
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
  SOCKET fd_{INVALID_SOCKET};

  OnRecv on_recv_;
  OnClose on_close_;
  OnError on_error_;
  OnSendReady on_send_ready_;

  bool attached_{false};
  bool peer_closed_{false};
  bool want_send_ready_{false};
  bool closed_{false};
  bool receiving_{false};

  std::vector<char> in_buf_;
  OVERLAPPED recv_overlapped_{};

  std::shared_ptr<RopHive::Windows::IocpHandleCompletionEventSource> source_;
};

} // namespace

std::shared_ptr<RopHive::Network::ITcpConnectionWatcher>
createIocpTcpConnectionWatcher(
    IOWorker &worker, TcpConnectionOption option,
    std::unique_ptr<ITcpStream> connected_stream,
    ITcpConnectionWatcher::OnRecv on_recv,
    ITcpConnectionWatcher::OnClose on_close,
    ITcpConnectionWatcher::OnError on_error,
    ITcpConnectionWatcher::OnSendReady on_send_ready) {
  const SOCKET fd = consumeWindowsStream(std::move(connected_stream));
  if (fd == INVALID_SOCKET) {
    if (on_error)
      on_error(WSAEBADF);
    return {};
  }
  return std::make_shared<IocpTcpConnectionWatcher>(
      worker, std::move(option), fd, std::move(on_recv), std::move(on_close),
      std::move(on_error), std::move(on_send_ready));
}

} // namespace RopHive::Windows

#endif // defined(_WIN32) or defined(_WIN64)
