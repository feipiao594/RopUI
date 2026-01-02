#pragma once

#include <cstdlib>
#include <iostream>
#include <utility>
#include <string_view>

namespace RopUI::Tests {

inline void fail(std::string_view expr, std::string_view file, int line) {
    std::cerr << "TEST FAIL: " << expr << " at " << file << ":" << line << "\n";
    std::cerr.flush();
    std::abort();
}

template <class Fn>
inline void run(std::string_view name, Fn&& fn) {
    std::cerr << "[RUN] " << name << "\n";
    std::cerr.flush();
    std::forward<Fn>(fn)();
    std::cerr << "[OK ] " << name << "\n";
    std::cerr.flush();
}

} // namespace RopUI::Tests

#define ROPUI_TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            ::RopUI::Tests::fail(#expr, __FILE__, __LINE__); \
        } \
    } while (0)
