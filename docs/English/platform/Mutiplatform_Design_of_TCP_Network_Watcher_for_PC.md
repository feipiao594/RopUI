# Multiplatform Unified Design of TCP Network Watchers for PC

This document targets **Windows / macOS / Linux (PC)**. It designs a unified set of TCP Server/Client Watcher interfaces and implementation constraints, so they can be seamlessly mounted into the existing `RopHive::IOWorker` (`IWorkerWatcher` + `IEventSource`) system and exhibit equivalent behavior across different platform backends (epoll/poll/kqueue/IOCP).

> Constraint: Watchers are only responsible for **socket lifecycle + event-driven accept/connect/send/recv**. They do not include DNS resolution, TLS, reconnect/backoff, etc.; those belong to higher-level components.

---

## 1. Background and Goals

### 1.1 Background

- The repository currently has a set of Linux TCP worker watchers: `platform/linux/network/watcher/` (accept/connect/connection). **They are for reference only**. They will be refactored/replaced to match this unified interface. Do not treat them as the final public API or as the basis for a cross-platform architecture.
- The scheduling layer provides a unified `IEventLoopCore/IEventSource` abstraction, with different backends on each platform:
  - Linux: epoll/poll (readiness)
  - macOS: kqueue/poll/cocoa (readiness + system events)
  - Windows: IOCP/win32 (completion + system messages)

### 1.2 Goals

1) Provide a **unified interface** (same names, semantics, and threading rules) covering the three low-level stages of TCP:
   - Listen/Accept (server)
   - Non-blocking Connect (client)
   - Connected Read/Write (both)

2) Platform-specific watcher implementations must:
   - Mount onto any compatible `IOWorker` (only depend on `IWorkerWatcher`, `IEventSource`, and the worker backend type).
   - Never block the worker thread (especially DNS).
   - Expose consistent error codes and close semantics.
   - Create platform-specific socket handles from Options (fd on POSIX; `SOCKET`/handle on Windows).

### 1.3 Non-goals

- DNS resolution is not performed inside watchers (upper layer).
- TLS/SSL is not performed inside watchers (upper layer wrapper).
- Watchers do not provide protocol-level read/write buffers; upper layers must manage buffering.

---

## 2. Unified Abstraction: Namespace and File Layout

Add a unified entry layer (interfaces + factories only):
- `platform/network/watcher/tcp_watchers.h`: unified interfaces and factory functions (**no platform details**)
- `platform/<os>/network/watcher/`: platform implementations

Unified naming:
- `namespace RopHive::Network { ... }`

Platform naming (keep existing style):
- `RopHive::Linux::...`
- `RopHive::MacOS::...`
- `RopHive::Windows::...`

---

## 3. Threading and Lifecycle Rules (Must Follow)

### 3.1 Thread Affinity (Owner Worker)

- Each watcher binds to an `IOWorker& worker_`, called the **owner worker**.
- `start()/stop()`, `trySend()/shutdownWrite()/close()` (if provided) must be called on the **owner worker thread**.
- Cross-thread invocation must be done via `hive.postToWorker(id, ...)` or `worker.postPrivate(...)` to hop back onto the owner worker thread.

### 3.2 Resource Ownership

- `AcceptWatcher`: owns the listen socket (created/handed to the watcher; closed on destruction).
- `ConnectWatcher`: owns its internally created socket; on success it transfers ownership of the connected socket to the upper layer / `ConnectionWatcher`.
- `ConnectionWatcher`: owns the connected socket; closes fd/socket on `stop()` or destruction.

### 3.3 Callback Capture Constraints (Consistent with Existing Watchers)

- Prefer not to capture a raw `this` in callbacks. If you need to hold watcher lifetime across callbacks, use `shared_ptr` / `weak_ptr` as a safety net (see watcher guidance in `docs/Chinese/platform/跨平台调度器开发.md`).
- `IEventSource` typically stores callbacks; callbacks must not assume the watcher still exists unless there is explicit lifetime management.

### 3.4 Cancel/Stop Semantics

