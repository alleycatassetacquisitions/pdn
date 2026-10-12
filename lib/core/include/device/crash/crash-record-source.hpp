#pragma once

#include <cstdint>

#include "device/crash/crash-record.hpp"

/**
 * The crash records still waiting to reach the server, oldest first.
 */
class CrashRecordSource {
public:
    /** Owned by whoever constructed it; nothing deletes it through this interface. */
    virtual ~CrashRecordSource() = default;

    /**
     * Fills `out` with the oldest record not yet uploaded. Returns false when
     * none is left.
     */
    virtual bool loadOldestPending(CrashRecord& out) = 0;

    /**
     * Records that the server accepted `crashNumber`, so it is not offered again.
     */
    virtual void markUploaded(uint32_t crashNumber) = 0;
};
