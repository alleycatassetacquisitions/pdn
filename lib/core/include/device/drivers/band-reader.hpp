#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/** Longest UID ISO/IEC 14443-A anticollision returns (triple size). */
constexpr size_t BAND_UID_MAX_SIZE = 10;

/** A band's UID as anticollision returned it. */
struct BandUid {
    std::array<uint8_t, BAND_UID_MAX_SIZE> bytes{};
    uint8_t length = 0;

    /** @return true for the sizes a band lookup takes: 4 (single) and 7 (double) bytes. */
    bool hasLookupLength() const { return length == 4 || length == 7; }
};

/**
 * A reader for ISO/IEC 14443-A bands and cards that reports UIDs and nothing
 * else: it never authenticates to, reads or writes a block, so the FDN holds no
 * card keys. The driver's initialize() is the boot check and its exec() polls
 * the reader each tick, latching a band that enters the field as one tap until
 * it leaves. No hardware driver exists until the part and its pins are confirmed.
 */
class BandReader {
public:
    /** Virtual so a driver is destroyed through this interface. */
    virtual ~BandReader() = default;

    /**
     * Takes the tap exec() latched, if any.
     * @return true, with the band's UID in @p uid, once per band that enters the
     * field; a band held in the field is reported again only after it leaves.
     */
    virtual bool takeTap(BandUid& uid) = 0;

    /** @return true while the reader answers. */
    virtual bool isResponding() = 0;
};
