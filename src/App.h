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

  // AP fallback after disconnect
  bool _apFallbackArmed = false;
  uint32_t _apFallbackAtMs = 0;

  // Étapes
  void initConfig();
  void initNetwork();
  void initMqtt();
  void initPortal();
  void initOta();
};
