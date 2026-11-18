#pragma once

#include <initializer_list>

#include "device/driver-names.hpp"
#include "device/drivers/driver-interface.hpp"

/** A band reader driver a test places bands on; it answers until told not to. */
class FakeBandReader : public BandReaderDriverInterface {
public:
    /** Registers under the FDN's band reader name. */
    FakeBandReader()
        : BandReaderDriverInterface(BAND_READER_DRIVER_NAME) {}

    /** The boot check: fails while the reader does not answer. */
    int initialize() override { return readerResponds ? 0 : -1; }

    /** Nothing to poll: placeBand() latches the tap as exec() would. */
    void exec() override {}

    /** Puts a band with @p uid in the field and latches it as one tap. */
    void placeBand(std::initializer_list<uint8_t> uid) {
        heldBand = BandUid{};
        for (uint8_t byte : uid) {
            if (heldBand.length == heldBand.bytes.size()) break;
            heldBand.bytes[heldBand.length++] = byte;
        }
        tapUnreported = true;
    }

    /** Sets whether the reader answers; one that does not reports no tap. */
    void setResponding(bool responding) { readerResponds = responding; }

    /** Reports the placed band once, while the reader answers. */
    bool takeTap(BandUid& uid) override {
        if (!readerResponds || !tapUnreported) return false;
        tapUnreported = false;
        uid = heldBand;
        return true;
    }

    /** @return what setResponding last set; true by default. */
    bool isResponding() override { return readerResponds; }

private:
    BandUid heldBand;
    bool tapUnreported = false;
    bool readerResponds = true;
};
