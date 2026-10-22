#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include "MQTT/MqttManager.h"
#include "MQTT/MqttDevice.h"
#include "Config.h"
#include "NetworkManager.h"
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
  NetworkManager _networkManager;
  OTA _ota;

  FrisquetManager _frisquetManager;

  // Portail
  Portal* _portal = nullptr;

  // Radio
  FrisquetRadio _radio;

  // AP fallback after disconnect
  bool _apFallbackArmed = false;
  uint32_t _apFallbackAtMs = 0;

  // Étapes
  void initConfig();
  void initNetwork();
  void initMqtt();
  void initPortal();
  void initOta();
  void initLocalUi();
  void updateLocalUi();
  void renderLocalUi();
  void sleepLocalUi();

  bool _bootBtnLastPressed = false;
  uint32_t _bootBtnDebounceMs = 0;
  uint8_t _uiPage = 0;
  bool _uiAwake = false;
  uint32_t _uiAwakeUntilMs = 0;
  uint32_t _uiNextRefreshMs = 0;
  bool _uiDirty = true;
};
