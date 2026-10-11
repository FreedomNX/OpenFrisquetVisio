#pragma once

#include <Board.h>
#include "../MQTT/MqttManager.h"
#include "../Logs.h"
#include "FrisquetDevice.h"
#include "Preferences.h"

class Zone {
    public:
        enum MODE_ZONE : uint8_t {
            INCONNU = 0xFF,
            AUTO = 0x05,
            CONFORT = 0x06,
            REDUIT = 0X07,
            HORS_GEL = 0x08
        };

        enum SOURCE {
            CONNECT,
            SATELLITE_PHYSIQUE,
            SATELLITE_VIRTUEL
        };

        struct Programmation {
            byte dimanche[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            byte lundi[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            byte mardi[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            byte mercredi[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            byte jeudi[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            byte vendredi[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            byte samedi[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        };
        
        Zone(uint8_t idZone, MqttManager& mqtt) : _idZone(idZone), _mqtt(mqtt) {}

        void begin();
        MqttManager& mqtt() { return _mqtt; }
        void loadConfig();
        void saveConfig();
        void publishMqtt();


        uint8_t getNumeroZone();

        void setTemperatureConfort(float temperature);
        void setTemperatureReduit(float temperature);
        void setTemperatureHorsGel(float temperature);
        void setTemperatureAmbiante(float temperature);
        void setTemperatureConsigne(float temperature);
        void setTemperatureDepart(float temperature);
        void setTemperatureConsigneDepart(float temperature);
        void setTemperatureBoost(float temperature);

        float getTemperatureConfort();
        float getTemperatureReduit();
        float getTemperatureHorsGel();
        
        float getTemperatureConsigne();
        float getTemperatureAmbiante();
        float getTemperatureDepart();
        float getTemperatureConsigneDepart();
        float getTemperatureBoost();

        bool confortActif();
        bool reduitActif();
        bool horsGelActif();

        bool derogationActive();

        uint8_t getIdZone();

        MODE_ZONE getMode();
        void setMode(MODE_ZONE mode, bool confort = false, bool derogation = false);
        void setMode(const String& mode, bool confort = false, bool derogation = false);
        byte getModeOptions();
        void setModeOptions(byte modeOptions);

        void activerBoost();
        void desactiverBoost();
        bool boostActif();

        String getNomMode();
        
        uint32_t getLastChange() { return _lastChange; }
        uint32_t getLastEnvoi() { return _lastEnvoi; }
        void refreshLastEnvoi() { _lastEnvoi = millis(); }
        void refreshLastChange() { _lastChange = millis(); }
        
        SOURCE getSource() { return _source; }
        void setSource(SOURCE source) { _source = source; }

        Programmation& getProgrammation() { return _programmation; };
        void setProgrammation(Programmation& programmation) { memcpy(&_programmation, &programmation, sizeof(programmation)); };

    private:
        MqttManager& _mqtt;
        struct {
            MqttEntity mode;
            MqttEntity temperatureAmbiante;
            MqttEntity temperatureConsigne;
            MqttEntity temperatureConfort;
            MqttEntity temperatureReduit;
            MqttEntity temperatureHorsGel;
            MqttEntity temperatureDepart;
            MqttEntity temperatureConsigneDepart;
            MqttEntity temperatureBoost;
            MqttEntity boost;
            MqttEntity thermostat;
        } _mqttEntities;

        uint8_t _idZone = 0x00;

        float _temperatureConfort = NAN;                       // Début 5°C -> max 30°C
        float _temperatureReduit = NAN;                        // Début 5°C -> max 30°C
        float _temperatureHorsGel = NAN;                       // Début 5°C -> max 30°C
        float _temperatureDepart = NAN;   
        float _temperatureConsigneDepart = NAN;
        float _temperatureConsigne = NAN; 
        float _temperatureAmbiante = NAN;
        float _temperatureBoost = NAN;

        MODE_ZONE _mode = MODE_ZONE::INCONNU;            // 0x05 auto - 0x06 confort - 0x07 reduit - 0x08 hors gel
        byte _modeOptions = 0x05;

        /*
        Mode Option structure bits
            inconnu1: 1 bit,
            boost: 1 bit,
            inconnu2: 2 bits,
            inconnu3: 2 bits,
            derogation: 1 bit,
            confort: 1 bit
        */
        Programmation _programmation;

        Preferences _preferences;

        SOURCE _source = SOURCE::SATELLITE_PHYSIQUE;
        uint32_t _lastChange = 0;
        uint32_t _lastEnvoi = 0;
};
