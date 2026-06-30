#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "Config.h"
#include "EventQueue.h"
#include "EventStore.h"
#include "FlushManager.h"

using namespace eventsdk;

static int passed = 0;
static int failed = 0;

#define ASSERT(cond, msg)                                          \
    do {                                                           \
        if (cond) { std::cout << "[PASS] " << (msg) << '\n'; ++passed; } \
        else      { std::cout << "[FAIL] " << (msg) << '\n'; ++failed; } \
    } while (0)

// ── EventQueue ────────────────────────────────────────────────────────────────

void test_queue_basic() {
    EventQueue q(5);
    ASSERT(q.empty(), "queue starts empty");

    q.push({"tap", "{}", 1000});
    ASSERT(q.size() == 1, "size == 1 after push");

    auto batch = q.drain(10);
    ASSERT(batch.size() == 1,         "drain returns 1 event");
    ASSERT(batch[0].name == "tap",    "event name preserved");
    ASSERT(batch[0].timestampMs == 1000, "timestamp preserved");
    ASSERT(q.empty(),                 "queue empty after drain");
}

void test_queue_drop_oldest_at_capacity() {
    EventQueue q(3);
    q.push({"e1", "{}", 1});
    q.push({"e2", "{}", 2});
    q.push({"e3", "{}", 3});
    q.push({"e4", "{}", 4});   // e1 evicted

    ASSERT(q.size() == 3, "capacity capped at 3");
    auto batch = q.drain(10);
    ASSERT(batch[0].name == "e2", "oldest (e1) dropped, e2 is first");
    ASSERT(batch[2].name == "e4", "newest (e4) preserved");
}

void test_queue_partial_drain() {
    EventQueue q(10);
    for (int i = 0; i < 5; ++i) q.push({"e", "{}", i});

    auto batch = q.drain(3);
    ASSERT(batch.size() == 3, "drain respects maxCount");
    ASSERT(q.size() == 2,     "remaining events stay in queue");
}

void test_queue_thread_safety() {
    EventQueue q(10000);
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&q, i]() {
            for (int j = 0; j < 100; ++j)
                q.push({"e", "{}", static_cast<int64_t>(i * 100 + j)});
        });
    }
    for (auto& t : threads) t.join();
    ASSERT(q.size() == 1000, "all 1000 events pushed across 10 threads");
}

// ── EventStore ────────────────────────────────────────────────────────────────

static const std::string kTestDir = "/tmp/eventsdk_test";

void setup_store_dir() {
    system(("mkdir -p " + kTestDir).c_str());
    system(("rm -f "   + kTestDir + "/event_store.dat").c_str());
}

void test_store_persist_and_load() {
    setup_store_dir();
    EventStore store(kTestDir);

    std::vector<Event> events = {
        {"purchase",      "{\"item\":\"sword\",\"price\":9.99}", 1000},
        {"session_start", "null",                                2000},
    };
    store.persist(events);

    auto loaded = store.load();
    ASSERT(loaded.size() == 2,                    "load returns 2 events");
    ASSERT(loaded[0].name == "purchase",          "first event name preserved");
    ASSERT(loaded[0].timestampMs == 1000,         "first event timestamp preserved");
    ASSERT(loaded[0].payload == events[0].payload,"first event payload preserved");
    ASSERT(loaded[1].name == "session_start",     "second event name preserved");
    ASSERT(loaded[1].payload == "null",           "null payload preserved");

    store.clear();
    ASSERT(store.load().empty(), "store empty after clear");
}

void test_store_payload_with_special_chars() {
    setup_store_dir();
    EventStore store(kTestDir);

    // payload contains tab and newline — must survive round-trip
    Event e{"click", "{\"label\":\"line1\\nline2\"}", 9999};
    store.persist({e});

    auto loaded = store.load();
    ASSERT(loaded.size() == 1,              "special-char event loaded");
    ASSERT(loaded[0].name == "click",       "name preserved");
    ASSERT(loaded[0].payload == e.payload,  "payload with escapes preserved");
    store.clear();
}

