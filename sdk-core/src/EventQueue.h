#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace eventsdk {

struct Event {
    std::string name;
    std::string payload;   // caller-supplied JSON value (object, array, string, null, etc.)
    int64_t timestampMs;
};

typedef std::vector<Event> Batch;

class EventQueue {
public:
    explicit EventQueue(std::size_t maxCapacity);

    void push(Event event);                          // evicts oldest when at capacity
    Batch drain(std::size_t maxCount);
    std::size_t size() const;
    bool empty() const;
    std::size_t capacity() const { return maxCapacity_; }
    uint64_t dropCount() const;                      // cumulative evictions since construction

private:
    mutable std::mutex mutex_;
    std::queue<Event> queue_;
    std::size_t maxCapacity_;
    uint64_t dropsTotal_{0};
};

} // namespace eventsdk
