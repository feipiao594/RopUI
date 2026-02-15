#include <stdexcept>

#include "../../schedule/io_worker.h"
#include "tcp_watchers.h"

#ifdef __linux__
#include "../../linux/network/watcher/tcp_accept_watcher.h"
#include "../../linux/network/watcher/tcp_connect_watcher.h"
#include "../../linux/network/watcher/tcp_connection_watcher.h"
#elif defined(_WIN32) or defined(_WIN64)
#include "../../windows/network/watcher/tcp_accept_watcher.h"
#include "../../windows/network/watcher/tcp_connect_watcher.h"
#include "../../windows/network/watcher/tcp_connection_watcher.h"
#endif

#ifdef __APPLE__
#include "../../macos/network/watcher/tcp_accept_watcher.h"
#include "../../macos/network/watcher/tcp_connect_watcher.h"
#include "../../macos/network/watcher/tcp_connection_watcher.h"
#endif

namespace RopHive::Network {

std::shared_ptr<ITcpAcceptWatcher>
createTcpAcceptWatcher(IOWorker &worker, TcpAcceptOption option,
                       ITcpAcceptWatcher::OnAccept on_accept,
                       ITcpAcceptWatcher::OnError on_error) {
#ifdef __linux__
  switch (worker.backendType()) {
  case BackendType::LINUX_EPOLL:
    return RopHive::Linux::createEpollTcpAcceptWatcher(
        worker, std::move(option), std::move(on_accept), std::move(on_error));
  case BackendType::LINUX_POLL:
    return RopHive::Linux::createPollTcpAcceptWatcher(
        worker, std::move(option), std::move(on_accept), std::move(on_error));
  default:
    break;
  }
#elif defined(_WIN32) or defined(_WIN64)
  switch (worker.backendType()) {
  case BackendType::WINDOWS_IOCP:
    return RopHive::Windows::createIocpTcpAcceptWatcher(
        worker, std::move(option), std::move(on_accept), std::move(on_error));
  default:
    break;
  }
#endif
#ifdef __APPLE__
  switch (worker.backendType()) {
  case BackendType::MACOS_KQUEUE:
    return RopHive::MacOS::createKqueueTcpAcceptWatcher(
        worker, std::move(option), std::move(on_accept), std::move(on_error));
  case BackendType::MACOS_POLL:
    return RopHive::MacOS::createPollTcpAcceptWatcher(
        worker, std::move(option), std::move(on_accept), std::move(on_error));
  default:
    break;
  }
#endif

  throw std::runtime_error(
      "createTcpAcceptWatcher: unsupported platform/backend");
}

std::shared_ptr<ITcpConnectWatcher>
createTcpConnectWatcher(IOWorker &worker, IpEndpoint remote,
                        TcpConnectOption option,
                        ITcpConnectWatcher::OnConnected on_connected,
                        ITcpConnectWatcher::OnError on_error) {
#ifdef __linux__
  switch (worker.backendType()) {
  case BackendType::LINUX_EPOLL:
    return RopHive::Linux::createEpollTcpConnectWatcher(
        worker, std::move(remote), std::move(option), std::move(on_connected),
        std::move(on_error));
  case BackendType::LINUX_POLL:
    return RopHive::Linux::createPollTcpConnectWatcher(
        worker, std::move(remote), std::move(option), std::move(on_connected),
        std::move(on_error));
  default:
    break;
  }
#elif defined(_WIN32) or defined(_WIN64)
  switch (worker.backendType()) {
  case BackendType::WINDOWS_IOCP:
    return RopHive::Windows::createIocpTcpConnectWatcher(
        worker, std::move(remote), std::move(option), std::move(on_connected),
        std::move(on_error));
  default:
    break;
  }
#endif
#ifdef __APPLE__
  switch (worker.backendType()) {
  case BackendType::MACOS_KQUEUE:
    return RopHive::MacOS::createKqueueTcpConnectWatcher(
        worker, std::move(remote), std::move(option), std::move(on_connected),
        std::move(on_error));
  case BackendType::MACOS_POLL:
    return RopHive::MacOS::createPollTcpConnectWatcher(
        worker, std::move(remote), std::move(option), std::move(on_connected),
        std::move(on_error));
  default:
    break;
  }
#endif
  throw std::runtime_error(
      "createTcpConnectWatcher: unsupported platform/backend");
}

std::shared_ptr<ITcpConnectionWatcher>
createTcpConnectionWatcher(IOWorker &worker, TcpConnectionOption option,
                           std::unique_ptr<ITcpStream> connected_stream,
                           ITcpConnectionWatcher::OnRecv on_recv,
                           ITcpConnectionWatcher::OnClose on_close,
                           ITcpConnectionWatcher::OnError on_error,
                           ITcpConnectionWatcher::OnSendReady on_send_ready) {
  if (!connected_stream) {
    throw std::runtime_error(
        "createTcpConnectionWatcher: connected_stream is null");
  }

#ifdef __linux__
  switch (worker.backendType()) {
  case BackendType::LINUX_EPOLL:
    return RopHive::Linux::createEpollTcpConnectionWatcher(
        worker, std::move(option), std::move(connected_stream),
        std::move(on_recv), std::move(on_close), std::move(on_error),
        std::move(on_send_ready));
  case BackendType::LINUX_POLL:
    return RopHive::Linux::createPollTcpConnectionWatcher(
        worker, std::move(option), std::move(connected_stream),
        std::move(on_recv), std::move(on_close), std::move(on_error),
        std::move(on_send_ready));
  default:
    break;
  }
#endif
#ifdef __APPLE__
  switch (worker.backendType()) {
  case BackendType::MACOS_KQUEUE:
    return RopHive::MacOS::createKqueueTcpConnectionWatcher(
        worker, std::move(option), std::move(connected_stream),
        std::move(on_recv), std::move(on_close), std::move(on_error),
        std::move(on_send_ready));
  case BackendType::MACOS_POLL:
    return RopHive::MacOS::createPollTcpConnectionWatcher(
        worker, std::move(option), std::move(connected_stream),
        std::move(on_recv), std::move(on_close), std::move(on_error),
        std::move(on_send_ready));
  default:
    break;
  }
#elif defined(_WIN32) or defined(_WIN64)
  switch (worker.backendType()) {
    case BackendType::WINDOWS_IOCP:
      return RopHive::Windows::createIocpTcpConnectionWatcher(
          worker, std::move(option), std::move(connected_stream),
          std::move(on_recv), std::move(on_close), std::move(on_error),
          std::move(on_send_ready));
    default:
      break;
  }
#endif
  throw std::runtime_error(
      "createTcpConnectionWatcher: unsupported platform/backend");
}

} // namespace RopHive::Network
