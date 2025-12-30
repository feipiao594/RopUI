# EventLoop

## 结构
EventLoop 这一层是真正跑的循环，本质是把 IO 多路复用、任务队列、定时器三件事串在一起：

- `EventLoop`：对外的驱动层，负责 `post`/`postDelayed`、超时计算、循环调度与退出。
- `EventLoopCore`：IO syscall 的代理层，只管收事件、分发事件源，不管任务队列。
- `EventCoreBackend`：平台相关实现（如 Linux 的 epoll/poll）。
- `EventSource`：事件源抽象（Linux 下通常关联一个 fd）。
- `WakeUpWatcher`：唤醒源，保证从其他线程投递任务时能把阻塞的 IO 拉起来。

## 核心接口

### EventLoop
负责主循环与任务调度：
EventLoop 持有一个 Backend，下面的函数实现都是围绕着 Backend 固定的几个方法来的

- `post(Task task)`：投递即时任务，线程安全。
- `postDelayed(Task task, Duration delay)`：投递延迟任务，线程安全。
- `requestExit()`：请求退出，安全唤醒阻塞的 loop。
- `attachSource(IEventSource* source)`：注册事件源。
- `detachSource(IEventSource* source)`：注销事件源。
- `run()`：进入主循环。

### IEventLoopCore
IO syscall 的统一代理接口：

- `runOnce(int timeout = -1)`：执行一次 IO 等待与分发。
- `addSource(IEventSource* source)`：添加事件源（延迟生效）。
- `removeSource(IEventSource* source)`：移除事件源（延迟生效）。
- `applyInitialChanges()`：应用 pending 变更（启动前/必要时调用）。

### IEventCoreBackend
平台相关后端（例如 epoll/poll/kqueue）：

- `addSource(IEventSource* source)`：将事件源注册到后端。
- `removeSource(IEventSource* source)`：从后端移除事件源。
- `wait(int timeout)`：等待事件。
- `rawEvents() const`：返回原始事件集合。

### IEventSource
事件源抽象，负责匹配与回调：

- `arm(IEventCoreBackend& backend)`：绑定到后端。
- `disarm(IEventCoreBackend& backend)`：从后端解绑。
- `matches(const void* raw_event) const`：判断事件是否属于自己。
- `dispatch(const void* raw_event)`：分发回调。

### WakeUpWatcher
可触发的事件源，向 loop 发送“醒来信号”：

- `notify()`：触发唤醒，保证阻塞的 `wait` 返回。

## 运行流程
`EventLoop::run()` 的主循环可以理解为：

1. `applyInitialChanges()`：注册初始事件源（包括唤醒源）。
2. `computeTimeoutMs()`：根据任务/定时器决定等待时间。
3. `core_->runOnce(timeout)`：执行 IO 等待并分发事件源。
4. `runExpiredTimers()`：执行到期定时任务。
5. `runReadyTasks()`：执行即时任务。
6. 循环直到 `requestExit()`。

## 任务与定时器

- 即时任务存放在 `tasks_`（`deque`）。
- 延迟任务存放在 `timers_`（小顶堆，按 deadline 排序）。
- `post()`/`postDelayed()` 只负责入队 + 唤醒，不在调用线程执行任务。

`computeTimeoutMs()` 的计算规则：

- 有即时任务：`timeout = 0`（不阻塞）。
- 无定时器：`timeout = -1`（无限等待）。
- 有定时器：取最近 deadline 与当前时间差（ms）。

## 线程与安全

- `post()`/`postDelayed()` 是线程安全的，内部用互斥锁保护队列。
- `runExpiredTimers()` 使用“先收集后执行”的策略，减少锁持有时间，避免回调里递归投递造成死锁。
- `requestExit()` 会唤醒 loop，保证阻塞状态也能退出。

## 简单示例

```cpp
using namespace RopEventloop;

int main() {
    EventLoop loop(BackendType::LINUX_EPOLL);

    // 注册事件源（假设 MySource 继承 IEventSource）
    // MySource source(...);
    // loop.attachSource(&source);

    loop.post([] {
        // 立即任务
    });

    loop.postDelayed([] {
        // 延迟任务
    }, std::chrono::milliseconds(16));

    loop.run();
    return 0;
}
```
