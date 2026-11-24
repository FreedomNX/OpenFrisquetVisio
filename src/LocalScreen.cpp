#include "LocalScreen.h"
#if defined(OFV_SCREEN_V3)
#include "FrisquetManager.h"
#include "OFVNetworkManager.h"
#include "Version.h"
#include <Wire.h>
#include <TimeLib.h>

namespace {
constexpr uint8_t ADDRESS = 0x3C, BUTTON = 0;
String number(float value, const char* unit = " C") {
    return isfinite(value) ? String(value, 1) + unit : String("--");
}
void line(int y, const String& text, bool large = false) {
    Heltec.display->setFont(large ? ArialMT_Plain_16 : ArialMT_Plain_10);
    // Ne pas laisser un texte long déborder de sa ligne sur le petit OLED.
    String visible = text;
    while (visible.length() && Heltec.display->getStringWidth(visible) > 128)
        visible.remove(visible.length() - 1);
    Heltec.display->drawString(0, y, visible);
}
}

bool LocalScreen::command(const uint8_t* bytes, size_t length) {
    Wire.beginTransmission(ADDRESS);
    Wire.write(uint8_t(0));
    Wire.write(bytes, length);
    return Wire.endTransmission() == 0;
}

void LocalScreen::begin(FrisquetManager& manager, OFVNetworkManager& network) {
    _manager = &manager;
    _network = &network;
    auto& cfg = manager.config();
    if (!cfg.displayEnabled()) {
        // Un redémarrage logiciel ne coupe pas forcément l'alimentation OLED.
        // Maintenir son reset évite de conserver une ancienne image allumée.
        pinMode(RST_OLED, OUTPUT);
        digitalWrite(RST_OLED, LOW);
        return;
    }
    pinMode(BUTTON, INPUT_PULLUP);
    // Heltec.begin(false, false, ...) a déjà activé Vext. Ne jamais couper
    // cette alimentation partagée pour mettre simplement l'écran en veille.
    pinMode(RST_OLED, OUTPUT);
    digitalWrite(RST_OLED, LOW);
    delay(5);
    digitalWrite(RST_OLED, HIGH);
    delay(5);
    Wire.begin(SDA_OLED, SCL_OLED, 400000);
    Wire.setTimeOut(5);
    Wire.beginTransmission(ADDRESS);
    if (Wire.endTransmission() != 0) {
        info("[ECRAN] OLED absent, affichage désactivé.");
        Wire.end();
        return;
    }
    if (!Heltec.display || !Heltec.display->init()) {
        error("[ECRAN] Initialisation OLED impossible.");
        return;
    }
    Wire.setClock(400000);
    Wire.setTimeOut(5);
    // Orientation matérielle ; le tampon reste dans l'ordre natif 128 x 64.
    const uint8_t setup[] = {0xAE, 0x20, 0x00, uint8_t(cfg.displayFlip() ? 0xA0 : 0xA1),
                             uint8_t(cfg.displayFlip() ? 0xC0 : 0xC8), 0x81, cfg.displayBrightness()};
    if (!command(setup, sizeof(setup))) { fail(); return; }
    Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
    _present = true;
    _navigation.begin(millis(), digitalRead(BUTTON) == LOW, cfg.displayAutoCycle());
    render(millis());
    // Premier écran avant l'initialisation Wi-Fi/radio. Ensuite, chaque tour
    // de la boucle ne transfère qu'un fragment de l'image.
    while (_present && _uploading) transfer();
    if (_present) info("[ECRAN] OLED détecté ; navigation par PRG activée.");
}

void LocalScreen::fail() {
    _present = false;
    _uploading = false;
    error("[ECRAN] OLED indisponible, affichage arrêté jusqu'au redémarrage.");
}

bool LocalScreen::availableBus() const {
    auto& radio = _manager->radio();
    return !radio.busy() && !radio.hasPendingPacket();
}

void LocalScreen::advance() {
    auto& cfg = _manager->config();
    do { _page = (_page + 1) % 6; }
    while ((_page == 1 && !cfg.useZone1()) || (_page == 2 && !cfg.useZone2())
        || (_page == 3 && !cfg.useZone3()));
    _dirty = true;
}

