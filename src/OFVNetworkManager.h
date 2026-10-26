#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "Logs.h"

class OFVNetworkManager {
public:
  struct Options {
    // Identification & accès
    String hostname = "open-frisquet-visio";
    String ssid;
    String password;

    // IP statique (optionnelle)
    bool useStaticIp = false;
    IPAddress localIp;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress dns1 = IPAddress(1,1,1,1);
    IPAddress dns2 = IPAddress(8,8,8,8);

    // Reconnexion
    bool autoReconnect = true;
    uint32_t firstConnectTimeoutMs = 15000; // temps max de la 1ère tentative
    uint32_t reconnectMinMs = 3000;         // backoff min
    uint32_t reconnectMaxMs = 60000;        // backoff max

    // Économie d’énergie (éviter si besoin d’ADC2, etc.)
    bool wifiSleep = false;
  };

  OFVNetworkManager() = default;

  void begin(const Options& opts);
  void loop();

  // Callbacks (facultatifs)
  void onConnected(std::function<void()> cb)      { _onConnected = std::move(cb); }
  void onDisconnected(std::function<void(const String& reason)> cb) { _onDisconnected = std::move(cb); }

  // Helpers
  bool isConnected() const { return WiFi.isConnected(); }
  String ipStr() const     { return WiFi.isConnected() ? WiFi.localIP().toString() : String(); }
  String macStr() const    { return WiFi.macAddress(); }
  int32_t rssi() const     { return WiFi.isConnected() ? WiFi.RSSI() : 0; }
  String ssid() const      { return WiFi.SSID(); }
  String hostname() const  { return _opts.hostname; }

  // Forcer une reconnexion immédiate (utile après changement de config)
  void requestReconnectNow();

private:
  Options _opts;

  // État interne
  uint32_t _tConnectStart = 0;    // début de tentative en cours
  uint32_t _tNextAttempt = 0;     // prochain créneau de tentative
  uint8_t  _failCount = 0;        // pour le backoff exponentiel
  bool     _everConnected = false;

  // Callbacks
  std::function<void()> _onConnected;
  std::function<void(const String& reason)> _onDisconnected;

  // Gestion événements
  wifi_event_id_t _evAny = 0;

  void attachEvents();
  void startConnect();
  void scheduleReconnect();
  uint32_t computeBackoffMs() const;

  static String reasonToString(uint8_t reason);
};
