//
// Created by Elli Furedy on 10/8/2024.
//

#pragma once

#include "drivers/driver-interface.hpp"
#include <string>
#include <functional>
#include "protocol-constants.hpp"

class SerialManager {
public:
    SerialManager(HWSerialWrapper* outputJack, HWSerialWrapper* inputJack)
        : outputJack(outputJack), inputJack(inputJack) {}

    ~SerialManager() = default;

    HWSerialWrapper* getOutputJack() { return outputJack; }
    void setOutputJack(HWSerialWrapper* jack) { outputJack = jack; }

    HWSerialWrapper* getInputJack() { return inputJack; }
    void setInputJack(HWSerialWrapper* jack) { inputJack = jack; }

    int getSerialWriteQueueSize(SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        return TRANSMIT_QUEUE_MAX_SIZE - getJack(jack)->availableForWrite();
    }

    void writeBytes(const uint8_t* data, size_t len, SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        getJack(jack)->writeBytes(data, len);
    }

    // One callback slot per jack, owned by RDC for the device's lifetime
    // (wired in RemoteDeviceCoordinator::initialize). Game code must not call
    // this; it would silently clobber RDC's frame parser.
    void setOnBytesReceivedCallback(const SerialBytesCallback& callback,
                                    SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK) {
        getJack(jack)->setBytesCallback(callback);
    }

    void flushSerial() {
        outputJack->flush();
        inputJack->flush();
    }

private:
    HWSerialWrapper* getJack(SerialIdentifier jack) {
        return jack == SerialIdentifier::OUTPUT_JACK ? outputJack : inputJack;
    }

    HWSerialWrapper* outputJack;
    HWSerialWrapper* inputJack;
};
