#include "FlushManager.h"
#include <chrono>
#include <sstream>

namespace eventsdk {

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    return out;
}

FlushManager::FlushManager(EventQueue& queue, const Config& config)
    : queue_(queue), config_(config) {}

FlushManager::~FlushManager() {
    stopTimer();
}

void FlushManager::setStore(EventStore* store) {
    store_ = store;
}

void FlushManager::setHealthCallback(HealthCallback cb) {
    healthCallback_ = std::move(cb);
}

void FlushManager::startTimer() {
    if (config_.flushIntervalSeconds <= 0) return;
    if (timerThread_.joinable()) return;

    stopTimer_ = false;
    timerThread_ = std::thread([this]() {
        while (true) {
            std::unique_lock<std::mutex> lock(timerMutex_);
            bool stopped = timerCv_.wait_for(lock,
                std::chrono::seconds(config_.flushIntervalSeconds),
                [this] { return stopTimer_; });
            if (stopped) break;
            lock.unlock();
            flush();
        }
    });
}

void FlushManager::stopTimer() {
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        stopTimer_ = true;
    }
    timerCv_.notify_one();
    if (timerThread_.joinable()) timerThread_.join();
}

void FlushManager::setTransport(Transport transport) {
    transport_ = std::move(transport);
}

void FlushManager::push(Event event) {
    queue_.push(std::move(event));

    if (!config_.autoFlush) return;

    std::size_t threshold = config_.autoFlushThreshold > 0
        ? config_.autoFlushThreshold
        : config_.batchSize;

    if (queue_.size() >= threshold) {
        flush();
    }
}

void FlushManager::loadPersistedEvents() {
    if (!store_ || config_.deliveryMode != DeliveryMode::AtLeastOnce) return;
    auto events = store_->load();
    if (events.empty()) return;
    for (auto& e : events) queue_.push(std::move(e));
    store_->clear();
}

void FlushManager::flush(FlushCallback callback) {
    auto batch = queue_.drain(config_.batchSize);
    if (batch.empty()) {
        if (callback) callback(true, "");
        emitHealth();
        return;
    }

    if (!transport_) {
        for (auto& e : batch) queue_.push(std::move(e));
        if (callback) callback(false, "no transport configured");
        return;
    }

    // AtLeastOnce: write to disk before attempting network call.
    // On crash between persist() and clear(), events are recovered via loadPersistedEvents().
    if (config_.deliveryMode == DeliveryMode::AtLeastOnce && store_) {
        store_->persist(batch);
    }

    std::string body = buildBatchPayload(batch);

    auto t0 = std::chrono::steady_clock::now();
    bool ok  = transport_(config_.endpoint, body);
    auto t1  = std::chrono::steady_clock::now();

    lastFlushLatencyMs_ = std::chrono::duration<double, std::milli>(t1 - t0).count();
    ++flushCount_;

    if (ok) {
        bytesSent_ += static_cast<uint64_t>(body.size());
        if (config_.deliveryMode == DeliveryMode::AtLeastOnce && store_) {
            store_->clear();
        }
    } else {
        ++transportFailures_;
        // AtMostOnce: events already drained, silently lost.
        // AtLeastOnce: events remain on disk; recovered on next loadPersistedEvents().
    }

    if (callback) callback(ok, ok ? "" : "transport returned failure");
    emitHealth();
}

void FlushManager::emitHealth() {
    if (!healthCallback_) return;
    HealthMetrics m;
    m.eventsQueued        = queue_.size();
    m.eventsDropped       = queue_.dropCount();
    m.flushCount          = flushCount_;
    m.transportFailures   = transportFailures_;
    m.lastFlushLatencyMs  = lastFlushLatencyMs_;
    m.bytesSent           = bytesSent_;
    m.queueUtilizationPct = (config_.maxQueueCapacity > 0)
        ? (static_cast<double>(queue_.size()) / config_.maxQueueCapacity * 100.0)
        : 0.0;
    healthCallback_(m);
}

std::size_t FlushManager::pendingCount() const {
    return queue_.size();
}

std::string FlushManager::buildBatchPayload(const std::vector<Event>& events) const {
    std::ostringstream ss;
    ss << "{\"events\":[";
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (i > 0) ss << ',';
        ss << "{\"name\":\"" << jsonEscape(events[i].name) << "\""
           << ",\"ts\":"     << events[i].timestampMs
           << ",\"payload\":" << events[i].payload << '}';
    }
    ss << "]}";
    return ss.str();
}

} // namespace eventsdk
