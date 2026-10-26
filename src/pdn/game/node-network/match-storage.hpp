#pragma once

#include <cstddef>

#include "alleycat-server/quickdraw-match-codec.hpp"
#include "device/drivers/storage-interface.hpp"

/** Persists finished duel records until the server confirms them. Usually holds zero or one. */
class MatchStorage {
public:
    static constexpr size_t MAX_PENDING_RECORDS = 100;

    explicit MatchStorage(StorageInterface* storage);

    /** @return false when storage is full or a write fails; the count is unchanged then. */
    bool append(const QuickdrawMatchRecord& record);

    size_t getCount() const;

    /** @return false, leaving @p record unchanged, when @p index is out of range or the stored bytes are malformed. */
    bool readAt(size_t index, QuickdrawMatchRecord& record) const;

    /** Removes every stored record. */
    void clear();

private:
    StorageInterface* storage = nullptr;
};
