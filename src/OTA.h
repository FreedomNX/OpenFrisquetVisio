#pragma once

#include <ArduinoOTA.h>
#include <string.h>
#include "Logs.h"

class OTA {
    public: 
        // Sans mot de passe, ArduinoOTA installe tout firmware reçu du réseau local.
        // On réutilise le mot de passe du portail ; « admin » étant public, l'OTA
        // reste désactivé tant qu'il n'a pas été changé.
        void begin(const char* hostname, const char* password) {
            if (password[0] == '\0' || strcmp(password, "admin") == 0) {
                info("[OTA] Désactivé : changer le mot de passe du portail pour l'activer.");
                return;
            }
            ArduinoOTA.setHostname(hostname);
            ArduinoOTA.setPassword(password);
            ArduinoOTA.setTimeout(25000);
            ArduinoOTA
                .onStart([]() {
                info("Mise à jour via OTA...");
                String type;
                if (ArduinoOTA.getCommand() == U_FLASH)
                    type = "sketch";
                else // U_SPIFFS
                    type = "filesystem";
            }).onEnd([](){ 
            }).onProgress([](unsigned int progress, unsigned int total){ 
            }).onError([](ota_error_t err) {
                error("Erreur lors de la mise à jour !");
            });

            ArduinoOTA.begin();
        }

        void loop() {
            ArduinoOTA.handle();
        }
};