#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include "Config.h"
#include "EventQueue.h"
#include "EventStore.h"
#include "HealthMetrics.h"

namespace eventsdk {

using FlushCallback  = std::function<void(bool success, const std::string& error)>;
using HealthCallback = std::function<void(const HealthMetrics&)>;

// Drains events from the queue in batches and delivers them via a platform-supplied
// transport function. Keeping HTTP out of the C++ core means the same core compiles
// on both iOS and Android without any platform HTTP dependency.
//
// Delivery guarantee (AtMostOnce): events drained before transport call; lost on failure.
// Delivery guarantee (AtLeastOnce): events persisted to EventStore before transport call;
//   store cleared only on success. On restart, call loadPersistedEvents() to recover.
class FlushManager {
public:
    FlushManager(EventQueue& queue, const Config& config);
    ~FlushManager();

    using Transport = std::function<bool(const std::string& url, const std::string& body)>;
    void setTransport(Transport transport);

    // Optional: required for AtLeastOnce delivery mode and crash recovery.
    void setStore(EventStore* store);

    // Optional: called after every flush with current health snapshot.
    void setHealthCallback(HealthCallback cb);

    // Push an event and auto-flush if threshold reached.
    void push(Event event);

    // Drain up to config.batchSize events and POST them via transport.
    // Events returned to queue if no transport is set.
    void flush(FlushCallback callback = nullptr);

    // Start background timer thread. Idempotent — only first call starts the thread.
    void startTimer();
    void stopTimer();

    // Re-queue any events persisted to disk from a previous run (AtLeastOnce mode).
    void loadPersistedEvents();

    std::size_t pendingCount() const;

private:
    std::string buildBatchPayload(const std::vector<Event>& events) const;
    void emitHealth();

    EventQueue&    queue_;
    const Config&  config_;
    EventStore*    store_{nullptr};
    Transport      transport_;
    HealthCallback healthCallback_;

    // Metrics (updated under no lock — only written from flush(), read in emitHealth())
    uint64_t flushCount_{0};
    uint64_t transportFailures_{0};
    uint64_t bytesSent_{0};
    double   lastFlushLatencyMs_{0.0};

    std::thread             timerThread_;
    std::mutex              timerMutex_;
    std::condition_variable timerCv_;
    bool                    stopTimer_{false};
};

} // namespace eventsdk
