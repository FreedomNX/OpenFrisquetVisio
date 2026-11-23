"""Developer radio tools never transmit as the Connect while it is passive.

In passive mode the Connect learns the association of the real box, and the
gateway also learns that of a physical satellite: memory reads only use the
active Connect or a virtual satellite Z1, and a raw frame sent from the
Connect address is refused. Run: python3 tests/test_diagnostic_passive_connect.py
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/Portal.cpp").read_text()


def extract(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


# Both memory routes use the shared selection, never the Connect directly.
for name in ("handleMemoryRead", "handleMemoryScan"):
    body = extract(f"void Portal::{name}(")
    assert "selectDiagnosticSender(_frisquetManager, idExpediteur, idAssociation);" in body, name
    assert "ID_CONNECT" not in body, name

# The raw frame check runs before any transmission.
send = extract("void Portal::handleSendRadio(")
guard = send.index("payload[1] == ID_CONNECT")
assert "passiveConnect(_frisquetManager.config())" in send[guard - 80:guard]
assert guard < send.index("radio.transmit(")

program = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#define ID_CONNECT 0x7E
#define ID_ZONE_1 0x08
struct Config {
    bool connect = false, passive = false, z1 = false, z1Virtuel = false;
    bool useConnect() { return connect; }
    bool useConnectPassive() { return passive; }
    bool useSatelliteZ1() { return z1; }
    bool useSatelliteVirtualZ1() { return z1Virtuel; }
};
struct Device {
    uint8_t association = 0xFF;
    bool estAssocie() { return association != 0xFF; }
    uint8_t getIdAssociation() { return association; }
};
struct FrisquetManager {
    Config cfg; Device c, z1;
    Config& config() { return cfg; }
    Device& connect() { return c; }
    Device& satelliteZ1() { return z1; }
};
''' + extract("static bool passiveConnect(") + "\n" + extract("static void selectDiagnosticSender(") + r'''
static void sender(FrisquetManager& m, uint8_t expected, uint8_t association) {
    uint8_t id = 0xAA, assoc = 0xAA;
    selectDiagnosticSender(m, id, assoc);
    assert(id == expected && assoc == association);
}
int main() {
    FrisquetManager m;
    m.cfg.connect = true; m.c.association = 0x2C;   // association learnt from the real box
    sender(m, ID_CONNECT, 0x2C);                     // active Connect: unchanged
    m.cfg.passive = true;
    sender(m, 0x00, 0x00);                           // passive, no Z1: refused
    m.cfg.z1 = true;
    m.z1.association = 0x31;                         // physical Z1, association learnt by observation
    sender(m, 0x00, 0x00);                           // never the identity of a real thermostat
    m.cfg.z1Virtuel = true;
    m.z1.association = 0xFF;
    sender(m, 0x00, 0x00);                           // virtual Z1 not associated yet
    m.z1.association = 0x31;
    sender(m, ID_ZONE_1, 0x31);                      // passive: virtual Z1, never the Connect
    m.cfg.connect = false;
    assert(!passiveConnect(m.cfg));                  // passive flag alone, Connect disabled
    sender(m, ID_ZONE_1, 0x31);
    m.cfg.z1Virtuel = false;
    sender(m, 0x00, 0x00);                           // no Connect, physical Z1: refused
    puts("ok");
}
'''

with tempfile.TemporaryDirectory(prefix="ofv-diagnostic-passive-") as directory:
    temp = Path(directory)
    (temp / "test.cpp").write_text(program)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", str(temp / "test.cpp"), "-o", str(temp / "test")], check=True)
    subprocess.run([str(temp / "test")], check=True, stdout=subprocess.DEVNULL)

print("OK: memory reads only as an active Connect or a virtual Z1; raw frames from a passive Connect refused")
