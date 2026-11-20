#pragma once

#include <cstdint>

/**
 * An NVS partition a storage driver opens by label. When the flashed partition
 * table lacks it, the driver registers `size` bytes at `offset` under that label
 * for the boot, so a deployed table needs no USB flash; a table that lists it
 * must list this range.
 */
struct NvsPartitionRange {
    const char* label;
    uint32_t offset;
    uint32_t size;
};

/** Where a storage driver's NVS partition comes from this boot. */
enum class NvsPartitionSource {
    TABLE,     // the flashed partition table's
    REGISTER,  // the driver's range, registered for this boot
    NONE,      // nowhere: the driver does not open
};

/**
 * Picks the source of a partition the flashed table holds (@p inTable, at
 * @p tableOffset, @p tableSize) or not, for a driver given @p range (null for
 * a partition only a table can hold). A table that lists the label at another
 * range is refused: it would split devices across two ranges.
 */
constexpr NvsPartitionSource chooseNvsPartitionSource(bool inTable, uint32_t tableOffset,
                                                      uint32_t tableSize,
                                                      const NvsPartitionRange* range) {
    if (inTable) {
        return range == nullptr || (tableOffset == range->offset && tableSize == range->size)
               ? NvsPartitionSource::TABLE
               : NvsPartitionSource::NONE;
    }
    return range != nullptr ? NvsPartitionSource::REGISTER : NvsPartitionSource::NONE;
}
