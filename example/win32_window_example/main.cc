#ifdef _WIN32

#include <log.hpp>
#include <windows.h>
#include <string>
#include <memory>
#include <vector>

#include <platform/schedule/eventloop.h>
#include <platform/windows/schedule/win32_backend.h>

using namespace RopHive;
using namespace RopHive::Windows;

// 全局窗口类名
static const wchar_t* WINDOW_CLASS_NAME = L"RopUI_Win32_Example";
static HWND g_hwnd = nullptr;

// 窗口过程
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CLOSE:
        ::DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        return ::DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

// 创建窗口
bool createWindow(const wchar_t* title, int width, int height) {
    HINSTANCE hInstance = ::GetModuleHandle(nullptr);

    // 注册窗口类
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!::RegisterClassExW(&wc)) {
        LOG(FATAL)("Failed to register window class");
        return false;
    }

    // 创建窗口
    g_hwnd = ::CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width, height,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!g_hwnd) {
        LOG(FATAL)("Failed to create window");
        return false;
    }

    ::ShowWindow(g_hwnd, SW_SHOW);
    ::UpdateWindow(g_hwnd);

    LOG(INFO)("Window created successfully");
    return true;
}

// 窗口事件监听Watcher
class WindowWatcher : public IWatcher {
public:
    explicit WindowWatcher(EventLoop& loop) : IWatcher(loop) {}
    ~WindowWatcher() override = default;

    void start() override {
        // 监听WM_PAINT消息
        paint_source_ = std::make_shared<Win32MessageEventSource>(
            WM_PAINT,
            [](const Win32RawEvent& ev) {
                LOG(INFO)("Received WM_PAINT message");
                PAINTSTRUCT ps;
                HDC hdc = ::BeginPaint(g_hwnd, &ps);
                
                // 绘制一些文本
                const wchar_t* text = L"Hello from RopUI EventLoop!";
                ::SetBkMode(hdc, TRANSPARENT);
                ::SetTextColor(hdc, RGB(0, 0, 128));
                
                RECT rc;
                ::GetClientRect(g_hwnd, &rc);
                ::DrawTextW(hdc, text, -1, &rc, 
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                
                ::EndPaint(g_hwnd, &ps);
            });

        // 监听WM_LBUTTONDOWN消息（鼠标左键点击）
        click_source_ = std::make_shared<Win32MessageEventSource>(
            WM_LBUTTONDOWN,
            [](const Win32RawEvent& ev) {
                int x = LOWORD(ev.lparam);
                int y = HIWORD(ev.lparam);
                LOG(INFO)("Mouse clicked at (%d, %d)", x, y);
                
                // 弹出消息框
                ::MessageBoxW(g_hwnd, 
                             L"You clicked the window!\nClick OK to continue.",
                             L"RopUI EventLoop", 
                             MB_OK | MB_ICONINFORMATION);
            });

        // 监听WM_KEYDOWN消息（键盘按键）
        key_source_ = std::make_shared<Win32MessageEventSource>(
            WM_KEYDOWN,
            [](const Win32RawEvent& ev) {
                LOG(INFO)("Key pressed: virtual key code = 0x%X", ev.wparam);
                
                // 如果按下ESC键，显示提示
                if (ev.wparam == VK_ESCAPE) {
                    ::MessageBoxW(g_hwnd, 
                                 L"ESC pressed!\nClose the window to exit.",
                                 L"RopUI EventLoop", 
                                 MB_OK | MB_ICONINFORMATION);
                }
            });

        // 监听WM_QUIT消息（退出程序）
        quit_source_ = std::make_shared<Win32MessageEventSource>(
            WM_QUIT,
            [this](const Win32RawEvent& ev) {
                LOG(INFO)("Received WM_QUIT, exit code: %d", ev.wparam);
                loop_.requestExit();
            });

        // 注册所有事件源
        attachSource(paint_source_);
        attachSource(click_source_);
        attachSource(key_source_);
        attachSource(quit_source_);
    }

    void stop() override {
        // 卸载所有事件源
        if (paint_source_) detachSource(paint_source_);
        if (click_source_) detachSource(click_source_);
        if (key_source_) detachSource(key_source_);
        if (quit_source_) detachSource(quit_source_);
    }

private:
    std::shared_ptr<Win32MessageEventSource> paint_source_;
    std::shared_ptr<Win32MessageEventSource> click_source_;
    std::shared_ptr<Win32MessageEventSource> key_source_;
    std::shared_ptr<Win32MessageEventSource> quit_source_;
};

int main() {
    logger::setMinLevel(LogLevel::INFO);
    LOG(INFO)("Starting Win32 Window Example with EventLoop");

    // 创建窗口
    if (!createWindow(L"RopUI - Win32 EventLoop Example", 800, 600)) {
        return 1;
    }

    // 创建EventLoop，使用Win32Backend
    EventLoop loop(BackendType::WINDOWS_WIN32);

    // 创建窗口事件监听Watcher
    auto window_watcher = std::make_unique<WindowWatcher>(loop);
    window_watcher->start();

    // 投递一个延迟任务
    loop.postDelayed([] {
        LOG(INFO)("This is a delayed task executed after 2 seconds");
        ::InvalidateRect(g_hwnd, nullptr, TRUE);  // 触发重绘
    }, std::chrono::milliseconds(2000));

    // 投递一些周期任务来演示任务系统
    int counter = 0;
    std::function<void()> periodic_task;
    periodic_task = [&loop, &counter, &periodic_task]() {
        counter++;
        LOG(INFO)("Periodic task #%d", counter);
        
        if (counter < 5) {
            // 继续投递下一次
            loop.postDelayed(periodic_task, std::chrono::milliseconds(3000));
        } else {
            LOG(INFO)("Periodic task completed");
        }
    };
    
    loop.postDelayed(periodic_task, std::chrono::milliseconds(3000));

    LOG(INFO)("Entering event loop...");
    LOG(INFO)("Try: Click the window, Press keys (ESC for message), Close window to exit");

    // 运行事件循环
    loop.run();

    LOG(INFO)("Event loop exited, cleaning up...");

    // 清理
    if (g_hwnd) {
        ::DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }

    ::UnregisterClassW(WINDOW_CLASS_NAME, ::GetModuleHandle(nullptr));

    LOG(INFO)("Win32 Window Example finished");
    return 0;
}

#else
#include <iostream>
int main() {
    std::cout << "This example only works on Windows" << std::endl;
    return 1;
}
#endif // _WIN32
