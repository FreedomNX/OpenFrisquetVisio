"""Exercise real config GET/POST handlers and NVS save/load with native stubs.

Run: python3 tests/test_config_passwords.py (C++ compiler required).
Authentication routing is exercised separately by test_portal_auth.py.
"""
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/Portal.cpp').read_text()


def extract(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def without_includes(path):
    return '\n'.join(line for line in path.read_text().splitlines()
                     if not line.startswith(('#include', '#pragma once')))


code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
struct String : std::string {
    using std::string::string;
    String(const std::string& value) : std::string(value) {}
    explicit String(unsigned value) : std::string(std::to_string(value)) {}
    String(float value, int decimals) {
        char text[32]; snprintf(text, sizeof(text), "%.*f", decimals, value); assign(text);
    }
    bool isEmpty() const { return empty(); }
    long toInt() const { return strtol(c_str(), nullptr, 10); }
    String substring(size_t from, size_t to) const { return substr(from, to - from); }
    void trim() {
        auto from = find_first_not_of(" \t\r\n");
        if (from == npos) clear(); else assign(substr(from, find_last_not_of(" \t\r\n") - from + 1));
    }
    void replace(const char* from, const char* to) {
        size_t at = 0;
        while ((at = find(from, at)) != npos) {
            std::string::replace(at, strlen(from), to); at += strlen(to);
        }
    }
    void toUpperCase() { for (char& c : *this) c = toupper(static_cast<unsigned char>(c)); }
    void toLowerCase() { for (char& c : *this) c = tolower(static_cast<unsigned char>(c)); }
};
String operator+(const String& a, const char* b) { String result(a); result.append(b); return result; }
String operator+(const String& a, uint8_t b) { return a + std::to_string(b).c_str(); }
struct IPAddress {
    explicit operator bool() const { return false; }
    uint8_t operator[](unsigned) const { return 0; }
    bool fromString(const String&) { return false; }
    String toString() const { return "0.0.0.0"; }
};
void delay(unsigned) {}
void error(const char*) { assert(false); }
void info(const char*, ...) {}
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
    bool begin(const char* name, bool ro) {
        assert(current.empty());
        if (ro && !nvs.count(name)) return false;
        current = name; readOnly = ro; nvs[current]; return true;
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
    template<class T> T get(const char* key, T fallback) const {
        if (isKey(key)) getBytes(key, &fallback, sizeof(fallback));
        return fallback;
    }
    bool getBool(const char* key, bool value) const { return get(key, value); }
    float getFloat(const char* key, float value) const { return get(key, value); }
    uint16_t getUShort(const char* key, uint16_t value) const { return get(key, value); }
    unsigned long getULong(const char* key, unsigned long value) const { return get(key, value); }
    void putBool(const char* key, bool value) { putBytes(key, &value, sizeof(value)); }
    void putFloat(const char* key, float value) { putBytes(key, &value, sizeof(value)); }
    void putUShort(const char* key, uint16_t value) { putBytes(key, &value, sizeof(value)); }
    void putULong(const char* key, unsigned long value) { putBytes(key, &value, sizeof(value)); }
    String getString(const char* key, const char* fallback) const {
        if (!isKey(key)) return fallback;
        const auto& data = nvs.at(current).at(key);
        return String(data.begin(), data.end());
    }
    void putString(const char* key, const String& value) { putBytes(key, value.c_str(), value.size()); }
};
std::map<std::string, Preferences::Namespace> Preferences::nvs;
'''
for path in ('src/Frisquet/NetworkID.h', 'src/RadioSettings.h', 'src/Config.h', 'src/Config.cpp'):
    code += without_includes(ROOT / path) + '\n'

code += r'''
enum HTTPMethod { HTTP_GET, HTTP_POST };
struct WebServer {
    std::map<std::string, String> args;
    String body;
    int code = 0;
    HTTPMethod method() const { return HTTP_POST; }
    bool hasArg(const char* key) const { return args.count(key); }
    String arg(const char* key) const { return hasArg(key) ? args.at(key) : String(); }
    void send(int status, const char*, const String& content) { code = status; body = content; }
};
struct FrisquetManager { Config cfg; Config& config() { return cfg; } };
struct Portal {
    FrisquetManager _frisquetManager;
    WebServer _srv;
    bool reboot = false;
    void handleGetConfig();
    void handlePostConfig();
    bool requireDeveloperMode() { return _frisquetManager.cfg.developerMode(); }
    void clearSessions() {}
    void scheduleReboot(unsigned) { reboot = true; }
};
'''
for signature in ('static String networkIdToStr(', 'static bool parseNetworkIdFromString(',
                  'static bool isValidIPv4(', 'static String jsonEscape(',
                  'static bool parseBoolArg(', 'void Portal::handleGetConfig(',
                  'void Portal::handlePostConfig('):
    code += extract(signature) + '\n'

code += r'''
// Every successful POST must persist credentials for a newly constructed Config.
void saveAndReboot(Portal& portal, const char* wifi, const char* mqtt) {
    portal.reboot = false;
    portal.handlePostConfig();
    assert(portal._srv.code == 200 && portal.reboot);
    Config rebooted; rebooted.load();
    assert(rebooted.getWiFiOptions().password == wifi);
    assert(rebooted.getMQTTOptions().password == mqtt);
    portal._frisquetManager.cfg.load();
    portal._srv.args.clear();
}
void emitConfig(Portal& portal) {
    portal.handleGetConfig(); assert(portal._srv.code == 200);
    puts(portal._srv.body.c_str());
}
int main() {
    Portal portal;
    auto& cfg = portal._frisquetManager.cfg;
    cfg.load();
    cfg.getWiFiOptions().password = "original wifi secret";
    cfg.getMQTTOptions().password = "original mqtt secret";
    cfg.save();
    emitConfig(portal);
    cfg.developerMode(true); emitConfig(portal);
    // Opening/saving without edits and changing unrelated fields retain both passwords.
    saveAndReboot(portal, "original wifi secret", "original mqtt secret");
    portal._srv.args = {{"wifiHostname", "renamed"}, {"mqttHost", "new-broker"}};
    saveAndReboot(portal, "original wifi secret", "original mqtt secret");
    assert(cfg.getWiFiOptions().hostname == "renamed");
    assert(cfg.getMQTTOptions().host == "new-broker");
    const char* replacement = " new \"quoted\" 'secret' +&%=\\é ";
    portal._srv.args = {{"wifiPass", replacement}};
    saveAndReboot(portal, replacement, "original mqtt secret");
    emitConfig(portal);
    portal._srv.args = {{"mqttPass", replacement}};
    saveAndReboot(portal, replacement, replacement);
    // Clearing either credential must leave the other intact, including after reboot.
    portal._srv.args = {{"wifiPass", ""}};
    saveAndReboot(portal, "", replacement); emitConfig(portal);
    portal._srv.args = {{"wifiPass", replacement}, {"mqttPass", ""}};
    saveAndReboot(portal, replacement, ""); emitConfig(portal);
    portal._srv.args = {{"wifiPass", ""}};
    saveAndReboot(portal, "", ""); emitConfig(portal);
    saveAndReboot(portal, "", "");
    // Spaces and literal stars are new credentials, never sentinels or trimmed values.
    portal._srv.args = {{"wifiPass", "   "}, {"mqttPass", "********"}};
    saveAndReboot(portal, "   ", "********"); emitConfig(portal);
}
'''

with tempfile.TemporaryDirectory(prefix='frisquet-config-passwords-') as directory:
    cpp = Path(directory) / 'test.cpp'
    cpp.write_text(code)
    binary = Path(directory) / 'test'
    subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], check=True, stdout=subprocess.PIPE, text=True)

responses = [json.loads(line) for line in result.stdout.splitlines()]
assert [(data['wifiPassSet'], data['mqttPassSet']) for data in responses] == [
    (True, True), (True, True), (True, True), (False, True),
    (True, False), (False, False), (True, True),
]
for data in responses:
    assert 'wifiPass' not in data and 'mqttPass' not in data and 'portalPassword' not in data
    for password in ('original wifi secret', 'original mqtt secret',
                     ' new "quoted" \'secret\' +&%=\\é ', '   ', '********'):
        assert password not in data.values()
print('OK: config responses omit secrets; omitted, replaced and cleared passwords survive reload')
