#include "radio-fixture.h"
void delay(uint32_t) {
    assert(false && "un appel occupé ne doit pas attendre");
}

#include <string>
#include <map>
#include <cstdlib>
#include <cctype>
void error(const char *, ...) {
}
class String : public std::string {
  public:
    using std::string::string;
    String(const std::string &v) : std::string(v) {}
    bool isEmpty() const { return empty(); }
    int toInt() const { return std::atoi(c_str()); }
    String substring(size_t a, size_t b) const { return substr(a, b - a); }
    void trim() {
        auto a = find_first_not_of(" \t\r\n"), b = find_last_not_of(" \t\r\n");
        *this = a == npos ? "" : substr(a, b - a + 1);
    }
    void toLowerCase() {
        for (auto &c : *this)
            c = std::tolower(static_cast<unsigned char>(c));
    }
};
struct IPAddress {
    bool fromString(const String &) { return true; }
};
enum HTTPMethod { HTTP_GET, HTTP_POST };
struct WebServer {
    HTTPMethod verb = HTTP_POST;
    int status = 0;
    std::map<std::string, String> args;
    HTTPMethod method() { return verb; }
    bool hasArg(const char *key) { return args.count(key); }
    String arg(const char *key) { return hasArg(key) ? args[key] : String(); }
    void send(int s, const char *, const String &) { status = s; }
    void sendHeader(const char *, const char *) {}
};
struct Config {
    struct Wifi {
        String hostname, ssid, password;
        bool useStaticIp = false;
        IPAddress localIp, gateway, subnet, dns1, dns2;
    } wifi;
    struct Mqtt {
        String host, username, password, clientId, baseTopic;
        uint16_t port = 1883;
    } mqtt;
    unsigned saves = 0;
    NetworkID network{{1, 2, 3, 4}};
    Wifi &getWiFiOptions() { return wifi; }
    Mqtt &getMQTTOptions() { return mqtt; }
    float radioRxBandwidth() { return 125; }
    void radioRxBandwidth(float) {}
    bool radioLnaBypass() { return false; }
    void radioLnaBypass(bool) {}
    void portalPassword(const String &) {}
    void save() { ++saves; }
    void setNetworkID(NetworkID v) { network = v; }
    const NetworkID &getNetworkID() { return network; }
    bool _useConnect = true;
    bool useConnect() { return _useConnect; }
    void useConnect(bool v) { _useConnect = v; }
    bool _useConnectPassive = false;
    bool useConnectPassive() { return _useConnectPassive; }
    void useConnectPassive(bool v) { _useConnectPassive = v; }
    bool _useSondeExterieure = true;
    bool useSondeExterieure() { return _useSondeExterieure; }
    void useSondeExterieure(bool v) { _useSondeExterieure = v; }
    bool _useDS18B20 = true;
    bool useDS18B20() { return _useDS18B20; }
    void useDS18B20(bool v) { _useDS18B20 = v; }
    bool _useZone1 = true;
    bool useZone1() { return _useZone1; }
    void useZone1(bool v) { _useZone1 = v; }
    bool _useZone2 = true;
    bool useZone2() { return _useZone2; }
    void useZone2(bool v) { _useZone2 = v; }
    bool _useZone3 = true;
    bool useZone3() { return _useZone3; }
    void useZone3(bool v) { _useZone3 = v; }
    bool _useSatelliteZ1 = true;
    bool useSatelliteZ1() { return _useSatelliteZ1; }
    void useSatelliteZ1(bool v) { _useSatelliteZ1 = v; }
    bool _useSatelliteZ2 = true;
    bool useSatelliteZ2() { return _useSatelliteZ2; }
    void useSatelliteZ2(bool v) { _useSatelliteZ2 = v; }
    bool _useSatelliteZ3 = true;
    bool useSatelliteZ3() { return _useSatelliteZ3; }
    void useSatelliteZ3(bool v) { _useSatelliteZ3 = v; }
    bool _useSatelliteVirtualZ1 = true;
    bool useSatelliteVirtualZ1() { return _useSatelliteVirtualZ1; }
    void useSatelliteVirtualZ1(bool v) { _useSatelliteVirtualZ1 = v; }
    bool _useSatelliteVirtualZ2 = true;
    bool useSatelliteVirtualZ2() { return _useSatelliteVirtualZ2; }
    void useSatelliteVirtualZ2(bool v) { _useSatelliteVirtualZ2 = v; }
    bool _useSatelliteVirtualZ3 = true;
    bool useSatelliteVirtualZ3() { return _useSatelliteVirtualZ3; }
    void useSatelliteVirtualZ3(bool v) { _useSatelliteVirtualZ3 = v; }
    bool _developerMode = true;
    bool developerMode() { return _developerMode; }
    void developerMode(bool v) { _developerMode = v; }
};
namespace RadioSettings {
bool validRxBandwidth(float v) {
    return v == 125;
}
bool supportsLnaBypass() {
    return true;
}
} // namespace RadioSettings
struct Device {
    FrisquetRadio &radio;
    unsigned calls = 0;
    bool associer(NetworkID &network, byte &association) {
        assert(!radio.busy());
        ++calls;
        network = {{5, 6, 7, 8}};
        association = 42;
        radio.setNetworkID(network);
        return true;
    }
    void setIdAssociation(byte) {}
    void saveConfig() {}
};
struct FrisquetManager {
    FrisquetRadio r;
    Config cfg;
    Device device{r};
    unsigned recoveries = 0;
    FrisquetRadio &radio() { return r; }
    Config &config() { return cfg; }
    Device &connect() { return device; }
    Device &sondeExterieure() { return device; }
    Device &satelliteZ1() { return device; }
    Device &satelliteZ2() { return device; }
    Device &satelliteZ3() { return device; }
    bool recupererNetworkID() {
        assert(!r.busy());
        ++recoveries;
        r.setNetworkID(cfg.network);
        return true;
    }
};
String jsonEscape(const String &v) {
    return v;
}
String networkIdToStr(const NetworkID &) {
    return "01:02:03:04";
}
bool parseNetworkIdFromString(const String &v, NetworkID &n) {
    n = {{1, 2, 3, 4}};
    return v == "01:02:03:04";
}
struct Portal {
    WebServer _srv;
    FrisquetManager _frisquetManager;
    unsigned reboots = 0;
    void clearSessions() {}
    void scheduleReboot(uint32_t) {
        ++reboots;
        _frisquetManager.radio().suspend(FrisquetRadio::Pause::Reboot, true);
    }
    void handlePostConfig();
    void handleSendRadio();
    void handleMemoryRead();
    void handleMemoryScan();
    void handlePairConnect();
    void handlePairSondeExt();
    void handlePairSatelliteZ1();
    void handlePairSatelliteZ2();
    void handlePairSatelliteZ3();
    void handleRecupNetworkId();
    bool requireRadioAvailable(bool cancelPending = false);
    bool requireDeveloperMode();
};

