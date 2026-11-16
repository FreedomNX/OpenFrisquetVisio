#include <cassert>
#include <cstdint>
uint32_t millis() { return 1000; }
void delay(unsigned) { assert(false); }
struct RadioSchedule {
    static bool quietWindow(uint32_t) { return true; }
    static uint32_t clockSeconds() { return 0; }
    bool due(unsigned, unsigned) { return false; }
};
struct Zone { void publishMqtt() {} };
#include <string>
#include <cstdio>
using String = std::string;
void info(const char *, ...) {
}
void error(const char *, ...) {
}
struct Config {
    bool passive = false;
    bool useConnectPassive() { return passive; }
};
struct Radio {
    bool occupied = true;
    bool busy() { return occupied; }
};
struct Boiler {
    unsigned publishes = 0;
    void publishModeEcs() { ++publishes; }
};
struct Connect {
    bool _initialReadPending = false;
    uint32_t _lastRecuperationTemperatures = 0, _lastRecuperationConsommation = 0,
             _lastRecuperationModeECS = 0, _lastEnvoiZone = 0;
    RadioSchedule _temperatureSchedules[3], _consommationSchedule, _ecsSchedule;
    Zone _zone1, _zone2, _zone3;
    void recupererVersionFirmware() { assert(false); }
    bool recupererInformations() { assert(false); return false; }
    bool recupererConsommation() { assert(false); return false; }
    bool recupererConsommationAnnuelle() { assert(false); return false; }
    bool recupererModeECS() { assert(false); return false; }
    void publishMqtt() { assert(false); }
    void envoiZones() { assert(false); }
    void loop();
    Config config;
    Radio link;
    Boiler _chaudiere;
    bool _modeEcsCommandPending = false, associated = true;
    int _requestedModeEcs = 0, current = 0, writes = 0, lastSent = 0;
    Radio &radio() { return link; }
    Config &getConfig() { return config; }
    bool estAssocie() { return associated; }
    bool setModeECS(const String &s) {
        if (s == "eco")
            current = 1;
        else if (s == "comfort")
            current = 2;
        else
            return false;
        return true;
    }
    bool setModeECS(int mode) {
        current = mode;
        return true;
    }
    int getModeECS() { return current; }
    bool envoyerModeECS() {
        assert(!link.occupied);
        ++writes;
        lastSent = current;
        return true;
    }
    void handleModeEcsCommand(const String &);
    bool sendPendingModeEcs();
};

#include "commands-methods.inc"

int main() {
    Connect c;
    c.handleModeEcsCommand("eco");
    c.handleModeEcsCommand("comfort");
    c.handleModeEcsCommand("invalid");
    assert(!c.writes && c._modeEcsCommandPending);
    c.loop();
    assert(!c.writes);
    c.current = 99; // une ancienne observation ne doit pas remplacer la consigne différée
    c.link.occupied = false;
    c.loop();
    assert(c.lastSent == 2 && c.writes == 1);
    c.loop();
    assert(c.writes == 1 && c._chaudiere.publishes == 1);
    c.handleModeEcsCommand("eco");
    assert(c.writes == 2 && c.lastSent == 1);
    c.link.occupied = true;
    c.handleModeEcsCommand("comfort");
    c.config.passive = true;
    c.link.occupied = false;
    c.loop();
    assert(!c._modeEcsCommandPending && c.writes == 2);
    c.config.passive = false;
    c.link.occupied = true;
    c.handleModeEcsCommand("eco");
    c.associated = false;
    c.link.occupied = false;
    c.loop();
    assert(!c._modeEcsCommandPending && c.writes == 2);
    puts("OK : dernière consigne ECS différée, préservée pendant les observations, envoyée une "
         "seule fois, annulée en mode passif ou sans association");
}