The unified interface should include explicit operations to **cancel a connect attempt** and **close a connection**. Implementations must guarantee:
- After `stop()`, no user callbacks are triggered anymore (or allow exactly one `on_close/on_error`, but define it clearly and keep it consistent).
- `cancel/close` should release the underlying handle as soon as possible (on Windows typically via `CancelIoEx` and/or closing the socket so pending I/O completes).

---

## 4. Unified Interface Design (Header Skeleton)

This section provides a “working-document level” interface draft: goals, method semantics, and implementation notes. Names may be adjusted to fit code style, but **semantics must remain identical**.

### 4.1 Unified Error Codes

Expose `int err` to upper layers:
- POSIX: use `errno` values (e.g., `ECONNRESET/EPIPE/ETIMEDOUT`)
- Windows: use `WSAGetLastError()` values (e.g., `WSAECONNRESET/WSAETIMEDOUT`)

If upper layers need string conversion, a helper `Network::errorToString(err)` can be added later.

### 4.2 Address / Endpoint Data (Tagged Union, No Native sockaddr)

To avoid binding DNS into watchers, `connect` input should be **resolved IP + port**.

We want an IPv4/IPv6 endpoint represented as a tagged union (like a Rust enum with payload):
- `V4 { ip: [u8;4], port }`
- `V6 { ip: [u8;16], port, scope_id }`

The endpoint itself should not store `sockaddr_in/sockaddr_in6` (ABI/platform representation). Use only standard-library types for address bytes and port, and convert to native sockaddr in platform implementations via **free functions (non-member functions)**.

In C++17+, the natural representation is `std::variant` (a standard tagged union):

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

Notes:
- Watcher APIs take `IpEndpoint` (resolved IP + port). Watchers do not do DNS.
- Each platform converts `IpEndpointV4/IpEndpointV6` into native sockaddr types:
  - POSIX (Linux/macOS): `sockaddr_in` / `sockaddr_in6`
  - Windows (Winsock): `SOCKADDR_IN` / `SOCKADDR_IN6`

Recommended free functions in each platform implementation file (sketch):
```cpp
// platform/<os>/network/... .cc
sockaddr_in toNativeSockaddr(const RopHive::Network::IpEndpointV4& ep);
sockaddr_in6 toNativeSockaddr(const RopHive::Network::IpEndpointV6& ep);

IpEndpointV4 toIpEndPoint(sockaddr_in ip, uint16_t port);
IpEndpointV6 toIpEndPoint(sockaddr_in6 ip, uint16_t port);
```

Ports are stored in host byte order in `IpEndpointV4/IpEndpointV6.port`, and converted with `htons()` when constructing native sockaddr.

The reverse direction (native sockaddr → `IpEndpoint`) is similarly recommended as a free function, used to fill `ITcpStream.peer/local`:
```cpp
// platform/<os>/network/... .cc
std::optional<RopHive::Network::IpEndpoint> fromNativeSockaddr(const sockaddr* sa, socklen_t len);
```

This way upper layers only ever see `IpEndpoint` (variant), and all platform-specific sockaddr parsing stays inside platform implementations.

### 4.3 Unified Connected Stream Carrier: `ITcpStream` and Platform-agnostic Options

For upper layers:
- The product of `accept` and the product of `connect` are semantically the same: **a connected TCP stream**.
- The next step is to pass that stream into `ConnectionWatcher` to drive I/O.

Define a unified carrier `ITcpStream` (polymorphic object passed by `std::unique_ptr`):
- Must have a `kind` tag to identify platform/backend (allow explicit downcast when needed).
- Must have a **virtual destructor** so derived destructors run correctly (typically to close native socket, or to support explicit ownership transfer).
- No other virtual methods are required (keep it minimal; do not put “drive I/O” logic into the stream object).

