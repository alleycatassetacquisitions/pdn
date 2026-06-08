#pragma once

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
