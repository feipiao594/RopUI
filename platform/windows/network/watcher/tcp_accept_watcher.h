#ifndef _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_ACCEPT_WATCHER_H
#define _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_ACCEPT_WATCHER_H

#ifdef _WIN32
#include <memory>
#include "../../../network/watcher/tcp_watchers.h"
#include "../../../schedule/io_worker.h"

namespace RopHive::Windows {
    
std::shared_ptr<RopHive::Network::ITcpAcceptWatcher>
createIocpTcpAcceptWatcher(RopHive::IOWorker& worker,
                            RopHive::Network::TcpAcceptOption option,
                            RopHive::Network::ITcpAcceptWatcher::OnAccept on_accept,
                            RopHive::Network::ITcpAcceptWatcher::OnError on_error);


} // namespace RopHive::Windows

#endif // _WIN32

#endif // _ROP_PLATFORM_WINDOWS_NETWORK_WATCHER_TCP_ACCEPT_WATCHER_H
