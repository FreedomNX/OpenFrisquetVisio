"""Check strict parsing of numeric MQTT commands.

Run: python3 tests/test_mqtt_numeric_commands.py
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
zone = (ROOT / "src/Frisquet/Zone.cpp").read_text()
probe = (ROOT / "src/Frisquet/SondeExterieure.cpp").read_text()

# String::toFloat() turns "abc" into 0 and "21abc" into 21: no longer used
# for MQTT commands.
for source in (zone, probe):
    assert "payload.toFloat()" not in source
# Ambient, Confort, Reduit, Hors Gel, Boost and outdoor temperature.
assert zone.count("NumberParsing::finiteDecimal(payload.c_str(), temperature)") == 5
assert probe.count("NumberParsing::finiteDecimal(payload.c_str(), temperature)") == 1
assert "temperature >= 0 && temperature <= 50" in zone
assert "setTemperatureExterieure(temperature);" in probe

program = r'''
#include "NumberParsing.h"
#include <cassert>
#include <cstdio>

int main() {
    struct { const char* text; float value; } accepted[] = {
        {"21", 21}, {"21.5", 21.5f}, {"-3.5", -3.5f}, {"+7", 7}, {".5", 0.5f}, {"5.", 5}, {"0", 0},
    };
    for (const auto& item : accepted) {
        float value = 99;
        assert(NumberParsing::finiteDecimal(item.text, value));
        assert(value == item.value);
    }
    const char* rejected[] = {
        "", "abc", "21abc", " 21", "21 ", "21,5", "nan", "inf", "-inf", "1e3", "0x10",
        "-", "+", ".", "5.0.0", "--1",
        "9999999999999999999999999999999999999999999999999",
    };
    for (const char* text : rejected) {
        float value = 99;
        assert(!NumberParsing::finiteDecimal(text, value));
        assert(value == 99);
    }
    float unused = 0;
    assert(!NumberParsing::finiteDecimal(nullptr, unused));
    puts("OK: numeric MQTT commands, strict decimal parsing");
}
'''

with tempfile.TemporaryDirectory(prefix="frisquet-mqtt-numbers-") as temp:
    cpp = Path(temp) / "mqtt_numeric_commands.cpp"
    binary = Path(temp) / "mqtt_numeric_commands"
    cpp.write_text(program)
    subprocess.run([
        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-g",
        "-I", str(ROOT / "src/Frisquet"), str(cpp), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
