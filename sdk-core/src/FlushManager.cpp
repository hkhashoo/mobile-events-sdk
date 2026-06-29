#include "FlushManager.h"
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

void FlushManager::startTimer() {
    if (config_.flushIntervalSeconds <= 0) return;
    if (timerThread_.joinable()) return;   // already running

    stopTimer_ = false;
    timerThread_ = std::thread([this]() {
        while (true) {
            std::unique_lock<std::mutex> lock(timerMutex_);
            bool stopped = timerCv_.wait_for(lock,
                std::chrono::seconds(config_.flushIntervalSeconds),
                [this] { return stopTimer_; });
            if (stopped) break;
            lock.unlock();   // release before potentially-slow transport call
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

void FlushManager::setTransport(Transport transport) {
    transport_ = std::move(transport);
}

void FlushManager::flush(FlushCallback callback) {
    auto batch = queue_.drain(config_.batchSize);
    if (batch.empty()) {
        if (callback) callback(true, "");
        return;
    }

    if (!transport_) {
        for (auto& e : batch) queue_.push(std::move(e));
        if (callback) callback(false, "no transport configured");
        return;
    }

    std::string body = buildBatchPayload(batch);
    bool ok = transport_(config_.endpoint, body);
    if (callback) callback(ok, ok ? "" : "transport returned failure");
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
