#pragma once
#include <cstddef>
#include <string>

namespace eventsdk {

enum class DeliveryMode {
    AtMostOnce,   // drain before send; events lost if transport fails (default)
    AtLeastOnce,  // persist before send; store cleared only on transport success
};

struct Config {
    std::string  endpoint;
    std::size_t  batchSize{20};
    int          flushIntervalSeconds{30};
    std::size_t  maxQueueCapacity{1000};
    std::string  storageDir;
    bool         autoFlush{false};
    std::size_t  autoFlushThreshold{0};  // 0 = use batchSize
    DeliveryMode deliveryMode{DeliveryMode::AtMostOnce};
};

} // namespace eventsdk
