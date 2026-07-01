# Benchmarks

Measured on macOS (Apple M-series, Release build `-O2`). Run via `./build/eventsdk_bench`.
C++ core only — no JNI/ObjC++ overhead, no network. Network latency numbers are separate (see end-to-end section).

## Reproducing

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/eventsdk_bench
```

---

## Push Throughput

`EventQueue::push()` under thread contention. Queue capacity set high enough that no drops occur.

| Threads | Events | Time | Throughput |
|---|---|---|---|
| 1 | 100,000 | 7.2 ms | **13.8M ev/sec** |
| 4 | 100,000 | 19.7 ms | 5.1M ev/sec |
| 8 | 100,000 | 17.4 ms | 5.7M ev/sec |

Single-threaded throughput is 13.8M ev/sec — far above any realistic mobile event rate (typical: 1–100 ev/sec). Mutex contention at 4–8 threads reduces throughput to ~5M ev/sec, still not a bottleneck in practice.

---

## Flush Latency (no-op transport)

Pure C++ overhead: drain queue → serialise JSON batch → call transport (immediate return). Measures `FlushManager::flush()` wall time excluding any network.

| Batch size | p50 | p99 |
|---|---|---|
| 10 events | 0.003 ms | 0.005 ms |
| 50 events | 0.013 ms | 0.030 ms |
| 200 events | 0.052 ms | 0.081 ms |

Serialisation is sub-millisecond even at 200 events. Network round-trip (~100–400ms on LTE) dominates end-to-end flush time entirely.

---

## Queue Memory

Payload estimate (lower bound — excludes `std::queue` node overhead and `std::string` SSO bookkeeping):

| Capacity | Payload est. | Approx RSS |
|---|---|---|
| 100 | 4.7 KB | ~10 KB |
| 1,000 | 46.9 KB | ~100 KB |
| 10,000 | 468.8 KB | ~1 MB |

Sample payload: `{"count":1,"device":"Pixel 7"}` (~48 bytes). Default `maxQueueCapacity=1000` costs under 100 KB — negligible on any modern device.

---

## EventStore Persist + Load (AtLeastOnce mode)

Tab-delimited append to disk, then full parse on load. Measured on local SSD.

| Events | persist() | load() |
|---|---|---|
| 100 | 0.25 ms | 0.08 ms |
| 1,000 | 0.57 ms | 0.49 ms |
| 5,000 | 2.02 ms | 2.08 ms |

At the default `batchSize=20`, persist() costs ~0.03 ms per flush — well within acceptable latency for the AtLeastOnce path. load() is called once on startup only.

---

## End-to-End Flush Latency (estimated, LTE)

C++ serialisation is negligible. End-to-end latency is dominated by HTTP round-trip.
Measured manually against webhook.site on LTE (Pixel 7 + iPhone 15):

| Batch size | ~p50 | ~p99 |
|---|---|---|
| 3 events | 110 ms | 330 ms |
| 20 events | 120 ms | 360 ms |

Timer-based flush (`flushIntervalSeconds=30`) means this overhead is invisible to users — it happens entirely in the background.
