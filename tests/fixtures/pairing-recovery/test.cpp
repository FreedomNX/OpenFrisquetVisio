#include <cassert>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "Frisquet/NetworkID.h"
#include "Buffer.h"

constexpr byte ID_CHAUDIERE = 0x80;
constexpr int16_t RADIOLIB_ERR_NONE = 0;
constexpr int16_t RADIOLIB_ERR_RX_TIMEOUT = -6;
constexpr size_t RADIOLIB_SX126X_MAX_PACKET_LENGTH = 255;

uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }
void delay(uint32_t ms) { clockMs += ms; }
void info(const char*, ...) {}
void logRadio(bool, byte*, size_t, float = 0) {}
std::string byteArrayToHexString(byte*, size_t) { return "synthetique"; }

bool same(const NetworkID& a, const NetworkID& b) {
    return !memcmp(a.bytes, b.bytes, 4);
}

struct Radio {
    struct Packet {
        std::vector<byte> bytes;
        int16_t result = 0;
    };

    std::deque<Packet> packets;
    std::vector<std::vector<byte>> sent;
    NetworkID network;
    size_t length = 0;
    unsigned rearms = 0;
    unsigned receives = 0;
    bool armed = true;

    void setSyncWord(byte* data, size_t n) {
        assert(n == 4);
        network = NetworkID(data);
        armed = false;
    }

    int16_t receive(byte* data, size_t n) {
        assert(n == 0 && network.isBroadcast());
        ++receives;
        clockMs += 408;
        armed = false;
        if (packets.empty()) return RADIOLIB_ERR_RX_TIMEOUT;
        auto packet = packets.front();
        packets.pop_front();
        length = packet.bytes.size();
        assert(length <= RADIOLIB_SX126X_MAX_PACKET_LENGTH);
        memcpy(data, packet.bytes.data(), length);
        return packet.result;
    }

    size_t getPacketLength() { return length; }
    float getLastPacketRSSI() { return -50; }

    int16_t transmit(byte* data, size_t n) {
        sent.emplace_back(data, data + n);
        armed = false;
        return RADIOLIB_ERR_NONE;
    }

    int16_t startReceive() {
        ++rearms;
        armed = true;
        return RADIOLIB_ERR_NONE;
    }

    bool accepts(const NetworkID& sender) {
        return armed && same(network, sender);
    }
};

struct FrisquetRadio : Radio {
    // Déclarations extraites de FrisquetRadio.h par le lanceur Python.
    #include "radio_types.inc"

    void setNetworkID(NetworkID networkID);
};

struct Config {
    NetworkID network{1, 2, 3, 4};
    unsigned saves = 0;
    NetworkID& getNetworkID() { return network; }
    void setNetworkID(NetworkID n) { network = n; }
    void save() { ++saves; }
};

struct FrisquetDevice {
    FrisquetRadio& r;
    Config& cfg;
    FrisquetRadio& radio() { return r; }
    Config& getConfig() { return cfg; }
    byte getId() { return 0x7e; }
    bool associer(NetworkID&, byte&);
};

struct FrisquetManager {
    FrisquetRadio& r;
    Config& cfg;
    FrisquetRadio& radio() { return r; }
    Config& config() { return cfg; }
    bool recupererNetworkID();
};

#include "production_methods.inc"

Radio::Packet announcement() {
    // En-tête (6), longueur (1), nouveau NetworkID (4).
    return {{0x7e, 0x80, 55, 42, 1, 0x41, 4, 5, 6, 7, 8}, 0};
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const bool manager = std::string(argv[1]) == "manager";
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        clockMs = 0;
        FrisquetRadio radio;
        Config cfg;
        radio.network = cfg.network;
        const NetworkID initial = cfg.network;
        NetworkID output{9, 10, 11, 12};
        byte association = 99;
        auto packet = announcement();
        if (scenario == 1) packet.result = -7; // erreur CRC
        if (scenario == 2) packet.bytes.resize(10);
        if (scenario == 3) packet.bytes[1] = 0x20; // mauvais expéditeur
        if (scenario == 4) packet.bytes[5] = 0x03; // mauvais type
        if (scenario == 5) {
            cfg.network = NetworkID(255, 255, 255, 255);
            radio.network = cfg.network;
        }
        if (scenario > 0 && scenario < 5) radio.packets.push_back(packet);
        const NetworkID configured = cfg.network;
        const bool ok = manager
            ? FrisquetManager{radio, cfg}.recupererNetworkID()
            : FrisquetDevice{radio, cfg}.associer(output, association);
        assert(!ok && clockMs >= 30000 && clockMs < 31000);
        assert(same(cfg.network, configured) && cfg.saves == 0);
        assert(same(output, NetworkID(9, 10, 11, 12)) && association == 99 && radio.sent.empty());
        assert(radio.accepts(configured) && radio.rearms == 1);

        // Une nouvelle tentative après cet échec peut encore réussir.
        radio.packets.push_back(announcement());
        const bool recovered = manager
            ? FrisquetManager{radio, cfg}.recupererNetworkID()
            : FrisquetDevice{radio, cfg}.associer(output, association);
        assert(recovered && radio.accepts(NetworkID(5, 6, 7, 8)) && radio.rearms == 2);
        assert(!radio.accepts(initial));
        if (manager) {
            assert(same(cfg.network, NetworkID(5, 6, 7, 8)) && cfg.saves == 1 && radio.sent.empty());
        } else {
            assert(same(cfg.network, configured) && cfg.saves == 0);
            assert(same(output, NetworkID(5, 6, 7, 8)) && association == 55 && radio.sent.size() == 10);
            for (const auto& confirmation : radio.sent) {
                assert(confirmation.size() == 10 && confirmation[0] == 0x80 && confirmation[1] == 0x7e);
                assert(confirmation[2] == 55 && confirmation[3] == 42 && confirmation[4] == 0x81 && confirmation[5] == 0x41);
                assert(!memcmp(confirmation.data() + 6, output.bytes, 4));
            }
        }
    }
    puts("OK : délai expiré, CRC, trames ignorées, réseau restauré, nouvelle tentative et écoute après succès");
}