Options should be platform-agnostic where possible: upper layer configures once; each platform implementation aligns semantics best-effort.

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
} // namespace RopHive::Network
```

#### Ownership and Destructor Rules for `ITcpStream` (Must Be Explicit)

`ITcpStream` is the common product of accept/connect, and is passed via `std::unique_ptr` in upper layers. Therefore, socket ownership must be explicit to avoid leaks or double-close.

Unified rules:
1) `ITcpStream` **owns** the underlying socket (fd/SOCKET) by default.
2) If upper layers receive `std::unique_ptr<ITcpStream>` and drop it unused, `ITcpStream` destruction must close the socket (RAII fallback; no leaks).
3) When upper layers pass the stream into `createTcpConnectionWatcher(..., std::unique_ptr<ITcpStream> connected_stream, ...)`:
   - ownership moves with the `unique_ptr`;
   - the connection watcher becomes the new owner, and closes the socket on stop/destruction/close.
4) Never allow two objects to “own” the same socket simultaneously:
   - if an implementation needs to move the native handle from stream into watcher state, it must be an explicit transfer (e.g., mark the stream handle invalid).

Notes:
- `ITcpStream` only requires a virtual destructor; derived classes can own the native socket and close it in their destructors.
- This rule is orthogonal to the `trySend` model (send semantics vs. RAII cleanup).

Platform-agnostic option structures (header skeleton):

```cpp
namespace RopHive::Network {

struct TcpAcceptOption {
    IpEndpoint local_bind;
    // Max accepts (or completed AcceptEx handling) per readable-ready callback/tick.
    // Goal: bound per-tick work to keep the worker responsive under accept storms.
    size_t max_accept_per_tick = 64;

    // Whether to best-effort fill `ITcpStream.peer/local` (metadata).
    // - true: implementation may call getpeername/getsockname, or parse AcceptEx buffers
    // - false: implementation may skip extra syscalls/parsing and leave them empty
    bool fill_endpoints = true;

    // --- Extension fields (each independent; std::nullopt = do not apply) ---
    // Principle: any socket option that may not be supported consistently across all platforms/backends
    // should be expressed as std::optional.
    // - std::nullopt: not configured; implementation must not attempt to set it
    // - has value: best-effort try to set; if unsupported/fails, should not abort accept unless upper layer decides
    std::optional<bool> set_close_on_exec; // POSIX: FD_CLOEXEC; Windows: non-inheritable (not fully equivalent)
    std::optional<bool> tcp_no_delay;      // TCP_NODELAY
    std::optional<bool> keep_alive;        // SO_KEEPALIVE

    // keepalive triple (best-effort):
    // - POSIX: TCP_KEEPIDLE/TCP_KEEPINTVL/TCP_KEEPCNT (not all systems support all fields)
    // - Windows: commonly WSAIoctl(SIO_KEEPALIVE_VALS) for idle/interval; count often not configurable / not equivalent
    std::optional<int> keep_alive_idle_sec;
    std::optional<int> keep_alive_interval_sec;
    std::optional<int> keep_alive_count;

    // SO_RCVBUF / SO_SNDBUF (best-effort; OS may adjust the final values)
    std::optional<int> recv_buf_bytes;
    std::optional<int> send_buf_bytes;

    // Buffering/overflow policy when accept production rate > upper-layer consumption rate.
    // Note: only meaningful if the implementation uses an internal queue + upper-layer drain model.
    // If the implementation directly invokes `OnAccept(stream)` per accept (no queue), these may be ignored.
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
    // Optional explicit bind before connect.
    //
    // - If `local_bind` is set, the implementation must best-effort bind to that exact endpoint (ip+port).
    // - Otherwise, `local_port` can be used as a shortcut to request a specific port (0 means ephemeral),
    //   and the implementation will bind to an "any" address (0.0.0.0/::) matching the target family.
    std::optional<IpEndpoint> local_bind;
    // Only specify local port (0 means let the OS choose an ephemeral port). If local_bind is set, local_bind wins.
    uint16_t local_port = 0;

    // Note: connect watcher creates and fully owns the socket, and must never block the worker.
    // Readiness backends must use non-blocking; Windows IOCP must be async by semantics.
    //
    // --- Extension fields (each independent; std::nullopt = do not apply) ---
    // Same as TcpAcceptOption: only when upper layer explicitly sets a value should we attempt best-effort.
    std::optional<bool> set_close_on_exec;
    std::optional<bool> tcp_no_delay;
    std::optional<bool> keep_alive;
    std::optional<int> keep_alive_idle_sec;
    std::optional<int> keep_alive_interval_sec;
    std::optional<int> keep_alive_count;
    std::optional<int> recv_buf_bytes;
    std::optional<int> send_buf_bytes;

