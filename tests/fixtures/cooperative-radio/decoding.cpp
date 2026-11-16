#include "connect-model.h"
#include "connect-declarations.inc"
#include "Frisquet/BoilerFirmware.h"
#include "connect-class.inc"
#include "connect-methods.inc"

void delay(uint32_t) { assert(false && "le décodage ne doit pas attendre"); }

struct Model {
    FrisquetRadio radio;
    Config cfg;
    MqttManager mqtt;
    Zone z1, z2, z3;
    Chaudiere boiler;
    Connect connect{radio, cfg, mqtt, z1, z2, z3, boiler};
};

std::vector<byte> response(size_t size) {
    std::vector<byte> data(size, 0);
    data[6] = size - 7;
    return data;
}
void put(std::vector<byte> &data, size_t at, uint16_t value) {
    data[at] = value >> 8;
    data[at + 1] = value;
}

int main() {
    Model m;
    auto information = response(63);
    put(information, 7, 215);
    put(information, 19, 205);
    put(information, 25, 1290); // sentinelle : température extérieure indisponible
    put(information, 41, 200);
    put(information, 43, 300);
    assert(m.connect.applyInformation(information.data(), information.size()));
    assert(m.z1.ambient == 21.5f && m.z1.setpoint == 20.5f);
    assert(m.boiler.outdoor == -999 && m.boiler.powerEcs == 20.0f && m.boiler.powerHeat == 30.0f);
    const byte version[] = {0x7e, 0x80, 55, 1, 0x81, 3, 4, 0x19, 0x02, 0, 0x11};
    assert(m.connect.setVersionFirmware(version, sizeof(version)));
    assert(m.connect.applyInformation(information.data(), information.size()));
    assert(m.boiler.powerEcs == 200.0f && m.boiler.powerHeat == 300.0f);
    assert(!m.connect.applyInformation(nullptr, information.size()));
    for (size_t n = 0; n < information.size(); ++n)
        assert(!m.connect.applyInformation(information.data(), n));
    for (byte length : {0, 57, 255}) {
        auto bad = information;
        bad[6] = length;
        put(bad, 7, 999);
        assert(!m.connect.applyInformation(bad.data(), bad.size()));
    }
    assert(m.z1.ambient == 21.5f && m.z1.setpoint == 20.5f);

    auto consumption = response(63);
    put(consumption, 25, 123);
    put(consumption, 27, 456);
    assert(m.connect.applyConsumption(consumption.data(), consumption.size()));
    assert(m.boiler.consEcs == 123 && m.boiler.heat == 456);
    assert(!m.connect.applyConsumption(nullptr, consumption.size()));
    for (size_t n = 0; n < consumption.size(); ++n)
        assert(!m.connect.applyConsumption(consumption.data(), n));
    for (byte length : {0, 55, 57, 255}) {
        auto bad = consumption;
        bad[6] = length;
        put(bad, 25, 999);
        assert(!m.connect.applyConsumption(bad.data(), bad.size()));
    }
    assert(m.boiler.consEcs == 123 && m.boiler.heat == 456);

    auto ecs = response(9);
    ecs[8] = 9;
    assert(m.connect.applyEcs(ecs.data(), ecs.size()));
    assert(m.connect.getModeECS() == Chaudiere::MODE_ECS::ECO);
    assert(!m.connect.applyEcs(nullptr, ecs.size()));
    for (size_t n = 0; n < ecs.size(); ++n)
        assert(!m.connect.applyEcs(ecs.data(), n));
    ecs[6] = 1;
    ecs[8] = 1;
    assert(!m.connect.applyEcs(ecs.data(), ecs.size()));
    assert(m.connect.getModeECS() == Chaudiere::MODE_ECS::ECO);
    puts("OK : décodage Connect, puissances, troncatures, longueurs déclarées et état préservé");
}
