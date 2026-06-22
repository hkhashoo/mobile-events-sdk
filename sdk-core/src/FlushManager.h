#pragma once
#include <functional>
#include <string>
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

    // transport(endpoint, json_body) -> true on success
    using Transport = std::function<bool(const std::string& url, const std::string& body)>;
    void setTransport(Transport transport);

    // Drain up to config.batchSize events and POST them. If no transport is set,
    // events are returned to the queue unmodified.
    void flush(FlushCallback callback = nullptr);

    std::size_t pendingCount() const;

private:
    std::string buildBatchPayload(const std::vector<Event>& events) const;

    EventQueue& queue_;
    const Config& config_;
    Transport transport_;
};

} // namespace eventsdk
