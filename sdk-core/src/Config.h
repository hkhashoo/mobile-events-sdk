#pragma once
#include <cstddef>
#include <string>

namespace eventsdk {

struct Config {
    std::string endpoint;
    std::size_t batchSize{20};
    int flushIntervalSeconds{30};
    std::size_t maxQueueCapacity{1000};
    std::string storageDir;
    bool        autoFlush{false};
    std::size_t autoFlushThreshold{0};  // 0 = use batchSize
};

} // namespace eventsdk
