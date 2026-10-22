#include "App.h"

namespace {
String fmtFloat(float value, uint8_t decimals = 1, const char* unit = "") {
  if (isnan(value)) {
    return "-";
  }
  String out((double)value, (unsigned int)decimals);
  if (unit && unit[0] != '\0') {
    out += unit;
  }
  return out;
}
}

App::App() : _mqtt(_wifiClient), _frisquetManager(_radio, _cfg, _mqtt) {}

void App::begin() {
  Serial.begin(115200);
  delay(50);

  Heltec.begin(true /*DisplayEnable enable*/, false /*LoRa Disable*/, true /*Serial Enable*/);
  initLocalUi();

  initConfig();
  initNetwork();
  initMqtt();
  initPortal();
  initOta();

  _frisquetManager.begin();
}

void App::loop() {
  // boucle des services
  updateLocalUi();
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

void App::initLocalUi() {
  pinMode(0, INPUT_PULLUP); // BOOT button (Heltec V3/V4 families)

  if (Heltec.display == nullptr) {
    return;
  }

  _uiPage = 0;
  _uiAwake = false;
  _uiAwakeUntilMs = 0;
  _uiNextRefreshMs = 0;
  _uiDirty = false;
  sleepLocalUi();
}

void App::updateLocalUi() {
  if (Heltec.display == nullptr) {
    return;
  }

  const uint32_t now = millis();
  if ((int32_t)(now - _bootBtnDebounceMs) < 0) {
    // Debounce active, continue with periodic refresh.
  } else {
    const bool pressed = (digitalRead(0) == LOW);
    if (pressed != _bootBtnLastPressed) {
      _bootBtnLastPressed = pressed;
      _bootBtnDebounceMs = now + 50;

      if (pressed) {
        if (!_uiAwake) {
          _uiAwake = true;
          _uiPage = 0; // Page 1: connectivite
          Heltec.display->displayOn();
        } else {
          _uiPage = (_uiPage + 1) % 3;
        }

        _uiAwakeUntilMs = now + 15000;
        _uiDirty = true;
      }
    }
  }

  if (!_uiAwake) {
    return;
  }

  if ((int32_t)(now - _uiAwakeUntilMs) >= 0) {
    sleepLocalUi();
    return;
  }

  if (_uiDirty || (int32_t)(now - _uiNextRefreshMs) >= 0) {
    renderLocalUi();
    _uiDirty = false;
    _uiNextRefreshMs = now + 1000;
  }
}

void App::renderLocalUi() {
  if (Heltec.display == nullptr) {
    return;
  }

  String l0;
  String l1;
  String l2;
  String l3;
  String l4;
  String l5;

  if (_uiPage == 0) {
    l0 = "Page 1/3 Connectivite";
    l1 = String("WiFi: ") + (WiFi.isConnected() ? "OK" : "OFF");
    l2 = String("MQTT: ") + (_mqtt.connected() ? "OK" : "OFF");
    l3 = String("IP: ") + (WiFi.isConnected() ? WiFi.localIP().toString() : "-");
    l4 = String("RSSI: ") + (WiFi.isConnected() ? String(WiFi.RSSI()) + " dBm" : "-");
    l5 = String("SSID: ") + (WiFi.isConnected() ? WiFi.SSID() : "-");
  } else if (_uiPage == 1) {
    const auto& c = _frisquetManager.chaudiere();
    l0 = "Page 2/3 Chaudiere";
    l1 = String("T CDC: ") + fmtFloat(c.getTemperatureCDC(), 1, "C");
    l2 = String("T ECS: ") + fmtFloat(c.getTemperatureECS(), 1, "C");
    l3 = String("T Ext: ") + fmtFloat(c.getTemperatureExterieure(), 1, "C");
    l4 = String("Press: ") + fmtFloat(c.getPression(), 1, " bar");
    l5 = String("Mode ECS: ") + c.getNomModeECS();
  } else {
    auto& z1 = _frisquetManager.zone1();
    const float sondeEnvoyee = _cfg.useSondeExterieure()
      ? _frisquetManager.sondeExterieure().getTemperatureExterieure()
      : NAN;

    l0 = "Page 3/3 Zone";
    l1 = String("Amb Z1: ") + (_cfg.useZone1() ? fmtFloat(z1.getTemperatureAmbiante(), 1, "C") : "-");
    l2 = String("Cons Z1: ") + (_cfg.useZone1() ? fmtFloat(z1.getTemperatureConsigne(), 1, "C") : "-");
    l3 = String("Dep Z1: ") + (_cfg.useZone1() ? fmtFloat(z1.getTemperatureDepart(), 1, "C") : "-");
    l4 = String("Boost Z1: ") + (_cfg.useZone1() ? (z1.boostActif() ? "ON" : "OFF") : "-");
    l5 = String("Sonde->Chaud: ") + fmtFloat(sondeEnvoyee, 1, "C");
  }

  Heltec.display->clear();
  Heltec.display->drawString(0, 0, l0);
  Heltec.display->drawString(0, 10, l1);
  Heltec.display->drawString(0, 20, l2);
  Heltec.display->drawString(0, 30, l3);
  Heltec.display->drawString(0, 40, l4);
  Heltec.display->drawString(0, 50, l5);
  Heltec.display->display();
}

void App::sleepLocalUi() {
  _uiAwake = false;
  _uiAwakeUntilMs = 0;
  _uiDirty = false;

  if (Heltec.display == nullptr) {
    return;
  }

  Heltec.display->clear();
  Heltec.display->display();
  Heltec.display->displayOff();
}
