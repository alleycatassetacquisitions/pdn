#pragma once

#include <cstddef>
#include <string>

/** Registration / upload path shared by MatchManager and NodeMatchManager. */
class StoredMatchUploadSource {
public:
    virtual ~StoredMatchUploadSource() = default;

    virtual size_t getStoredMatchCount() = 0;
    virtual std::string toJson() = 0;
    virtual void clearStorage() = 0;
};
