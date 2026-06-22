#include "EventStore.h"
#include <fstream>

namespace eventsdk {

static std::string escapeField(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else                out += c;
    }
    return out;
}

static std::string unescapeField(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[++i]) {
                case '\\': out += '\\'; break;
                case 't':  out += '\t'; break;
                case 'n':  out += '\n'; break;
                default:   out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

EventStore::EventStore(const std::string& storageDir)
    : filePath_(storageDir + "/event_store.dat") {}

void EventStore::persist(const std::vector<Event>& events) {
    std::ofstream file(filePath_, std::ios::app);
    for (const auto& e : events) {
        file << e.timestampMs << '\t'
             << escapeField(e.name) << '\t'
             << escapeField(e.payload) << '\n';
    }
}

std::vector<Event> EventStore::load() const {
    std::vector<Event> events;
    std::ifstream file(filePath_);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto t1 = line.find('\t');
        auto t2 = (t1 != std::string::npos) ? line.find('\t', t1 + 1) : std::string::npos;
        if (t1 == std::string::npos || t2 == std::string::npos) continue;

        Event e;
        try { e.timestampMs = std::stoll(line.substr(0, t1)); } catch (...) { continue; }
        e.name    = unescapeField(line.substr(t1 + 1, t2 - t1 - 1));
        e.payload = unescapeField(line.substr(t2 + 1));
        events.push_back(std::move(e));
    }
    return events;
}

void EventStore::clear() {
    std::ofstream file(filePath_, std::ios::trunc);
}

} // namespace eventsdk