void test_store_append_across_calls() {
    setup_store_dir();
    EventStore store(kTestDir);

    store.persist({{"e1", "{}", 1}});
    store.persist({{"e2", "{}", 2}});

    auto loaded = store.load();
    ASSERT(loaded.size() == 2,       "two separate persist calls both saved");
    ASSERT(loaded[0].name == "e1",   "e1 first");
    ASSERT(loaded[1].name == "e2",   "e2 second");
    store.clear();
}

// ── FlushManager ─────────────────────────────────────────────────────────────

static Config makeConfig(std::size_t batchSize = 5) {
    Config cfg;
    cfg.endpoint         = "http://example.com/events";
    cfg.batchSize        = batchSize;
    cfg.maxQueueCapacity = 100;
    return cfg;
}

void test_flush_no_transport_returns_events() {
    auto cfg = makeConfig();
    EventQueue q(cfg.maxQueueCapacity);
    q.push({"tap", "{}", 1000});

    FlushManager fm(q, cfg);
    bool cbCalled = false;
    fm.flush([&](bool ok, const std::string& err) {
        ASSERT(!ok,         "flush fails without transport");
        ASSERT(!err.empty(), "error message provided");
        cbCalled = true;
    });
    ASSERT(cbCalled,              "callback invoked");
    ASSERT(fm.pendingCount() == 1, "event returned to queue when no transport");
}

void test_flush_with_transport_succeeds() {
    auto cfg = makeConfig();
    EventQueue q(cfg.maxQueueCapacity);
    q.push({"tap",      "{}",              1000});
    q.push({"purchase", "{\"price\":9.99}", 2000});

    FlushManager fm(q, cfg);
    std::string capturedUrl, capturedBody;
    fm.setTransport([&](const std::string& url, const std::string& body) {
        capturedUrl  = url;
        capturedBody = body;
        return true;
    });

    bool success = false;
    fm.flush([&](bool ok, const std::string&) { success = ok; });

    ASSERT(success,                                          "flush succeeds");
    ASSERT(capturedUrl == cfg.endpoint,                      "correct endpoint");
    ASSERT(capturedBody.find("tap") != std::string::npos,   "body contains event name");
    ASSERT(capturedBody.find("9.99") != std::string::npos,  "body contains payload field");
    ASSERT(q.empty(),                                        "queue drained after flush");
}

void test_flush_respects_batch_size() {
    auto cfg = makeConfig(2);
    EventQueue q(cfg.maxQueueCapacity);
    for (int i = 0; i < 5; ++i) q.push({"e", "{}", i});

    FlushManager fm(q, cfg);
    fm.setTransport([](const std::string&, const std::string&) { return true; });

    fm.flush();
    ASSERT(q.size() == 3, "first flush drains batchSize=2, 3 remain");

    fm.flush();
    ASSERT(q.size() == 1, "second flush drains 2 more, 1 remains");
}

void test_flush_empty_queue_is_noop() {
    auto cfg = makeConfig();
    EventQueue q(cfg.maxQueueCapacity);
    FlushManager fm(q, cfg);

    bool cbCalled = false;
    fm.flush([&](bool ok, const std::string&) {
        ASSERT(ok,    "flush on empty queue reports success");
        cbCalled = true;
    });
    ASSERT(cbCalled, "callback invoked for empty flush");
}

// ── FlushManager::push + auto-flush ──────────────────────────────────────────

void test_autoflush_triggers_at_threshold() {
    auto cfg = makeConfig(5);
    cfg.autoFlush          = true;
    cfg.autoFlushThreshold = 3;

    EventQueue q(cfg.maxQueueCapacity);
    FlushManager fm(q, cfg);

    int flushCount = 0;
    fm.setTransport([&](const std::string&, const std::string&) {
        ++flushCount;
        return true;
    });

    fm.push({"e1", "{}", 1});
    fm.push({"e2", "{}", 2});
    ASSERT(flushCount == 0, "no auto-flush below threshold");

    fm.push({"e3", "{}", 3});   // threshold reached
    ASSERT(flushCount == 1,  "auto-flush triggered at threshold=3");
    ASSERT(q.empty(),        "queue drained after auto-flush");
}

