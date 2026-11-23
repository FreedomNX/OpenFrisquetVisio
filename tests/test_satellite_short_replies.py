"""Virtual satellite: incomplete boiler replies must not be used.

The real transport (sendAsk/receiveExpected) accepts a reply shorter than the
expected structure and returns the received length. The real satellite
methods must then reject it before reading the boiler state, date or CDC
temperature. Run: python3 tests/test_satellite_short_replies.py
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
satellite = (ROOT / 'src/Frisquet/Satellite.cpp').read_text()


def extract(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def method(path, signature):
    return extract((ROOT / path).read_text(), signature)


# The four exchanges reading the A029 or A05E reply check it before use.
for name, reply in (('recupererInfosChaudiere', 'donneesZones'),
                    ('recupererTemperatureCDC', 'donneesTemperatureCDC'),
                    ('envoyerTemperatureAmbiante', 'donneesZones'),
                    ('envoyerConsigne', 'donneesZones')):
    body = extract(satellite, f'bool Satellite::{name}(')
    check = f'!reponseComplete(&{reply}, length, sizeof({reply}))'
    assert check in body, name
    assert body.index(check) < body.index('return true;'), name

radio = re.sub(r'^#.*\n', '', (ROOT / 'src/Frisquet/FrisquetRadio.h').read_text(), flags=re.M)
code = r'''
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
using byte = uint8_t;
using std::isnan;
uint32_t tick = 0;
uint32_t millis() { return tick; }
void delay(uint32_t ms) { tick += ms; }
void info(const char*, ...) {}
void debug(const char*, ...) {}
void error(const char*, ...) {}
void logRadio(bool, const byte*, size_t, float = 0) {}
constexpr int RADIOLIB_ERR_NONE = 0, RADIOLIB_ERR_RX_TIMEOUT = -6;
constexpr int RADIOLIB_ERR_TX_TIMEOUT = -5, RADIOLIB_ERR_ADDRESS_NOT_FOUND = -7;
constexpr size_t RADIOLIB_SX126X_MAX_PACKET_LENGTH = 255;
struct NetworkID { byte bytes[4]; };
''' + method('src/Frisquet/Utils.h', 'struct fword') + ';\n' \
    + method('src/Frisquet/Utils.h', 'struct temperature16') + r''';
// Boiler side: answers every request with `reply`, header taken from the request.
struct Radio {
    std::vector<byte> reply;
    byte header[6] = {};
    unsigned transmits = 0;
    int transmit(byte* data, size_t n) {
        assert(n >= 6); ++transmits;
        memcpy(header, data, 6);
        std::swap(header[0], header[1]); header[4] |= 0x80;
        return 0;
    }
    int startReceive() { return 0; }
    bool hasReceivedPacket() { return true; }
    int readData(byte* out, size_t) {
        memcpy(out, reply.data(), reply.size());
        memcpy(out, header, reply.size() < 6 ? reply.size() : 6);
        return 0;
    }
    int receive(byte* out, size_t n) { return readData(out, n); }
    size_t getPacketLength() { return reply.size(); }
    float getLastPacketRSSI() { return -60; }
};
''' + radio + r'''
volatile bool FrisquetRadio::receivedFlag = false;
volatile bool FrisquetRadio::interruptReceive = false;
bool FrisquetRadio::bufferUnexpectedPacket(const byte*, size_t) { return false; }
''' + '\n'.join(method('src/Frisquet/FrisquetRadio.cpp', signature) for signature in (
    'int16_t FrisquetRadio::sendAsk(', 'int16_t FrisquetRadio::selectPacket(',
    'int16_t FrisquetRadio::receiveExpected(')) + r'''
''' + extract(satellite, 'static bool reponseComplete(') + r'''
struct Zone { byte getNumeroZone() { return 1; } };
struct Config { byte boilerId() { return 0x80; } };
struct Chaudiere {
    float cdc = -1;
    void setTemperatureCDC(float value) { cdc = value; }
};
struct Satellite {
    FrisquetRadio radioMock; Config cfg; Zone _zone; Chaudiere _chaudiere;
    std::vector<byte> etats; unsigned dates = 0;
    byte getId() { return 8; }
    byte getNumeroZone() { return 1; }
    bool estAssocie() { return true; }
    byte getIdAssociation() { return 0x2C; }
    byte message = 0;
    byte incrementIdMessage() { return ++message; }
    Config& getConfig() { return cfg; }
    FrisquetRadio& radio() { return radioMock; }
    void setEtatChaudiere(byte etat) { etats.push_back(etat); }
    bool setDateRecue(const byte*) { ++dates; return true; }
    bool recupererInfosChaudiere();
    bool recupererTemperatureCDC();
};
''' + extract(satellite, 'bool Satellite::recupererInfosChaudiere(') + '\n' \
    + extract(satellite, 'bool Satellite::recupererTemperatureCDC(') + r'''

// A029, 0x15 words: header, length 0x2A, then 42 bytes (boiler state at 17).
std::vector<byte> zones(size_t size, byte declared = 0x2A) {
    std::vector<byte> reply(size, 0x5A);
    if (size > 6) reply[6] = declared;
    if (size > 17) reply[17] = 0x28;
    return reply;
}

int main() {
    // Complete replies are used.
    Satellite ok; ok.radioMock.reply = zones(49);
    assert(ok.recupererInfosChaudiere() && ok.etats == std::vector<byte>{0x28} && ok.dates == 1);
    Satellite cdc; cdc.radioMock.reply = {0, 0, 0, 0, 0, 0, 2, 0x02, 0x6C};  // 62.0 °C
    assert(cdc.recupererTemperatureCDC() && cdc._chaudiere.cdc == 62.0f);

    // Short, long or inconsistent replies: success from the transport, but
    // nothing is read; one retry, then failure.
    const std::vector<std::vector<byte>> rejected = {
        zones(7), zones(18), zones(48), zones(60, 0x35), zones(49, 0x29), zones(49, 0x2B),
    };
    for (const auto& reply : rejected) {
        Satellite s; s.radioMock.reply = reply;
        byte out[255]; size_t length = 49;
        assert(s.radio().sendAsk(8, 0x80, 0x2C, 1, 1, 0xA029, 0x15, out, length) == RADIOLIB_ERR_NONE);
        assert(!s.recupererInfosChaudiere() && s.etats.empty() && s.dates == 0);
    }
    for (const auto& reply : std::vector<std::vector<byte>>{
            {0, 0, 0, 0, 0, 0, 2, 0x02}, {0, 0, 0, 0, 0, 0, 1, 0x02, 0x6C}, {0, 0, 0, 0, 0, 0, 2}}) {
        Satellite s; s.radioMock.reply = reply;
        assert(!s.recupererTemperatureCDC() && s._chaudiere.cdc == -1);
    }
    puts("OK: virtual satellite rejects short, long and inconsistent boiler replies");
}
'''

with tempfile.TemporaryDirectory(prefix='ofv-satellite-replies-') as directory:
    temp = Path(directory)
    (temp / 'test.cpp').write_text(code)
    binary = temp / 'test'
    subprocess.run([
        'c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-copy',
        '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        '-fno-omit-frame-pointer', '-I', str(ROOT / 'src'), str(temp / 'test.cpp'), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
