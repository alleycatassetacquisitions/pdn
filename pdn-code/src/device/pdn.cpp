#include "device/pdn.hpp"

#include "comms_constants.hpp"

PDN *PDN::GetInstance() {
    static PDN instance;
    return &instance;
}


PDN::PDN() {

}

int PDN::begin() override {
    initializePins();

    Serial1.begin(BAUDRATE, SERIAL_8E2, TXr, TXt, true);

    Serial2.begin(BAUDRATE, SERIAL_8E2, RXr, RXt, true);

    Serial1.setTxBufferSize(TRANSMIT_QUEUE_MAX_SIZE);
    Serial2.setTxBufferSize(TRANSMIT_QUEUE_MAX_SIZE);

    return 1;
}

void PDN::initializePins() {
    gpio_reset_pin(GPIO_NUM_38);
    gpio_reset_pin(GPIO_NUM_39);
    gpio_reset_pin(GPIO_NUM_40);
    gpio_reset_pin(GPIO_NUM_41);

    gpio_pad_select_gpio(GPIO_NUM_38);
    gpio_pad_select_gpio(GPIO_NUM_39);
    gpio_pad_select_gpio(GPIO_NUM_40);
    gpio_pad_select_gpio(GPIO_NUM_41);

    // init display
    pinMode(displayCS, OUTPUT);
    pinMode(displayDC, OUTPUT);
    digitalWrite(displayCS, 0);
    digitalWrite(displayDC, 0);

    pinMode(motorPin, OUTPUT);

    pinMode(TXt, OUTPUT);
    pinMode(TXr, INPUT);

    pinMode(RXt, OUTPUT);
    pinMode(RXr, INPUT);

    pinMode(displayLightsPin, OUTPUT);
    pinMode(gripLightsPin, OUTPUT);
}

void Device::tick() {
    getPeripherals().tick();
}