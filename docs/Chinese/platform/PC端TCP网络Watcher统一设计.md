# PC 端 TCP 网络 Watcher 统一设计

本文档面向 **Windows / macOS / Linux（PC 三端）**，设计一套统一的 TCP Server/Client Watcher 接口与实现约束，使其可以无缝挂载到现有的 `RopHive::IOWorker`（`IWorkerWatcher` + `IEventSource`）体系中，并能在不同平台后端（epoll/poll/kqueue/IOCP）下得到等价行为。

> 约束：Watcher 只负责“套接字生命周期 + 事件驱动收发/建连/accept”，不内置域名解析、TLS、重连等策略；这些属于更上层组件。

---

## 1. 背景与目标

### 1.1 背景
- 仓库目前有一套 Linux TCP worker watchers：`platform/linux/network/watcher/`（accept/connect/connection）。**它们仅供参考**，后续会按本文档的统一接口进行重构/替换；不要把它们当成最终公共 API 或跨平台架构的依据。
- 调度层提供了统一的 `IEventLoopCore/IEventSource` 抽象，并在不同平台存在不同后端：
  - Linux: epoll/poll（readiness）
  - macOS: kqueue/poll/cocoa（readiness + 系统事件）
  - Windows: IOCP/win32（completion + 系统消息）

### 1.2 目标
1) 提供 **统一接口**（同名、同语义、同线程规则）覆盖 TCP 的三个底层阶段：
   - Listen/Accept（server）
   - Non-blocking Connect（client）
   - Connected Read/Write（both）
2) Watcher 在各平台的实现必须：
   - 可挂载到任意符合条件(暂时不考虑挂不上的问题)的 `IOWorker`（只依赖 `IWorkerWatcher`、`IEventSource` 与 worker 的 backend type）
   - 不阻塞 worker 线程（尤其是域名解析）
   - 对外暴露一致的错误码与关闭语义
   - 各自会根据 Option 内容创建平台的 socket fd (windows 可能不是 fd, 但同理)

### 1.3 非目标
- 不在 watcher 内部做 **域名解析**（DNS）：上层做，也不在 watcher 内部做 **TLS/SSL**：上层封装
- watcher 内部是没有读写缓冲区的，需要在上层完成

---

## 2. 统一抽象：命名空间与文件布局

建议新增统一入口层（仅接口与工厂）：
- `platform/network/watcher/tcp_watchers.h`：统一接口与工厂函数（**不含平台细节**）
- `platform/<os>/network/watcher/`：平台实现

命名建议（统一层）：
- `namespace RopHive::Network { ... }`

平台实现建议（保持现有风格）：
- `RopHive::Linux::...`
- `RopHive::MacOS::...`
- `RopHive::Windows::...`

---

## 3. 线程与生命周期规则（必须遵守）

### 3.1 线程归属（Owner Worker）
- 每个 watcher 都绑定一个 `IOWorker& worker_`，称为 **owner worker**。
- `start()/stop()`、`trySend()/shutdownWrite()/close()`（若提供）必须在 **owner worker 线程**调用。
- 跨线程调用的正确方式：`hive.postToWorker(id, ...)` 或 `worker.postPrivate(...)` 让操作回到 owner worker 执行。

### 3.2 资源所有权
- `AcceptWatcher`：**拥有** `listen socket`（上层创建后把所有权移交给 watcher；watcher 析构时关闭）
- `ConnectWatcher`：**拥有**其内部创建的 socket，成功后把连接 socket 的所有权 **转移**给上层/ConnectionWatcher。
- `ConnectionWatcher`：**拥有** 连接 socket，`stop()/析构` 时关闭 fd/socket。

### 3.3 回调与捕获约束（与现有 watcher 设计保持一致）
- 回调尽量不要捕获裸 `this`；需要跨回调持有 watcher 生命周期时，用 `shared_ptr` / `weak_ptr` 兜底（参考 `docs/Chinese/platform/跨平台调度器开发.md` 中 watcher 设计建议）。
- Watcher 的 `IEventSource` 通常保存 callback；callback 内部不要假设 watcher 仍存活，除非有明确的生命周期托管机制。

### 3.4 “取消/停止”语义
统一接口应包含“取消连接尝试/关闭连接”的显式方法（见第 4 节）。实现必须保证：
- `stop()` 之后不会再触发任何用户回调（或仅允许一次 `on_close/on_error`，但要定义清楚并统一）。
- `cancel/close` 要尽快释放底层句柄（Windows 上通常要配合 `CancelIoEx`/关闭 socket 使 IO 完成返回）。