#include "portal-methods.inc"

using R = FrisquetRadio;
void reserve(Portal &p) {
    R::RadioTrameHeader header;
    header.idExpediteur = 0x7e;
    header.idDestinataire = 0x80;
    assert(p._frisquetManager.r.beginObservation(8, header));
}
int main(int argc, char **argv) {
    assert(argc == 2);
    for (auto handler : {&Portal::handleSendRadio, &Portal::handleMemoryRead,
                         &Portal::handleMemoryScan}) {
        for (bool developer : {false, true}) {
            Portal p;
            reserve(p);
            p._frisquetManager.cfg.developerMode(developer);
            (p.*handler)();
            assert(p._srv.status == (developer ? 503 : 403));
            assert(p._frisquetManager.r.ownsObservation(8));
        }
    }
    if (std::string(argv[1]) == "config") {
        for (const char *field :
             {"wifiIp", "wifiGw", "wifiMask", "wifiDns1", "wifiDns2", "networkID"}) {
            Portal p;
            reserve(p);
            p._srv.args[field] = "invalide";
            p.handlePostConfig();
            assert(p._srv.status == 400 && p.reboots == 0 && p._frisquetManager.cfg.saves == 0);
            assert(p._frisquetManager.r.ownsObservation(8));
        }
        Portal accepted;
        reserve(accepted);
        accepted.handlePostConfig();
        assert(accepted._srv.status == 200 && accepted.reboots == 1 &&
               accepted._frisquetManager.cfg.saves == 1);
        assert(!accepted._frisquetManager.r.ownsObservation(8) && accepted._frisquetManager.r.busy());
    } else {
        using Handler = void (Portal::*)();
        for (Handler handler : {&Portal::handlePairConnect, &Portal::handlePairSondeExt,
                                &Portal::handlePairSatelliteZ1, &Portal::handlePairSatelliteZ2,
                                &Portal::handlePairSatelliteZ3, &Portal::handleRecupNetworkId}) {
            {
                Portal p;
                reserve(p);
                (p.*handler)();
                assert(p._srv.status == 200 && !p._frisquetManager.r.busy());
                assert(p._frisquetManager.device.calls + p._frisquetManager.recoveries == 1);
            }
            for (auto reason : {R::Pause::ArduinoOta, R::Pause::HttpOta, R::Pause::Reboot}) {
                Portal p;
                p._frisquetManager.r.suspend(reason, true);
                (p.*handler)();
                assert(p._srv.status == 503 && p._frisquetManager.r.busy());
                assert(p._frisquetManager.device.calls + p._frisquetManager.recoveries == 0);
            }
            Portal invalid;
            reserve(invalid);
            invalid._srv.verb = HTTP_GET;
            (invalid.*handler)();
            assert(invalid._srv.status == 405 && invalid._frisquetManager.r.ownsObservation(8));
        }
        Portal disabled;
        reserve(disabled);
        disabled._frisquetManager.cfg.useConnect(false);
        disabled.handlePairConnect();
        assert(disabled._srv.status == 400 && disabled._frisquetManager.r.ownsObservation(8));
        Portal passive;
        reserve(passive);
        passive._frisquetManager.cfg.useConnectPassive(true);
        passive.handlePairConnect();
        assert(passive._srv.status == 400 && passive._frisquetManager.r.ownsObservation(8));
        Portal ordinary;
        reserve(ordinary);
        assert(!ordinary.requireRadioAvailable());
        assert(ordinary._srv.status == 503 && ordinary._frisquetManager.r.ownsObservation(8));
    }
    puts("OK : validation HTTP, préemption des observations et protection OTA/redémarrage");
}
