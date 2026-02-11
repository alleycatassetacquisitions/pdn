#pragma once

#include <memory>

/*
 * Snapshot is a base struct that states can extend to bundle
 * their data when paused. onStatePaused() returns a unique_ptr<Snapshot>,
 * onStateResumed() consumes it.
 */
struct Snapshot {
    virtual ~Snapshot() = default;
};

/*
 * StateId is a simple wrapper in case we eventually need to add
 * more data to identify a state.
 */
struct StateId {
    StateId() : id(-1) {}

    explicit StateId(int stateid) : id(stateid) {}

    int id;

    bool operator<(const StateId& other) const {
        return id < other.id;
    }
};