    // Whether to best-effort fill `ITcpStream.peer/local` (metadata).
    bool fill_endpoints = true;

    // If you want connect timeout, prefer "upper layer timer + cancel()".
    // Watcher may optionally provide internal timeout later, but must not block the worker.
    //
    // Internal result queue capacity (only meaningful if using "queue + drain" model;
    // if the implementation calls OnConnected(stream) directly, this may be ignored).
    size_t connected_queue_capacity = 1024;
};

struct TcpConnectionOption {
    // --- Extension socket options (each independent; std::nullopt = do not apply) ---
    std::optional<bool> tcp_no_delay; // TCP_NODELAY
    std::optional<bool> keep_alive;   // SO_KEEPALIVE
    std::optional<int> keep_alive_idle_sec;
    std::optional<int> keep_alive_interval_sec;
    std::optional<int> keep_alive_count;
    std::optional<int> recv_buf_bytes; // SO_RCVBUF
    std::optional<int> send_buf_bytes; // SO_SNDBUF

    // SO_LINGER: if set, best-effort enable linger and use this seconds value.
    // Note: linger=0 may behave differently across platforms (commonly close triggers RST); upper layers should understand it.
    std::optional<int> linger_sec;

    // --- Read/Write scheduling bounds ---
    // Max read/write bytes per dispatch/completion batch (platform-agnostic framework bound).
    // Goal: keep worker responsive under high throughput; prevent a single connection from monopolizing the timeslice.
    size_t max_read_bytes_per_tick = 256 * 1024;
    size_t max_write_bytes_per_tick = 256 * 1024;

    // --- Sending model note ---
    // This document uses a trySend model: watcher does not implement protocol-layer write buffering,
    // it only tries to push bytes into the kernel / submit async sends.
    // Therefore, we do not provide options like max_pending_write_bytes or pending_overflow (internal write queue).
};

} // namespace RopHive::Network
```

#### 4.3.1 Options Overview

This section “flattens” fields across the three Options for easier implementation cross-checking.

For platform-specific options: use `std::optional`, and by default it produces no effect.

##### `TcpAcceptOption` (server accept stage)
All platforms can configure with stable semantics:
- `max_accept_per_tick`: max accepts per readiness/tick (storm bound).
- `fill_endpoints`: whether to fill `ITcpStream.peer/local` (disable to reduce extra syscalls/parsing; Windows may parse AcceptEx buffers; POSIX can use getpeername/getsockname).

May not work on all platforms/backends (best-effort / may be ignored):
- `set_close_on_exec / tcp_no_delay / keep_alive / keep_alive_* / recv_buf_bytes / send_buf_bytes`: optional extension fields.
- `accepted_queue_capacity / overflow`: only meaningful for “internal queue + upper-layer drain” model; if implementation is direct `OnAccept(stream)`, can ignore.

##### `TcpConnectOption` (client connect stage)
All platforms can configure with stable semantics:
- `local_bind`: bind to a specific local IP+port (intent; best-effort but typically feasible; Windows ConnectEx may even require a bind).
- `local_port`: only specify local port (0=ephemeral); `local_bind` wins if both are set.
- `fill_endpoints`: whether to fill `ITcpStream.peer/local` (metadata).

May not work on all platforms/backends (best-effort / may be ignored):
- `set_close_on_exec / tcp_no_delay / keep_alive / keep_alive_* / recv_buf_bytes / send_buf_bytes`: optional extension fields.
- `connected_queue_capacity`: only meaningful for “internal queue + upper-layer drain” model; if implementation is direct `OnConnected(stream)`, can ignore.

##### `TcpConnectionOption` (connected I/O stage)
All platforms can configure with stable semantics:
- `max_read_bytes_per_tick / max_write_bytes_per_tick`: per-tick read/write caps (platform-agnostic scheduling bound).
- `OnSendReady`: edge callback from write-blocked → writable again (semantics must match; trigger points vary by backend).

May not work on all platforms/backends (best-effort / may be ignored):
- `tcp_no_delay / keep_alive / keep_alive_* / recv_buf_bytes / send_buf_bytes / linger_sec`: optional extension fields.

##### Priority Rules Between Option vs. Stream Metadata (Must Follow)

Two categories must be distinguished:
- **Options** (`TcpAcceptOption/TcpConnectOption/TcpConnectionOption`) represent **upper-layer intent** (“what you want the system to do”).
- **Stream metadata** (`ITcpStream.peer/local`) represent best-effort **observed results** (“what the system actually did”).

Unified rules:
1) Any configurable behavior must follow **Options** (best-effort apply when supported by platform/backend):
   - e.g. TCP_NODELAY, keepalive, buffer sizes, linger policy, per-tick caps, queue capacities/overflow policy.
2) `ITcpStream.peer/local` is only for **observation**:
   - e.g. when `local_port=0`, the OS picks an ephemeral port; stream records the final chosen port.
   - OS may adjust buffer sizes; stream/system query returns the final effective values.
3) They must not override each other:
   - Observed metadata must not “rewrite” Option intent;
   - Options must not pretend to be final results; the final result is stream/system observation.

### 4.4 Decouple Listen Socket Creation (Move `makeListenSocket` out of Examples)

`makeListenSocket()` in `example/tcp_server_example/main.cc` is Linux/POSIX-style sample code. It only demonstrates accept/connection watcher collaboration, and is not suitable as a cross-platform framework API.

To unify the server entry on **Linux/macOS/Windows**, we should move listen socket creation into platform layers, and provide a minimal abstraction in the unified layer:
1) Platform layer creates the native listen socket (socket/bind/listen, non-blocking, close-on-exec / inheritable, REUSEADDR/REUSEPORT, etc. best-effort).
2) Unified layer accepts ownership of a “prepared” listen socket and lets `ITcpAcceptWatcher` drive accept.

Recommended evolution direction (future iteration; not part of current watcher core):
- Add a lightweight structure that only carries “listen socket ownership” (similar to `ITcpStream`):
  - `struct ITcpListenSocket { kind tag + virtual ~dtor; /* holds fd/SOCKET */ };`
  - Evolve accept watcher factory from `intptr_t listen_handle` to `std::unique_ptr<ITcpListenSocket> listen_socket`.
- Provide a unified factory (dispatch internally by platform/backend):
  - `createTcpListenSocket(worker, IpEndpoint local, TcpListenOption option) -> std::unique_ptr<ITcpListenSocket>`
  - So examples/business code no longer needs `#include <sys/socket.h>` or Windows Winsock details.

