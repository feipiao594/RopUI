#include "tcp_connect_watcher.h"

#if defined(_WIN32) or defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <cerrno>
#include <utility>

#include "../../schedule/iocp_backend.h"
#include "tcp_socket_common.h"

namespace RopHive::Windows {
namespace {

using namespace RopHive::Network;

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
    startConnect();
    if (fd_ == INVALID_SOCKET)
      return;

    source_ = std::make_shared<RopHive::Windows::IocpHandleCompletionEventSource>(
        reinterpret_cast<HANDLE>(fd_),
        fd_,
        [this](const IocpRawEvent& event) { onCompletion(event); });
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
      fail(WSAEINVAL);
      return;
    }

    const int family = reinterpret_cast<sockaddr *>(&ss)->sa_family;
    fd_ = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (fd_ == INVALID_SOCKET) {
      fail(WSAGetLastError());
      return;
    }

    if (!TcpDetail::applyTcpNonBlock(fd_)) {
      fail(WSAGetLastError());
      return;
    }

    TcpDetail::applyCloseOnExecIfConfigured(fd_, option_.set_close_on_exec);
    TcpDetail::applyTcpNoDelayIfConfigured(fd_, option_.tcp_no_delay);
    TcpDetail::applyKeepAliveIfConfigured(
        fd_, option_.keep_alive, option_.keep_alive_idle_sec,
        option_.keep_alive_interval_sec, option_.keep_alive_count);
    TcpDetail::applyBufSizeIfConfigured(fd_, option_.recv_buf_bytes,
                                        option_.send_buf_bytes);
    // TcpDetail::applyLingerIfConfigured(fd_, option_.linger_sec);

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
          fail(WSAEAFNOSUPPORT);
          return;
        }
      }

      sockaddr_storage bind_ss{};
      socklen_t bind_len = 0;
      if (!TcpDetail::toSockaddr(bind_ep, bind_ss, bind_len)) {
        fail(WSAEINVAL);
        return;
      }
      if (::bind(fd_, reinterpret_cast<sockaddr *>(&bind_ss), bind_len) != 0) {
        fail(WSAGetLastError());
        return;
      }
    }
  }

  void startConnect() {
    sockaddr_storage ss{};
    socklen_t ss_len = 0;
    if (!TcpDetail::toSockaddr(remote_, ss, ss_len)) {
      fail(WSAEINVAL);
      return;
    }

    // Fetch ConnectEx function pointer
    GUID guid_connect_ex = WSAID_CONNECTEX;
    LPFN_CONNECTEX connect_ex = nullptr;
    DWORD bytes = 0;
    if (WSAIoctl(fd_, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &guid_connect_ex, sizeof(guid_connect_ex),
                 &connect_ex, sizeof(connect_ex),
                 &bytes, nullptr, nullptr) != 0) {
      fail(WSAGetLastError());
      return;
    }

    overlapped_ = {};
    BOOL rc = connect_ex(fd_, reinterpret_cast<sockaddr *>(&ss), ss_len,
                         nullptr, 0, &bytes, &overlapped_);
    if (!rc) {
      const int err = WSAGetLastError();
      if (err != WSA_IO_PENDING) {
        fail(err);
        return;
      }
    }
  }

  void onCompletion(const IocpRawEvent& event) {
    if (canceled_)
      return;
    if (event.error) {
      fail((int)event.error);
      return;
    }
    checkConnectResult();
  }

  void checkConnectResult() {
    if (canceled_ || fd_ == INVALID_SOCKET)
      return;

    int err = 0;
    socklen_t len = sizeof(err);
    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char *>(&err), &len) != 0) {
      fail(WSAGetLastError());
      return;
    }
    if (err != 0) {
      fail(err);
      return;
    }

    auto stream = std::make_unique<WindowsTcpStream>(fd_, accept_buffer_dummy_);
    fd_ = INVALID_SOCKET;
    stop();
    if (option_.fill_endpoints) {
      // best-effort fill peer/local (Windows specific implementation)
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

  SOCKET fd_{INVALID_SOCKET};
  OVERLAPPED overlapped_{};
  char accept_buffer_dummy_[2 * (sizeof(sockaddr_storage) + 32)] = {};

  bool attached_{false};
  bool canceled_{false};
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

#endif // defined(_WIN32) or defined(_WIN64)
