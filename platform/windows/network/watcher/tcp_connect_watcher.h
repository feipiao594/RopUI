
#ifndef _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_CONNECT_WATCHER_H
#define _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_CONNECT_WATCHER_H

#ifdef _WIN32
#include <memory>
#include "../../../network/watcher/tcp_watchers.h"
#include "../../../schedule/io_worker.h"

namespace RopHive::Windows {

std::shared_ptr<RopHive::Network::ITcpConnectWatcher>
createIocpTcpConnectWatcher(RopHive::IOWorker& worker,
                            RopHive::Network::IpEndpoint remote,
                            RopHive::Network::TcpConnectOption option,
                            RopHive::Network::ITcpConnectWatcher::OnConnected on_connected,
                            RopHive::Network::ITcpConnectWatcher::OnError on_error);

} // namespace RopHive::Windows

#endif // _WIN32

#endif // _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_CONNECT_WATCHER_H
