#include <log.hpp>
#include <platform/schedule/hive.h>
#include <platform/windows/schedule/watcher/win32_worker_timer.h>
#include <platform/schedule/io_worker.h>
#include <chrono>
#include <iostream>

#ifdef __linux__
#define DEFAULT_BACKEND BackendType::LINUX_EPOLL
#endif
#ifdef __APPLE__
#define DEFAULT_BACKEND BackendType::MACOS_KQUEUE
#endif
#ifdef _WIN32
#include <ws2tcpip.h>
#define DEFAULT_BACKEND BackendType::WINDOWS_WIN32
WSADATA wsaData;
#endif

void enableANSI();

// 七数码管结构
struct Segments {
    bool a, b, c, d, e, f, g;
};

Segments decodeBCD(int digit);


void printDigit(const Segments& s, int row);
void displayClock(uint64_t totalSeconds);
uint64_t todaySeconds() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm* local_tm = std::localtime(&now_c);
    uint64_t today_seconds = (local_tm->tm_hour * 3600) + 
                         (local_tm->tm_min * 60) + 
                         local_tm->tm_sec;
    return today_seconds;
}


int main(int argc, char* argv[]) {
    using namespace RopHive;

#if defined(_WIN32)
    int ret = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (ret != 0) {
        LOG(ERROR)("WSAStartup failed: %d\n", ret);
        return 1;
    }
#endif
    logger::setMinLevel(LogLevel::DEBUG);

    Hive hive;
    auto opt = hive.options();
    opt.io_backend = DEFAULT_BACKEND;

    auto worker = std::make_shared<IOWorker>(opt);
    hive.attachIOWorker(worker);

    enableANSI();


    hive.postToWorker(0, [worker] {
        auto* self = IOWorker::currentWorker();
        if (!self) return;
        using namespace std::chrono_literals;

        auto counter = std::make_shared<std::chrono::duration<uint64_t>>(0s);
        auto watcher = std::make_shared<Windows::Win32WorkerTimerWatcher>(*self, [counter, worker] {
            std::cout << "\033[2J\033[H\n\n"; // clear console
            std::cout << "Local time now: " << std::endl;
            displayClock(todaySeconds());
            std::cout << "Current counter is: " << std::endl;
            displayClock(counter->count());
            *counter += 1s;
        });
        self->adoptWatcher(watcher);
        watcher->setSpec(3s, 1s);
        watcher->start();
        LOG(INFO)("Timer will start within 3 seconds......");

    });

    hive.run();
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}


void enableANSI() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

Segments decodeBCD(int digit)
{
    bool A = digit & 0b1000;
    bool B = digit & 0b0100;
    bool C = digit & 0b0010;
    bool D = digit & 0b0001;

    Segments s{};

    // 数码管控制逻辑, 使用数电组合逻辑实现
    s.a = A || C || (B && D) || (!B && !D);
    s.b = !B || (!C && !D) || (C && D);
    s.c = B || !C || D;
    s.d = A || (C && !D) || (!B && C) || (!B && !D) || (B && !C && D);
    s.e = (!B && !D) || (C && !D);
    s.f = A || (!C && !D) || (B && !C) || (B && !D);
    s.g = A || (B && !C) || (!B && C) || (C && !D);

    return s;
}

void printDigit(const Segments& s, int row)
{
    if (row == 0)
        std::cout << " " << (s.a ? "_" : " ") << " ";
    else if (row == 1)
        std::cout << (s.f ? "|" : " ")
             << (s.g ? "_" : " ")
             << (s.b ? "|" : " ");
    else
        std::cout << (s.e ? "|" : " ")
             << (s.d ? "_" : " ")
             << (s.c ? "|" : " ");
}

void displayClock(uint64_t totalSeconds)
{
    uint64_t hours   = totalSeconds / 3600;
    uint64_t minutes = (totalSeconds % 3600) / 60;
    uint64_t seconds = totalSeconds % 60;

    int digits[6] = {
        int((hours / 10) % 10),
        int(hours % 10),
        int(minutes / 10),
        int(minutes % 10),
        int(seconds / 10),
        int(seconds % 10)
    };


    for (int row = 0; row < 3; row++)
    {
        for (int i = 0; i < 6; i++)
        {
            Segments seg = decodeBCD(digits[i]);
            printDigit(seg, row);

            if (i == 1 || i == 3)
                std::cout << (row == 1 ? " . " : "   ");
        }
        std::cout << std::endl;
    }
}