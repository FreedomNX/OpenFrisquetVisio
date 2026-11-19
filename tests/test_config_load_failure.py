"""A configuration that could not be read must not be overwritten by defaults.

Runs the real Config::load/save with an in-memory NVS (ASan/UBSan).
Run: python3 tests/test_config_load_failure.py
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def without_includes(path):
    return '\n'.join(line for line in path.read_text().splitlines()
                     if not line.startswith(('#include', '#pragma once')))


code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
struct String : std::string {
    using std::string::string;
    String(const std::string& value) : std::string(value) {}
    explicit String(uint8_t value) : std::string(std::to_string(value)) {}
    bool isEmpty() const { return empty(); }
};
String operator+(const String& a, const char* b) { String result(a); result.append(b); return result; }
String operator+(const String& a, uint8_t b) { return (a + std::to_string(b).c_str()); }
struct IPAddress {
    explicit operator bool() const { return false; }
    uint8_t operator[](unsigned) const { return 0; }
    bool fromString(const String&) { return false; }
};
void delay(unsigned) {}
static unsigned errors = 0;
void error(const char*) { ++errors; }
struct OFVNetworkManager { struct Options {
    String hostname, ssid, password;
    bool useStaticIp, autoReconnect, wifiSleep;
    IPAddress localIp, gateway, subnet, dns1, dns2;
    unsigned long firstConnectTimeoutMs, reconnectMinMs, reconnectMaxMs;
}; };
struct MqttManager { struct Options {
    String clientId, host, username, password, baseTopic;
    uint16_t port, keepAliveSec;
    bool cleanSession;
}; };
// Values are stored as text: enough to compare what was written.
class Preferences {
    std::string current;
public:
    using Namespace = std::map<std::string, std::string>;
    static std::map<std::string, Namespace> nvs;
    static unsigned failingOpens;
    static unsigned writes;
    bool begin(const char* name, bool readOnly) {
        assert(current.empty());
        if (failingOpens) { --failingOpens; return false; }
        if (readOnly && !nvs.count(name)) return false;  // Like NVS: nothing to read.
        current = name; nvs[current];
        return true;
    }
    void end() { current.clear(); }  // Like ESP32 Preferences: no-op when not started.
    bool isKey(const char* key) const { return !current.empty() && nvs.at(current).count(key); }
    std::string get(const char* key, const std::string& fallback) const {
        return isKey(key) ? nvs.at(current).at(key) : fallback;
    }
    void put(const char* key, const std::string& value) {
        assert(!current.empty());
        ++writes; nvs[current][key] = value;
    }
    size_t getBytes(const char* key, void* value, size_t size) const {
        const std::string data = get(key, "");
        assert(data.size() == size); memcpy(value, data.data(), size); return size;
    }
    size_t putBytes(const char* key, const void* value, size_t size) {
        put(key, std::string(static_cast<const char*>(value), size)); return size;
    }
    uint8_t getUChar(const char* key, uint8_t fallback) const { return std::stoi(get(key, std::to_string(fallback))); }
    void putUChar(const char* key, uint8_t value) { put(key, std::to_string(value)); }
    bool getBool(const char* key, bool fallback) const { return get(key, fallback ? "1" : "0") == "1"; }
    void putBool(const char* key, bool value) { put(key, value ? "1" : "0"); }
    String getString(const char* key, const char* fallback) const { return String(get(key, fallback)); }
    void putString(const char* key, const String& value) { put(key, value); }
    float getFloat(const char* key, float fallback) const { return std::stof(get(key, std::to_string(fallback))); }
    void putFloat(const char* key, float value) { put(key, std::to_string(value)); }
    uint16_t getUShort(const char* key, uint16_t fallback) const { return std::stoi(get(key, std::to_string(fallback))); }
    void putUShort(const char* key, uint16_t value) { put(key, std::to_string(value)); }
    unsigned long getULong(const char* key, unsigned long fallback) const { return std::stoul(get(key, std::to_string(fallback))); }
    void putULong(const char* key, unsigned long value) { put(key, std::to_string(value)); }
};
std::map<std::string, Preferences::Namespace> Preferences::nvs;
unsigned Preferences::failingOpens = 0;
unsigned Preferences::writes = 0;
'''
for header in ('src/Frisquet/NetworkID.h', 'src/RadioSettings.h',
               'src/Frisquet/BoilerIdentity.h', 'src/Config.h', 'src/Config.cpp'):
    code += without_includes(ROOT / header) + '\n'
code += r'''
int main() {
    // Stored configuration of an installed device.
    Preferences::nvs["sysconfig"] = {
        {"wifiSsid", "maison"}, {"wifiPass", "secret-wifi"}, {"portalPass", "secret-portail"},
        {"useConnect", "1"}, {"useConnectPass", "1"},
    };
    const auto stored = Preferences::nvs;

    // The first open fails: defaults stay in RAM and every save is refused,
    // even once NVS opens again, so the stored configuration survives.
    Preferences::failingOpens = 1;
    Config failed; failed.load();
    assert(errors == 1 && failed.portalPassword() == "admin" && !failed.useConnect());
    failed.developerMode(true); failed.save(); failed.save();
    assert(errors == 3 && Preferences::writes == 0 && Preferences::nvs == stored);

    // The next boot reads the configuration again.
    Config reboot; reboot.load();
    assert(reboot.portalPassword() == "secret-portail" && reboot.useConnect() && reboot.useConnectPassive());

    // Normal case: a successful load allows saving.
    reboot.developerMode(true); reboot.save();
    assert(Preferences::writes > 0 && Preferences::nvs["sysconfig"]["developerMode"] == "1");
    assert(Preferences::nvs["sysconfig"]["wifiPass"] == "secret-wifi");

    // A failed open during save writes nothing either.
    const unsigned before = Preferences::writes;
    Preferences::failingOpens = 1;
    reboot.save();
    assert(Preferences::writes == before);
    puts("OK: unreadable configuration is never overwritten by defaults");
}
'''

with tempfile.TemporaryDirectory(prefix='frisquet-config-load-') as temp:
    cpp = Path(temp) / 'config_load_failure.cpp'
    binary = Path(temp) / 'config_load_failure'
    cpp.write_text(code)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g',
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
