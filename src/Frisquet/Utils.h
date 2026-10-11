#pragma once
#include <Board.h>

struct fword {
    fword() {}
    fword(byte b1, byte b2) {
        bytes[0] = b1;
        bytes[1] = b2;
    }
    fword(byte bytes[]) {
        memcpy(this->bytes, bytes, 2);
    }
    fword(uint16_t intValue) {
        this->bytes[0] = (intValue >> 8) & 0xFF;
        this->bytes[1] = intValue & 0xFF;
    }

    uint16_t toUInt16() const {
        uint16_t intValue = bytes[0] << 8 | bytes[1];
        return intValue;
    }

    uint16_t toInt16() const {
        int16_t intValue = bytes[0] << 8 | bytes[1];
        return intValue;
    }

    byte bytes[2] = {0};
};

struct temperature8 {
    uint8_t value = 0;

    temperature8() {}
    temperature8(uint8_t b0) {
        this->value = b0;
    }
    temperature8(float value) {
        if(isnan(value)) {
            value = 5;
        }
        this->value = uint8_t((round(value * 2.0f) / 2)*10) - 50;   // Début 5°C -> 0 - MAX 30°C -> 250, incrément 0,5
    }

    float toFloat() const {
        return float((this->value+50) / 10.0f);
    }

    bool operator ==(const temperature8 b) {
        return value == b.value;
    }
    bool operator ==(const float b) {
        return value == temperature8(b).value;
    }
    bool operator !=(const temperature8 b) {
        return value != b.value;
    }
    bool operator !=(const float b) {
        return value != temperature8(b).value;
    }
    bool operator <=(const temperature8 b) {
        return value <= b.value;
    }
    bool operator <=(const float b) {
        return value <= temperature8(b).value;
    }
    bool operator <(const temperature8 b) {
        return value <=b.value;
    }
    bool operator <(const float b) {
        return value < temperature8(b).value;
    }
    bool operator >=(const temperature8 b) {
        return value >= b.value;
    }
    bool operator >=(const float b) {
        return value >= temperature8(b).value;
    }
    bool operator >(const temperature8 b) {
        return value > b.value;
    }
    bool operator >(const float b) {
        return value > temperature8(b).value;
    }
};

struct temperature16 {
    byte bytes[2] = {0};

    temperature16() {}
    temperature16(byte b0, byte b1) {
        this->bytes[0] = b0;
        this->bytes[1] = b1;
    }
    temperature16(byte bytes[]) {
        memcpy(this->bytes, bytes, 2);
    }
    temperature16(float value) {
        if(isnan(value)) {
            value = 0;
        }
        int16_t intValue = static_cast<int16_t>(round(value * 10.0f));
        this->bytes[0] = (intValue >> 8) & 0xFF;
        this->bytes[1] = intValue & 0xFF;
    }

    float toFloat() const {
        int16_t intValue = static_cast<int16_t>((bytes[0] << 8) | bytes[1]);
        return intValue / 10.0f;
    }

    bool operator ==(const temperature16 b) {
        return toFloat() == b.toFloat();
    }
    bool operator ==(const float b) {
        return toFloat() == b;
    }
    bool operator !=(const temperature16 b) {
        return toFloat() != b.toFloat();
    }
    bool operator !=(const float b) {
        return toFloat() != b;
    }
    bool operator <=(const temperature16 b) {
        return toFloat() <= b.toFloat();
    }
    bool operator <=(const float b) {
        return toFloat() <= b;
    }
    bool operator <(const temperature16 b) {
        return toFloat() < b.toFloat();
    }
    bool operator <(const float b) {
        return toFloat() < b;
    }
    bool operator >=(const temperature16 b) {
        return toFloat() >= b.toFloat();
    }
    bool operator >=(const float b) {
        return toFloat() >= b;
    }
    bool operator >(const temperature16 b) {
        return toFloat() == b.toFloat();
    }
    bool operator >(const float b) {
        return toFloat() > b;
    }
};

struct pression16 {
    byte bytes[2] = {0};

    pression16() {}
    pression16(byte b0, byte b1) {
        this->bytes[0] = b0;
        this->bytes[1] = b1;
    }
    pression16(byte bytes[]) {
        memcpy(this->bytes, bytes, 2);
    }
    pression16(float value) {
        if(isnan(value)) {
            value = 0;
        }
        int16_t intValue = static_cast<int16_t>(round(value * 5120.0f));
        this->bytes[0] = (intValue >> 8) & 0xFF;
        this->bytes[1] = intValue & 0xFF;
    }

    float toFloat() const {
        int16_t intValue = static_cast<int16_t>((bytes[0] << 8) | bytes[1]);
        return intValue / 5120.0f;
    }

    bool operator ==(const temperature16 b) {
        return toFloat() == b.toFloat();
    }
    bool operator ==(const float b) {
        return toFloat() == b;
    }
    bool operator !=(const temperature16 b) {
        return toFloat() != b.toFloat();
    }
    bool operator !=(const float b) {
        return toFloat() != b;
    }
    bool operator <=(const temperature16 b) {
        return toFloat() <= b.toFloat();
    }
    bool operator <=(const float b) {
        return toFloat() <= b;
    }
    bool operator <(const temperature16 b) {
        return toFloat() < b.toFloat();
    }
    bool operator <(const float b) {
        return toFloat() < b;
    }
    bool operator >=(const temperature16 b) {
        return toFloat() >= b.toFloat();
    }
    bool operator >=(const float b) {
        return toFloat() >= b;
    }
    bool operator >(const temperature16 b) {
        return toFloat() == b.toFloat();
    }
    bool operator >(const float b) {
        return toFloat() > b;
    }
};

struct Date {
    uint8_t annee = 0;
    uint8_t mois = 0;
    uint8_t jour = 0;
    uint8_t heure = 0;
    uint8_t minute = 0;
    uint8_t seconde = 0;

    Date() {}
    Date(byte donnees[6]) {
        annee = ((donnees[0] >> 4) * 10) + (donnees[0] & 0x0F);
        mois = ((donnees[1] >> 4) * 10) + (donnees[1] & 0x0F);
        jour = ((donnees[2] >> 4) * 10) + (donnees[2] & 0x0F);
        heure = ((donnees[3] >> 4) * 10) + (donnees[3] & 0x0F);
        minute = ((donnees[4] >> 4) * 10) + (donnees[4] & 0x0F);
        seconde = ((donnees[5] >> 4) * 10) + (donnees[5] & 0x0F);
    }

    uint32_t toTime() const {
        uint16_t fullYear = 2000 + annee;
        uint32_t jours = 0;

        // Ajoute les jours des années précédentes
        for (uint16_t y = 2000; y < fullYear; y++) {
            jours += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
        }

        // Ajoute les jours des mois précédents
        static const uint8_t jpm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
        for (uint8_t m = 1; m < mois; m++) {
            jours += jpm[m - 1];
            if (m == 2 && (fullYear % 4 == 0 && (fullYear % 100 != 0 || fullYear % 400 == 0)))
                jours++; // février bissextile
        }

        // Ajoute les jours du mois courant
        jours += (jour - 1);

        // Convertit tout en secondes
        uint32_t secondes = (jours * 86400UL)
                        + (heure * 3600UL)
                        + (minute * 60UL)
                        + seconde;

        return secondes;
    }
};

String byteArrayToHexString(uint8_t *byteArray, int length);