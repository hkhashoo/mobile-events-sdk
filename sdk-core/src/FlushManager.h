#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include "Config.h"
#include "EventQueue.h"

namespace eventsdk {

using FlushCallback = std::function<void(bool success, const std::string& error)>;

// Drains events from the queue in batches and delivers them via a platform-supplied
// transport function. Keeping HTTP out of the C++ core means the same core compiles
// on both iOS and Android without any platform HTTP dependency.
//
// Delivery guarantee: at-most-once. Events drained from the queue are lost if the
// transport returns false. Pair with EventStore for at-least-once semantics.
class FlushManager {
public:
    FlushManager(EventQueue& queue, const Config& config);
    ~FlushManager();

    // transport(endpoint, json_body) -> true on success
    using Transport = std::function<bool(const std::string& url, const std::string& body)>;
    void setTransport(Transport transport);

    // Push an event and auto-flush if autoFlush is enabled and threshold is reached.
    void push(Event event);

    // Drain up to config.batchSize events and POST them. If no transport is set,
    // events are returned to the queue unmodified.
    void flush(FlushCallback callback = nullptr);

    // Start background timer thread that flushes every config.flushIntervalSeconds.
    // Idempotent — safe to call multiple times, only first call starts the thread.
    void startTimer();
    void stopTimer();

    std::size_t pendingCount() const;

private:
    std::string buildBatchPayload(const std::vector<Event>& events) const;

    EventQueue&    queue_;
    const Config&  config_;
    Transport      transport_;

    std::thread             timerThread_;
    std::mutex              timerMutex_;
    std::condition_variable timerCv_;
    bool                    stopTimer_{false};
};

} // namespace eventsdk