At the current stage (to push watcher unification), the accept watcher is changed to **create the listen socket internally**, and examples/business code should no longer directly call `socket/bind/listen`.

### 4.5 `ITcpAcceptWatcher` (Server)

Responsibility: listen socket readiness → accept loop → callback for each new connection.

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

Implementation notes:
- Must use a non-blocking accept loop (POSIX: `accept4` + `EAGAIN/EINTR`).
- When calling `on_accept`, the accepted client socket must be non-blocking (POSIX: `accept4(..., SOCK_NONBLOCK|SOCK_CLOEXEC)`).
- Error handling: for `ERR/HUP` on the listen socket readiness, prefer `getsockopt(SO_ERROR)` to extract the real error code instead of substituting constants.
- Listen socket creation and ownership:
  - `createTcpAcceptWatcher(worker, option, ...)` creates the listen socket internally (`socket/bind/listen`), sets non-blocking, best-effort applies listen-related options (bind address from `option.local_bind`).
  - On success, the accept watcher owns the listen socket; it closes it in stop/destructor/close paths.

---

### 4.6 `ITcpConnectWatcher` (Client)

Responsibility: initiate a non-blocking connect, and transfer the connected socket to upper layers on success (usually to create an `ITcpConnectionWatcher`).

```cpp
namespace RopHive::Network {

class ITcpConnectWatcher : public IWorkerWatcher {
public:
    using OnConnected = std::function<void(std::unique_ptr<ITcpStream> stream)>;
    using OnError     = std::function<void(int err)>;

    virtual ~ITcpConnectWatcher() = default;

    // cancel(): explicitly cancel the connect attempt and release the handle; may be called multiple times.
    // Semantics: after cancel(), on_connected will not fire anymore; whether on_error fires must be unified:
    // - Recommended: cancel() does not trigger on_error (upper layers decide whether it is an error).
    virtual void cancel() = 0;
};

std::shared_ptr<ITcpConnectWatcher>
createTcpConnectWatcher(IOWorker& worker,
                        TcpConnectOption option,
                        ITcpConnectWatcher::OnConnected on_connected,
                        ITcpConnectWatcher::OnError on_error = {});

} // namespace RopHive::Network
```

