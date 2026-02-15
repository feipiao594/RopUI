#ifndef _ROPUI_WIN32_WRAPPER_H
#define _ROPUI_WIN32_WRAPPER_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <log.hpp>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#undef min
#undef max
#undef ERROR
#undef DELETE
#undef GetMessage

namespace RopHive::Windows {
    extern WSADATA wsaData;
    inline void global_init() {
        int ret = WSAStartup(MAKEWORD(2,2), &wsaData);
        if (ret != 0) {
            LOG(ERROR)("WSAStartup failed: %d\n", ret);
            return;
        }
    }

    inline void global_cleanup() {
        WSACleanup();
    }
}

#endif // _ROPUI_WIN32_WRAPPER_H