---

## 4. 统一接口设计（头文件骨架）

> 这里给出“工作文档级”的接口草案：目的、方法语义、实现要点。实际落地可根据代码风格微调命名，但**语义必须一致**。

### 4.1 统一错误码
统一对外暴露 `int err`：
- POSIX：使用 `errno` 值（如 `ECONNRESET/EPIPE/ETIMEDOUT`）
- Windows：使用 `WSAGetLastError()` 值（如 `WSAECONNRESET/WSAETIMEDOUT`）

上层如需统一字符串：提供 `Network::errorToString(err)`（后续可加）。

### 4.2 地址/端点数据结构（类 Rust 的“带参数 enum”，但不存平台 sockaddr）
为避免把 DNS 绑死在 watcher 里，connect 的输入建议是“已解析的 IP + 端口”。

同时我们希望把 IPv4/IPv6 端点变成一个“带标签的联合体”（类似 Rust 的含参数 enum）：
- `V4 { ip: [u8;4], port }`
- `V6 { ip: [u8;16], port, scope_id }`

注意：端点本身不直接存 `sockaddr_in/sockaddr_in6`（这些属于平台/ABI 相关表示）。端点只用标准库的基础类型表示地址与端口；在具体平台实现里再通过 **普通函数（非成员函数）**把它转换成 native sockaddr 用于 `connect/bind/AcceptEx/ConnectEx`。

在 C++17+ 中最合适的表达是 `std::variant`：它就是标准库提供的“含参数 enum”（tagged union）。

```cpp
namespace RopHive::Network {

using Ipv4Bytes = std::array<uint8_t, 4>;
using Ipv6Bytes = std::array<uint8_t, 16>;

struct IpEndpointV4 {
    Ipv4Bytes ip{};
    uint16_t port{0}; // host byte order
};

struct IpEndpointV6 {
    Ipv6Bytes ip{};
    uint16_t port{0};     // host byte order
    uint32_t scope_id{0}; // IPv6 zone index (0 means none/unspecified)
};

// Tagged union endpoint (like Rust enum with payload).
using IpEndpoint = std::variant<IpEndpointV4, IpEndpointV6>;

} // namespace RopHive::Network
```

说明：
- watcher 的对外接口接收 `IpEndpoint`（已解析的 IP + 端口），不负责 DNS。
- 具体平台需要把 `IpEndpointV4/IpEndpointV6` 解释成 native sockaddr 类型：
  - POSIX（Linux/macOS）：`sockaddr_in` / `sockaddr_in6`
  - Windows（Winsock）：`SOCKADDR_IN` / `SOCKADDR_IN6`

建议在各自平台实现文件中提供如下 **自由函数**（示意）：
```cpp
// platform/<os>/network/... .cc
sockaddr_in toNativeSockaddr(const RopHive::Network::IpEndpointV4& ep);
sockaddr_in6 toNativeSockaddr(const RopHive::Network::IpEndpointV6& ep);

IpEndpointV4 toIpEndPoint(sockaddr_in ip, uint16_t port);
IpEndpointV6 toIpEndPoint(sockaddr_in6 ip, uint16_t port);
```

端口统一用 host byte order 存在 `IpEndpointV4/IpEndpointV6.port`，转换到 native sockaddr 时再 `htons()`。

反向方向（把 native sockaddr 解析回 `IpEndpoint`，用于填充 `ITcpStream.peer/local`）同理建议封装为自由函数：
```cpp
// platform/<os>/network/... .cc
std::optional<RopHive::Network::IpEndpoint> fromNativeSockaddr(const sockaddr* sa, socklen_t len);
```
这样统一层/上层永远只看到 `IpEndpoint`（variant），而平台相关的 sockaddr 解释逻辑全部收在平台实现里。

### 4.3 统一的连接载体：`ITcpStream` 与平台无关 Option

对于上层来说：
- `accept` 的产物与 `connect` 的产物在语义上都是一个整体：**一个已连接的 TCP 流**。
- 上层拿到这个整体后，下一步就是把它交给 `ConnectionWatcher` 去驱动收发。

