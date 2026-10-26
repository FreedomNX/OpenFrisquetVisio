#include "App.h"
#include <Board.h>

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

  xTaskCreatePinnedToCore(wifiTaskEntry, "wifi", 6144, this, 1, &_wifiTask, ARDUINO_RUNNING_CORE);

  _frisquetManager.begin();
}

void App::loop() {
  // boucle des services
  _ota.loop();
  _portal->loop();
  _mqtt.loop();
  _frisquetManager.loop();
  delay(10);
}

void App::wifiTaskEntry(void* self) {
  static_cast<App*>(self)->wifiLoop();
}

void App::wifiLoop() {
  for (;;) {
    _networkManager.loop();
    if (_wifiConfigured) {
      const uint32_t offline = _networkManager.offlineForMs();
      if (offline >= kRestartAfterOfflineMs) {
        error("[WIFI] Hors ligne depuis %lu s, redémarrage.", (unsigned long)(offline / 1000));
        vTaskDelay(200 / portTICK_PERIOD_MS);
        ESP.restart();
      }
      if (!_apFallbackStarted && offline >= kApFallbackAfterMs) {
        _apFallbackStarted = true;
        info("[WIFI] AP fallback activé après %lu s hors ligne.", (unsigned long)(offline / 1000));
        _portal->startAp();
      }
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void App::initConfig() {
  _cfg.load();
}

void App::initNetwork() {
    _networkManager.onConnected([&](){
        info("[WIFI] CONNECTED  IP=%s  RSSI=%ddBm\n", _networkManager.ipStr().c_str(), _networkManager.rssi());
        if (_apFallbackStarted) {
          _apFallbackStarted = false;
          WiFi.softAPdisconnect(true);
          WiFi.mode(WIFI_STA);
        }
    });
    _networkManager.onDisconnected([&](const String& reason){
        info("[WIFI] DISCONNECTED (%s)\n", reason.c_str());
    });

    _wifiConfigured = !_cfg.getWiFiOptions().ssid.isEmpty();
    _networkManager.begin(_cfg.getWiFiOptions());
}

void App::initMqtt() {
  _mqtt.begin(_cfg.getMQTTOptions());
}

void App::initPortal() {
  _portal = new Portal(_frisquetManager);
  // AP immédiat seulement sans WiFi configuré (première installation) ;
  // sinon l'AP de secours est géré par la politique hors ligne de loop().
  _portal->begin(/*startApFallbackIfNoWifi=*/!_wifiConfigured);

  info("[PORTAIL] Portail initialisé.");
}

void App::initOta() {
  _ota.begin(_networkManager.hostname().c_str());
}
