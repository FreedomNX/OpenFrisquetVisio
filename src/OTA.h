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
            }).onError([updating](ota_error_t err) {
                if (updating) updating(false);
                error("Erreur lors de la mise à jour !");
            });

            ArduinoOTA.begin();
        }

        void loop() {
            ArduinoOTA.handle();
        }
};
