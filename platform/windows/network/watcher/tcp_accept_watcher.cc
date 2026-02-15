#include "tcp_accept_watcher.h"


#if defined(_WIN32) or defined(_WIN64)
#include  "../../schedule/iocp_backend.h"
#include  "../../schedule/win32_backend.h"
#include <cerrno>
#include <utility>
#include <winsock2.h>
#include <ws2tcpip.h>
#include  "./tcp_socket_common.h"

namespace RopHive::Windows {
  namespace {
#pragma region Utilites
    SOCKET createListenSocket(const Network::TcpAcceptOption &option)  {
      sockaddr_storage addr_storage{};
      socklen_t addr_len = 0;

      if (!TcpDetail::toSockaddr(option.local, addr_storage, addr_len)) {
        WSASetLastError(WSAEINVAL);
        return INVALID_SOCKET;
      }

      const int family =
          reinterpret_cast<sockaddr *>(&addr_storage)->sa_family;

      SOCKET fd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
      if (fd == INVALID_SOCKET) {
        return fd;
      }

      if (!TcpDetail::applyTcpNonBlock(fd)) {
        TcpDetail::closeFd(fd);
        return INVALID_SOCKET;
      }

      TcpDetail::applyTcpReuseAddr(fd, option);

      // ====== IPv6 only ======
      if (family == AF_INET6 &&
          option.ipv6_only.has_value()) {

        BOOL opt = *option.ipv6_only ? TRUE : FALSE;
        setsockopt(fd,
                   IPPROTO_IPV6,
                   IPV6_V6ONLY,
                   reinterpret_cast<const char *>(&opt),
                   sizeof(opt));
        }


      // ====== bind ======
      if (::bind(fd,
                 reinterpret_cast<sockaddr *>(&addr_storage),
                 addr_len) != 0) {
        TcpDetail::closeFd(fd);
        return INVALID_SOCKET;
      }

      // ====== listen ======
      if (::listen(fd, option.backlog) != 0) {
        TcpDetail::closeFd(fd);
        return INVALID_SOCKET;
      }

      return fd;
    }

    // 新增：为 AcceptEx 等创建一个 accept socket（与 listen socket 同家族），
    // 设置非阻塞并 best-effort 应用 option 中的 socket 配置。
    SOCKET createAcceptSocket(SOCKET listen_fd, const Network::TcpAcceptOption &option) {
      if (listen_fd == INVALID_SOCKET) {
        WSASetLastError(WSAEINVAL);
        return INVALID_SOCKET;
      }

      sockaddr_storage addr{};
      int addr_len = sizeof(addr);
      if (::getsockname(listen_fd, reinterpret_cast<sockaddr *>(&addr), &addr_len) != 0) {
        return INVALID_SOCKET;
      }

      const int family = reinterpret_cast<sockaddr *>(&addr)->sa_family;
      SOCKET s = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
      if (s == INVALID_SOCKET) {
        return INVALID_SOCKET;
      }

      // 必须设置为非阻塞以配合 IOCP / AcceptEx 使用
      if (!TcpDetail::applyTcpNonBlock(s)) {
        TcpDetail::closeFd(s);
        return INVALID_SOCKET;
      }

      // best-effort 应用可选配置（不应因失败而中止 accept）
      TcpDetail::applyCloseOnExecIfConfigured(s, option.set_close_on_exec);
      TcpDetail::applyTcpNoDelayIfConfigured(s, option.tcp_no_delay);
      TcpDetail::applyKeepAliveIfConfigured(s, option.keep_alive,
                                           option.keep_alive_idle_sec,
                                           option.keep_alive_interval_sec,
                                           option.keep_alive_count);
      TcpDetail::applyBufSizeIfConfigured(s, option.recv_buf_bytes, option.send_buf_bytes);
      // linger 通常对 accept socket 没必要，但按需也可应用（此处略）

      return s;
    }

#pragma endregion

#pragma region Interface Implementations
    class IocpTcpAcceptWatcher final : public RopHive::Network::ITcpAcceptWatcher {
    public:
      explicit IocpTcpAcceptWatcher(RopHive::IOWorker& worker, SOCKET listen_fd, Network::TcpAcceptOption option,
        OnAccept on_accept, OnError on_error)
      : ITcpAcceptWatcher(worker), listen_fd_(listen_fd), option_(option), on_accept_(std::move(on_accept)), on_error_(std::move(on_error)){

        // get AcceptEx function address and magical parameters
        const auto [accept_ex_fn, bytes_of_fn] = TcpDetail::fetchAcceptEx(listen_fd_);

        if (!accept_ex_fn) {
          on_error_(WSAGetLastError());
        }

        this->accept_ex_ = accept_ex_fn;
        this->bytes = bytes_of_fn;

        source_ = std::make_shared<RopHive::Windows::IocpHandleCompletionEventSource>(
          reinterpret_cast<HANDLE>(listen_fd_),
          listen_fd_,
          [this](const IocpRawEvent& events) {
            this->onCompletion(events);
          }
        );

      }

