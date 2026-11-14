#pragma once

#include <cstdint>
#include "wireless/record-relay-types.hpp"

// One record as the FDN's storage holds it: what the PDN's frame carried, plus
// who sent it. The struct is the stored layout.
template <class Content> struct FdnStoredEntry {
    uint8_t senderMac[6];
    uint8_t kind;
    uint32_t recordNumber;
    Content content;
} __attribute__((packed));

using FdnStoredCrashEntry = FdnStoredEntry<CrashRecordContent>;
using FdnStoredMatchEntry = FdnStoredEntry<MatchRecordContent>;
static_assert(sizeof(FdnStoredCrashEntry) == 53, "stored crash entry size");
static_assert(sizeof(FdnStoredMatchEntry) == 139, "stored match entry size");
