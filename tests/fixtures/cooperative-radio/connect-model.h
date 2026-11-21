#pragma once
#include <string>
using String = std::string;
#include "radio-fixture.h"

#define ID_CONNECT 0x7e
#include "Frisquet/BoilerIdentity.h"
#define ID_ZONE_1 8
#define ID_ZONE_2 9
#define ID_ZONE_3 10
void error(const char *, ...) {
}
uint32_t seconds = 0;
constexpr int timeNotSet = 0;
int timeStatus() {
    return 1;
}
uint32_t now() {
    return seconds;
}
struct Config {
    bool enabled = true, passive = false;
    byte boiler = 0x80;
    byte boilerId() const { return boiler; }
    uint16_t boilerMemoryOffset() const { return BoilerIdentity::memoryOffset(boiler); }
    bool useConnect() { return enabled; }
    bool useConnectPassive() { return passive; }
    bool useZone1() { return true; }
    bool useZone2() { return true; }
    bool useZone3() { return true; }
    bool useSondeExterieure() { return true; }
    bool useSatelliteZ1() { return true; }
    bool useSatelliteZ2() { return true; }
    bool useSatelliteZ3() { return true; }
};
struct MqttManager {};
struct FrisquetDevice {
    FrisquetRadio &_radio;
    Config &_cfg;
    byte id, association = 55, message = 0;
    FrisquetDevice(FrisquetRadio &r, Config &c, MqttManager &, byte i)
        : _radio(r), _cfg(c), id(i) {}
    FrisquetRadio &radio() { return _radio; }
    Config &getConfig() { return _cfg; }
    byte getId() { return id; }
    byte getIdAssociation() { return association; }
    bool estAssocie() { return association != 255; }
    byte incrementIdMessage() { return ++message; }
};
struct Zone {
    enum class SOURCE { CONNECT, SATELLITE };
    SOURCE source = SOURCE::CONNECT;
    unsigned publishes = 0, calls = 0;
    float ambient = -999, setpoint = -999, departure = -999, target = -999;
    uint32_t changed = 0, sent = 0;
    bool enAttente() const {return false;}
    uint32_t getLastChange() { return changed; }
    uint32_t getLastEnvoi() { return sent; }
    SOURCE getSource() { return source; }
    void setTemperatureAmbiante(float v) { ambient = v; }
    void setTemperatureConsigne(float v) { setpoint = v; }
    void setTemperatureDepart(float v) { departure = v; }
    void setTemperatureConsigneDepart(float v) { target = v; }
    void publishMqtt() { ++publishes; }
    void loop() { ++calls; }
};
struct Chaudiere {
    enum class MODE_ECS : byte { INCONNU = 255, ECO = 8, MAX = 0 };
    MODE_ECS mode = MODE_ECS::INCONNU, lastSent = MODE_ECS::INCONNU;
    std::vector<MODE_ECS> publishedModes;
    unsigned publishes = 0, writes = 0, zoneWrites = 0;
    float outdoor = -999, ecs = -999, cdc = -999, fumes = -999, powerEcs = -999, powerHeat = -999,
          pressure = -999;
    int heat = -999, consEcs = -999, annualHeat = -999, annualEcs = -999;
    uint16_t state = 0xffff;
    byte boilerStatus = 0;
    bool statusKnown = false;
    void setEtatChaudiere(byte value) { boilerStatus = value; statusKnown = true; }
    void setTemperatureExterieure(float v) { outdoor = v; }
    void setTemperatureECS(float v) { ecs = v; }
    void setTemperatureCDC(float v) { cdc = v; }
    void setTemperatureFumees(float v) { fumes = v; }
    void setPuissanceInstantaneeECS(float v) { powerEcs = v; }
    void setPuissanceInstantaneeChauffage(float v) { powerHeat = v; }
    void setModeFonctionnement(uint16_t v) { state = v; }
    void setPression(float v) { pressure = v; }
    void setConsommationChauffage(int v) { heat = v; }
    void setConsommationECS(int v) { consEcs = v; }
    void setConsommationAnnuelle(int h, int e) {
        annualHeat = h;
        annualEcs = e;
    }
    MODE_ECS getModeECS() { return mode; }
    bool setModeECS(MODE_ECS v) {
        mode = v;
        return true;
    }
    bool setModeECS(const String &s) {
        if (s == "eco")
            mode = MODE_ECS::ECO;
        else if (s == "max")
            mode = MODE_ECS::MAX;
        else
            return false;
        return true;
    }
    void publishModeEcs() {
        ++publishes;
        publishedModes.push_back(mode);
    }
};
