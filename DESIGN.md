# Design Notes — mobile-events-sdk

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application Layer                         │
│           Android (Java/Kotlin)          iOS (Swift)            │
└────────────────────────┬─────────────────────────┬─────────────┘
                         │ JNI                      │ direct call
┌────────────────────────▼─────────────────────────▼─────────────┐
│                     Platform Bridge Layer                        │
│       EventSDK_JNI.cpp (Android)      EventSDK.mm (iOS)        │
└────────────────────────────────┬────────────────────────────────┘
                                 │ C++ includes
┌────────────────────────────────▼────────────────────────────────┐
│                       C++ Core (C++17)                           │
│   EventQueue        EventStore        FlushManager     Config   │
└────────────────────────────────┬────────────────────────────────┘
                                 │ std::function callback
┌────────────────────────────────▼────────────────────────────────┐
│              Pluggable Transport  (caller-supplied)              │
│   HttpURLConnection (Android)        URLSession (iOS)           │
└─────────────────────────────────────────────────────────────────┘
```

The C++ core has zero platform dependencies — it compiles identically on Android and iOS. HTTP lives entirely in the caller-supplied transport function.

---

## Threading Model

Three logical threads interact with the SDK:

```
Caller Thread              Timer Thread                Transport Thread
      │                        │                              │
      │  logEvent(name, json)  │                              │
      │──►push(event)          │                              │
      │   [EventQueue.mutex_]  │                              │
      │                        │  wait_for(interval, stopCv)  │
      │                        │◄────────────────────────────  │
      │                        │  flush() — drain batch        │
      │                        │──►transport_(url, body)──────►│ HTTP POST
      │                        │                              │◄─ bool
      │                        │  [loop or break if stopped]  │
      │                        │                              │
      │  shutdown()            │                              │
      │──►stopTimer_ = true    │                              │
      │──►timerCv_.notify_one()│                              │
      │──►timerThread_.join()◄─┘                              │
```

Key details:

- **`EventQueue`** guards its internal `std::queue` with `mutex_` on every push/drain. This lock is held only for queue manipulation (microseconds), not during the transport call.
- **`FlushManager::startTimer()`** is idempotent — guarded by `timerThread_.joinable()`. Safe to call multiple times.
- **`condition_variable::wait_for`** wakes early on `stopTimer_`, so shutdown never waits a full flush interval.
- **Android**: the timer fires on a native `std::thread`. Because it calls back into Java, `attachIfNeeded()` calls `AttachCurrentThread` and the transport lambda detaches after the call completes.
- **iOS**: `EventSDK.mm` wraps the C++ transport lambda in an Objective-C block (copied via `[transport copy]`) so ARC keeps the block alive for the lifetime of the timer thread.

---

## EventQueue

Bounded FIFO backed by `std::queue<Event>`. When at capacity, the oldest event is dropped (drop-head eviction) to bound memory usage. `drain(n)` returns up to *n* events and removes them atomically under the same lock.

This means events can be lost if the queue fills faster than flush empties it. The threshold is configurable (`maxQueueCapacity`, default 1000). For higher-durability use cases, pair with `EventStore`.

---

## EventStore

Tab-delimited line-per-event file:

```
<timestampMs>\t<name>\t<payload>
```

Tabs, newlines, and backslashes inside fields are backslash-escaped, so no field ever contains a literal tab. This avoids embedding JSON inside JSON (the payload field is raw JSON) while keeping the format trivially parseable without a JSON library.

`persist()` appends; `load()` reads and parses; `clear()` truncates to zero. The store is intentionally not thread-safe — callers (the platform bridges) are responsible for sequencing store access.

---

## Flush Strategy

Two triggers, one code path (`FlushManager::flush()`):

### 1. Threshold-based (autoFlush)
`push()` checks `queue_.size() >= threshold` after every insert. Threshold defaults to `batchSize` when `autoFlushThreshold` is 0. This path runs synchronously on the caller's thread — the transport call blocks the caller until the HTTP round-trip completes. Suitable for low-frequency events.

### 2. Timer-based
A single background `std::thread` sleeps via `condition_variable::wait_for`. On wake it calls `flush()` then loops. Timer starts in `setTransport()` (guaranteed transport exists when timer fires) and stops cleanly in `stopTimer()` / destructor.

### Delivery guarantee: at-most-once

`drain()` removes events from the queue before the transport call. If the transport returns `false` (network error, non-2xx), events are discarded — not retried. This keeps the core simple and stateless. The trade-off:

| Property            | This SDK          | Alternative              |
|---------------------|-------------------|--------------------------|
| Delivery guarantee  | at-most-once      | at-least-once            |
| Duplicate risk      | None              | Retries may double-send  |
| State machine cost  | Zero              | Retry queue + backoff    |
| Suitable for        | Analytics events  | Financial/critical data  |

For at-least-once, persist the batch to `EventStore` before calling the transport, then call `store.clear()` only on success.

---

## JNI Bridge (Android)

### `JNI_OnLoad`
Caches `JavaVM*` in `gJvm`. The JVM pointer is safe to store globally and read from any thread without a lock.

### Global ref for the transport object
`NewGlobalRef(transport)` prevents the GC from collecting the Java `Transport` implementation while the C++ timer thread holds a reference to it. Matching `DeleteGlobalRef` in `nativeShutdown` prevents a leak.

### `AttachCurrentThread` / `DetachCurrentThread`
The timer thread is a native `std::thread` — the JVM does not know about it. `attachIfNeeded()` attaches only when needed and the lambda detaches immediately after the Java call. Failing to detach leaks a thread descriptor in the JVM.

### `jmethodID` caching
`GetMethodID` is cached once in `nativeSetTransport`. Calling it on every flush would be valid but wasteful — method lookup involves class hierarchy traversal.

### Known limitation: `gStateMutex` held during flush
`nativeFlush` and the timer both hold `gStateMutex` for the full transport call. This means `logEvent` blocks during an HTTP round-trip. The fix is to snapshot `shared_ptr<SDKState>` under the lock, release it, then call the transport outside — a straightforward refactor deferred to keep the demo code readable.

---

## ObjC++ Bridge (iOS)

`EventSDK.mm` uses an `.mm` extension so the compiler treats it as Objective-C++. This allows direct `#include` of C++ headers with no bridging overhead.

