#pragma once
#include <stdint.h>

namespace BoilerIdentity {
constexpr uint8_t DEFAULT_ID = 0x80;
inline bool valid(uint8_t id) { return id == 0x80 || id == 0x84; }
inline uint8_t alternate(uint8_t id) { return id == 0x84 ? 0x80 : 0x84; }
inline uint16_t memoryOffset(uint8_t id) { return id == 0x84 ? 0xC8 : 0; }

// Date reçue en BCD : YY MM DD hh mm ss (années 2000 à 2099).
inline bool validDate(const uint8_t* bytes) {
    uint8_t date[6];
    for (unsigned i = 0; i < 6; ++i) {
        if ((bytes[i] >> 4) > 9 || (bytes[i] & 15) > 9) return false;
        date[i] = (bytes[i] >> 4) * 10 + (bytes[i] & 15);
    }
    if (date[1] < 1 || date[1] > 12 || date[3] > 23 || date[4] > 59 || date[5] > 59) return false;
    const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const uint8_t maxDay = days[date[1] - 1] + (date[1] == 2 && date[0] % 4 == 0 ? 1 : 0);
    return date[2] >= 1 && date[2] <= maxDay;
}
}
