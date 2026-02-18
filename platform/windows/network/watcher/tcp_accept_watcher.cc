#include "tcp_accept_watcher.h"

#ifdef _WIN32

#include <algorithm>
#include <utility>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "../../schedule/iocp_backend.h"
#include "tcp_socket_common.h"

namespace RopHive::Windows {
namespace {

using namespace RopHive::Network;

static SOCKET createListenSocket(const TcpAcceptOption &option) {
  sockaddr_storage ss{};
  socklen_t ss_len = 0;
  if (!TcpDetail::toSockaddr(option.local, ss, ss_len)) {
    ::WSASetLastError(WSAEINVAL);
    return INVALID_SOCKET;
  }

  const int family = reinterpret_cast<sockaddr *>(&ss)->sa_family;
  SOCKET fd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
  if (fd == INVALID_SOCKET) {
    return INVALID_SOCKET;
  }

  TcpDetail::applyCloseOnExecIfConfigured(fd, option.set_close_on_exec);
  TcpDetail::applyTcpReuseAddrIfConfigured(fd, option.reuse_addr);

  if (family == AF_INET6 && option.ipv6_only.has_value()) {
    const BOOL opt = *option.ipv6_only ? TRUE : FALSE;
    (void)::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
                       reinterpret_cast<const char *>(&opt), sizeof(opt));
  }

  if (::bind(fd, reinterpret_cast<sockaddr *>(&ss), ss_len) != 0) {
    TcpDetail::closeFd(fd);
    return INVALID_SOCKET;
  }

  if (::listen(fd, option.backlog) != 0) {
    TcpDetail::closeFd(fd);
    return INVALID_SOCKET;
  }

  return fd;
}

static SOCKET createAcceptSocket(SOCKET listen_fd) {
  sockaddr_storage ss{};
  int ss_len = sizeof(ss);
  if (::getsockname(listen_fd, reinterpret_cast<sockaddr *>(&ss), &ss_len) !=
      0) {
    return INVALID_SOCKET;
  }
  const int family = reinterpret_cast<sockaddr *>(&ss)->sa_family;
  return ::socket(family, SOCK_STREAM, IPPROTO_TCP);
}

static void bestEffortConfigureAcceptedSocket(SOCKET fd,
                                              const TcpAcceptOption &option) {
  TcpDetail::applyCloseOnExecIfConfigured(fd, option.set_close_on_exec);
  TcpDetail::applyTcpNoDelayIfConfigured(fd, option.tcp_no_delay);
  TcpDetail::applyKeepAliveIfConfigured(
      fd, option.keep_alive, option.keep_alive_idle_sec,
      option.keep_alive_interval_sec, option.keep_alive_count);
  TcpDetail::applyBufSizeIfConfigured(fd, option.recv_buf_bytes,
                                      option.send_buf_bytes);
}

class IocpTcpAcceptWatcher final : public ITcpAcceptWatcher {
public:
  IocpTcpAcceptWatcher(IOWorker &worker, SOCKET listen_fd,
                       TcpAcceptOption option, OnAccept on_accept,
                       OnError on_error)
      : ITcpAcceptWatcher(worker), listen_fd_(listen_fd),
        option_(std::move(option)), on_accept_(std::move(on_accept)),
        on_error_(std::move(on_error)) {
    const auto [accept_ex, _] = TcpDetail::fetchAcceptEx(listen_fd_);
    accept_ex_ = accept_ex;

    source_ =
        std::make_shared<RopHive::Windows::IocpHandleCompletionEventSource>(
            reinterpret_cast<HANDLE>(listen_fd_),
            static_cast<ULONG_PTR>(listen_fd_),
            [this](const IocpRawEvent &event) { onCompletion(event); });
  }

  ~IocpTcpAcceptWatcher() override {
    stop();
    source_.reset();

    for (auto &op : ops_) {
      TcpDetail::closeFd(op.accept_fd);
    }
    ops_.clear();

    TcpDetail::closeFd(listen_fd_);
  }

  void start() override {
    if (attached_)
      return;
    if (!accept_ex_) {
      if (on_error_)
        on_error_(WSAEINVAL);
      return;
    }

    attachSource(source_);
    attached_ = true;

    const size_t depth = std::max<size_t>(
        1, std::min(option_.max_accept_per_tick, static_cast<size_t>(32)));
    ops_.resize(depth);
    for (auto &op : ops_) {
      postAccept(op);
    }
  }