因此这里定义一个统一载体 `ITcpStream`（多态对象，`unique_ptr` 传递）：
- 必须有 `kind tag`，用于识别其来源平台/后端（必要时允许上层做显式 downcast 获取额外字段）。
- 必须有 **虚析构**，保证派生类析构正确执行（通常意味着派生类负责关闭原生 socket，或显式转移所有权）。
- 除虚析构外不强制要求其他虚函数（尽量保持简单，避免把“驱动 IO 的逻辑”塞进 stream 对象本身）。

同时，一些行为参数（Option）应该是平台无关的：上层配置一次，三端实现尽量 best-effort 对齐语义。

```cpp
namespace RopHive::Network {

enum class TcpStreamKind : uint8_t {
    LinuxEpoll,
    LinuxPoll,
    MacKqueue,
    MacPoll,
    WindowsIocp,
};

struct ITcpStream {
    explicit ITcpStream(TcpStreamKind k) : kind(k) {}
    virtual ~ITcpStream() = default;

    TcpStreamKind kind;

    // Best-effort metadata for upper layers (logging/sharding).
    // `std::nullopt` means "unknown/unset".
    std::optional<IpEndpoint> peer;
    std::optional<IpEndpoint> local;
};

#### `ITcpStream` 的所有权与析构约束（必须写死）

`ITcpStream` 是 accept/connect 的共同产物，用 `std::unique_ptr` 在上层传递。
因此必须明确其“socket 所有权”归属，否则很容易泄漏或 double-close。

统一规定：
1) `ITcpStream` **默认拥有**底层 socket（fd/SOCKET）的所有权。
2) 如果上层拿到 `std::unique_ptr<ITcpStream>` 后“没有使用/直接丢弃”，那么在 `ITcpStream` 析构时：
   - 必须关闭底层 socket（RAII 兜底），保证不会泄漏。
3) 当上层把 stream 交给 `createTcpConnectionWatcher(..., std::unique_ptr<ITcpStream> connected_stream, ...)` 时：
   - 所有权随 `unique_ptr` move 进入 connection watcher；
   - connection watcher 成为新的 owner，并在其 stop/析构/close 路径中负责关闭底层 socket。
4) 不允许出现“两个对象同时认为自己拥有同一个 socket”的情况：
   - 如果实现需要把 socket 从 stream 转移到 watcher 内部结构，必须是显式的所有权转移（例如 stream 内部把 native 句柄置为 invalid）。

补充说明：
- `ITcpStream` 只有虚析构这一条虚函数需求；其析构本身可以由派生类完成（派生类持有 native socket 并在析构中 close）。
- 这条规则不影响“trySend 模型”：trySend 讨论的是 connection watcher 的发送语义，与 stream 的资源兜底回收是两件事。

struct TcpAcceptOption {
    IpEndpoint local_bind;
    // 每次“可读就绪”回调内，最多执行多少次 accept（或 AcceptEx 完成处理）。
    // 目的：限制单次 tick 的工作量，避免 accept 风暴把 worker 卡死。
    size_t max_accept_per_tick = 64;

    // 是否 best-effort 填充 `ITcpStream.peer/local`（观测信息）。
    // - true：实现可以调用 getpeername/getsockname 或从 AcceptEx 缓冲解析来填充；
    // - false：实现可以不做额外系统调用，把 peer/local 留空（降低开销）。
    bool fill_endpoints = true;

    // --- 扩展字段（每个字段独立；默认 std::nullopt=不允许生效） ---
    // 原则：凡是“可能无法在所有平台/后端 100% 生效”的 socket 选项，都用 std::optional 表达。
    // - std::nullopt：上层没有显式配置，实现不得尝试设置（避免“看似默认生效但平台忽略”的误解）。
    // - 有值：实现 best-effort 尝试设置；若平台不支持/失败，不应当中断连接（除非上层另有策略）。
    std::optional<bool> set_close_on_exec; // POSIX: FD_CLOEXEC；Windows: non-inheritable（语义不完全等价）
    std::optional<bool> tcp_no_delay;      // TCP_NODELAY
    std::optional<bool> keep_alive;        // SO_KEEPALIVE

    // keepalive 三元组（best-effort）：
    // - POSIX：TCP_KEEPIDLE/TCP_KEEPINTVL/TCP_KEEPCNT（并非所有系统都支持全部字段）
    // - Windows：常用 WSAIoctl(SIO_KEEPALIVE_VALS) 设置 idle/interval；count 往往不可配或语义不一致
    std::optional<int> keep_alive_idle_sec;
    std::optional<int> keep_alive_interval_sec;
    std::optional<int> keep_alive_count;

    // SO_RCVBUF/SO_SNDBUF（best-effort；OS 可能调整最终值）
    std::optional<int> recv_buf_bytes;
    std::optional<int> send_buf_bytes;

    // 当 accept 产出速度 > 上层消费速度时的缓冲/溢出策略。
    // 说明：该组参数仅在实现采用“内部队列 + 上层 drain”的模型时有意义；
    // 如果实现是“accept 到一个就直接 OnAccept(stream) 回调（不做队列）”，这些字段可能被忽略。
    enum class OverflowPolicy : uint8_t {
        // Close the newly accepted socket immediately.
        CloseNew,
        // Drop (leak-free) the newly accepted socket without reporting error.
        DropSilent,
        // Close new socket and call OnError once (rate-limited by implementation).
        CloseAndReport,
    };
    size_t accepted_queue_capacity = 1024;
    OverflowPolicy overflow = OverflowPolicy::CloseNew;
};

struct TcpConnectOption {
    // connect 前是否显式 bind 本地端点（可选）。
    //
    // - If `local_bind` is set, the implementation must best-effort bind to that exact endpoint (ip+port).
    // - Otherwise, `local_port` can be used as a shortcut to request a specific port (0 means ephemeral),
    //   and the implementation will bind to an "any" address (0.0.0.0/::) matching the target family.
    std::optional<IpEndpoint> local_bind;
    // 仅指定本地端口（0=让 OS 选择临时端口）。若同时设置了 local_bind，则以 local_bind 为准。
    uint16_t local_port = 0;

    // 说明：connect watcher **创建并完全持有 socket**，且实现必须保证不会阻塞 worker。
    // readiness 后端必须使用 non-blocking；Windows IOCP 语义上必须是异步的。
    //
    // --- 扩展字段（每个字段独立；默认 std::nullopt=不允许生效） ---
    // 与 TcpAcceptOption 同理：只有上层显式给值，才 best-effort 尝试设置。
    std::optional<bool> set_close_on_exec;
    std::optional<bool> tcp_no_delay;
    std::optional<bool> keep_alive;
    std::optional<int> keep_alive_idle_sec;
    std::optional<int> keep_alive_interval_sec;
    std::optional<int> keep_alive_count;
    std::optional<int> recv_buf_bytes;
    std::optional<int> send_buf_bytes;

    // 是否 best-effort 填充 `ITcpStream.peer/local`（观测信息）。
    bool fill_endpoints = true;

    // If you want connect timeout, prefer "upper layer timer + cancel()".
    // Watcher may still optionally provide internal timeout later, but should not block worker.
    //
    // 内部结果队列容量（仅当实现采用“队列 + 上层 drain”模型时有意义；
    // 若实现是“一次 connect 完成就直接回调 OnConnected(stream)”，可能被忽略）。
    size_t connected_queue_capacity = 1024;
};

struct TcpConnectionOption {
    // --- 扩展 socket 选项（每个字段独立；默认 std::nullopt=不允许生效） ---
    std::optional<bool> tcp_no_delay; // TCP_NODELAY
    std::optional<bool> keep_alive;   // SO_KEEPALIVE
    std::optional<int> keep_alive_idle_sec;
    std::optional<int> keep_alive_interval_sec;
    std::optional<int> keep_alive_count;
    std::optional<int> recv_buf_bytes; // SO_RCVBUF
    std::optional<int> send_buf_bytes; // SO_SNDBUF

    // SO_LINGER：当设置该值时，best-effort 启用 linger 并使用此秒数。
    // 注意：不同平台对 linger=0 的行为可能不同（常见是 close 触发 RST），需要上层理解其含义。
    std::optional<int> linger_sec;

    // --- Read/Write scheduling bounds ---
    // 每次事件派发/完成包处理内的读写上限（框架行为，平台无关）。
    // 目的：高吞吐下保持 worker 响应性，避免单连接占满时间片。
    size_t max_read_bytes_per_tick = 256 * 1024;
    size_t max_write_bytes_per_tick = 256 * 1024;

    // --- Sending model note ---
    // 本文档采用 trySend 模型：watcher 不做“协议层写缓冲”，仅负责尽量把数据推进到内核/提交异步发送。
    // 因此不提供 max_pending_write_bytes/pending_overflow 这类“watcher 内部写队列”的配置。
};

} // namespace RopHive::Network
```

