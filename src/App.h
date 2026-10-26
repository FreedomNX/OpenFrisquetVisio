#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include "MQTT/MqttManager.h"
#include "MQTT/MqttDevice.h"
#include "Config.h"
#include "OFVNetworkManager.h"
#include "Portal.h"
#include "Logs.h"
#include "OTA.h"

#include "Frisquet/FrisquetRadio.h"
#include "FrisquetManager.h"

class App {
public:
  App();

  void begin();
  void loop();

private:
  // Dépendances et services
  Config _cfg;
  WiFiClient _wifiClient;
  MqttManager _mqtt;
  OFVNetworkManager _networkManager;
  OTA _ota;

  FrisquetManager _frisquetManager;

  // Portail
  Portal* _portal = nullptr;

  // Radio
  FrisquetRadio _radio;

  // Politique hors ligne : AP de secours après 5 min, redémarrage après 10 min
  static constexpr uint32_t kApFallbackAfterMs = 5UL * 60UL * 1000UL;
  static constexpr uint32_t kRestartAfterOfflineMs = 10UL * 60UL * 1000UL;
  bool _apFallbackStarted = false;
  bool _wifiConfigured = false;

  // La gestion WiFi tourne dans sa propre tâche : la boucle principale peut
  // rester bloquée plusieurs minutes par les échanges radio (retries).
  TaskHandle_t _wifiTask = nullptr;
  static void wifiTaskEntry(void* self);
  void wifiLoop();

  // Étapes
  void initConfig();
  void initNetwork();
  void initMqtt();
  void initPortal();
  void initOta();
};