  void stop() override {
    if (!attached_)
      return;
    detachSource(source_);
    attached_ = false;
  }

private:
  struct AcceptOp {
    OVERLAPPED ov{};
    SOCKET accept_fd{INVALID_SOCKET};
    DWORD bytes{0};
    AcceptBufferPtr buffer;
    bool in_flight{false};
  };

  bool postAccept(AcceptOp &op) {
    if (!attached_)
      return false;

    TcpDetail::closeFd(op.accept_fd);
    op.accept_fd = createAcceptSocket(listen_fd_);
    if (op.accept_fd == INVALID_SOCKET) {
      if (on_error_)
        on_error_(::WSAGetLastError());
      return false;
    }

    op.buffer = std::make_shared<AcceptBuffer>();
    std::fill(op.buffer->begin(), op.buffer->end(), 0);

    op.ov = {};
    op.bytes = 0;

    constexpr DWORD addr_len = sizeof(sockaddr_storage) + 16;
    const BOOL ok = accept_ex_(listen_fd_, op.accept_fd, op.buffer->data(), 0,
                               addr_len, addr_len, &op.bytes, &op.ov);
    if (!ok) {
      const int err = ::WSAGetLastError();
      if (err != WSA_IO_PENDING) {
        if (on_error_)
          on_error_(err);
        TcpDetail::closeFd(op.accept_fd);
        op.buffer.reset();
        return false;
      }
    }

    op.in_flight = true;
    return true;
  }

  AcceptOp *findOp(OVERLAPPED *ov) {
    if (!ov)
      return nullptr;
    for (auto &op : ops_) {
      if (&op.ov == ov)
        return &op;
    }
    return nullptr;
  }

  void onCompletion(const IocpRawEvent &event) {
    if (!attached_)
      return;
    if (event.overlapped == nullptr)
      return;

    AcceptOp *op = findOp(event.overlapped);
    if (!op)
      return;
    op->in_flight = false;

    DWORD transferred = 0;
    DWORD flags = 0;
    const BOOL ok = ::WSAGetOverlappedResult(
        listen_fd_, reinterpret_cast<LPWSAOVERLAPPED>(event.overlapped),
        &transferred, FALSE, &flags);
    if (!ok) {
      const int err = ::WSAGetLastError();
      TcpDetail::closeFd(op->accept_fd);
      op->buffer.reset();
      if (on_error_)
        on_error_(err);
      postAccept(*op);
      return;
    }

    (void)transferred; // AcceptEx bytes are \"initial data\"; may be 0 on
                       // success.

    (void)::setsockopt(op->accept_fd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                       reinterpret_cast<const char *>(&listen_fd_),
                       sizeof(listen_fd_));

    bestEffortConfigureAcceptedSocket(op->accept_fd, option_);

    auto stream = std::make_unique<WindowsTcpStream>(op->accept_fd, op->buffer);
    if (option_.fill_endpoints) {
      TcpDetail::bestEffortFillEndpoints(op->accept_fd, *stream);
    }

    op->accept_fd = INVALID_SOCKET;
    op->buffer.reset();

    if (on_accept_) {
      on_accept_(std::move(stream));
    } else {
      // No consumer; close immediately.
      stream.reset();
    }

    postAccept(*op);
  }

private:
  SOCKET listen_fd_{INVALID_SOCKET};
  TcpAcceptOption option_;
  OnAccept on_accept_;
  OnError on_error_;

  bool attached_{false};
  LPFN_ACCEPTEX accept_ex_{nullptr};
  std::vector<AcceptOp> ops_;

  std::shared_ptr<RopHive::Windows::IocpHandleCompletionEventSource> source_;
};

} // namespace

std::shared_ptr<RopHive::Network::ITcpAcceptWatcher>
createIocpTcpAcceptWatcher(IOWorker &worker, TcpAcceptOption option,
                           ITcpAcceptWatcher::OnAccept on_accept,
                           ITcpAcceptWatcher::OnError on_error) {
  SOCKET listen_fd = createListenSocket(option);
  if (listen_fd == INVALID_SOCKET) {
    if (on_error)
      on_error(::WSAGetLastError());
    return {};
  }

  return std::make_shared<IocpTcpAcceptWatcher>(
      worker, listen_fd, std::move(option), std::move(on_accept),
      std::move(on_error));
}

} // namespace RopHive::Windows

#endif // _WIN32