void test_autoflush_disabled() {
    auto cfg = makeConfig(5);
    cfg.autoFlush = false;

    EventQueue q(cfg.maxQueueCapacity);
    FlushManager fm(q, cfg);

    bool flushed = false;
    fm.setTransport([&](const std::string&, const std::string&) {
        flushed = true;
        return true;
    });

    for (int i = 0; i < 10; ++i) fm.push({"e", "{}", i});
    ASSERT(!flushed,       "no auto-flush when disabled");
    ASSERT(q.size() == 10, "all events remain in queue");
}

void test_timer_flush() {
    auto cfg = makeConfig(10);
    cfg.flushIntervalSeconds = 1;

    EventQueue q(cfg.maxQueueCapacity);
    FlushManager fm(q, cfg);

    std::atomic<int> flushCount{0};
    fm.setTransport([&](const std::string&, const std::string&) {
        ++flushCount;
        return true;
    });

    fm.push({"e1", "{}", 1});
    fm.push({"e2", "{}", 2});
    fm.startTimer();

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    fm.stopTimer();

    ASSERT(flushCount >= 1, "timer triggered at least one flush");
    ASSERT(q.empty(),       "queue drained by timer flush");
}

void test_autoflush_threshold_zero_uses_batchsize() {
    auto cfg = makeConfig(3);
    cfg.autoFlush          = true;
    cfg.autoFlushThreshold = 0;   // should fall back to batchSize=3

    EventQueue q(cfg.maxQueueCapacity);
    FlushManager fm(q, cfg);

    int flushCount = 0;
    fm.setTransport([&](const std::string&, const std::string&) {
        ++flushCount;
        return true;
    });

    fm.push({"e1", "{}", 1});
    fm.push({"e2", "{}", 2});
    ASSERT(flushCount == 0, "no flush at 2 events with batchSize=3");

    fm.push({"e3", "{}", 3});
    ASSERT(flushCount == 1, "auto-flush at batchSize=3 when threshold=0");
}

// ── Drop counter ─────────────────────────────────────────────────────────────

void test_queue_drop_counter_increments() {
    EventQueue q(2);
    q.push({"e1", "{}", 1});
    q.push({"e2", "{}", 2});
    ASSERT(q.dropCount() == 0, "no drops below capacity");

    q.push({"e3", "{}", 3});  // evicts e1
    ASSERT(q.dropCount() == 1, "drop counter = 1 after one eviction");

    q.push({"e4", "{}", 4});  // evicts e2
    ASSERT(q.dropCount() == 2, "drop counter = 2 after two evictions");
}

// ── HealthMetrics ─────────────────────────────────────────────────────────────

void test_health_metrics_emitted_after_flush() {
    auto cfg = makeConfig(5);
    EventQueue q(cfg.maxQueueCapacity);
    FlushManager fm(q, cfg);

    HealthMetrics received;
    bool cbCalled = false;
    fm.setHealthCallback([&](const HealthMetrics& m) {
        received = m;
        cbCalled = true;
    });

    fm.setTransport([](const std::string&, const std::string& body) {
        return true;
    });

    fm.push({"e1", "{}", 1});
    fm.push({"e2", "{}", 2});
    fm.flush();

    ASSERT(cbCalled,                          "health callback invoked after flush");
    ASSERT(received.flushCount == 1,          "flushCount = 1");
    ASSERT(received.transportFailures == 0,   "no transport failures");
    ASSERT(received.bytesSent > 0,            "bytesSent > 0 after successful flush");
    ASSERT(received.lastFlushLatencyMs >= 0,  "latency is non-negative");
    ASSERT(received.eventsQueued == 0,        "queue empty after flush");
}

void test_health_metrics_transport_failure() {
    auto cfg = makeConfig(5);
    EventQueue q(cfg.maxQueueCapacity);
    FlushManager fm(q, cfg);

    HealthMetrics received;
    fm.setHealthCallback([&](const HealthMetrics& m) { received = m; });
    fm.setTransport([](const std::string&, const std::string&) { return false; });

    fm.push({"e1", "{}", 1});
    fm.flush();

    ASSERT(received.transportFailures == 1, "transportFailures = 1 after failure");
    ASSERT(received.bytesSent == 0,         "bytesSent = 0 on failure");
}

