"""Check the temperature encodings from Utils.h under UBSan.

temperature8 encodes 5..30 degC by 0.5 steps as 0..250. The conversion must
not go through an out-of-range float -> uint8_t cast (undefined behaviour
from 26 degC).

Run: python3 tests/test_temperature_encoding.py
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

BOARD = r'''
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
using byte = uint8_t;
using std::isnan;
using std::round;
class String {};
'''

TEST = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include "Frisquet/Utils.h"

int main() {
    // Every 0.5 degC step between 5 and 30 degC round-trips exactly.
    for (int step = 10; step <= 60; ++step) {
        const float value = step / 2.0f;
        temperature8 encoded(value);
        assert(encoded.value == step * 5 - 50);
        assert(encoded.toFloat() == value);
    }
    assert(temperature8(30.0f).value == 250);
    assert(temperature8(19.74f).toFloat() == 19.5f);
    assert(temperature8(19.76f).toFloat() == 20.0f);

    // Out-of-range and non-finite inputs stay within the protocol range.
    assert(temperature8(NAN).toFloat() == 5.0f);
    assert(temperature8(INFINITY).toFloat() == 30.0f);
    assert(temperature8(-INFINITY).toFloat() == 5.0f);
    assert(temperature8(-100.0f).toFloat() == 5.0f);
    assert(temperature8(100.0f).toFloat() == 30.0f);
    assert(temperature16(NAN).toFloat() == 0.0f);
    assert(temperature16(INFINITY).toFloat() == temperature16(3276.7f).toFloat());
    assert(temperature16(1e9f).toFloat() == temperature16(3276.7f).toFloat());
    assert(temperature16(-1e9f).toFloat() == temperature16(-3276.8f).toFloat());

    std::puts("OK: temperature8/temperature16 encodings");
}
'''

with tempfile.TemporaryDirectory(prefix="ofv-temperature-") as tmp:
    (Path(tmp) / "Board.h").write_text(BOARD)
    (Path(tmp) / "test.cpp").write_text(TEST)
    binary = Path(tmp) / "temperature_test"
    subprocess.run([
        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-fsanitize=address,undefined,float-cast-overflow", "-fno-sanitize-recover=all",
        "-fno-omit-frame-pointer", "-g",
        "-I", tmp, "-I", str(ROOT / "src"), str(Path(tmp) / "test.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