### 4.3.1 Option 配置总览

下面把三个 Option 的字段“摊平列出来”，便于上层直接配置/对照实现。

对于平台相关的选项，设置为 optional，默认不会产生效果

#### `TcpAcceptOption`（server accept 阶段）
**所有平台都能配置且语义稳定：**
- `max_accept_per_tick`：单次就绪/tick 最多 accept 次数（限制风暴）。
- `fill_endpoints`：是否填充 `ITcpStream.peer/local`（关闭可减少额外 syscall/解析开销；Windows 可从 AcceptEx 缓冲解析，POSIX 可用 getpeername/getsockname）。

**可能不是所有平台/后端都能完全生效（best-effort/可能被忽略）：**
- `set_close_on_exec / tcp_no_delay / keep_alive / keep_alive_* / recv_buf_bytes / send_buf_bytes`：
  扩展字段（std::optional）。`nullopt` 表示未配置/禁止生效；有值时 best-effort 尝试设置（平台可能忽略或调整最终值）。
- `accepted_queue_capacity / overflow`：仅当实现采用“内部队列 + 上层 drain”模型时有意义；若实现直接 `OnAccept(stream)`，可忽略。

#### `TcpConnectOption`（client connect 阶段）
**所有平台都能配置且语义稳定：**
- `local_bind`：本地 bind 到“指定 IP+port”（意图；best-effort 但大多平台可做；Windows 的 ConnectEx 甚至要求先 bind）。
- `local_port`：仅指定本地端口（0=临时端口）；若同时设置 `local_bind`，以 `local_bind` 为准。
- `fill_endpoints`：是否填充 `ITcpStream.peer/local`（观测）。

