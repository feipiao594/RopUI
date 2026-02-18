#include "tcp_connection_watcher.h"

#ifdef _WIN32

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

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

    source_ =
        std::make_shared<RopHive::Windows::IocpHandleCompletionEventSource>(
            reinterpret_cast<HANDLE>(fd_), static_cast<ULONG_PTR>(fd_),
            [this](const IocpRawEvent &event) { onCompletion(event); });
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

    // IOCP: treat a concurrent send pipeline as \"busy\". When busy, the caller
    // should wait for OnSendReady then try again.
    if (sending_) {
      want_send_ready_ = true;
      res.would_block = true;
      return res;
    }

    send_buf_.assign(data.begin(),
                     data.begin() + static_cast<ptrdiff_t>(limit));

    send_ov_ = {};
    WSABUF wsabuf{};
    wsabuf.buf = send_buf_.data();
    wsabuf.len = static_cast<ULONG>(send_buf_.size());

    sending_ = true;
    const int rc = ::WSASend(fd_, &wsabuf, 1, nullptr, 0, &send_ov_, nullptr);
    if (rc == SOCKET_ERROR) {
      const int err = ::WSAGetLastError();
      if (err != WSA_IO_PENDING) {
        sending_ = false;
        send_buf_.clear();
        res.err = err;
        if (on_error_)
          on_error_(err);
        closeNow();
        return res;
      }
    }

    res.n = send_buf_.size();
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

    recv_ov_ = {};
    WSABUF wsabuf{};
    wsabuf.buf = in_buf_.data();
    wsabuf.len = static_cast<ULONG>(in_buf_.size());

    DWORD flags = 0;
    const int rc =
        ::WSARecv(fd_, &wsabuf, 1, nullptr, &flags, &recv_ov_, nullptr);
    if (rc == SOCKET_ERROR) {
      const int err = ::WSAGetLastError();
      if (err != WSA_IO_PENDING) {
        if (on_error_)
          on_error_(err);
        closeNow();
        return;
      }
    }
    receiving_ = true;
  }

  void onCompletion(const IocpRawEvent &event) {
    if (fd_ == INVALID_SOCKET || closed_)
      return;
    if (event.overlapped == nullptr)
      return;

    if (event.overlapped == &recv_ov_) {
      onRecvComplete();
      return;
    }
    if (event.overlapped == &send_ov_) {
      onSendComplete();
      return;
    }
  }

  void onRecvComplete() {
    receiving_ = false;

    DWORD transferred = 0;
    DWORD flags = 0;
    const BOOL ok = ::WSAGetOverlappedResult(
        fd_, reinterpret_cast<LPWSAOVERLAPPED>(&recv_ov_), &transferred, FALSE,
        &flags);
    if (!ok) {
      const int err = ::WSAGetLastError();
      if (on_error_)
        on_error_(err);
      closeNow();
      return;
    }

    if (transferred == 0) {
      peer_closed_ = true;
      closeNow();
      return;
    }

    if (on_recv_) {
      on_recv_(
          std::string_view(in_buf_.data(), static_cast<size_t>(transferred)));
    }

    if (!closed_)
      startRecv();
  }

  void onSendComplete() {
    sending_ = false;

    DWORD transferred = 0;
    DWORD flags = 0;
    const BOOL ok = ::WSAGetOverlappedResult(
        fd_, reinterpret_cast<LPWSAOVERLAPPED>(&send_ov_), &transferred, FALSE,
        &flags);
    if (!ok) {
      const int err = ::WSAGetLastError();
      if (on_error_)
        on_error_(err);
      closeNow();
      return;
    }

    (void)transferred;
    send_buf_.clear();

    if (want_send_ready_ && !closed_) {
      want_send_ready_ = false;
      if (on_send_ready_)
        on_send_ready_();
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
  bool sending_{false};

  std::vector<char> in_buf_;
  OVERLAPPED recv_ov_{};

  std::vector<char> send_buf_;
  OVERLAPPED send_ov_{};

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

#endif // _WIN32
