#ifndef _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_CONNECTION_WATCHER_H
#define _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_CONNECTION_WATCHER_H

#ifdef _WIN32
#include <memory>
#include "../../../network/watcher/tcp_watchers.h"
#include "../../../schedule/io_worker.h"

namespace RopHive::Windows {

std::shared_ptr<RopHive::Network::ITcpConnectionWatcher>
createIocpTcpConnectionWatcher(
    RopHive::IOWorker& worker,
    RopHive::Network::TcpConnectionOption option,
    std::unique_ptr<RopHive::Network::ITcpStream> connected_stream,
    RopHive::Network::ITcpConnectionWatcher::OnRecv on_recv,
    RopHive::Network::ITcpConnectionWatcher::OnClose on_close,
    RopHive::Network::ITcpConnectionWatcher::OnError on_error,
    RopHive::Network::ITcpConnectionWatcher::OnSendReady on_send_ready);

} // namespace RopHive::Windows

#endif // _WIN32

#endif // _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_CONNECTION_WATCHER_H