**可能不是所有平台/后端都能完全生效（best-effort/可能被忽略）：**
- `set_close_on_exec / tcp_no_delay / keep_alive / keep_alive_* / recv_buf_bytes / send_buf_bytes`：
  扩展字段（std::optional）。语义同 `TcpAcceptOption`。
- `connected_queue_capacity`：仅当实现采用“内部队列 + 上层 drain”模型时有意义；若实现直接 `OnConnected(stream)`，可忽略。

#### `TcpConnectionOption`（connected 收发阶段）
**所有平台都能配置且语义稳定：**
- `max_read_bytes_per_tick / max_write_bytes_per_tick`：每 tick 读写上限（平台无关，属于框架调度约束）。
- `OnSendReady`：写阻塞→恢复可写的边沿回调（语义要对齐；不同后端触发点可能不同）。

**可能不是所有平台/后端都能完全生效（best-effort/可能被忽略）：**
- `tcp_no_delay / keep_alive / keep_alive_* / recv_buf_bytes / send_buf_bytes / linger_sec`：
  扩展字段（std::optional）。`nullopt` 表示未配置/禁止生效；有值时 best-effort 尝试设置。

#### Option 与 Stream 的优先级规则（必须遵守）

这里需要明确区分两类信息：
- **Option（TcpAcceptOption/TcpConnectOption/TcpConnectionOption）** 表达的是“上层的配置意图”（你希望系统怎么做）。
- **Stream 元数据（ITcpStream.peer/local）** 表达的是“实现对真实结果的 best-effort 观测”（系统最终实际怎么做）。

因此统一规定如下：
1) 任何“可配置项”的处理都以 **Option 为准**（只要平台/后端允许，实现必须 best-effort 应用 Option）。
   - 例如：是否启用 `TCP_NODELAY`、是否启用 keepalive、期望的 buffer 大小、linger 策略、每 tick 读写上限、队列容量与溢出策略等。
2) `ITcpStream.peer/local` 仅作为 **结果观测**：
   - 例如：`local_port=0` 时 OS 会选择一个临时端口；stream 里记录的是这个“最终端口”。
   - OS 可能会调整 buffer size，stream/系统查询到的是“最终生效值”。
3) 两者不允许互相覆盖：
   - stream 的观测结果不能反过来改变/推翻 Option 的意图；
   - Option 也不能假装是最终结果，最终结果以 stream/系统查询为准。

