"""A consumption read while MQTT is down is published once the broker is back.

Compiles the real Chaudiere::publishMqtt() with a broker that can be cut off.
Run: python3 tests/test_consumption_mqtt_outage.py
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/Frisquet/Chaudiere.cpp").read_text()


def extract(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


program = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using std::isfinite;
using std::isnan;
struct String : std::string {
    String(const char* s = "") : std::string(s) {}
};
struct Entity { const char* name; };
struct Mqtt {
    bool online = false;
    std::map<std::string, std::vector<std::string>> sent;
    bool publishState(const Entity& e, const String& payload) {
        if (!online) return false;  // PubSubClient::publish() fails while disconnected
        sent[e.name].push_back(payload);
        return true;
    }
    bool publishState(const Entity& e, float value, uint8_t decimals = 2) {
        char buf[32];
        snprintf(buf, sizeof buf, "%.*f", decimals, value);
        return publishState(e, String(buf));
    }
    bool publishAvailability(const Entity&, bool) { return online; }
};
struct Config { bool useSondeExterieure() const { return false; } };
struct Etat { bool anomalieDetecte() const { return false; } };
struct Chaudiere {
    enum MODE_ECS : uint8_t { INCONNU = 0xFF };
    Mqtt _mqtt;
    Config _cfg;
    struct {
        Entity etatChaudiere{"etat"}, anomalieChaudiere{"anomalie"}, tempECS{"tempECS"}, tempCDC{"tempCDC"},
            tempFumees{"tempFumees"}, tempExterieure{"tempExterieure"},
            puissanceInstantaneeECS{"puissanceECS"}, puissanceInstantaneeChauffage{"puissanceChauffage"},
            consommationChauffage{"chauffage"}, consommationECS{"ecs"},
            consommationAnnuelleChauffage{"annuelleChauffage"}, consommationAnnuelleECS{"annuelleECS"},
            modeECS{"modeECS"}, pression{"pression"};
    } _mqttEntities;
    int16_t chauffage = -1, ecs = -1;
    int32_t annuelleChauffage = -1, annuelleECS = -1;
    int16_t _lastPubConsommationECS = -1;
    int16_t _lastPubConsommationChauffage = -1;
    int32_t _lastPubConsommationAnnuelleChauffage = -1;
    int32_t _lastPubConsommationAnnuelleECS = -1;
    String getNomEtatChaudiere() const { return "Arrêt"; }
    bool etatChaudiereConnu() const { return true; }
    Etat getEtatChaudiere() const { return {}; }
    float getTemperatureECS() const { return NAN; }
    float getTemperatureCDC() const { return NAN; }
    float getTemperatureFumees() const { return NAN; }
    float getTemperatureExterieure() const { return NAN; }
    float getPuissanceInstantaneeECS() const { return NAN; }
    float getPuissanceInstantaneeChauffage() const { return NAN; }
    float getPression() const { return NAN; }
    int16_t getConsommationChauffage() const { return chauffage; }
    int16_t getConsommationECS() const { return ecs; }
    int32_t getConsommationAnnuelleChauffage() const { return annuelleChauffage; }
    int32_t getConsommationAnnuelleECS() const { return annuelleECS; }
    MODE_ECS getModeECS() const { return INCONNU; }
    String getNomModeECS() const { return ""; }
    void publishMqtt();
    void publishModeEcs();
};
''' + extract("void Chaudiere::publishMqtt(") + "\n" + extract("void Chaudiere::publishModeEcs(") + r'''

int main() {
    const char* counters[] = {"chauffage", "ecs", "annuelleChauffage", "annuelleECS"};
    Chaudiere c;
    // Values read from the boiler while the broker is unreachable.
    c.chauffage = 1234; c.ecs = 56; c.annuelleChauffage = 40000; c.annuelleECS = 7;
    c.publishMqtt();
    c.publishMqtt();
    for (auto name : counters) assert(c._mqtt.sent[name].empty());

    // Back online, values unchanged: each counter is published once.
    c._mqtt.online = true;
    c.publishMqtt();
    c.publishMqtt();
    for (auto name : counters) assert(c._mqtt.sent[name].size() == 1);
    assert(c._mqtt.sent["chauffage"][0] == "1234.00" && c._mqtt.sent["annuelleChauffage"][0] == "40000");

    // A new value is published once; a failed publication is retried.
    c.chauffage = 1240;
    c._mqtt.online = false;
    c.publishMqtt();
    c._mqtt.online = true;
    c.publishMqtt();
    c.publishMqtt();
    assert(c._mqtt.sent["chauffage"].size() == 2 && c._mqtt.sent["chauffage"][1] == "1240.00");
    assert(c._mqtt.sent["ecs"].size() == 1);
    puts("ok");
}
'''

with tempfile.TemporaryDirectory(prefix="ofv-consumption-outage-") as directory:
    temp = Path(directory)
    (temp / "test.cpp").write_text(program)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", str(temp / "test.cpp"), "-o", str(temp / "test")], check=True)
    subprocess.run([str(temp / "test")], check=True, stdout=subprocess.DEVNULL)

print("OK: consumptions read during an MQTT outage are published after reconnection")
