#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

/**
 * Observers of one event, each keyed on the object that registered it, called
 * in the order they registered.
 *
 * A callback may call set() on the list firing it: a clear is skipped at once
 * and destroyed when the fire unwinds; an install waits for the next fire.
 */
template <typename Callback> class CallbackList {
public:
    /**
     * Installs `callback` for `owner`, replacing the one `owner` already holds.
     * A null `callback` removes `owner`'s entry and touches no other.
     */
    void set(Callback callback, const void* owner) {
        for (size_t i = 0; i < pending.size(); i++) {
            if (pending[i].owner != owner) continue;
            pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
        for (size_t i = 0; i < entries.size(); i++) {
            if (entries[i].owner != owner || entries[i].removed) continue;
            if (firing > 0) {
                entries[i].removed = true;
            } else {
                entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
            }
            break;
        }
        if (!callback) return;
        // Appending to `entries` mid-fire could reallocate it under the
        // callback that is running.
        std::vector<Entry>& target = firing > 0 ? pending : entries;
        target.push_back({owner, std::move(callback), false});
    }

    /** Calls every installed callback with `args`. */
    template <typename... Args> void fire(const Args&... args) {
        firing++;
        for (size_t i = 0; i < entries.size(); i++) {
            if (!entries[i].removed) entries[i].callback(args...);
        }
        firing--;
        if (firing > 0) return;
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [](const Entry& entry) { return entry.removed; }),
                      entries.end());
        for (Entry& entry : pending) {
            entries.push_back(std::move(entry));
        }
        pending.clear();
    }

private:
    struct Entry {
        const void* owner;
        Callback callback;
        // Cleared during a fire; kept alive until that fire unwinds.
        bool removed;
    };

    std::vector<Entry> entries;
    // Installed during a fire; joins `entries` when the fire unwinds.
    std::vector<Entry> pending;
    // Depth of nested fire() calls; entries are only erased at zero.
    int firing = 0;
};