void test_health_drop_count_reflected() {
    auto cfg = makeConfig(5);
    cfg.maxQueueCapacity = 2;
    EventQueue q(2);
    FlushManager fm(q, cfg);

    HealthMetrics received;
    fm.setHealthCallback([&](const HealthMetrics& m) { received = m; });
    fm.setTransport([](const std::string&, const std::string&) { return true; });

    q.push({"e1", "{}", 1});
    q.push({"e2", "{}", 2});
    q.push({"e3", "{}", 3});  // drops e1
    fm.flush();

    ASSERT(received.eventsDropped == 1,           "eventsDropped = 1 reflected in health");
    ASSERT(received.queueUtilizationPct == 0.0,   "utilization 0 after flush");
}

// ── AtLeastOnce ──────────────────────────────────────────────────────────────

void test_at_least_once_persists_before_send() {
    setup_store_dir();
    auto cfg = makeConfig(5);
    cfg.deliveryMode = DeliveryMode::AtLeastOnce;

    EventQueue q(cfg.maxQueueCapacity);
    EventStore store(kTestDir);
    store.clear();

    FlushManager fm(q, cfg);
    fm.setStore(&store);

    bool transportCalled = false;
    fm.setTransport([&](const std::string&, const std::string&) {
        // at point of transport call, events must already be on disk
        auto onDisk = store.load();
        ASSERT(!onDisk.empty(), "events persisted to disk before transport call");
        transportCalled = true;
        return true;
    });

    fm.push({"buy", "{\"item\":\"sword\"}", 1000});
    fm.flush();

    ASSERT(transportCalled, "transport was called");
    ASSERT(store.load().empty(), "store cleared after successful send");
}

void test_at_least_once_keeps_on_disk_on_failure() {
    setup_store_dir();
    auto cfg = makeConfig(5);
    cfg.deliveryMode = DeliveryMode::AtLeastOnce;

    EventQueue q(cfg.maxQueueCapacity);
    EventStore store(kTestDir);
    store.clear();

    FlushManager fm(q, cfg);
    fm.setStore(&store);
    fm.setTransport([](const std::string&, const std::string&) { return false; });

    fm.push({"buy", "{}", 1000});
    fm.flush();

    auto onDisk = store.load();
    ASSERT(onDisk.size() == 1, "event remains on disk after transport failure");
    ASSERT(onDisk[0].name == "buy", "correct event on disk");
    store.clear();
}

void test_at_least_once_recover_on_restart() {
    setup_store_dir();
    auto cfg = makeConfig(5);
    cfg.deliveryMode = DeliveryMode::AtLeastOnce;

    EventQueue q(cfg.maxQueueCapacity);
    EventStore store(kTestDir);
    store.clear();

    // Simulate a crash: event written to disk, never sent
    store.persist({{"crash_event", "{}", 9999}});

    FlushManager fm(q, cfg);
    fm.setStore(&store);
    fm.loadPersistedEvents();

    ASSERT(fm.pendingCount() == 1, "crashed event recovered into queue on restart");

    int flushCount = 0;
    fm.setTransport([&](const std::string&, const std::string& body) {
        ++flushCount;
        ASSERT(body.find("crash_event") != std::string::npos, "recovered event in flush body");
        return true;
    });

    fm.flush();
    ASSERT(flushCount == 1,        "recovered event sent");
    ASSERT(store.load().empty(),   "store cleared after successful send");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== EventSDK Unit Tests ===\n\n";

    test_queue_basic();
    test_queue_drop_oldest_at_capacity();
    test_queue_partial_drain();
    test_queue_thread_safety();
    test_queue_drop_counter_increments();

    test_store_persist_and_load();
    test_store_payload_with_special_chars();
    test_store_append_across_calls();

    test_flush_no_transport_returns_events();
    test_flush_with_transport_succeeds();
    test_flush_respects_batch_size();
    test_flush_empty_queue_is_noop();
    test_timer_flush();
    test_autoflush_triggers_at_threshold();
    test_autoflush_disabled();
    test_autoflush_threshold_zero_uses_batchsize();

    test_health_metrics_emitted_after_flush();
    test_health_metrics_transport_failure();
    test_health_drop_count_reflected();

    test_at_least_once_persists_before_send();
    test_at_least_once_keeps_on_disk_on_failure();
    test_at_least_once_recover_on_restart();

    std::cout << '\n' << passed << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}
