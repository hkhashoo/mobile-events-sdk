#include "EventQueue.h"
#include <algorithm>

namespace eventsdk {

EventQueue::EventQueue(std::size_t maxCapacity) : maxCapacity_(maxCapacity) {}

void EventQueue::push(Event event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= maxCapacity_) {
        queue_.pop();
    }
    queue_.push(std::move(event));
}

std::vector<Event> EventQueue::drain(std::size_t maxCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Event> batch;
    batch.reserve(std::min(maxCount, queue_.size()));
    while (!queue_.empty() && batch.size() < maxCount) {
        batch.push_back(std::move(queue_.front()));
        queue_.pop();
    }
    return batch;
}

std::size_t EventQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool EventQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

} // namespace eventsdk
