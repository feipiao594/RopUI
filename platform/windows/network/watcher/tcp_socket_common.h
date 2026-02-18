#ifndef _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_SOCKET_COMMON_H
#define _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_SOCKET_COMMON_H

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN

#include "../../win32_wrapper.h"

#include <mstcpip.h>
#include <mswsock.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <tuple>

#include "../../../network/ip_endpoint.h"
#include "../../../network/watcher/tcp_watchers.h"

namespace RopHive::Windows::TcpDetail {

inline void closeFd(SOCKET &fd) {
  if (fd != INVALID_SOCKET) {
    ::closesocket(fd);
    fd = INVALID_SOCKET;
  }
}

inline bool toSockaddr(const RopHive::Network::IpEndpoint &ep,
                       sockaddr_storage &out, socklen_t &out_len) {
  std::memset(&out, 0, sizeof(out));

  if (const auto *v4 = std::get_if<RopHive::Network::IpEndpointV4>(&ep)) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(v4->port);
    std::memcpy(&a.sin_addr, v4->ip.data(), v4->ip.size());
    std::memcpy(&out, &a, sizeof(a));
    out_len = sizeof(a);
    return true;
  }

  if (const auto *v6 = std::get_if<RopHive::Network::IpEndpointV6>(&ep)) {
    sockaddr_in6 a{};
    a.sin6_family = AF_INET6;
    a.sin6_port = htons(v6->port);
    std::memcpy(&a.sin6_addr, v6->ip.data(), v6->ip.size());
    a.sin6_scope_id = v6->scope_id;
    std::memcpy(&out, &a, sizeof(a));
    out_len = sizeof(a);
    return true;
  }

  return false;
}

inline std::optional<RopHive::Network::IpEndpoint>
fromSockaddr(const sockaddr *sa, int len) {
  if (!sa)
    return std::nullopt;

  if (sa->sa_family == AF_INET) {
    if (len < static_cast<int>(sizeof(sockaddr_in)))
      return std::nullopt;
    const auto *a = reinterpret_cast<const sockaddr_in *>(sa);
    RopHive::Network::IpEndpointV4 v4;
    std::memcpy(v4.ip.data(), &a->sin_addr, v4.ip.size());
    v4.port = ntohs(a->sin_port);
    return RopHive::Network::IpEndpoint{v4};
  }

  if (sa->sa_family == AF_INET6) {
    if (len < static_cast<int>(sizeof(sockaddr_in6)))
      return std::nullopt;
    const auto *a = reinterpret_cast<const sockaddr_in6 *>(sa);
    RopHive::Network::IpEndpointV6 v6;
    std::memcpy(v6.ip.data(), &a->sin6_addr, v6.ip.size());
    v6.port = ntohs(a->sin6_port);
    v6.scope_id = a->sin6_scope_id;
    return RopHive::Network::IpEndpoint{v6};
  }

  return std::nullopt;
}

inline void bestEffortFillEndpoints(SOCKET fd,
                                    RopHive::Network::ITcpStream &stream) {
  sockaddr_storage peer{};
  int peer_len = sizeof(peer);
  if (!stream.peer.has_value() &&
      ::getpeername(fd, reinterpret_cast<sockaddr *>(&peer), &peer_len) == 0) {
    stream.peer = fromSockaddr(reinterpret_cast<sockaddr *>(&peer), peer_len);
  }

  sockaddr_storage local{};
  int local_len = sizeof(local);
  if (!stream.local.has_value() &&
      ::getsockname(fd, reinterpret_cast<sockaddr *>(&local), &local_len) ==
          0) {
    stream.local =
        fromSockaddr(reinterpret_cast<sockaddr *>(&local), local_len);
  }
}

inline void applyTcpNonBlock(SOCKET fd) {
  u_long mode = 1;
  if(::ioctlsocket(fd, FIONBIO, &mode) != 0)
    LOG(WARN)("handle applyTcpNonBlock failed");
}

inline void applyCloseOnExecIfConfigured(SOCKET fd,
                                         const std::optional<bool> &v) {
  if (!v.has_value())
    return;
  HANDLE h = reinterpret_cast<HANDLE>(fd);
  auto ret = false;
  if (h == nullptr)
    return;
  if (*v) {
    ret = ::SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
  } else {
    ret = ::SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  }
  if(!ret) LOG(WARN)("fd applyCloseOnExecIfConfigured failed");
}

inline void applyTcpReuseAddrIfConfigured(SOCKET fd,
                                          const std::optional<bool> &v) {
  if (!v.has_value())
    return;
  const BOOL opt = *v ? TRUE : FALSE;
  if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char *>(&opt), sizeof(opt)) != 0)
    LOG(WARN)("fd applyTcpReuseAddrIfConfigured failed");
}