Implementation notes:
- POSIX: `socket` + `O_NONBLOCK` + `connect`; after `EINPROGRESS`, watch for writable and then use `getsockopt(SO_ERROR)` to decide success/failure.
- Windows (IOCP): prefer `ConnectEx` (avoid “non-blocking connect + WSAEventSelect”). `cancel()` should quickly abort pending connect (commonly via `closesocket` to force completion, and/or `CancelIoEx/CancelIo`).

### 4.7 `ITcpConnectionWatcher` (Connected I/O)

Responsibility: drive read/write/close events for a connected socket via the worker backend; provide a minimal send abstraction.

```cpp
namespace RopHive::Network {

class ITcpConnectionWatcher : public IWorkerWatcher {
public:
    using OnRecv  = std::function<void(std::string_view chunk)>;
    using OnClose = std::function<void()>;
    using OnError = std::function<void(int err)>;
    using OnSendReady = std::function<void()>;

    virtual ~ITcpConnectionWatcher() = default;

    // Sending model: trySend (no protocol-layer write buffering inside watcher).
    struct TrySendResult {
        // Bytes accepted/advanced by this call.
        // - POSIX (readiness): bytes written into kernel send buffer.
        // - Windows (IOCP): bytes submitted into an async send op, or 0 if would-block/busy.
        size_t n = 0;

        // True if the connection cannot currently accept more bytes:
        // - POSIX: EAGAIN/EWOULDBLOCK
        // - Windows IOCP: send pipeline is busy/backpressured
        bool would_block = false;

        // 0 means no error.
        int err = 0;
    };

    // trySend(): try to push bytes into kernel / submit async send.
    // - Does not guarantee all `data` is sent in one call (may advance partially or not at all).
    // - If would_block=true, upper layer should buffer remaining bytes and wait for OnSendReady.
    virtual TrySendResult trySend(std::string_view data) = 0;

    // shutdownWrite(): half-close write side.
    virtual void shutdownWrite() = 0;

    // close(): actively close immediately.
    virtual void close() = 0;

    // Note: under trySend model, watcher does not maintain protocol-layer write buffering,
    // therefore no pendingWriteBytes() API is provided.
};

std::shared_ptr<ITcpConnectionWatcher>
createTcpConnectionWatcher(IOWorker& worker,
                           TcpConnectionOption option,
                           std::unique_ptr<ITcpStream> connected_stream,
                           ITcpConnectionWatcher::OnRecv on_recv,
                           ITcpConnectionWatcher::OnClose on_close = {},
                           ITcpConnectionWatcher::OnError on_error = {},
                           ITcpConnectionWatcher::OnSendReady on_send_ready = {});

} // namespace RopHive::Network
```

Implementation notes:
- POSIX readiness:
  - `EPOLLIN/POLLIN` → `recv` loop until `EAGAIN`; `n==0` means peer closed.
  - `EPOLLOUT/POLLOUT` → allow further `trySend`; implementation should trigger `OnSendReady` once on the blocked→ready edge.
  - Prefer subscribing to `RDHUP/HUP/ERR` and handle half-close/close (Linux can use `EPOLLRDHUP`).
- Windows (IOCP) completion:
  - Common pattern: always keep one pending recv + serialize sends via overlapped `WSASend`.
  - Each I/O op needs its own `OVERLAPPED` and buffer; completion callback submits the next recv/send.
  - close/cancel must handle pending I/O: closing the socket typically causes pending ops to complete with errors; the implementation must suppress callbacks after stop.

---

## 5. Platform Implementation Strategy (Landing Path)

### 5.1 Linux (epoll/poll)