### 4.4 ITcpAcceptWatcher（Server）
职责：监听 socket readiness → accept 循环 → 为每个新连接触发回调。

```cpp
namespace RopHive::Network {

class ITcpAcceptWatcher : public IWorkerWatcher {
public:
    using OnAccept = std::function<void(std::unique_ptr<ITcpStream> stream)>;
    using OnError  = std::function<void(int err)>;

    virtual ~ITcpAcceptWatcher() = default;

    // start(): attach source(s) to worker; begin receiving accept events.
    // stop(): detach source(s); stop future callbacks.
};

// Factory: create an accept watcher for current worker backend.
std::shared_ptr<ITcpAcceptWatcher>
createTcpAcceptWatcher(IOWorker& worker,
                       TcpAcceptOption option,
                       ITcpAcceptWatcher::OnAccept on_accept,
                       ITcpAcceptWatcher::OnError on_error = {});

} // namespace RopHive::Network
```

实现要点：
- 必须使用 non-blocking accept loop（如 POSIX: `accept4` + `EAGAIN/EINTR`）。
- `on_accept` 触发时，客户端 socket 必须处于非阻塞模式（POSIX 可用 `accept4(..., SOCK_NONBLOCK|SOCK_CLOEXEC)`）。
- 错误处理建议：listen socket readiness 上的 `ERR/HUP` 要尽量用 `getsockopt(SO_ERROR)` 取真实错误码，而不是常量替代。
- **listen socket 的创建与所有权**：
  - `createTcpAcceptWatcher(worker, option, ...)` 内部负责：`socket/bind/listen` + 非阻塞设置 + best-effort 应用 listen 相关 option（其中 bind 地址来自 `option.local`）。
  - 创建成功后 listen socket 的所有权归 accept watcher；watcher `stop()/析构/close` 路径中负责关闭。

---

## 9. listen socket 创建的解耦（示例里的 makeListenSocket 去平台化）

`example/tcp_server_example/main.cc` 里的 `makeListenSocket()` 是 Linux/POSIX 风格的示例代码，它的存在只为演示“accept watcher/connection watcher 的协作”，并不适合作为跨平台框架 API 的一部分。

为了让 **PC 三端（Linux/macOS/Windows）** 的 server 入口统一，建议把“listen socket 创建”从示例/业务层剥离到平台层，并在统一层提供一个最小、清晰的抽象：

1) **平台层负责创建 native listen socket**（包含：socket/bind/listen、非阻塞、CLOEXEC/可继承、REUSEADDR/REUSEPORT 等 best-effort 设置）。
2) **统一层只接收一个“已准备好的 listen socket 所有权”**，并交给 `ITcpAcceptWatcher` 驱动 accept。

推荐的演进方向（后续迭代，不在当前 watcher 核心里实现）：
- 增加一个只承载“listen socket 所有权”的轻量结构体（类似 `ITcpStream` 的做法）：
  - `struct ITcpListenSocket { kind tag + virtual ~dtor; /* 内部可持有 fd/SOCKET 等 */ };`
  - accept watcher 工厂从 `intptr_t listen_handle` 演进为 `std::unique_ptr<ITcpListenSocket> listen_socket`。
- 提供一个统一的工厂（内部按平台/后端分发）：
  - `createTcpListenSocket(worker, IpEndpoint local, TcpListenOption option) -> std::unique_ptr<ITcpListenSocket>`
  - 这样示例/业务层完全不需要 `#include <sys/socket.h>` 或 Windows 的 Winsock 细节。

当前阶段为了推进 watcher 统一设计，accept watcher 已改为 **内部创建 listen socket**，示例/业务层不再直接操作 `socket/bind/listen`。

### 4.5 ITcpConnectWatcher（Client）
职责：发起非阻塞连接，连接成功后把“已连接 socket”移交给上层（通常用来创建 `ITcpConnectionWatcher`）。

```cpp
namespace RopHive::Network {

class ITcpConnectWatcher : public IWorkerWatcher {
public:
    using OnConnected = std::function<void(std::unique_ptr<ITcpStream> stream)>;
    using OnError     = std::function<void(int err)>;

    virtual ~ITcpConnectWatcher() = default;

    // cancel(): 显式取消连接尝试并释放句柄；允许多次调用。
    // 语义：cancel() 之后不再触发 on_connected；on_error 是否触发需要统一规定：
    // - 推荐：cancel() 不触发 on_error（由上层自行决定是否视为错误）。
    virtual void cancel() = 0;
};

std::shared_ptr<ITcpConnectWatcher>
createTcpConnectWatcher(IOWorker& worker,
                        TcpConnectOption option,
                        ITcpConnectWatcher::OnConnected on_connected,
                        ITcpConnectWatcher::OnError on_error = {});

} // namespace RopHive::Network
```

