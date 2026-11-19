#include "RadioCrc.h"
#include <assert.h>
#include <stdio.h>

using namespace RadioCrc;

static void seal(uint8_t* raw) {
    const uint16_t crc = crc16(raw, raw[0] + 1);
    raw[raw[0] + 1] = crc >> 8;
    raw[raw[0] + 2] = crc;
}

static void slip(const uint8_t* src, uint8_t* dst, size_t at, bool extraZero) {
    memset(dst, 0, RAW_LENGTH);
    for (size_t bit = 0; bit < RAW_LENGTH * 8 - 1; ++bit) {
        bool value = bit < at ? getBit(src, bit)
            : extraZero ? (bit == at ? false : getBit(src, bit - 1)) : getBit(src, bit + 1);
        if (value) dst[bit / 8] |= 1U << (7 - bit % 8);
    }
}

int main() {
    // Vecteur indépendant : CRC-16/SPI-FUJITSU de "123456789" = E5CC,
    // puis complément final demandé par le SX1262 = 1A33.
    assert(crc16(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0x1A33);
    const uint8_t logged[] = {
        63, 0x7E,0x80,0xAA,0xBE,0x81,0x03,0x38,0x00,0xCD,0x04,0xF6,0x04,0xF6,0x01,0x80,
        0x00,0xC6,0x00,0xC6,0x00,0xCD,0,0,0,0,0,0x67,0x02,0x9F,0x02,0xB1,0x02,0x28,
        0x04,0xF6,0x04,0xF6,0x01,0xE7,0,0,0,0,0,0,0,0,0x1E,0,0,0x05,0,0,0,0,0,0,0,0,0,0,0,0
    };
    static_assert(sizeof(logged) == 64, "log fixture");
    assert(crc16(logged, sizeof(logged)) == 0x222F);
    uint8_t raw[RAW_LENGTH] = {}, damaged[RAW_LENGTH], out[MAX_PAYLOAD + 2];
    memcpy(raw, logged, sizeof(logged));
    seal(raw);
    memset(out, 0xA5, sizeof(out));
    assert(decode(raw, sizeof(raw), out + 1).status == Status::Valid);
    assert(memcmp(out + 1, raw + 1, MAX_PAYLOAD) == 0);
    assert(out[0] == 0xA5 && out[MAX_PAYLOAD + 1] == 0xA5);

    // Perte ou ajout n'importe où dans la dernière suite : reconstruction au
    // début de cette suite, CRC compris. Les octets de marge sont du bruit.
    const bool directions[] = {false, true};
    for (bool extra : directions) {
        for (size_t bit = 52 * 8; bit < 64 * 8; ++bit) {
            slip(raw, damaged, bit, extra);
            damaged[67] = 0xAD;
            const Result result = decode(damaged, sizeof(damaged), out + 1);
            assert(result.status == Status::Repaired);
            assert(result.correctedBits == 1);
            assert(result.adjustment == (extra ? -1 : 1));
            assert(memcmp(out + 1, raw + 1, MAX_PAYLOAD) == 0);
        }
    }
    // Premier bit de CRC à 1 : le zéro manquant produit bien un 01 final.
    raw[4] = 0xC0;
    raw[31] = 0xA4; raw[33] = 0x1F; raw[39] = 0xDE;
    seal(raw);
    assert(raw[64] & 0x80);
    slip(raw, damaged, 450, false);
    assert(damaged[63] == 1);
    assert(decode(damaged, sizeof(damaged), out).status == Status::Repaired);

    // Capture réelle du 21/10 à 19:09:23 : un zéro perdu vers 255, deux vers 334.
    const uint8_t realMulti[RAW_LENGTH] = {
        0x31,0x08,0x80,0x0A,0x06,0x81,0x17,0x2A,0x00,0x64,0x00,0x00,
        0x26,0x10,0x21,0x19,0x09,0x23,0x28,0x03,0x00,0xC8,0x00,0xCD,
        0x00,0x01,0,0,0,0,0x04,0xF6,0,0,0,0,0,0,0,0,0x09,0xEC,
        0,0,0,0,0,0,0,0x02,0x96,0x59,0x55,0x55,0x55,0x3B,0xB2,
        0x7F,0x27,0xEF,0xFF,0xFE,0xE3,0xFF,0x7F,0x2F,0xAA,0x7E
    };
    memset(out, 0xA5, sizeof(out));
    assert(decode(realMulti, sizeof(realMulti), out + 1).status == Status::Invalid);
    for (uint8_t byte : out) assert(byte == 0xA5);

    // Deux changements dans une même longue suite (au moins 64 zéros) sont
    // réparés dans les deux sens ; trois restent rejetés.
    uint8_t intermediate[RAW_LENGTH];
    for (uint8_t count = 2; count <= 3; ++count) {
        for (bool extra : directions) {
            memcpy(damaged, raw, sizeof(raw));
            for (uint8_t i = 0; i < count; ++i) {
                slip(damaged, intermediate, 450, extra);
                memcpy(damaged, intermediate, sizeof(damaged));
            }
            memset(out, 0xA5, sizeof(out));
            const Result result = decode(damaged, sizeof(damaged), out + 1);
            if (count == 2) {
                assert(result.status == Status::Repaired && result.correctedBits == 2);
                assert(result.adjustment == (extra ? -2 : 2));
                assert(memcmp(out + 1, raw + 1, MAX_PAYLOAD) == 0);
                assert(out[0] == 0xA5 && out[MAX_PAYLOAD + 1] == 0xA5);
            } else {
                assert(result.status == Status::Invalid && result.correctedBits == 0);
                for (uint8_t byte : out) assert(byte == 0xA5);
            }
        }
    }
    // Quatre zéros manquants dépassent la limite autorisée.
    memcpy(damaged, raw, sizeof(raw));
    for (uint8_t i = 0; i < 4; ++i) {
        slip(damaged, intermediate, 450, false);
        memcpy(damaged, intermediate, sizeof(damaged));
    }
    assert(decode(damaged, sizeof(damaged), out).status == Status::Invalid);

    // Deux suites différentes, y compris un glissement dans chaque sens.
    for (bool extraFirst : directions) {
        for (bool extraSecond : directions) {
            slip(raw, intermediate, 450, extraSecond);
            slip(intermediate, damaged, 200, extraFirst);
            memset(out, 0xA5, sizeof(out));
            assert(decode(damaged, sizeof(damaged), out).status == Status::Invalid);
            for (uint8_t byte : out) assert(byte == 0xA5);
        }
    }

    // Ancien cas ambigu de la recherche étendue : -1 aux bits 95 et 286 (deux
    // suites, non recherché) ou +2 au bit 411 (une suite de 99 zéros). Seule
    // la seconde solution relève du modèle à un endroit et elle est unique.
    const uint8_t extendedAmbiguous[RAW_LENGTH] = {
        0x3F,0x64,0x3D,0x0A,0xFA,0xF0,0x03,0xAA,0x9E,0x59,0xAB,0x4E,
        0,0,0,0,0,0,0xBA,0xE4,0x49,0x98,0x5C,0x47,0,0,0,0,0,0,
        0xB6,0x7F,0x8D,0x2A,0x3E,0xDC,0,0,0,0,0,0,
        0xBA,0xD6,0x18,0x29,0x65,0xC8,0x7A,0xA7,0xA1,0x60,
        0,0,0,0,0,0,0,0,0,0,0,0x02,0x91,0x96,0x55,0x78
    };
    const Result extended = decode(extendedAmbiguous, RAW_LENGTH, out);
    assert(extended.status == Status::Repaired && extended.bit == 411 && extended.adjustment == 2);
    memset(out, 0xA5, sizeof(out));

    // Ancien rejet à 14:28 : glissement + autre altération du CRC. Il doit
    // rester rejeté avec la recherche limitée à un seul changement.
    const uint8_t badCrc[RAW_LENGTH] = {
        0x3F,0x7E,0x80,0xAA,0x3A,0x81,0x03,0x38,0x00,0xC8,0x04,0xF6,
        0x04,0xF6,0x01,0x16,0x00,0xC6,0x00,0xC6,0x00,0xCD,0,0,0,0,
        0,0x6B,0x02,0x65,0x02,0xC4,0x01,0x45,0x04,0xF6,0x04,0xF6,
        0x01,0xE3,0,0,0,0,0,0,0,0,0x1E,0,0,0x05,
        0,0,0,0,0,0,0,0,0,0,0,0x01,0xFD,0x7C,0x55,0x55
    };
    assert(decode(badCrc, RAW_LENGTH, out).status == Status::Invalid);
    for (uint8_t byte : out) assert(byte == 0xA5);

    // Trame très fragmentée : recherche simple bornée par le nombre de suites,
    // sans combinaisons ni passe supplémentaire.
    uint8_t fragmented[RAW_LENGTH] = {};
    fragmented[0] = 63; fragmented[6] = 3; fragmented[65] = 0x12;
    for (size_t bit = 56; bit < 512; bit += 17)
        fragmented[bit / 8] |= 1U << (7 - bit % 8);
    assert(decode(fragmented, RAW_LENGTH, out).status == Status::Invalid);
    for (uint8_t byte : out) assert(byte == 0xA5);

    // Deux reconstructions différentes passent le CRC : rejeter sans copier.
    const uint8_t ambiguous[RAW_LENGTH] = {
        0x3F,0xB7,0xCE,0xD6,0xAF,0x03,0x03,0x46,0x97,0xA7,0x5F,0x13,
        0,0,0,0,0,0,0xD9,0x48,0x55,0x02,0xD9,0x54,0,0,0,0,0,0,
        0x98,0x8E,0xFE,0x82,0x44,0x2E,0,0,0,0,0,0,
        0xB5,0x6F,0xCF,0x37,0x43,0x92,0x22,0xD0,0x63,0x27,
        0,0,0,0,0,0,0,0,0,0,0,0x01,0x80,0xF5,0x60,0x08
    };
    memset(out, 0xA5, sizeof(out));
    assert(decode(ambiguous, sizeof(ambiguous), out).status == Status::Ambiguous);
    for (uint8_t byte : out) assert(byte == 0xA5);

    // Glissement au milieu du paquet, vers le bit 256, et sur réponse courte.
    // Deux glissements dans une suite plus courte que 64 zéros restent rejetés.
    const uint8_t slipLengths[] = {15, 49, 63};
    for (uint8_t length : slipLengths) {
        memset(raw, 0xAD, sizeof(raw));
        raw[0] = length;
        const size_t start = length == 15 ? 9 : 32;
        memset(raw + start, 0, 5);
        seal(raw);
        for (bool extra : directions) {
            slip(raw, damaged, start * 8 + 16, extra);
            assert(decode(damaged, sizeof(damaged), out).status == Status::Repaired);
            assert(memcmp(out, raw + 1, length) == 0);
            slip(damaged, intermediate, start * 8 + 16, extra);
            assert(decode(intermediate, sizeof(intermediate), out).status == Status::Invalid);
        }
    }

    // Captures réelles du 31/10 (réponse de 63 octets, un zéro de trop deux
    // fois) et du 09/11 (satellite, 49 octets, deux zéros perdus) : la seconde
    // porte l'horodatage 26 11 09 08 40 12, cohérent avec sa réception.
    const uint8_t realDoubleExtra[RAW_LENGTH] = {
        0x3F,0x7E,0x80,0x6E,0x61,0x81,0x03,0x38,0x02,0xB1,0x03,0x29,0x01,0xF4,
        0x04,0xF6,0x04,0xF6,0x02,0x1F,0,0,0,0,0,0,0,0,0x20,0,0,0x0D,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0x19,0x18,0x1A,0xB9,0xB4,0x1D,0x4A,0xAA
    };
    const uint8_t realDoubleExtraTail[] = {0x00,0x00,0x64,0x60,0x6A,0xE6};
    Result real = decode(realDoubleExtra, RAW_LENGTH, out);
    assert(real.status == Status::Repaired && real.adjustment == -2 && real.bit == 256);
    assert(memcmp(out, realDoubleExtra + 1, 32) == 0);
    assert(memcmp(out + 57, realDoubleExtraTail, sizeof(realDoubleExtraTail)) == 0);
    const uint8_t realDoubleMissing[RAW_LENGTH] = {
        0x31,0x08,0x80,0xAC,0x34,0x81,0x17,0x2A,0x00,0x4A,0x00,0x00,
        0x26,0x11,0x09,0x08,0x40,0x12,0x28,0x01,0x00,0xC0,0x00,0xC3,
        0x00,0x25,0x01,0x79,0x01,0x7C,0x04,0xF6,0,0,0,0,0,0,0,0,
        0x04,0xF6,0,0,0,0,0,0,0,0x01,0x5D,0x04,0xAA,0xAA,0xAA,0x8C,
        0xED,0xB7,0x97,0x0D,0x0C,0x8E,0x1D,0xE8,0x38,0x9C,0xAA,0x5C
    };
    const uint8_t realDoubleMissingTail[] = {0x04,0xF6,0,0,0,0,0,0,0,0};
    real = decode(realDoubleMissing, RAW_LENGTH, out);
    assert(real.status == Status::Repaired && real.adjustment == 2 && real.correctedBits == 2);
    assert(memcmp(out + 39, realDoubleMissingTail, sizeof(realDoubleMissingTail)) == 0);

    // Formats courts et toutes les longueurs supportées : ignorer la marge.
    for (size_t length = HEADER_LENGTH; length <= MAX_PAYLOAD; ++length) {
        memset(raw, 0xAD, sizeof(raw));
        raw[0] = length;
        seal(raw);
        assert(decode(raw, sizeof(raw), out).status == Status::Valid);
    }
    // Ne pas tenter de réparer l'en-tête, les runs courts ou le CRC seul.
    const size_t corruptBits[] = {12, 80, 64 * 8};
    for (size_t bit : corruptBits) {
        memcpy(damaged, raw, sizeof(raw));
        damaged[bit / 8] ^= 1U << (7 - bit % 8);
        memset(out, 0xA5, sizeof(out));
        assert(decode(damaged, sizeof(damaged), out).status == Status::Invalid);
        for (uint8_t byte : out) assert(byte == 0xA5);
    }
    const uint8_t badLengths[] = {0, 5, 64, 255};
    for (uint8_t length : badLengths) {
        raw[0] = length;
        assert(decode(raw, sizeof(raw), out).status == Status::InvalidLength);
    }
    assert(decode(nullptr, 0, out).status == Status::Invalid);
    assert(decode(raw, sizeof(raw), nullptr).status == Status::Invalid);
    // Toutes les tailles de capture tronquées ou excessives restent bornées.
    raw[0] = MAX_PAYLOAD;
    for (size_t length = 0; length < MAX_PAYLOAD + 3; ++length)
        assert(decode(raw, length, out).status != Status::Repaired);
    assert(decode(raw, RAW_LENGTH + 1, out).status == Status::Invalid);
    puts("OK: CRC, frames 6..63, single slips, double slips in long zero runs, rejection of other multiple slips, ambiguity and bounds");
}
