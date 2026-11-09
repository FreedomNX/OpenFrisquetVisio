#pragma once

#include <stdint.h>
#include <stddef.h>

// 0x7541, deux mots : 19 01 00 08 représente 19.01.0008 en BCD.
struct BoilerFirmware {
    uint8_t major = 0;
    uint8_t minor = 0;
    uint16_t build = 0;
    bool known = false;

    bool decode(const uint8_t* data, size_t length) {
        *this = BoilerFirmware();
        if (!data || length != 4) return false;
        for (size_t i = 0; i < length; ++i) {
            if ((data[i] >> 4) > 9 || (data[i] & 0x0F) > 9) return false;
        }
        if (data[0] == 0) return false;
        major = decimal(data[0]);
        minor = decimal(data[1]);
        build = decimal(data[2]) * 100 + decimal(data[3]);
        known = true;
        return true;
    }

    bool directPower() const {
        return known && (major > 19 || (major == 19 && minor >= 2));
    }

    float power(uint16_t raw) const {
        return static_cast<int16_t>(raw) / (directPower() ? 1.0f : 10.0f);
    }

private:
    static uint8_t decimal(uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0x0F); }
};