实现要点：
- POSIX：`socket` + `O_NONBLOCK` + `connect`，`EINPROGRESS` 后监听可写，再用 `getsockopt(SO_ERROR)` 判定。
- Windows(IOCP)：使用 `ConnectEx`（或非阻塞 connect + WSAEventSelect 方案不推荐）。`cancel()` 需要能尽快终止 pending connect（通常靠 `closesocket` 触发完成，或 `CancelIoEx`/`CancelIo`）。

### 4.6 ITcpConnectionWatcher（Connected IO）
职责：对一个已连接的 socket，基于 worker 后端驱动读写与关闭事件；提供一个最小的发送缓冲。

```cpp
namespace RopHive::Network {

class ITcpConnectionWatcher : public IWorkerWatcher {
public:
    using OnRecv  = std::function<void(std::string_view chunk)>;
    using OnClose = std::function<void()>;
    using OnError = std::function<void(int err)>;
    using OnSendReady = std::function<void()>;

    virtual ~ITcpConnectionWatcher() = default;

    // 发送模型：trySend（不做 watcher 内部协议层写缓冲）。
    struct TrySendResult {
        // Bytes accepted/advanced by this call.
        // - POSIX(readiness): bytes written into kernel send buffer.
        // - Windows(IOCP): bytes submitted into an async send operation, or 0 if would-block/busy.
        size_t n = 0;

        // True if the connection cannot currently accept more bytes (POSIX: EAGAIN/EWOULDBLOCK;
        // Windows IOCP: send pipeline is busy / backpressured).
        bool would_block = false;

        // 0 means no error.
        int err = 0;
    };

    // trySend(): 尽量把数据推进到内核/提交异步发送。
    // - 不保证一次调用把 data 全部发送完（可能部分推进或完全推进不了）。
    // - 若返回 would_block=true，上层应缓存未发送的部分，并等待 OnSendReady 再继续 trySend。
    virtual TrySendResult trySend(std::string_view data) = 0;

    // shutdownWrite(): half-close write side.
    virtual void shutdownWrite() = 0;

    // close(): actively close immediately.
    virtual void close() = 0;

    // 注：trySend 模型下，watcher 不维护协议层写缓冲，因此不提供 pendingWriteBytes()。
};

std::shared_ptr<ITcpConnectionWatcher>
createTcpConnectionWatcher(IOWorker& worker,
                           TcpConnectionOption option,
                           std::unique_ptr<ITcpStream> connected_stream,
                           ITcpConnectionWatcher::OnData on_data,
                           ITcpConnectionWatcher::OnClose on_close = {},
                           ITcpConnectionWatcher::OnError on_error = {},
                           ITcpConnectionWatcher::OnSendReady on_send_ready = {});

} // namespace RopHive::Network
```

实现要点：
- POSIX readiness：
  - `EPOLLIN/POLLIN` → `recv` loop until `EAGAIN`；`n==0` 视为 peer close。
  - `EPOLLOUT/POLLOUT` → 允许继续 `trySend`；实现应通过内部状态触发一次 `OnSendReady`（blocked->ready 边沿）。
  - 尽量订阅 `RDHUP/HUP/ERR` 并处理 half-close/close（Linux 可用 `EPOLLRDHUP`）。
- Windows(IOCP) completion：
  - 常用模式是 “永远保持一个 pending recv” + “写队列串行发出 overlapped WSASend”。
  - 需要为每个 I/O op 持有独立的 `OVERLAPPED` 与 buffer；完成回调中再投递下一次 recv/send。
  - close/cancel 必须处理 pending I/O：关闭 socket 通常会让 pending op 以 error completion 回来；实现需屏蔽 stop 后的回调。

---

## 5. 平台实现策略（落地路径）

### 5.1 Linux（epoll/poll）
- 现有实现可作为“语义参考 / 原型验证”（但需要重构以满足统一接口与取消语义等要求）：
  - `platform/linux/network/watcher/tcp_accept_watcher.*`
  - `platform/linux/network/watcher/tcp_connect_watcher.*`
  - `platform/linux/network/watcher/tcp_connection_watcher.*`
