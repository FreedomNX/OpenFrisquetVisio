#pragma once
#include <Arduino.h>
#include <functional>

using wifi_event_id_t = int;
class IPAddress {
public:
    IPAddress(byte = 0, byte = 0, byte = 0, byte = 0) {}
    explicit operator bool() const { return false; }
    byte operator[](size_t) const { return 0; }
    bool fromString(const String&) { return false; }
    String toString() const { return {}; }
};
struct WifiSimule {
    bool isConnected() const { return true; }
    IPAddress localIP() const { return {}; }
    String macAddress() const { return {}; }
    int RSSI() const { return 0; }
    String SSID() const { return {}; }
};
extern WifiSimule WiFi;
