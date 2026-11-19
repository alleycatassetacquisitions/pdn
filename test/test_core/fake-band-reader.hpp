#pragma once

#include <initializer_list>
#include <utility>

#include "device/driver-names.hpp"
#include "device/drivers/driver-interface.hpp"

/** A band reader driver a test taps bands on; it answers until told not to. */
class FakeBandReader : public BandReaderDriverInterface {
public:
    /** Registers under the FDN's band reader name. */
    FakeBandReader()
        : BandReaderDriverInterface(BAND_READER_DRIVER_NAME) {}

    /** The boot check: fails while the reader does not answer. */
    int initialize() override { return readerResponds ? 0 : -1; }

    /** Nothing to poll: tapBand() reports the tap as exec() would. */
    void exec() override {}

    /** Taps a band with @p uid; a reader that does not answer reports nothing. */
    void tapBand(std::initializer_list<uint8_t> uid) {
        if (!readerResponds || !tapHandler) return;
        BandUid band;
        for (uint8_t byte : uid) {
            if (band.length == band.bytes.size()) break;
            band.bytes[band.length++] = byte;
        }
        tapHandler(band);
    }

    /** Sets whether the reader answers. */
    void setResponding(bool responding) { readerResponds = responding; }

    /** Keeps @p handler as the one tap handler. */
    void setTapHandler(BandTapHandler handler) override { tapHandler = std::move(handler); }

    /** @return what setResponding last set; true by default. */
    bool isResponding() override { return readerResponds; }

private:
    BandTapHandler tapHandler;
    bool readerResponds = true;
};
