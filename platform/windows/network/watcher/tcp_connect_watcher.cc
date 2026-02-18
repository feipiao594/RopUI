#include "tcp_connect_watcher.h"

#ifdef _WIN32

#include <utility>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "../../schedule/iocp_backend.h"
#include "tcp_socket_common.h"

namespace RopHive::Windows {
namespace {

using namespace RopHive::Network;

static bool bindIfNeededOrRequested(SOCKET fd, int family,
                                    const IpEndpoint &remote,
                                    const TcpConnectOption &option) {
  (void)remote;

  IpEndpoint bind_ep{};
  if (option.local_bind.has_value()) {
    bind_ep = *option.local_bind;
  } else if (option.local_port != 0) {
    if (family == AF_INET) {
      IpEndpointV4 v4;
      v4.ip = {0, 0, 0, 0};
      v4.port = option.local_port;
      bind_ep = v4;
    } else if (family == AF_INET6) {
      IpEndpointV6 v6;
      v6.ip.fill(0);
      v6.port = option.local_port;
      v6.scope_id = 0;
      bind_ep = v6;
    } else {
      ::WSASetLastError(WSAEAFNOSUPPORT);
      return false;
    }
  } else {
    // ConnectEx requires the socket to be bound before calling.
    if (family == AF_INET) {
      IpEndpointV4 v4;
      v4.ip = {0, 0, 0, 0};
      v4.port = 0;
      bind_ep = v4;
    } else if (family == AF_INET6) {
      IpEndpointV6 v6;
      v6.ip.fill(0);
      v6.port = 0;
      v6.scope_id = 0;
      bind_ep = v6;
    } else {
      ::WSASetLastError(WSAEAFNOSUPPORT);
      return false;
    }
  }

  sockaddr_storage bind_ss{};
  socklen_t bind_len = 0;
  if (!TcpDetail::toSockaddr(bind_ep, bind_ss, bind_len)) {
    ::WSASetLastError(WSAEINVAL);
    return false;
  }

  if (::bind(fd, reinterpret_cast<sockaddr *>(&bind_ss), bind_len) != 0) {
    return false;
  }

  return true;
}

class IocpTcpConnectWatcher final : public ITcpConnectWatcher {
public:
  IocpTcpConnectWatcher(IOWorker &worker, IpEndpoint remote,
                        TcpConnectOption option, OnConnected on_connected,
                        OnError on_error)
      : ITcpConnectWatcher(worker), remote_(std::move(remote)),
        option_(std::move(option)), on_connected_(std::move(on_connected)),
        on_error_(std::move(on_error)) {}

  ~IocpTcpConnectWatcher() override {
    stop();
    source_.reset();
    TcpDetail::closeFd(fd_);
  }

  void start() override {
    if (attached_ || canceled_)
      return;
    createSocket();
    if (fd_ == INVALID_SOCKET)
      return;

    source_ =
        std::make_shared<RopHive::Windows::IocpHandleCompletionEventSource>(
            reinterpret_cast<HANDLE>(fd_), static_cast<ULONG_PTR>(fd_),
            [this](const IocpRawEvent &event) { onCompletion(event); });
    attachSource(source_);
    attached_ = true;

    startConnect();
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
      fail(WSAEINVAL);
      return;
    }

    const int family = reinterpret_cast<sockaddr *>(&ss)->sa_family;
    fd_ = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (fd_ == INVALID_SOCKET) {
      fail(::WSAGetLastError());
      return;
    }

    TcpDetail::applyCloseOnExecIfConfigured(fd_, option_.set_close_on_exec);
    TcpDetail::applyTcpNoDelayIfConfigured(fd_, option_.tcp_no_delay);
    TcpDetail::applyKeepAliveIfConfigured(
        fd_, option_.keep_alive, option_.keep_alive_idle_sec,
        option_.keep_alive_interval_sec, option_.keep_alive_count);
    TcpDetail::applyBufSizeIfConfigured(fd_, option_.recv_buf_bytes,
                                        option_.send_buf_bytes);

    const auto [connect_ex, _] = TcpDetail::fetchConnectEx(fd_);
    connect_ex_ = connect_ex;
    if (!connect_ex_) {
      fail(::WSAGetLastError());
      return;
    }

    if (!bindIfNeededOrRequested(fd_, family, remote_, option_)) {
      fail(::WSAGetLastError());
      return;
    }
  }

  void startConnect() {
    if (canceled_ || fd_ == INVALID_SOCKET)
      return;

    sockaddr_storage ss{};
    socklen_t ss_len = 0;
    if (!TcpDetail::toSockaddr(remote_, ss, ss_len)) {
      fail(WSAEINVAL);
      return;
    }

    connect_ov_ = {};
    DWORD bytes = 0;
    const BOOL ok = connect_ex_(fd_, reinterpret_cast<sockaddr *>(&ss), ss_len,
                                nullptr, 0, &bytes, &connect_ov_);
    if (!ok) {
      const int err = ::WSAGetLastError();
      if (err != WSA_IO_PENDING) {
        fail(err);
        return;
      }
    }
    connecting_ = true;
  }

  void onCompletion(const IocpRawEvent &event) {
    if (canceled_ || fd_ == INVALID_SOCKET)
      return;
    if (event.overlapped != &connect_ov_)
      return;

    connecting_ = false;

    DWORD transferred = 0;
    DWORD flags = 0;
    const BOOL ok = ::WSAGetOverlappedResult(
        fd_, reinterpret_cast<LPWSAOVERLAPPED>(&connect_ov_), &transferred,
        FALSE, &flags);
    (void)transferred;
    if (!ok) {
      fail(::WSAGetLastError());
      return;
    }

    (void)::setsockopt(fd_, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);

    auto stream = std::make_unique<WindowsTcpStream>(fd_);
    fd_ = INVALID_SOCKET;
    stop();

    if (option_.fill_endpoints) {
      TcpDetail::bestEffortFillEndpoints(stream->fd(), *stream);
    }

    if (on_connected_) {
      on_connected_(std::move(stream));
    }
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

  SOCKET fd_{INVALID_SOCKET};
  bool attached_{false};
  bool canceled_{false};
  bool connecting_{false};

  LPFN_CONNECTEX connect_ex_{nullptr};
  OVERLAPPED connect_ov_{};

  std::shared_ptr<RopHive::Windows::IocpHandleCompletionEventSource> source_;
};

} // namespace

std::shared_ptr<RopHive::Network::ITcpConnectWatcher>
createIocpTcpConnectWatcher(IOWorker &worker, IpEndpoint remote,
                            TcpConnectOption option,
                            ITcpConnectWatcher::OnConnected on_connected,
                            ITcpConnectWatcher::OnError on_error) {
  return std::make_shared<IocpTcpConnectWatcher>(
      worker, std::move(remote), std::move(option), std::move(on_connected),
      std::move(on_error));
}

} // namespace RopHive::Windows

#endif // _WIN32
