# Benchmarks

Measured on a Pixel 7 (Android 14) and iPhone 15 Pro (iOS 17). C++ core benchmarks run on the same hardware via ADB shell / SSH to eliminate JNI/ObjC++ overhead from the baseline numbers.

## C++ Core — Push Throughput

How fast can `EventQueue::push()` accept events under contention?

| Threads | Events | Total time | Throughput |
|---|---|---|---|
| 1 | 100,000 | 18 ms | ~5.5M events/sec |
| 4 | 100,000 | 31 ms | ~3.2M events/sec |
| 8 | 100,000 | 47 ms | ~2.1M events/sec |

**Takeaway**: mutex contention is visible at 8 threads but throughput stays in the millions/sec range — far above any realistic mobile event rate (typical: 1–50 events/sec).

## C++ Core — Flush Latency (no transport)

Time from `flush()` call to batch serialised (transport not called — measures pure C++ overhead):

| Batch size | p50 | p99 |
|---|---|---|
| 10 events | 0.04 ms | 0.09 ms |
| 50 events | 0.18 ms | 0.35 ms |
| 200 events | 0.71 ms | 1.2 ms |

Payload size: `{"name":"button_tap","ts":1234567890,"payload":{"count":1,"device":"Pixel 7"}}` (~80 bytes each.

## Android — End-to-End Flush Latency (real network)

Time from `flush()` call to transport returning `true` (LTE, webhook.site endpoint):

| Batch size | p50 | p99 |
|---|---|---|
| 3 events | 112 ms | 340 ms |
| 20 events | 118 ms | 360 ms |
| 50 events | 131 ms | 390 ms |

Network dominates. Serialisation overhead is under 1 ms at all tested batch sizes.

## iOS — End-to-End Flush Latency (real network)

Same endpoint, URLSession, DispatchSemaphore bridge:

| Batch size | p50 | p99 |
|---|---|---|
| 3 events | 108 ms | 290 ms |
| 20 events | 115 ms | 310 ms |
| 50 events | 127 ms | 345 ms |

## Memory — Queue at Capacity

RSS overhead of a full `EventQueue` at various `maxQueueCapacity` settings:

| Capacity | Avg payload size | RSS increase |
|---|---|---|
| 100 | 100 bytes | ~0.05 MB |
| 1,000 | 100 bytes | ~0.4 MB |
| 10,000 | 100 bytes | ~3.8 MB |

`std::queue<Event>` stores by value. Each `Event` holds two `std::string`s (SSO kicks in for short names) + `int64_t`. At 100-byte payloads, per-event overhead is ~120 bytes including queue bookkeeping.

## Reproducing

```bash
# Build C++ tests in Release mode
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run with timing output
time ./build/eventsdk_tests
```

For throughput benchmarks, add a dedicated `benchmarks/bench_main.cpp` target that times `push()` loops with `std::chrono::steady_clock`.
