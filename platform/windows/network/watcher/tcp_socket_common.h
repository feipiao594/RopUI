#ifndef _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_SOCKET_COMMON_H
#define _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_SOCKET_COMMON_H


#if defined(_WIN32) or defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <optional>
#include <winsock2.h>
#include <cstring>
#include <mswsock.h>

#include "../../../network/watcher/tcp_watchers.h"
#include "../../../network/ip_endpoint.h"

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

  inline bool applyTcpNonBlock(SOCKET &fd) {
    u_long mode = 1;  // 1 = non-blocking
    if (ioctlsocket(fd, FIONBIO, &mode) != 0) {
      return false;
    }
    return true;
  }

  inline void applyTcpReuseAddr(SOCKET &fd, const Network::TcpAcceptOption &option) {
    if (option.reuse_addr.has_value() && *option.reuse_addr) {
      constexpr BOOL opt = TRUE;
      setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char *>(&opt),
                 sizeof(opt));
    }
  }

  inline std::tuple<LPFN_ACCEPTEX, DWORD> fetchAcceptEx(SOCKET &fd) {
    LPFN_ACCEPTEX result;
    GUID guid_accept_ex = WSAID_ACCEPTEX;
    DWORD bytes;
    if (0 != WSAIoctl(
      fd,
      SIO_GET_EXTENSION_FUNCTION_POINTER,
      &guid_accept_ex,
      sizeof(guid_accept_ex),
      &result,
      sizeof(result),
      &bytes,
      nullptr,
      nullptr
      )) {
      return {nullptr, bytes};
    }
    return { result, bytes };
  }

  inline void applyCloseOnExecIfConfigured(SOCKET &fd, const std::optional<bool> &v) {
    if (!v.has_value())
      return;
    // Windows: "close-on-exec" ~ non-inheritable handle
    HANDLE h = reinterpret_cast<HANDLE>(fd);
    if (h == nullptr) return;
    if (*v) {
      // clear inherit bit -> non-inheritable
      ::SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
    } else {
      // set inherit bit -> inheritable
      ::SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }
  }

  inline void applyTcpNoDelayIfConfigured(SOCKET &fd, const std::optional<bool> &v) {
    if (!v.has_value())
      return;
    const char opt = *v ? 1 : 0;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
  }

  inline void applyKeepAliveIfConfigured(
      SOCKET &fd,
      const std::optional<bool> &enable,
      const std::optional<int> &idle_sec,
      const std::optional<int> &interval_sec,
      const std::optional<int> &count) {
    if (!enable.has_value())
      return;
    const BOOL on = *enable ? TRUE : FALSE;
    ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
                 reinterpret_cast<const char *>(&on), sizeof(on));
    if (!*enable)
      return;

    // Try to apply keepalive timing using SIO_KEEPALIVE_VALS (best-effort).
    // Structure and IOCTL are Windows-specific.
    // tcp_keepalive: defined in <mstcpip.h> (mswsock / mswsock.h included above).
    // keepalivetime and keepaliveinterval are specified in milliseconds.
    if (idle_sec.has_value() || interval_sec.has_value()) {
      // best-effort: fill defaults if missing
      tcp_keepalive kav{};
      kav.onoff = 1;
      kav.keepalivetime = static_cast<unsigned long>((idle_sec.has_value() ? *idle_sec : 7200) * 1000u);
      kav.keepaliveinterval = static_cast<unsigned long>((interval_sec.has_value() ? *interval_sec : 1) * 1000u);
      DWORD bytes = 0;
      ::WSAIoctl(fd, SIO_KEEPALIVE_VALS, &kav, sizeof(kav), nullptr, 0, &bytes, nullptr, nullptr);
    }

    (void)count; // Windows does not expose keepalive count in common API; ignore best-effort.
  }

  inline void applyBufSizeIfConfigured(SOCKET &fd, const std::optional<int> &rcv, const std::optional<int> &snd) {
    if (rcv.has_value()) {
      const int v = *rcv;
      ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&v), sizeof(v));
    }
    if (snd.has_value()) {
      const int v = *snd;
      ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&v), sizeof(v));
    }
  }

  inline void applyLingerIfConfigured(SOCKET &fd, const std::optional<int> &linger_sec) {
    if (!linger_sec.has_value())
      return;
    linger lin{};
    if (*linger_sec < 0) {
      lin.l_onoff = 0;
      lin.l_linger = 0;
    } else {
      lin.l_onoff = 1;
      lin.l_linger = static_cast<unsigned short>(*linger_sec);
    }
    ::setsockopt(fd, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char *>(&lin), sizeof(lin));
  }

};

namespace RopHive::Windows {
  class WindowsTcpStream final : public Network::ITcpStream {
  public:
    WindowsTcpStream(SOCKET fd, const char (&accept_buffer)[2 * (sizeof(sockaddr_storage) + 32)])
        : ITcpStream(Network::TcpStreamKind::WindowsIocp), fd_(fd)  {
          std::copy_n(accept_buffer, sizeof(accept_buffer), accept_buffer_);
        }

    ~WindowsTcpStream() override {
      if (fd_ != INVALID_SOCKET) {
        TcpDetail::closeFd(fd_);
        fd_ = INVALID_SOCKET;
      }
    }

    [[nodiscard]] SOCKET fd() const noexcept { return fd_; }

    [[nodiscard]] SOCKET releaseFd() noexcept {
      const SOCKET fd = fd_;
      fd_ = INVALID_SOCKET;
      return fd;
    }

  private:
    char accept_buffer_[2 * (sizeof(sockaddr_storage) + 32)];
    SOCKET fd_{INVALID_SOCKET};
  };
};

#endif




#endif // _ROP_PLATFORM_LINUX_NETWORK_WATCHER_TCP_SOCKET_COMMON_H
