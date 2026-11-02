#include "App.h"
#include <Board.h>
#include "CrashTrace.h"

App::App() : _mqtt(_wifiClient), _frisquetManager(_radio, _cfg, _mqtt) {}

void App::begin() {
  Serial.begin(115200);
  delay(50);

  CrashTrace::begin();
#if defined(BOARD_HELTEC)
  Heltec.begin(true /*DisplayEnable*/, false /*LoRa Disable*/, true /*Serial Enable*/);
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
  afficherEcran();
  // boucle des services
  const uint32_t loopStart = millis();
  CrashTrace::mark(CrashTrace::S_OTA);
  _ota.loop();
  CrashTrace::mark(CrashTrace::S_PORTAL);
  _portal->loop();
  CrashTrace::mark(CrashTrace::S_MQTT);
  _mqtt.loop();
  CrashTrace::mark(CrashTrace::S_MANAGER);
  _frisquetManager.loop();
  CrashTrace::mark(CrashTrace::S_IDLE);
  CrashTrace::loopDone(millis() - loopStart);
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

// Affichage local : la carte peut être loin d'un écran, et il est utile de lire
// ambiante et consigne sans passer par Home Assistant.
void App::afficherEcran() {
#if defined(BOARD_HELTEC)
  static uint32_t derniere = 0;
  if(millis() - derniere < 2000) return;
  derniere = millis();

  SSD1306Wire* d = Heltec.display;
  if(!d) return;

  Zone& z1 = _frisquetManager.zone1();
  const float amb  = z1.getTemperatureAmbiante();
  const float cons = z1.getTemperatureConsigne();
  const Zone::MODE_ZONE mode = z1.getMode();
  const bool confort = z1.confortActif();
  const Chaudiere::ETAT_CHAUDIERE etat = _frisquetManager.chaudiere().getEtatChaudiere();

  char buf[40];
  d->clear();
  d->setTextAlignment(TEXT_ALIGN_LEFT);
  d->setFont(ArialMT_Plain_10);

  snprintf(buf, sizeof(buf), "Chaudiere: %s",
           etat.anomalie ? "DEFAUT" : (etat.fonctionnement ? "Chauffage" : "Veille"));
  d->drawString(0, 0, buf);

  const char* modeTxt = "Inconnu";
  switch(mode) {
    case Zone::MODE_ZONE::AUTO:    modeTxt = "Auto";    break;
    case Zone::MODE_ZONE::CONFORT: modeTxt = "Confort"; break;
    case Zone::MODE_ZONE::REDUIT:  modeTxt = "Reduit";  break;
    case Zone::MODE_ZONE::HORS_GEL:modeTxt = "Hors gel";break;
    default: break;
  }
  snprintf(buf, sizeof(buf), "%s / %s", modeTxt, confort ? "Confort" : "Reduit");
  d->drawString(0, 12, buf);

  d->setFont(ArialMT_Plain_24);
  if(isnan(amb)) snprintf(buf, sizeof(buf), "--.-");
  else           snprintf(buf, sizeof(buf), "%.1f", amb);
  d->drawString(0, 24, buf);
  d->setFont(ArialMT_Plain_10);
  d->drawString(58, 36, "C");

  if(!isnan(cons)) {
    snprintf(buf, sizeof(buf), "cons %.1f", cons);
    d->drawString(78, 36, buf);
  }

  snprintf(buf, sizeof(buf), "WiFi %d  MQTT %s", (int)WiFi.RSSI(),
           _frisquetManager.mqtt().connected() ? "OK" : "--");
  d->drawString(0, 52, buf);

  d->display();
#endif
}
