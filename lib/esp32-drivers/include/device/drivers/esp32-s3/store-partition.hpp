#pragma once

#include <cstdint>

// The NVS partition at the end of flash, as partitions.csv lays it out: the
// FDN's record store, and on a PDN its match ring. Deployed tables end before
// it, so a PDN registers this range under this label at runtime; renaming or
// moving it in the table takes a USB flash of every device.
inline constexpr const char STORE_PARTITION_LABEL[] = "fdn_store";
constexpr uint32_t STORE_PARTITION_OFFSET = 0x7E1000;
constexpr uint32_t STORE_PARTITION_SIZE = 0x1F000;
