#pragma once
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <deque>
#include <vector>
using byte = uint8_t;
using std::isnan;
#include "radio-types.inc"
constexpr int16_t RADIOLIB_ERR_NONE = 0, RADIOLIB_ERR_RX_TIMEOUT = -6,
                  RADIOLIB_ERR_ADDRESS_NOT_FOUND = -9;
constexpr size_t RADIOLIB_SX126X_MAX_PACKET_LENGTH = 255;
uint32_t clockMs = 0;
uint32_t millis() {
    return clockMs;
}
void delay(uint32_t);
void info(const char *, ...) {
}
void logRadio(bool, byte *, size_t, float = 0) {
}
struct NetworkID {
    byte bytes[4];
};
struct Radio {
    struct Packet {
        std::vector<byte> data;
        int16_t result = 0;
    };
    std::deque<Packet> packets;
    size_t lastLength = 0;
    unsigned reads = 0, rearms = 0, syncChanges = 0;
    std::vector<std::vector<byte>> sent;
    int16_t txResult = 0;
    int16_t rearmResult = 0;
    bool armed = true;
    bool hasReceivedPacket() { return !packets.empty(); }
    int16_t readData(byte *out, size_t) {
        ++reads;
        armed = false;
        if (packets.empty())
            return RADIOLIB_ERR_RX_TIMEOUT;
        auto packet = packets.front();
        packets.pop_front();
        lastLength = packet.data.size();
        memcpy(out, packet.data.data(), lastLength);
        return packet.result;
    }
    int16_t startReceive() {
        ++rearms;
        armed = rearmResult == 0;
        return rearmResult;
    }
    size_t getPacketLength() { return lastLength; }
    float getLastPacketRSSI() { return -55; }
    void setSyncWord(byte *, size_t) { ++syncChanges; }
    int16_t transmit(byte *data, size_t n) {
        sent.emplace_back(data, data + n);
        armed = false;
        return txResult;
    }
    int16_t receive(byte *, size_t) {
        assert(false && "un appel occupé ne doit pas lancer de réception");
        return -1;
    }
};
#include "radio-declarations.inc"
volatile bool FrisquetRadio::receivedFlag = false, FrisquetRadio::interruptReceive = false;
#include "radio-methods.inc"
