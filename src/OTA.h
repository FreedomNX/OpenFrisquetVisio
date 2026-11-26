#pragma once

#include <ArduinoOTA.h>
#include <functional>
#include "Logs.h"

class OTA {
    public: 
        void begin(const char* hostname, const char* password, std::function<void(bool)> updating = {}) {
            ArduinoOTA.setHostname(hostname);
            ArduinoOTA.setPassword(password);
            ArduinoOTA.setTimeout(25000);
            ArduinoOTA
                .onStart([updating]() {
                if (updating) updating(true);
                info("Mise à jour via OTA...");
                String type;
                if (ArduinoOTA.getCommand() == U_FLASH)
                    type = "sketch";
                else // U_SPIFFS
                    type = "filesystem";
            }).onEnd([updating](){
                if (updating) updating(false);
            }).onProgress([](unsigned int progress, unsigned int total){ 
            }).onError([this, updating](ota_error_t err) {
                if (updating) updating(false);
                if (err == OTA_AUTH_ERROR) {
                    // Vérifier un mot de passe bloque la boucle des services environ une seconde
                    // (PBKDF2, 10 000 itérations) : les essais suivants attendent la fin de la pause.
                    _authPauseStartMs = millis();
                    _authPaused = true;
                    error("[OTA] Mot de passe refusé, mises à jour réseau ignorées pendant 30 s.");
                } else {
                    error("Erreur lors de la mise à jour !");
                }
            });

            ArduinoOTA.begin();
        }

        void loop() {
            if (_authPaused && millis() - _authPauseStartMs < kAuthPauseMs) return;
            _authPaused = false;
            ArduinoOTA.handle();
        }

    private:
        static constexpr uint32_t kAuthPauseMs = 30000;
        bool _authPaused = false;
        uint32_t _authPauseStartMs = 0;
};
