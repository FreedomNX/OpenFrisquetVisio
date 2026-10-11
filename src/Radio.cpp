#include "Radio.h"

Radio::Radio() : _radio(new Module(RADIO_NSS, RADIO_DIO0, RADIO_RST, RADIO_GPIO)) {}

void Radio::init() {
#if defined(RADIO_SCK)
    SPI.begin(RADIO_SCK, RADIO_MISO, RADIO_MOSI, RADIO_NSS);
#endif
    _radio.beginFSK();
    _radio.setFrequency(868.96);
    _radio.setBitRate(25.0);
    _radio.setFrequencyDeviation(50.0);
    _radio.setRxBandwidth(250.0);
    _radio.setPreambleLength(4);
}