void LocalScreen::loop() {
    if (!_present) return;
    const uint32_t now = millis();
    const auto action = _navigation.poll(now, digitalRead(BUTTON) == LOW);
    if (action == ScreenNavigation::Action::Next) advance();
    if (action == ScreenNavigation::Action::Home) { _page = 0; _dirty = true; }
    if (action == ScreenNavigation::Action::Wake || action == ScreenNavigation::Action::ToggleCycle)
        _dirty = true;
    if (_navigation.cycleDue(now)) { advance(); _navigation.pageChanged(now); }
    if (_navigation.shouldSleep(now, _manager->config().displayTimeout())) _navigation.sleep();
    _desiredPower = _navigation.awake();
    // Les gestes sont traités même pendant une transaction radio, mais aucun
    // transfert I2C ne prend alors de temps à la réception.
    if (!availableBus()) return;
    if (!_desiredPower) {
        if (_power) {
            const uint8_t off = 0xAE;
            if (!command(&off, 1)) { fail(); return; }
            _power = false;
        }
        _uploading = false;
        return;
    }
    if (!_uploading && (_dirty || now - _lastRender >= 1000)) render(now);
    if (_uploading) transfer();
}

void LocalScreen::render(uint32_t now) {
    auto& boiler = _manager->chaudiere();
    auto& radio = _manager->radio();
    Heltec.display->clear();
    const bool stale = radio.hasValidReception() && now - radio.lastValidReceptionMs() > 900000;
    if (_page == 0) {
        const float probe = _manager->sondeExterieure().getTemperatureExterieure();
        const float outdoor = _manager->config().useSondeExterieure() && isfinite(probe)
            ? probe : boiler.getTemperatureExterieure();
        char clock[12] = "Heure --:--";
        if (timeStatus() != timeNotSet) snprintf(clock, sizeof(clock), "%02d:%02d:%02d", hour(), minute(), second());
        line(0, String(clock) + (_navigation.cycling() ? "  Auto" : ""));
        line(14, boiler.getNomEtatChaudiere());
        line(29, "Ext. " + number(outdoor), true);
        line(49, String(_network->isConnected() ? "WiFi OK" : "WiFi --")
            + (_manager->mqtt().connected() ? "  MQTT OK" : "  MQTT --"));
    } else if (_page <= 3) {
        Zone* zones[] = {&_manager->connect().getZone1(), &_manager->connect().getZone2(), &_manager->connect().getZone3()};
        Zone& zone = *zones[_page - 1];
        line(0, "Zone " + String(_page) + "  " + zone.getNomMode());
        line(14, number(zone.getTemperatureAmbiante()), true);
        line(34, "Consigne " + number(zone.getTemperatureConsigne()));
        line(49, zone.boostActif() ? "Boost actif" : stale ? "Radio ancienne >15min" : "PRG : page suivante");
    } else if (_page == 4) {
        line(0, stale ? "Chaudiere - RX ancien" : "Chaudiere");
        line(12, "ECS " + number(boiler.getTemperatureECS()));
        line(25, "Chauffage " + number(boiler.getPuissanceInstantaneeChauffage(), " kW"));
        line(38, "Puiss. ECS " + number(boiler.getPuissanceInstantaneeECS(), " kW"));
        line(51, !boiler.etatChaudiereConnu() ? "Anomalie : --"
            : boiler.getEtatChaudiere().anomalie ? "Anomalie detectee" : "Anomalie : aucune");
    } else {
        line(0, _network->isConnected() ? _network->ipStr() : String("AP ") + WiFi.softAPIP().toString());
        line(13, _network->isConnected() ? "WiFi " + String(_network->rssi()) + " dBm" : "WiFi deconnecte");
        line(26, String("MQTT ") + (_manager->mqtt().connected() ? "connecte" : "deconnecte"));
        const String age = !radio.hasValidReception() ? "RX : aucune trame" :
            String("RX il y a ") + String((now - radio.lastValidReceptionMs()) / 1000) + " s";
        line(39, age);
        line(51, String(OPENFRISQUETVISIO_VERSION));
    }
    _lastRender = now;
    _dirty = false;
    _offset = 0;
    _uploading = true;
}

void LocalScreen::transfer() {
    // Au plus une transaction I2C par tour de boucle : environ 0,8 ms de
    // données à 400 kHz, timeout 5 ms en cas de panne. Aucun display() bloquant.
    if (_offset == 1026) {
        const uint8_t on = 0xAF;
        if (!command(&on, 1)) { fail(); return; }
        _power = true;
        _uploading = false;
        return;
    }
    if (_offset == 0) {
        const uint8_t bounds[] = {0x21, 0, 127, 0x22, 0, 7};
        if (!command(bounds, sizeof(bounds))) { fail(); return; }
        _offset = 1;
        return;
    }
    const size_t start = _offset - 1;
    Wire.beginTransmission(ADDRESS);
    Wire.write(uint8_t(0x40));
    Wire.write(Heltec.display->buffer + start, 32);
    if (Wire.endTransmission() != 0) { fail(); return; }
    _offset += 32;
    if (_offset == 1025) {
        _uploading = false;
        if (!_power) {
            // Allumer au tour suivant après l'image complète, sans flash du
            // contenu de la RAM OLED au démarrage ou au réveil.
            _offset = 1026;
            _uploading = true;
        }
    }
}
#endif
