# RopHive 调度层理论设计

在此之前设计 EventLoop 时，并没有非常深入的考虑跨平台的性质，但在看过 Windows 和 MacOS 之后，原本的 EventLoop 设计露出缺陷

Windows 和 MacOS 的 UI 由于有内核的参与，他们暴露在外的接口大多是一些可以看作是同 Linux epoll/poll 等直接的 IO syscall 的封装，但是不能拆分的系统级 API，由于这个原因，所有对这个封装内可能存在的 IO syscall 的操作必须使用系统给予的 API，他们通常不能单独吧事件拆出来塞进一个真正的 IO syscall 里(比如 Windows 下的 IOCP，MacOS 下的 kqueue)。而他们本身给予的这个 API 有时也限制多多，比如 Windows，它的 `MsgWaitForMultipleObjectsEx` 这类接口限制了监听句柄的数量上限，仅仅只有 64 个，这意味着使用多线程去处理网络问题是必然的，然其实 UI 线程也能处理一些网络请求，单开一个线程专门处理网络有时候会造成一些浪费，因此我们需要一个多线程的调度器，要比 EventLoop 复杂的多

我把这个调度器称为 `RopHive` Rop 下的一个蜂巢，`RopHive` 旨在建立一个 **IO-Computing 混合驱动** 的多线程调度系统。它将系统级 UI 线程抽象为与其他 IO 相同的 syscall，通过分层队列管理，解决 UI 响应与重型计算的负载矛盾。

## 1. RopHive 蜂巢

RopHive 是调度器中枢，遵照下面的伪代码进行设计

```cpp
RopHive hive(); // 创建一个蜂巢

HiveWorker worker1(...); // 创建一个 Worker 蜜蜂
worker1.dosomething();

hive.attach(worker1);

hive.run();
```

worker 是对线程资源管理的一种封装形式，他们具有线程亲和性，RopHive 内部会在绑定 worker 时会根据放进去的 worker 数量进行动态调整，最终这些 worker 都会绑定到 RopHive 内部线程池(其实也可以叫 worker 池)上，这部分 worker 具体绑定在线程并运行的过程应该在 run 函数发生时进行

RopHive 支持动态增加 worker

RopHive 是一个真正的调度器，对外来看 RopHive 可以提交 RopTask(任务)，RopTask 有立即执行(形如现在 EventLoop 里的 post 方法)的，也有延迟执行(形如现在 EventLoop 里的 postDelay 方法)的，由于多线程模型，也允许递交到一个具体的 worker 中
RopTask 具有及其庞大的复杂性，在 EventLoop 里，他只是简单的一个没有返回值的函数，也就是一段要被执行的逻辑，但是在这里其相当的复杂

我们的 RopHive 中可以创建专门处理计算类任务的 worker，他们内部使用一些多线程同步手段模拟 syscall，正如普通的线程池的线程一样

因此 RopTask 要分为 IO 密集型任务和计算密集型任务，其中后者的 post 将被投递到特殊的队列当中，由这些计算类 worker 以共享模式获取，而关于 IO 密集型任务，将由其他的 IO 线程进行处理，主要的算法是 work stealing。前者可以称为 ComputeTask，后者为 MicroTask，获取的 Task 会根据这两个类型放到 Rophive 持有的 GlobalComputeTaskPool 和 GlobalMicroTaskPool 这两个池子里

关于 postDelay 要补充的一点是：计算任务不允许 Delay 提交，在 debug 下触发 assert，release 下忽略
在外面 Hive 的 postDelay 会递交给具体的一个下挂的 worker 的 delay 池内，具体选择策略如下，如果 worker 数量为 1，则直接递交给这唯一的 worker，如果 worker 数量大于等于 2 则使用下述算法

逻辑：
    随机选出两个 IO-worker(A 和 B)。
    比较它们的 timer_heap_.size()(堆的大小)。
    分发给堆更小(任务更少)的那一个。
这种方法需要 worker 暴露一个获取计数器堆数量的方法

在 hive run 时，至少一个 worker 需要被加入，第一个加入的 worker 会置于主线程

hive 可以解除除了主线程 worker 以外的任何一个 worker，但不负责其中 task 的回收，并且可以在运行过程中新建 worker

## 2 Worker (蜜蜂)

