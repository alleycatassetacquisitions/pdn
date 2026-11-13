#pragma once

#include <cstdint>

#include "wireless/record-relay-types.hpp"

/**
 * One kind of record a PDN holds until an FDN has stored it; Traits names the
 * kind. The record relay asks a source only for its lowest pending record and
 * tells it only that a record was acknowledged; when, to whom and how often a
 * record is sent is the relay's business.
 */
template <class Traits> class RecordSource {
public:
    /** A source is owned by whoever built it, never by the relay. */
    virtual ~RecordSource() = default;

    /**
     * Writes the lowest-numbered record above the mark into @p recordBytes and
     * its number into @p recordNumber. Reading changes nothing: the mark moves
     * only in advanceMark, so a record is never retired without an
     * acknowledgement. A source that cannot read a record it should hold
     * passes over it here and offers the next one.
     * @return false when nothing can be offered: nothing is pending, or every
     * pending record is unreadable; the outputs are then untouched.
     */
    virtual bool readLowestPending(uint32_t& recordNumber,
                                   uint8_t (&recordBytes)[Traits::RECORD_SIZE]) = 0;

    /**
     * Moves the mark to @p recordNumber, which an FDN has acknowledged storing.
     * A number at or below the mark is ignored: a mark never moves backwards.
     */
    virtual void advanceMark(uint32_t recordNumber) = 0;
};

// What follows is the arithmetic every source shares: records are numbered
// from 1, the newest ringCapacity of them are held, and a mark names the
// highest number an FDN has acknowledged.

/**
 * @return The lowest record number still to be sent, or 0 when none is. A
 * record older than the newest @p ringCapacity is no longer held, so the
 * range starts after it.
 */
constexpr uint32_t firstPendingRecordNumber(uint32_t mark, uint32_t newestNumber,
                                            uint32_t ringCapacity) {
    if (mark >= newestNumber) {
        return 0;
    }
    const uint32_t oldestHeld = newestNumber > ringCapacity ? newestNumber - (ringCapacity - 1) : 1;
    return mark + 1 > oldestHeld ? mark + 1 : oldestHeld;
}

/**
 * @return The mark after record @p acknowledgedNumber is acknowledged. A mark
 * only moves up, and never past the newest record.
 */
constexpr uint32_t advancedRecordMark(uint32_t mark, uint32_t newestNumber,
                                      uint32_t acknowledgedNumber) {
    return acknowledgedNumber > mark && acknowledgedNumber <= newestNumber ? acknowledgedNumber
                                                                           : mark;
}

/**
 * Finds the lowest pending record that @p loadRecord, a `bool(uint32_t number)`,
 * can read, and writes its number to @p recordNumber. An unreadable record is
 * passed over, never retired: nothing here can move a mark, so it stays
 * pending until a later record's acknowledgement moves the mark past it.
 * @p reportUnreadable, a `void(uint32_t number)`, is told of each record
 * passed over once: @p highestReportedUnreadable carries what was already
 * reported from one call to the next.
 * @return false when no pending record could be read; @p recordNumber is then untouched.
 */
template <class LoadRecord, class ReportUnreadable>
bool findLowestReadableRecord(uint32_t mark, uint32_t newestNumber, uint32_t ringCapacity,
                              uint32_t& highestReportedUnreadable, uint32_t& recordNumber,
                              LoadRecord&& loadRecord, ReportUnreadable&& reportUnreadable) {
    // number == 0: nothing pending, or number++ wrapped past a newest of UINT32_MAX.
    for (uint32_t number = firstPendingRecordNumber(mark, newestNumber, ringCapacity);
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
