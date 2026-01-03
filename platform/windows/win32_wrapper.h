#ifndef _ROPUI_WIN32_WRAPPER_H
#define _ROPUI_WIN32_WRAPPER_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#undef min
#undef max
#undef ERROR
#undef DELETE
#undef GetMessage

#endif // _ROPUI_WIN32_WRAPPER_H