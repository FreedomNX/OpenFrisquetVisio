"""The "Écrasement consigne" switch only accepts ON/OFF and publishes its real state.

Run: python3 tests/test_ecrasement_on_off.py
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/Frisquet/Satellite.cpp").read_text()


def extract(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


# The MQTT callback delegates to the tested method.
begin = extract(source, "void Satellite::begin(")
callback = begin[begin.index("mqtt().onCommand(_mqttEntities.ecrasementConsigne"):]
assert "commandeEcrasement(payload);" in callback[:callback.index("});")]

program = r'''
#include <cassert>
#include <cstdio>
#include <string>
#include <strings.h>
#include <vector>
struct String : std::string {
    String(const char* s = "") : std::string(s) {}
    bool equalsIgnoreCase(const char* other) const { return strcasecmp(c_str(), other) == 0; }
};
struct Entity {};
std::vector<std::string> published;
unsigned errors = 0;
void info(const char*, ...) {}
void error(const char*, ...) { ++errors; }
struct Mqtt { void publishState(const Entity&, const char* payload) { published.push_back(payload); } };
struct Satellite {
    Mqtt _mqtt;
    struct { Entity ecrasementConsigne; } _mqttEntities;
    bool _ecrasement = false;
    unsigned changes = 0;
    Mqtt& mqtt() { return _mqtt; }
    int getNumeroZone() { return 1; }
    void setEcrasement(bool value) { _ecrasement = value; ++changes; }
    bool getEcrasement() { return _ecrasement; }
    void commandeEcrasement(const String& payload);
};
''' + extract(source, "void Satellite::commandeEcrasement(") + r'''
int main() {
    Satellite s;
    s.commandeEcrasement("ON");
    assert(s._ecrasement && published.back() == "ON");
    s.commandeEcrasement("off");
    assert(!s._ecrasement && published.back() == "OFF");
    s.commandeEcrasement("on");
    assert(s._ecrasement && published.back() == "ON");
    // Unknown commands change nothing; the real state is republished.
    const unsigned changes = s.changes;
    for (const char* payload : {"true", "1", "", "ONN", "activer"}) {
        s.commandeEcrasement(payload);
        assert(s._ecrasement && published.back() == "ON");
    }
    assert(s.changes == changes && errors == 5);
    s.commandeEcrasement("OFF");
    s.commandeEcrasement("1");
    assert(!s._ecrasement && published.back() == "OFF");
    for (const auto& state : published) assert(state == "ON" || state == "OFF");
    puts("ok");
}
'''

with tempfile.TemporaryDirectory(prefix="ofv-ecrasement-") as directory:
    temp = Path(directory)
    (temp / "test.cpp").write_text(program)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", str(temp / "test.cpp"), "-o", str(temp / "test")], check=True)
    subprocess.run([str(temp / "test")], check=True, stdout=subprocess.DEVNULL)

print("OK: écrasement switch accepts ON/OFF only and publishes its real state")