蜂巢内的最小调度单元，每个 Worker 绑定一个系统线程。Worker 之间具有全等性，他们是一个通用的执行容器，各自持有一个 EventLoopCore，可以用类似 EventLoop 的方法，将裸的 EventSource 绑定到 EventLoopCore 中，因此 Worker 持有的 EventLoopCore 才决定了其性质。

EventLoopCore 是具体的系统等待的核心，例如在 Windows 上，可以有 `RopHive::Windows::IOCPEventLoopCore`，和 `RopHive::Windows::Win32EventLoopCore`，这决定了它们可以注册什么样的事件，并用何种等待核心进行 wait(timeout)

Worker 中有特殊的计算类 Worker，他们可能只具有一个用条件变量模拟的 EventLoopCore

Worker 有一个独属于自己要处理的 private_queue，有一个双端 local_dqueue 负责实现那些可以不由自己处理的任务，有一组 timer_heap 处理自己这边的定时任务，由于 private_queue 可以由外部的 RopHive 使用 postDelayed 提交，需要有一个 inbound_buffer 能够提交独属于自己的任务(在处理的时候，会把这个缓冲区与 private_queue 合并)，需要处理好这之间的线程安全问题

Worker 有一组 post 方法，首先，这里所调用的 post 方法是处理当前 worker 产生的 RopTask，通常来说它们会由自己这边注册的 EventSource 的原始回调进行 post，如果任务是必须当前线程进行处理的，则使用 postSelf 投递到 private_queue，如果这个任务不是很和当前线程绑定或者这是一个计算型任务，可以调用 RopHive 的 post 方法递交给全局池，如果这个任务不是和当前线程绑定，但是它可能会调用线程内的资源，由当前线程处理可以相对来说提高速度，可以使用 postToLocal 放进双端队列，让其他 worker 能有 work stealing 的机会

在这些 Queue 里只有 DQueue 有一个上限值，应该在 worker 被初始化的时候根据其实际的含义进行赋值，当 DQueue 满时 postToLocal 会实际上变成 post 给 hive 的全局池，以此保证在负载庞大的时候，global 会被处理，work stealing 被旁路

关于定时 Task，worker 原则上推荐选择给自己，使用 postSelfDelayed ，但也可以选择递交给 RopHive，让它随机选个 worker 运行

worker 内运行的 task 应该有办法获取自己在哪个 id 的 worker 下运行，这个 id 应该被存储在 hive 里，由它进行分配，这里直接采用递增的下标标记就好了

worker 需要持有一个 wakeup 的 eventsource，注册这种东西的方式要像 EventLoop 的 Watcher 一样，这里称为 WorkerWatcher，任何一个 EventSource 都是平台相关，注册进对应 worker 里的 EventLoopCore 都需要像 EventLoop 一样，通过一个 enum 来判定注册的 Source 和平台是否相同，如果否则丢弃并 warning，debug 下可以直接断言

worker 需要一个唤醒机制，通过持有 WakeupWorkerWatcher 实现。WakeupWorkerWatcher 是 WorkerWatcher 的一种具体实现，worker 直接拥有这个 watcher，同时其也可以引出一个 wakeup 方法，这个 wakeup 方法是唯一可以唤醒 worker 的方法，且线程安全

## 3. 蜂巢的节拍：Worker 运行逻辑

每个 Worker 的 `run()` 循环必须遵循严格的“能量收割”顺序，以确保系统 API 的消息不被阻塞：

