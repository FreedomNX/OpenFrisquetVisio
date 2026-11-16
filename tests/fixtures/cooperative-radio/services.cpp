#include <cassert>
#include <cstdint>
#include <cstdio>
uint32_t clockMs = 0;
uint32_t millis() {
    return clockMs;
}
void delay(unsigned ms) {
    clockMs += ms;
}
struct Service {
    unsigned calls = 0, polls = 0;
    void loop() { ++calls; }
    void pollObservation() { ++polls; }
};
struct Config {
    bool useZone1() { return true; }
    bool useZone2() { return true; }
    bool useZone3() { return true; }
    bool useConnect() { return true; }
    bool useSondeExterieure() { return true; }
    bool useSatelliteZ1() { return true; }
    bool useSatelliteZ2() { return true; }
    bool useSatelliteZ3() { return true; }
};
struct FrisquetRadio {
    static inline bool receivedFlag = false;
    bool occupied = false, packet = true;
    unsigned polls = 0;
    void pollObservation() { ++polls; }
    bool busy() { return occupied; }
    bool hasReceivedPacket() { return packet; }
    bool hasBufferedPacket() { return false; }
};
struct FrisquetManager {
    FrisquetRadio _radio;
    Config _cfg;
    Service _zone1, _zone2, _zone3, _connect, _sondeExterieure, _satelliteZ1, _satelliteZ2,
        _satelliteZ3;
    unsigned dispatches = 0;
    void onRadioReceive() {
        ++dispatches;
        _radio.packet = false;
        _radio.occupied = true;
    }
    void loop();
};
struct App {
    Service _ota, portal, _mqtt;
    Service *_portal = &portal;
    FrisquetManager _frisquetManager;
    void loop();
};

#include "services-methods.inc"

int main() {
    App app;
    auto &m = app._frisquetManager;
    for (unsigned n = 0; n < 50; ++n)
        app.loop();
    assert(clockMs == 500 && app._ota.calls == 50 && app.portal.calls == 50 &&
           app._mqtt.calls == 50);
    assert(m._radio.polls == 50 && m.dispatches == 1 && !m._connect.calls && !m._zone1.calls);
    assert(m._satelliteZ1.polls == 50 && m._satelliteZ2.polls == 50 && m._satelliteZ3.polls == 50);
    // Un paquet arrivé pendant l'observation appartient encore à son propriétaire.
    m._radio.packet = true;
    app.loop();
    assert(m.dispatches == 1 && m._radio.packet && !m._connect.calls);
    assert(app._ota.calls == 51 && app.portal.calls == 51 && app._mqtt.calls == 51);
    m._radio.packet = false;
    m._radio.occupied = false;
    app.loop();
    assert(m._connect.calls == 1 && m._zone1.calls == 1 && m._satelliteZ1.calls == 1 &&
           clockMs == 520);
    puts("OK : les boucles App et du gestionnaire exécutent OTA, HTTP et MQTT pendant les attentes "
         "radio");
}