- 统一重构时建议：
  - 提供 `Network::createTcp*Watcher(...)` 工厂：根据 `worker.options().io_backend` 或 `worker.core()->backendType()` 选择 epoll/poll 实现。
  - 完善错误码：accept/connect 的错误尽量用 `SO_ERROR` 获取真实值。
  - 明确 cancel 行为：connect watcher `cancel()` 应立即 `close(fd)`（现在 stop 不 close 的行为不利于超时/取消）。

### 5.2 macOS（kqueue/poll）
- 使用 `platform/macos/schedule/kqueue_backend.h` 的 `KqueueReadinessEventSource` 与 `PollReadinessEventSource`。
- POSIX socket API 与 Linux 类似（同样 `recv/send`、`O_NONBLOCK`、`getsockopt(SO_ERROR)`）。
- 注意：
  - kqueue 使用 `(ident=fd, filter=EVFILT_READ/EVFILT_WRITE)` 区分读写；通常需要为同一 fd 注册两类 source（或在一个 source 内动态改 filter/flags）。

### 5.3 Windows（IOCP）
- 使用 `platform/windows/schedule/iocp_backend.h` 的 `IocpHandleCompletionEventSource`：
  - arm() 时把 socket 关联到 IOCP（`associateHandle`）
  - dispatch() 收到完成事件后，分发给 watcher 内部状态机
- 核心 API（建议）：
  - server accept：`AcceptEx`
  - client connect：`ConnectEx`
  - connection recv/send：`WSARecv/WSASend`
- 关键实现要求：
  - 每个连接必须有一份 `ConnectionState`（包含 socket、读写 buffer、overlapped structs、关闭标记）。
  - stop/cancel/close 必须“幂等”，并屏蔽 stop 后的回调（避免 use-after-free）。

---

## 6. 与 Hive/IOWorker 的集成方式（推荐用法）

### 6.1 Server（单 acceptor + 分发连接到 worker）
推荐：一个 worker 专职 accept，accept 到的 client socket 通过 `Hive::postToWorker(...)` 分发到目标 worker，在目标 worker 上创建 `ConnectionWatcher`：

1) accept worker 上：`AcceptWatcher` 回调拿到 `std::unique_ptr<ITcpStream> stream`
2) 选择目标 worker（轮询/哈希/负载）
3) `hive.postToWorker(target, [stream = std::move(stream)]() mutable { createTcpConnectionWatcher(..., std::move(stream), ...)->start(); })`

这样可以让连接的读写 watcher 与其工作负载自然绑定到某个 worker。

### 6.2 Client（resolve → connect → connection）
- DNS/解析：在 compute 池或独立 resolver 做（避免 IO worker 阻塞）。
- connect watcher 仅接 `IpEndpoint`（已解析的 IP + 端口）。

---

## 7. 覆盖面清单（底层“必须覆盖”的接口场景）

为了保证“统一 watcher 能覆盖 PC 端 TCP 的底层阶段”，实现必须覆盖：
- listen：accept 循环（`EINTR/EAGAIN`），支持高并发 accept burst
- connect：非阻塞 connect 成功/失败判定（`SO_ERROR` / Windows completion error）
- connection：
  - 读：连续读取直到 `EAGAIN` / completion，正确处理 `n==0` 的对端关闭
  - 写：发送缓冲 + 可写事件/完成驱动；支持部分写与 `EAGAIN`
  - close：对端关闭/本端关闭都要可观测（`on_close` 或 `on_error`，需一致）

建议额外覆盖（但可作为后续迭代）：
- peer/local 地址获取（accept/connect 回调返回 peer 信息）
- 显式 cancel（connect/connection），并在 stop 后不再回调用户

---

## 8. 后续扩展点（不进入 watcher 核心）

保持 watcher “薄而硬”，将以下功能放在上层组件：
- `DnsResolver`：跨平台解析（线程池包装 `getaddrinfo` / Windows `GetAddrInfoEx`）
- `TcpClient`：重连/退避/多地址尝试（Happy Eyeballs）
- `TcpServer`：多 acceptor（Linux `SO_REUSEPORT`）/连接分桶/连接池
- `TlsConnection`：对 `ITcpConnectionWatcher` 进行 TLS 包装
