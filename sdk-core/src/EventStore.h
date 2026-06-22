#pragma once
#include <string>
#include <vector>
#include "EventQueue.h"

namespace eventsdk {

// Appends events to a newline-delimited file on disk.
// Line format: <timestampMs>\t<name>\t<payload>
// Tabs and backslashes within fields are backslash-escaped.
class EventStore {
public:
    explicit EventStore(const std::string& storageDir);

    void persist(const std::vector<Event>& events);
    std::vector<Event> load() const;
    void clear();

private:
    std::string filePath_;
};

} // namespace eventsdk