inline std::tuple<LPFN_ACCEPTEX, DWORD> fetchAcceptEx(SOCKET fd) {
  LPFN_ACCEPTEX result = nullptr;
  GUID guid_accept_ex = WSAID_ACCEPTEX;
  DWORD bytes = 0;
  if (0 != ::WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid_accept_ex,
                      sizeof(guid_accept_ex), &result, sizeof(result), &bytes,
                      nullptr, nullptr)) {
    return {nullptr, bytes};
  }
  return {result, bytes};
}

inline std::tuple<LPFN_CONNECTEX, DWORD> fetchConnectEx(SOCKET fd) {
  LPFN_CONNECTEX result = nullptr;
  GUID guid_connect_ex = WSAID_CONNECTEX;
  DWORD bytes = 0;
  if (0 != ::WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid_connect_ex,
                      sizeof(guid_connect_ex), &result, sizeof(result), &bytes,
                      nullptr, nullptr)) {
    return {nullptr, bytes};
  }
  return {result, bytes};
}

inline void applyTcpNoDelayIfConfigured(SOCKET fd,
                                        const std::optional<bool> &v) {
  if (!v.has_value())
    return;
  const char opt = *v ? 1 : 0;
  if(::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) != 0)
    LOG(WARN)("fd applyTcpNoDelayIfConfigured failed");
}

inline void applyKeepAliveIfConfigured(SOCKET fd,
                                       const std::optional<bool> &enable,
                                       const std::optional<int> &idle_sec,
                                       const std::optional<int> &interval_sec,
                                       const std::optional<int> &count) {
  if (!enable.has_value())
    return;
  const BOOL on = *enable ? TRUE : FALSE;
  auto ret = false;
  ret = ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
                     reinterpret_cast<const char *>(&on), sizeof(on));
  if(!ret) LOG(WARN)("fd applyKeepAliveIfConfigured failed");
  if (!*enable)
    return;

  if (idle_sec.has_value() || interval_sec.has_value()) {
    tcp_keepalive kav{};
    kav.onoff = 1;
    kav.keepalivetime =
        static_cast<ULONG>((idle_sec.has_value() ? *idle_sec : 7200) * 1000u);
    kav.keepaliveinterval = static_cast<ULONG>(
        (interval_sec.has_value() ? *interval_sec : 1) * 1000u);
    DWORD bytes = 0;
    ret = ::WSAIoctl(fd, SIO_KEEPALIVE_VALS, &kav, sizeof(kav), nullptr, 0,
                     &bytes, nullptr, nullptr);
  }
  if(!ret) LOG(WARN)("fd applyKeepAliveIfConfigured failed");
  (void)count;
}

inline void applyBufSizeIfConfigured(SOCKET fd, const std::optional<int> &rcv,
                                     const std::optional<int> &snd) {
  if (rcv.has_value()) {
    const int v = *rcv;
    auto ret = ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                       reinterpret_cast<const char *>(&v), sizeof(v));
    if(!ret) LOG(WARN)("fd applyBufSizeIfConfigured set recv_buf failed");
  }
  if (snd.has_value()) {
    const int v = *snd;
    auto ret = ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                       reinterpret_cast<const char *>(&v), sizeof(v));
    if(!ret) LOG(WARN)("fd applyBufSizeIfConfigured set send_buf failed");
  }
}

inline void applyLingerIfConfigured(SOCKET fd,
                                    const std::optional<int> &linger_sec) {
  if (!linger_sec.has_value())
    return;
  linger lin{};
  if (*linger_sec < 0) {
    lin.l_onoff = 0;
    lin.l_linger = 0;
  } else {
    lin.l_onoff = 1;
    lin.l_linger = static_cast<u_short>(*linger_sec);
  }
  auto ret = ::setsockopt(fd, SOL_SOCKET, SO_LINGER,
                     reinterpret_cast<const char *>(&lin), sizeof(lin));
  if(!ret) LOG(WARN)("fd applyLingerIfConfigured failed");
}

} // namespace RopHive::Windows::TcpDetail

namespace RopHive::Windows {

static constexpr size_t kAcceptBufferBytes =
    2 * (sizeof(sockaddr_storage) + 32);
using AcceptBuffer = std::array<char, kAcceptBufferBytes>;
using AcceptBufferPtr = std::shared_ptr<AcceptBuffer>;

class WindowsTcpStream final : public RopHive::Network::ITcpStream {
public:
  explicit WindowsTcpStream(SOCKET fd, AcceptBufferPtr accept_buffer = {})
      : ITcpStream(RopHive::Network::TcpStreamKind::WindowsIocp), fd_(fd),
        accept_buffer_(std::move(accept_buffer)) {}

  ~WindowsTcpStream() override { TcpDetail::closeFd(fd_); }

  SOCKET fd() const noexcept { return fd_; }

  SOCKET releaseFd() noexcept {
    const SOCKET fd = fd_;
    fd_ = INVALID_SOCKET;
    return fd;
  }

private:
  SOCKET fd_{INVALID_SOCKET};
  AcceptBufferPtr accept_buffer_;
};

} // namespace RopHive::Windows

#endif // _WIN32

#endif // _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_SOCKET_COMMON_H
