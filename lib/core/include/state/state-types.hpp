#pragma once

#include <memory>

/*
 * Snapshots are implemented per state. The struct is defined by a state and utilized
 * when a state is paused, and passed back to the state in the onStateResume method.
 */
struct Snapshot {
    virtual ~Snapshot() = default;
};

//StateId is a simple wrapper in case we eventually need to add more data to identify a state.
struct StateId {

    StateId() : id(0) {}
    explicit StateId(int stateid) {
        id = stateid;
    }

    bool operator<(const StateId& other) const {
        return id < other.id;
    }

    int id;
};