### Block lifetime across C++ lambda
```objc
EventSDKTransport copied = [transport copy];  // ARC retains heap copy
```
The C++ lambda captures `copied` by value. ARC's retain/release on the block object ensures it stays alive as long as the lambda exists (i.e., as long as `FlushManager` holds the `Transport` std::function). Without the copy, the original block variable would be stack-allocated and undefined after `setTransport:` returns.

### Completion on main queue
`flushWithCompletion:` dispatches the callback to `dispatch_get_main_queue()` so callers can safely update UI in the completion handler without an explicit `DispatchQueue.main.async`.

---

## Failure Modes

| Failure | AtMostOnce behavior | AtLeastOnce behavior | Detection |
|---|---|---|---|
| Transport returns false | Batch silently discarded | Batch stays on disk; re-queued on restart | `HealthMetrics.transportFailures` |
| Process killed during flush (pre-send) | Events lost | Events on disk; recovered via `loadPersistedEvents()` | Drop in event count at server |
| Process killed during flush (post-send, pre-clear) | — | Events re-sent on restart (duplicate delivery) | Server-side deduplication by event ts |
| Disk full on `persist()` | No effect (not called) | `persist()` throws; events remain in-memory queue | `std::ofstream` failure (currently silent) |
| Queue at `maxQueueCapacity` | Oldest event dropped | Oldest event dropped | `HealthMetrics.eventsDropped` |
| `flushIntervalSeconds = 0` | Timer thread never starts | Same | Config validation |
| `setTransport` never called | `flush()` returns events to queue | Same | `HealthMetrics.transportFailures` (0, no calls) |
| Duplicate `init()` call | Previous state destroyed | Previous state destroyed + store reloaded | Avoid via app lifecycle |

### Known limitation: `gStateMutex` held during flush (Android)

`nativeFlush` and the timer both hold `gStateMutex` for the full transport call. This means `logEvent()` blocks during an HTTP round-trip.

**Fix** (not yet implemented): snapshot `std::shared_ptr<SDKState>` under the lock, release it, then call transport outside:
```cpp
std::shared_ptr<SDKState> snap;
{
    std::lock_guard<std::mutex> lock(gStateMutex);
    snap = gState;            // O(1), no copy
}
snap->flushManager->flush();  // lock released, logEvent() unblocked
```

---

## What Was Left Out (and Why)

| Feature | Reason omitted |
|---|---|
| HTTP retry + backoff | Adds state machine complexity; analytics rarely need it |
| Encryption at rest | EventStore is a local cache, not a secrets store |
| Batching across restarts | Load-on-init + re-queue is a one-liner extension to the current store API |
| Compression (gzip) | Transport is pluggable — caller can compress in the transport function |
| Event deduplication | At-most-once already avoids duplicates; dedup needs a seen-set with bounded memory |