1. **系统消息优先**：调用 `IEventLoopCore::runOnce(timeout)`。在 Windows 下，这对应 `PeekMessage`；在 Linux 下，这处理 FD 读写。接着需要调用 IEventLoopCore 处理原始回调的方法，这些回调里会具体的调用 worker 和 hive 的 post 语义分发 RopTask
2. **定向收割 (Private)**：拿取并清空 `InboundBuffer`，将其合并到自己的 private_queue 里，以此处理来自其他线程的 `postPrivate` 指令(优先级最高，通常涉及状态机变更)和自己的产生的一些和自己强绑定的一些任务，
3. **定时器触发 (Timer)**：处理到期的延迟任务。这和 EventLoop 的实现是一样的
4. **本地消化 (Local DQueue Processing)** Worker 此时锁定的目标是自己的 local_dqueue。由于遵循 LIFO (后进先出) 逻辑，Worker 会尝试从队列的 Bottom 端进行 pop 操作。如果此时队列中有任务，Worker 会取出至多 N(一个到时候会确定的数，是一个 DQueue 上限的分段函数，需要实际测试，这是一个系统的超参数) 个并执行。执行完毕后，它不会立刻向下走，而是会重新回到第 1 步(系统消息优先)，通过频繁检查系统 IO 和私有队列来维持高响应性。当 local_dqueue 彻底取空，即 bottom 追上 top 索引时，Worker 才会判定本地任务耗尽，进入第 5 步，另外在每 global_probe_interval(暂定为 47) 次没有进入到第 5 步的循环，则尝试进行第 5 步
5. **全局支援 (Global Queue Harvesting)** Worker 判定自己已经完全处理完了“自留地”的任务，于是它向 RopHive 持有的 GlobalMicroTaskPool 发起请求。为了平衡吞吐量和锁开销，Worker 不会只拿一个任务，而是尝试一次性 Batch Pop(例如 16 个任务)。它会将这些任务从全局池中取出，并一次性填入自己的 local_dqueue 中。如果全局池有活，它领完活后会再次回到第 1 步开始循环(实际是跳过 6 并在 7 设置 timeout=0 / 让 wakeup 工作)。如果全局池也为空，则判定全站公共任务耗尽，进入第 6 步。
6. **窃取 (Work Stealing)** 这是 Worker 最后的挣扎。它会利用 RopHive 维护的 Worker 列表，随机挑选一个邻居 Worker 作为“受害者”。它会窥探邻居的 local_dqueue，并尝试随机发起 CAS (Compare-And-Swap) 窃取。如果窃取成功，它会把拿到的任务(可能是一个或多个，取决于策略)转移到自己的队列中。如果随机挑了几个邻居都偷不到活(判定为全网空闲)，它才会无奈地进入第 7 步。
7. **深度睡眠 (Deep Sleep & Wait)** 实际上第 7 步和第 1 步是同一个事情，都是新进的循环。在这里单独列出因为这是 4 5 6 7 的最后的兜底状态。Worker 会首先查询自己的 timer_heap，计算出距离最近一个定时器到期还有多少毫秒(记为 timeout)。如果堆里没活，timeout 就设为 -1。然后，它会回到第一步调用 IEventLoopCore::runOnce(timeout) 真正进入内核态阻塞。此时，线程交出 CPU 控制权，直到以下三种情况之一发生：内核监测到注册的 FD 有事件、timeout 时间到期、或者其他线程通过 Waker(如 eventfd 或 PostThreadMessage)将其物理捅醒。一旦醒来，Worker 会立刻从第 1 步重新开始这一轮收割。

## 4.其他
这边提到有如下一些 post，注意下述不包括计算 worker
|调用场景|任务类型|推荐方法|最终去向|窃取性|唤醒策略|
|:-:|:-:|:-:|:-:|:-:|:-:|
|外部提交随机任务|不限|hive.post|全局池 (Global Pool)|可被领用|只唤醒一个空闲 Worker|
|外部提交定时任务|Micro|hive.postDelayed|某 Worker 的 timer_heap|禁用|立即 wakeup 目标 Worker|
|跨线程状态更新|不限|hive.postToWorker|目标 private_queue|禁用|立即 wakeup 目标 Worker|
|Worker 产生的子任务|Micro|worker.postToLocal|local_dqueue (或溢出到 Global)|可被窃取|仅当 worker 处于 sleep 状态时才需要唤醒|
|Worker 内部回调|不限|worker.postSelf|private_queue|禁用|仅当 worker 处于 sleep 状态时才需要唤醒|

其中 hive.post 这个操作在 worker 内调用时，需要 worker 内添加一个缓冲区，因为每次循环都会经过第 7 步，在第 7 步骤前(也就是循环的最后)，将 post 操作放进的缓冲区的全局任务全部通过 postBatch 方法递交给全局池，并清空缓冲区

关于这里只唤醒一个"空闲 Worker"的策略：令 hive 维护一个 sleeping_io_workers 位图/列表；若非空，任选其一唤醒；若空则不唤醒(说明已有 worker 在跑)。每个 worker 写自己的 sleep 标志，hive 读全部；标志用 atomic<bool> 保护

其余方法由于比较灵活，可以不设置 postBatch 的方法，但填充 worker 的缓冲区所用到的代理 post 方法和在6 7之间的第6.5步，批上传操作需要完成，不必须，但能极大增加高负载情况下的处理速度，也可以设置一个开关控制当前 worker 是否需要这个优化

关于具体多线程竞态等设计在设计具体类时决定