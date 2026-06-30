#pragma once
#include <cstddef>
#include <cstdint>

namespace eventsdk {

struct HealthMetrics {
    std::size_t eventsQueued{0};         // current in-memory queue depth
    uint64_t    eventsDropped{0};        // cumulative drops due to capacity eviction
    uint64_t    flushCount{0};           // total flush() calls that had events
    uint64_t    transportFailures{0};    // total transport() calls returning false
    double      lastFlushLatencyMs{0.0}; // wall-clock duration of last transport call
    uint64_t    bytesSent{0};            // cumulative bytes delivered to transport
    double      queueUtilizationPct{0.0};// eventsQueued / maxQueueCapacity * 100
};

} // namespace eventsdk
