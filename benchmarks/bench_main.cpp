#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "Config.h"
#include "EventQueue.h"
#include "EventStore.h"
#include "FlushManager.h"

using namespace eventsdk;
using Clock = std::chrono::steady_clock;

static double elapsedMs(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static Event makeEvent(int i) {
    return {"button_tap",
            "{\"count\":" + std::to_string(i) + ",\"device\":\"Pixel 7\"}",
            static_cast<int64_t>(i)};
}

// ── 1. Push throughput ────────────────────────────────────────────────────────

static void bench_push_throughput(int numThreads, int eventsPerThread) {
    const int total = numThreads * eventsPerThread;
    EventQueue q(static_cast<std::size_t>(total + 1));

    auto t0 = Clock::now();
    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&q, t, eventsPerThread]() {
            for (int i = 0; i < eventsPerThread; ++i)
                q.push(makeEvent(t * eventsPerThread + i));
        });
    }
    for (auto& th : threads) th.join();
    double ms = elapsedMs(t0);

    printf("push throughput  threads=%-2d  events=%6d  time=%6.1fms  rate=%7.0f ev/s\n",
           numThreads, total, ms, total / (ms / 1000.0));
}

// ── 2. Flush latency (no-op transport) ───────────────────────────────────────

static void bench_flush_latency(int batchSize, int runs) {
    Config cfg;
    cfg.endpoint        = "http://localhost";
    cfg.batchSize       = static_cast<std::size_t>(batchSize);
    cfg.maxQueueCapacity = static_cast<std::size_t>(batchSize * runs + 1);

    EventQueue q(cfg.maxQueueCapacity);
    FlushManager fm(q, cfg);
    fm.setTransport([](const std::string&, const std::string&) { return true; });

    for (int i = 0; i < batchSize * runs; ++i) q.push(makeEvent(i));

    std::vector<double> latencies;
    latencies.reserve(static_cast<std::size_t>(runs));

    for (int r = 0; r < runs; ++r) {
        // Refill so every flush has batchSize events
        for (int i = 0; i < batchSize; ++i) q.push(makeEvent(i));
        auto t0 = Clock::now();
        fm.flush();
        latencies.push_back(elapsedMs(t0));
    }

    // Drain what was pre-loaded above
    fm.flush();

    std::sort(latencies.begin(), latencies.end());
    double p50 = latencies[static_cast<std::size_t>(runs * 0.50)];
    double p99 = latencies[static_cast<std::size_t>(runs * 0.99)];
    printf("flush latency    batchSize=%-4d  runs=%-4d  p50=%6.3fms  p99=%6.3fms\n",
           batchSize, runs, p50, p99);
}

// ── 3. Queue memory cost ──────────────────────────────────────────────────────

static void bench_queue_memory(int capacity) {
    // Rough in-process estimate: allocate, fill, report
    // Real RSS requires /proc/self/status or task_info — not cross-platform.
    // We report bytes of payload held in the queue as a lower bound.
    EventQueue q(static_cast<std::size_t>(capacity));
    Event sample = makeEvent(0);
    std::size_t eventBytes = sample.name.size() + sample.payload.size() + sizeof(int64_t);

    for (int i = 0; i < capacity; ++i) q.push(makeEvent(i));

    std::size_t estBytes = static_cast<std::size_t>(capacity) * eventBytes;
    printf("queue memory     capacity=%-6d  payload_est=%zuB  (~%.1f KB)\n",
           capacity, estBytes, estBytes / 1024.0);
}

// ── 4. At-least-once persist + load round-trip ───────────────────────────────

static void bench_store_roundtrip(int numEvents) {
    system("mkdir -p /tmp/eventsdk_bench && rm -f /tmp/eventsdk_bench/event_store.dat");
    EventStore store("/tmp/eventsdk_bench");

    std::vector<Event> events;
    events.reserve(static_cast<std::size_t>(numEvents));
    for (int i = 0; i < numEvents; ++i) events.push_back(makeEvent(i));

    auto t0 = Clock::now();
    store.persist(events);
    double persistMs = elapsedMs(t0);

    auto t1 = Clock::now();
    auto loaded = store.load();
    double loadMs = elapsedMs(t1);

    store.clear();
    printf("store roundtrip  events=%-5d  persist=%6.2fms  load=%6.2fms  verified=%s\n",
           numEvents, persistMs, loadMs,
           loaded.size() == static_cast<std::size_t>(numEvents) ? "OK" : "FAIL");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    printf("=== EventSDK Benchmarks ===\n\n");

    printf("-- Push throughput (single vs multi-thread) --\n");
    bench_push_throughput(1,  100000);
    bench_push_throughput(4,   25000);
    bench_push_throughput(8,   12500);
    printf("\n");

    printf("-- Flush latency (no-op transport, 1000 runs each) --\n");
    bench_flush_latency(10,  1000);
    bench_flush_latency(50,  1000);
    bench_flush_latency(200, 1000);
    printf("\n");

    printf("-- Queue memory estimate --\n");
    bench_queue_memory(100);
    bench_queue_memory(1000);
    bench_queue_memory(10000);
    printf("\n");

    printf("-- EventStore persist + load round-trip --\n");
    bench_store_roundtrip(100);
    bench_store_roundtrip(1000);
    bench_store_roundtrip(5000);
    printf("\n");

    return 0;
}
