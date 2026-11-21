#pragma once
#include <Arduino.h>

#include <modules/SX126x/SX126x.h>

// Seule la puce reliée au bus SPI est simulée.
class SX1276 {
public:
    virtual int16_t startReceive() = 0;
    virtual int16_t receive(byte*, size_t, RadioLibTime_t) = 0;
    virtual int16_t readData(byte*, size_t) = 0;
    void setPacketReceivedAction(void (*)()) {}
    void setSyncWord(byte*, size_t) {}
};
