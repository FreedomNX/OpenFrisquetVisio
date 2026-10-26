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

  // Durée écoulée sans IP (0 si connecté)
  uint32_t offlineForMs() const { return _offlineSinceMs ? (millis() - _offlineSinceMs) : 0; }

private:
  Options _opts;

  // État interne
  uint32_t _tConnectStart = 0;    // début de tentative en cours
  uint32_t _tNextAttempt = 0;     // prochain créneau de tentative
  uint32_t _offlineSinceMs = 0;   // début de la période hors ligne (0 = en ligne)
  uint8_t  _failCount = 0;        // pour le backoff exponentiel
  bool     _everConnected = false;

  void markOffline() { if (!_offlineSinceMs) _offlineSinceMs = millis() ? millis() : 1; }

  // Événements WiFi : le callback système ne fait que poser des drapeaux,
  // tout appel à l'API WiFi (disconnect/begin) ou aux logs se fait dans loop().
  volatile bool _evDisconnected = false;
  volatile bool _evGotIp = false;
  volatile uint8_t _evReason = 0;

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
