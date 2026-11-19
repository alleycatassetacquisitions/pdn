#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

/** Longest UID ISO/IEC 14443-A anticollision returns (triple size). */
constexpr size_t BAND_UID_MAX_SIZE = 10;

/** A band's UID as anticollision returned it. */
struct BandUid {
    std::array<uint8_t, BAND_UID_MAX_SIZE> bytes{};
    uint8_t length = 0;

    /** @return true for the sizes anticollision returns: 4, 7 or 10 bytes. */
    bool hasLookupLength() const { return length == 4 || length == 7 || length == 10; }
};

/** Told of each tap, with the band's UID. */
using BandTapHandler = std::function<void(const BandUid& uid)>;

/**
 * A reader for ISO/IEC 14443-A bands and cards that reports UIDs and nothing
 * else: it never authenticates to, reads or writes a block, so the FDN holds no
 * card keys. The driver's exec() polls the reader each tick and reports a band
 * that enters the field as one tap until it leaves. A driver whose initialize()
 * boot check fails stays registered, so ask isResponding(). No hardware driver
 * exists until the part and its pins are confirmed, so an FDN registers none and
 * Device::getPeripheral finds no reader.
 */
class BandReader {
public:
    /** Virtual so a driver is destroyed through this interface. */
    virtual ~BandReader() = default;

    /**
     * Sets the one handler told of each tap, from exec(); an empty handler
     * clears it. A band held in the field is reported again only after it leaves.
     */
    virtual void setTapHandler(BandTapHandler handler) = 0;

    /** @return true while the reader answers. */
    virtual bool isResponding() = 0;
};
