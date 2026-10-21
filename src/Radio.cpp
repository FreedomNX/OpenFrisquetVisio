#include "Radio.h"

Radio::Radio() : _radio(new Module(SS, DIO0, RST_LoRa, BUSY_LoRa)) {}

void Radio::init() {
    _radio.beginFSK();
    _radio.setFrequency(868.96);
    _radio.setBitRate(25.0);
    _radio.setFrequencyDeviation(50.0);
    _radio.setRxBandwidth(250.0);
    _radio.setPreambleLength(16);
}
