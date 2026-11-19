"""Wi-Fi and MQTT settings the core or the broker would reject are refused.

Run: python3 tests/test_portal_network_limits.py
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


# The checks run before the first change to the Wi-Fi or MQTT options.
post = extract("void Portal::handlePostConfig(")
checks = post.index("const char* invalidNetwork = nullptr;")
assert checks < post.index("auto& w = ")
for check in ('isValidHostname(_srv.arg("wifiHostname"))', 'fitsBytes(_srv.arg("wifiSsid"), 32)',
              '!_srv.arg("wifiPass").isEmpty() && !isValidWifiPassword(_srv.arg("wifiPass"))',
              'isValidMqttBaseTopic(_srv.arg("mqttBaseTopic"))'):
    assert check in post[checks:post.index("auto& w = ")], check

program = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
struct String : std::string {
    String(const char* s) : std::string(s) {}
    String(const char* s, size_t n) : std::string(s, n) {}
    bool isEmpty() const { return empty(); }
    size_t length() const { return size(); }
};
''' + "\n".join(extract(f"static bool {name}(") for name in (
    "fitsBytes", "isValidHostname", "isValidWifiPassword", "isValidMqttBaseTopic")) + r'''
int main() {
    const std::string a31(31, 'a'), a32(32, 'a'), hex64(64, 'F');
    assert(isValidHostname("open-frisquet-visio") && isValidHostname("esp32-device"));
    assert(isValidHostname(a31.c_str()) && !isValidHostname(a32.c_str()));
    assert(!isValidHostname("") && !isValidHostname("-heltec") && !isValidHostname("heltec-"));
    assert(!isValidHostname("mon_heltec") && !isValidHostname("mon heltec") && !isValidHostname("chaudière"));
    assert(!isValidHostname(String("ab\0cd", 5)));

    assert(fitsBytes(a32.c_str(), 32) && !fitsBytes((a32 + "a").c_str(), 32));
    assert(!fitsBytes("Caf\xC3\xA9" "0123456789012345678901234567", 32));  // 33 bytes, 29 characters
    assert(!fitsBytes(String("Ma\0Box", 6), 32));

    assert(isValidWifiPassword("12345678") && isValidWifiPassword(std::string(63, 'x').c_str()));
    assert(!isValidWifiPassword("1234567") && !isValidWifiPassword(std::string(65, 'x').c_str()));
    assert(isValidWifiPassword(hex64.c_str()) && isValidWifiPassword(std::string(64, 'a').c_str()));
    assert(!isValidWifiPassword(std::string(64, 'x').c_str()));
    assert(!isValidWifiPassword(String("1234\0" "5678", 9)));

    assert(isValidMqttBaseTopic("frisquet") && isValidMqttBaseTopic("maison/chaudiere"));
    assert(isValidMqttBaseTopic("") && !isValidMqttBaseTopic("frisquet/+") && !isValidMqttBaseTopic("#"));
    assert(!isValidMqttBaseTopic("fris\nquet") && !isValidMqttBaseTopic(String("a\0b", 3)));
    puts("ok");
}
'''

with tempfile.TemporaryDirectory(prefix="ofv-network-limits-") as directory:
    temp = Path(directory)
    (temp / "test.cpp").write_text(program)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", str(temp / "test.cpp"), "-o", str(temp / "test")], check=True)
    subprocess.run([str(temp / "test")], check=True, stdout=subprocess.DEVNULL)

print("OK: hostname, SSID, Wi-Fi password and MQTT prefix are checked before any change")
