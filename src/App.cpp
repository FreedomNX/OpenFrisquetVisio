#include "App.h"

App::App() : _mqtt(_wifiClient), _frisquetManager(_radio, _cfg, _mqtt) {}

void App::begin() {
  Serial.begin(115200);
  delay(50);

#if defined(BOARD_HELTEC)
  Heltec.begin(false /*DisplayEnable disable*/, false /*LoRa Disable*/, true /*Serial Enable*/);
#endif

  initConfig();
  initNetwork();
  initMqtt();
  initPortal();
  initOta();

  _frisquetManager.begin();
}

void App::loop() {
  // boucle des services
  _networkManager.loop();
  if (_apFallbackArmed && !WiFi.isConnected()) {
    if ((int32_t)(millis() - _apFallbackAtMs) >= 0) {
      _apFallbackArmed = false;
      WiFi.mode(WIFI_AP_STA);
      info("[WIFI] AP fallback activé après délai.");
    }
  }
  _ota.loop();
  _portal->loop();
  _mqtt.loop();
  _frisquetManager.loop();
  delay(10);
}

void App::initConfig() {
  _cfg.load();
}

void App::initNetwork() {
    _networkManager.onConnected([&](){
        info("[WIFI] CONNECTED  IP=%s  RSSI=%ddBm\n", _networkManager.ipStr().c_str(), _networkManager.rssi());
        _apFallbackArmed = false;
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
    });
    _networkManager.onDisconnected([&](const String& reason){
        info("[WIFI] DISCONNECTED (%s)\n", reason.c_str());
        _apFallbackArmed = true;
        _apFallbackAtMs = millis() + 60000;
    });

    _networkManager.begin(_cfg.getWiFiOptions());
}

void App::initMqtt() {
  _mqtt.begin(_cfg.getMQTTOptions());
}

void App::initPortal() {
  _portal = new Portal(_frisquetManager);
  _portal->begin(/*startApFallbackIfNoWifi=*/true);

  info("[PORTAIL] Portail initialisé.");
}

void App::initOta() {
  _ota.begin(_networkManager.hostname().c_str());
}
