#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <cstdint>

namespace cli {

enum class EventType {
    STATE_CHANGE,
    BUTTON_PRESS,
    SERIAL_TX,
    SERIAL_RX,
    ESPNOW_TX,
    ESPNOW_RX,
    DISPLAY_TEXT,
    DISPLAY_CLEAR,
    HTTP_REQUEST,
    HTTP_RESPONSE,
};

struct Event {
    unsigned long timestampMs = 0;
    EventType type;
    int deviceIndex = -1;
    std::string detail;

    // Type-specific queryable fields
    std::string fromState;
    std::string toState;
    std::string message;
    std::string button;
};

inline const char* eventTypeName(EventType type) {
    switch (type) {
        case EventType::STATE_CHANGE:  return "STATE_CHANGE";
        case EventType::BUTTON_PRESS:  return "BUTTON_PRESS";
        case EventType::SERIAL_TX:     return "SERIAL_TX";
        case EventType::SERIAL_RX:     return "SERIAL_RX";
        case EventType::ESPNOW_TX:     return "ESPNOW_TX";
        case EventType::ESPNOW_RX:     return "ESPNOW_RX";
        case EventType::DISPLAY_TEXT:  return "DISPLAY_TEXT";
        case EventType::DISPLAY_CLEAR: return "DISPLAY_CLEAR";
        case EventType::HTTP_REQUEST:  return "HTTP_REQUEST";
        case EventType::HTTP_RESPONSE: return "HTTP_RESPONSE";
    }
    return "UNKNOWN";
}

class EventLogger {
public:
    void log(const Event& event) {
        events_.push_back(event);
    }

    std::vector<Event> getAll() const {
        return events_;
    }

    std::vector<Event> getByType(EventType type) const {
        std::vector<Event> result;
        for (auto& e : events_) {
            if (e.type == type) result.push_back(e);
        }
        return result;
    }

    std::vector<Event> getByDevice(int deviceIndex) const {
        std::vector<Event> result;
        for (auto& e : events_) {
            if (e.deviceIndex == deviceIndex) result.push_back(e);
        }
        return result;
    }

    std::vector<Event> getByTypeAndDevice(EventType type, int deviceIndex) const {
        std::vector<Event> result;
        for (auto& e : events_) {
            if (e.type == type && e.deviceIndex == deviceIndex) result.push_back(e);
        }
        return result;
    }

    Event getLast(EventType type) const {
        for (int i = static_cast<int>(events_.size()) - 1; i >= 0; i--) {
            if (events_[i].type == type) return events_[i];
        }
        return Event{};
    }

    Event getLast(EventType type, int deviceIndex) const {
        for (int i = static_cast<int>(events_.size()) - 1; i >= 0; i--) {
            if (events_[i].type == type && events_[i].deviceIndex == deviceIndex) {
                return events_[i];
            }
        }
        return Event{};
    }

    bool hasEvent(EventType type, const std::string& detailSubstring) const {
        for (auto& e : events_) {
            if (e.type == type) {
                if (e.detail.find(detailSubstring) != std::string::npos ||
                    e.message.find(detailSubstring) != std::string::npos) {
                    return true;
                }
            }
        }
        return false;
    }

    size_t count(EventType type) const {
        size_t c = 0;
        for (auto& e : events_) {
            if (e.type == type) c++;
        }
        return c;
    }

    size_t count(EventType type, int deviceIndex) const {
        size_t c = 0;
        for (auto& e : events_) {
            if (e.type == type && e.deviceIndex == deviceIndex) c++;
        }
        return c;
    }

    void clear() {
        events_.clear();
    }

    size_t size() const {
        return events_.size();
    }

    std::string formatEvent(const Event& event) const {
        char ts[16];
        snprintf(ts, sizeof(ts), "[%04lums]", event.timestampMs);

        std::string line = std::string(ts) + " " + eventTypeName(event.type);

        // Pad type name to 14 chars for alignment
        while (line.size() < 24) line += ' ';

        line += "dev=" + std::to_string(event.deviceIndex);

        switch (event.type) {
            case EventType::STATE_CHANGE:
                line += " from=" + event.fromState + " to=" + event.toState;
                break;
            case EventType::BUTTON_PRESS:
                line += " button=" + event.button;
                break;
            case EventType::SERIAL_TX:
            case EventType::SERIAL_RX:
                line += " msg=\"" + event.message + "\"";
                break;
            case EventType::DISPLAY_TEXT:
                line += " text=\"" + event.message + "\"";
                break;
            default:
                if (!event.detail.empty()) {
                    line += " " + event.detail;
                }
                break;
        }

        return line;
    }

    bool writeToFile(const std::string& path) const {
        FILE* f = fopen(path.c_str(), "w");
        if (!f) return false;
        for (auto& e : events_) {
            fprintf(f, "%s\n", formatEvent(e).c_str());
        }
        fclose(f);
        return true;
    }

private:
    std::vector<Event> events_;
};

} // namespace cli

#endif // NATIVE_BUILD
