"""Run Config::load/save with an in-memory NVS to check issue #31 (ASan/UBSan)."""
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
void error(const char*) { assert(false); }
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
class Preferences {
    std::string current;
    bool readOnly = false;
public:
    using Bytes = std::vector<uint8_t>;
    using Namespace = std::map<std::string, Bytes>;
    static std::map<std::string, Namespace> nvs;
    static unsigned legacyOpens;
    bool begin(const char* name, bool ro) {
        assert(current.empty());
        if (!strcmp(name, "net-conf")) ++legacyOpens;
        if (ro && !nvs.count(name)) return false;
        current = name; readOnly = ro;
        nvs[current];
        return true;
    }
    void end() { assert(!current.empty()); current.clear(); }
    bool isKey(const char* key) const { return nvs.at(current).count(key); }
    size_t getBytes(const char* key, void* value, size_t size) const {
        const auto& data = nvs.at(current).at(key);
        assert(data.size() == size);
        memcpy(value, data.data(), size); return size;
    }
    size_t putBytes(const char* key, const void* value, size_t size) {
        assert(!readOnly && !current.empty());
        const auto* bytes = static_cast<const uint8_t*>(value);
        nvs[current][key] = Bytes(bytes, bytes + size); return size;
    }
    bool getBool(const char* key, bool fallback) const {
        bool value = fallback;
        if (isKey(key)) getBytes(key, &value, sizeof(value));
        return value;
    }
    void putBool(const char* key, bool value) { putBytes(key, &value, sizeof(value)); }
    String getString(const char*, const char* fallback) const { return fallback; }
    float getFloat(const char*, float fallback) const { return fallback; }
    uint16_t getUShort(const char*, uint16_t fallback) const { return fallback; }
    unsigned long getULong(const char*, unsigned long fallback) const { return fallback; }
    void putString(const char*, const String&) {}
    void putFloat(const char*, float) {}
    void putUShort(const char*, uint16_t) {}
    void putULong(const char*, unsigned long) {}
};
std::map<std::string, Preferences::Namespace> Preferences::nvs;
unsigned Preferences::legacyOpens = 0;
'''
code += without_includes(ROOT / 'src/Frisquet/NetworkID.h') + '\n'
code += without_includes(ROOT / 'src/RadioSettings.h') + '\n'
code += without_includes(ROOT / 'src/Config.h') + '\n'
code += without_includes(ROOT / 'src/Config.cpp') + '\n'
code += r'''
void seed(const char* ns, const char* key, const NetworkID& id) {
    Preferences::nvs[ns][key] = Preferences::Bytes(id.bytes, id.bytes + sizeof(id.bytes));
}
void expect(Config& config, const NetworkID& expected) {
    assert(!memcmp(config.getNetworkID().bytes, expected.bytes, sizeof(expected.bytes)));
}
void reset() { Preferences::nvs.clear(); Preferences::legacyOpens = 0; }
int main() {
    const NetworkID oldId(0x11, 0x22, 0x33, 0x44), pairedId(0x55, 0x66, 0x77, 0x88);
    // Both namespaces exist: the latest pairing must survive boot and settings saves.
    reset(); seed("net-conf", "net_id", oldId); seed("sysconfig", "networkID", pairedId);
    Config boot; boot.load(); expect(boot, pairedId);
    assert(Preferences::legacyOpens == 0);
    boot.developerMode(true); boot.save();
    Config reboot; reboot.load(); expect(reboot, pairedId); assert(reboot.developerMode());
    assert(Preferences::legacyOpens == 0);
    // Old installations still migrate, then keep a newly paired ID after reboot.
    reset(); seed("net-conf", "net_id", oldId);
    Config migrated; migrated.load(); expect(migrated, oldId);
    assert(Preferences::legacyOpens == 1);
    migrated.save();
    Config migratedReboot; migratedReboot.load(); expect(migratedReboot, oldId);
    assert(Preferences::legacyOpens == 1);
    migratedReboot.setNetworkID(pairedId); migratedReboot.save();
    Config pairedReboot; pairedReboot.load(); expect(pairedReboot, pairedId);
    assert(Preferences::legacyOpens == 1);
    assert(Preferences::nvs.at("net-conf").count("net_id") == 1);
    // Current installations and fresh installations without a legacy ID.
    reset(); seed("sysconfig", "networkID", pairedId);
    Config current; current.load(); expect(current, pairedId);
    assert(Preferences::legacyOpens == 0);
    reset(); Config fresh; fresh.load(); expect(fresh, NetworkID());
    reset(); Preferences::nvs["net-conf"];
    Config noLegacyId; noLegacyId.load(); expect(noLegacyId, NetworkID());
    puts("OK: current pairing preserved across reboot/settings saves, legacy migration, fresh NVS");
}
'''

with tempfile.TemporaryDirectory(prefix='frisquet-config-migration-') as directory:
    cpp = Path(directory) / 'test.cpp'
    cpp.write_text(code)
    binary = Path(directory) / 'test'
    subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