      ~IocpTcpAcceptWatcher() override {
        stop();
        source_.reset();
        TcpDetail::closeFd(listen_fd_);
      }

      void start() override {
        if (attached_)
          return;
        attachSource(source_);
        attached_ = true;
        accepted = 0;
        while (accepted < option_.max_accept_per_tick) {
          accept_socket_ = createAcceptSocket(listen_fd_, option_);
          if (accept_socket_ == INVALID_SOCKET) {
            on_error_(WSAGetLastError());
            stop();
            return;
          }

          overlapped_ = {}; // keep it zero before accept!
          std::fill_n(accept_buffer_, sizeof(accept_buffer_), 0);

          this->accept_ex_(
            listen_fd_,
            accept_socket_,
            accept_buffer_,
            0,
            sizeof(sockaddr_storage) + 16,
            sizeof(sockaddr_storage) + 16,
            &this->bytes,
            &overlapped_
          );
        }
      }

      void stop() override {
        if (!attached_)
          return;
        detachSource(source_);
        attached_ = false;
      }
    private:

      void onCompletion(const IocpRawEvent& event) {
        if (event.error) {
          on_error_((int)event.error);
        } else if (event.key == accept_socket_) {
          if (event.key == INVALID_SOCKET || event.bytes == 0) {
            on_error_(WSAECONNABORTED);
            TcpDetail::closeFd(accept_socket_);
            return;
          }
          accepted++;
          auto stream = std::make_unique<WindowsTcpStream>(accept_socket_, accept_buffer_);

        }
      }

      size_t accepted = 0;


      SOCKET listen_fd_;
      bool attached_ = false;
      Network::TcpAcceptOption option_;
      OnAccept on_accept_;
      OnError on_error_;

      std::once_flag fetch_accept;
      LPFN_ACCEPTEX accept_ex_;
      DWORD bytes;
      SOCKET accept_socket_ = INVALID_SOCKET;
      OVERLAPPED overlapped_{};
      char accept_buffer_[2 * (sizeof(sockaddr_storage) + 32)] = {};

      std::shared_ptr<RopHive::Windows::IocpEventSource> source_;
    };
#pragma endregion
  }

#pragma region Backend Implementations
  std::shared_ptr<RopHive::Network::ITcpAcceptWatcher>
    createIocpTcpAcceptWatcher(RopHive::IOWorker& worker,
                                RopHive::Network::TcpAcceptOption option,
                                RopHive::Network::ITcpAcceptWatcher::OnAccept on_accept,
                                RopHive::Network::ITcpAcceptWatcher::OnError on_error) {
    SOCKET fd = createListenSocket(option);
    if (fd == INVALID_SOCKET) {
      return nullptr;
    }
    return std::make_shared<IocpTcpAcceptWatcher>(
      worker,
      fd,
      option,
      on_accept,
      on_error
    );
  }

#pragma endregion

}


#endif // defined(_WIN32) or defined(_WIN64)
