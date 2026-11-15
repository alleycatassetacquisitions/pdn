#pragma once

#include <cstdint>

#include "wireless/record-relay-types.hpp"

/** One kind of record a PDN holds until an FDN has stored it; Traits names the kind. */
template <class Traits> class RecordSource {
public:
    /** A source is owned by whoever built it, never by the relay. */
    virtual ~RecordSource() = default;

    /**
     * Writes the lowest-numbered record above the mark into @p recordBytes and
     * its number into @p recordNumber. Reading changes nothing: the mark moves
     * only in advanceMark, so a record is never retired without an
     * acknowledgement. A record the source cannot read is passed over.
     * @return false when nothing can be offered; the outputs are then untouched.
     */
    virtual bool readLowestPending(uint32_t& recordNumber,
                                   uint8_t (&recordBytes)[Traits::RECORD_SIZE]) = 0;

    /**
     * Moves the mark to @p recordNumber, which an FDN has acknowledged storing.
     * @return false when the mark did not move there: the number is at or below
     * the mark or above the newest record, or the write failed.
     */
    virtual bool advanceMark(uint32_t recordNumber) = 0;
};

/**
 * @return The mark after record @p acknowledgedNumber is acknowledged: it only
 * moves up, and never past @p newestNumber.
 */
constexpr uint32_t advancedRecordMark(uint32_t mark, uint32_t newestNumber,
                                      uint32_t acknowledgedNumber) {
    return acknowledgedNumber > mark && acknowledgedNumber <= newestNumber ? acknowledgedNumber
                                                                           : mark;
}

/**
 * Finds the lowest record above @p mark, among the newest @p ringCapacity up to
 * @p newestNumber, that @p loadRecord (`bool(uint32_t number)`) can read, and
 * writes its number to @p recordNumber. An unreadable record is passed over,
 * never retired: it stays above the mark, pending, until a later record's
 * acknowledgement moves the mark past it. @p reportUnreadable
 * (`void(uint32_t number)`) hears of each one once; @p highestReportedUnreadable
 * carries that across calls.
 * @return false when no pending record could be read.
 */
template <class LoadRecord, class ReportUnreadable>
bool findLowestReadableRecord(uint32_t mark, uint32_t newestNumber, uint32_t ringCapacity,
                              uint32_t& highestReportedUnreadable, uint32_t& recordNumber,
                              LoadRecord&& loadRecord, ReportUnreadable&& reportUnreadable) {
    if (mark >= newestNumber) return false;
    const uint32_t oldestHeld = newestNumber > ringCapacity ? newestNumber - ringCapacity + 1 : 1;
    // number != 0: number++ wraps past a newest of UINT32_MAX.
    for (uint32_t number = mark + 1 > oldestHeld ? mark + 1 : oldestHeld;
         number != 0 && number <= newestNumber; number++) {
        if (loadRecord(number)) {
            recordNumber = number;
            return true;
        }
        if (number > highestReportedUnreadable) {
            highestReportedUnreadable = number;
            reportUnreadable(number);
        }
    }
    return false;
}