- Existing implementations can be used as “semantic reference / prototype validation” (but must be refactored to satisfy unified interfaces and cancel semantics):
  - `platform/linux/network/watcher/tcp_accept_watcher.*`
  - `platform/linux/network/watcher/tcp_connect_watcher.*`
  - `platform/linux/network/watcher/tcp_connection_watcher.*`
- During unification refactor:
  - Provide `Network::createTcp*Watcher(...)` factories: dispatch to epoll/poll implementation based on `worker.options().io_backend` or `worker.core()->backendType()`.
  - Improve error codes: for accept/connect errors, prefer `SO_ERROR` to get the real error.
  - Clarify cancel behavior: connect watcher `cancel()` should immediately close the fd (current “stop without close” makes timeout/cancel harder).

### 5.2 macOS (kqueue/poll)

- Use `KqueueReadinessEventSource` and `PollReadinessEventSource` from `platform/macos/schedule/kqueue_backend.h`.
- POSIX socket APIs are similar to Linux (`recv/send`, `O_NONBLOCK`, `getsockopt(SO_ERROR)`).
- Note:
  - kqueue distinguishes read/write by `(ident=fd, filter=EVFILT_READ/EVFILT_WRITE)`. Usually you register two sources for one fd, or switch filters/flags dynamically inside one source.

### 5.3 Windows (IOCP)

- Use `IocpHandleCompletionEventSource` from `platform/windows/schedule/iocp_backend.h`:
  - On arm(), associate the socket with IOCP (`associateHandle`)
  - On dispatch(), route completion events into the watcher state machine
- Core APIs (recommended):
  - server accept: `AcceptEx`
  - client connect: `ConnectEx`
  - connection recv/send: `WSARecv/WSASend`
- Key implementation requirements:
  - Each connection must have a `ConnectionState` (socket, read/write buffers, overlapped structs, close flags).
  - stop/cancel/close must be idempotent, and must suppress callbacks after stop (avoid use-after-free).

---

## 6. Integration with Hive/IOWorker (Recommended Usage)

### 6.1 Server (Single Acceptor + Dispatch Connections to Workers)

Recommended: dedicate one worker to accept, and dispatch accepted client sockets to target workers via `Hive::postToWorker(...)`. Create the `ConnectionWatcher` on the target worker:

1) On accept worker: `AcceptWatcher` callback receives `std::unique_ptr<ITcpStream> stream`
2) Choose a target worker (round-robin/hash/load)
3) `hive.postToWorker(target, [stream = std::move(stream)]() mutable { createTcpConnectionWatcher(..., std::move(stream), ...)->start(); })`

This binds each connection’s I/O watcher naturally to the worker that will process its workload.

### 6.2 Client (resolve → connect → connection)

- DNS/resolve must run on a compute pool / dedicated resolver (avoid blocking I/O worker).
- Connect watcher only accepts `IpEndpoint` (resolved IP + port).

---

## 7. Coverage Checklist (Low-level Scenarios That Must Be Covered)

To ensure “unified watchers cover all low-level TCP stages on PC”, implementations must cover:
- listen: accept loop (`EINTR/EAGAIN`), handle high-concurrency accept bursts
- connect: non-blocking connect success/failure (`SO_ERROR` / Windows completion error)
- connection:
  - read: continuously read until `EAGAIN` / completion, handle `n==0` peer close correctly
  - write: send buffering + writable readiness/completion, support partial write and `EAGAIN`
  - close: peer close and local close must be observable (`on_close` or `on_error`, must be consistent)

Recommended extra coverage (can be a later iteration):
- peer/local endpoint capture (accept/connect callback provides peer info)
- explicit cancel (connect/connection), and no callbacks after stop

---

## 8. Future Extension Points (Not in Watcher Core)

Keep watchers “thin but hard”, and move these capabilities to upper-layer components:
- `DnsResolver`: cross-platform resolve (thread-pool wrapper of `getaddrinfo` / Windows `GetAddrInfoEx`)
- `TcpClient`: reconnect/backoff/multi-address attempts (Happy Eyeballs)
- `TcpServer`: multi-acceptor (Linux `SO_REUSEPORT`) / sharding / connection pools
- `TlsConnection`: TLS wrapper over `ITcpConnectionWatcher`
