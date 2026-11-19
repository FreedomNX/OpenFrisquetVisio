"""Check that every boiler date received in a frame is validated before use.

Run: python3 tests/test_date_recue.py
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FRISQUET = ROOT / "src/Frisquet"
device = (FRISQUET / "FrisquetDevice.h").read_text()

# Dates taken from frames go through setDateRecue(); only recupererDate(),
# which already calls validDate(), sets a Date directly.
expected = {"Connect.cpp": 1, "Satellite.cpp": 4, "SondeExterieure.cpp": 1}
for name, count in expected.items():
    source = (FRISQUET / name).read_text()
    assert "setDate(" not in source, name
    assert source.count("setDateRecue(") == count, name
explicit = (FRISQUET / "FrisquetDevice.cpp").read_text()
assert explicit.count("setDate(date)") == 1
assert "BoilerIdentity::validDate(donnees.date)" in explicit


def extract(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


method = extract(device, "bool setDateRecue(")
utils = (FRISQUET / "Utils.h").read_text()
date = extract(utils, "struct Date {") + ";"

program = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "Frisquet/BoilerIdentity.h"
using byte = uint8_t;
static void debug(const char*, ...) {}
''' + date + r'''
struct Device {
    Date _date;
    unsigned sets = 0;
    void setDate(Date& date) { _date = date; ++sets; }
''' + method + r'''
};

int main() {
    Device device;
    const byte valid[] = {0x26, 0x11, 0x19, 0x09, 0x50, 0x13};
    assert(device.setDateRecue(valid) && device.sets == 1);
    assert(device._date.annee == 26 && device._date.mois == 11 && device._date.jour == 19);
    assert(device._date.heure == 9 && device._date.minute == 50 && device._date.seconde == 13);
    const byte rejected[][6] = {
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // all zeros: month 0
        {0x26, 0x1A, 0x19, 0x09, 0x50, 0x13},  // not BCD
        {0x26, 0x13, 0x19, 0x09, 0x50, 0x13},  // month 13
        {0x26, 0x11, 0x31, 0x09, 0x50, 0x13},  // 31 November
        {0x25, 0x02, 0x29, 0x09, 0x50, 0x13},  // 29 February, non leap year
        {0x26, 0x11, 0x19, 0x24, 0x50, 0x13},  // 24 h
        {0x26, 0x11, 0x19, 0x09, 0x60, 0x13},  // 60 min
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    };
    for (const auto& bcd : rejected) {
        assert(!device.setDateRecue(bcd));
        assert(device.sets == 1 && device._date.mois == 11 && device._date.jour == 19);
    }
    puts("OK: received dates validated before setting the clock");
}
'''

with tempfile.TemporaryDirectory(prefix="frisquet-date-") as temp:
    cpp = Path(temp) / "date_recue.cpp"
    binary = Path(temp) / "date_recue"
    cpp.write_text(program)
    subprocess.run([
        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-g",
        "-I", str(ROOT / "src"), str(cpp), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
