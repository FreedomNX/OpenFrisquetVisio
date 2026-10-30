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

        // Programmation hebdomadaire : 7 jours, du dimanche au samedi (42 octets).
        // Chaque jour contient 6 octets = 48 créneaux de 30 minutes.
        // 1 = Confort, 0 = Réduit. Octets en ordre chronologique, bits lus
        // du poids faible au poids fort : bit 0 du premier octet = 00:00–00:30,
        // bit 7 = 03:30–04:00 ; le deuxième octet commence à 04:00.
        // Pour le créneau n (0 à 47) : jour[n / 8] & (0x01 << (n % 8)).
        // Exemple observé : 01 00 00 00 00 00 = Confort de 00:00 à 00:30 uniquement.
        // FF FF FF FF FF FF = Confort toute la journée ; 00 ... 00 = Réduit.
        struct Programmation {
            byte dimanche[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            byte lundi[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            byte mardi[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            byte mercredi[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            byte jeudi[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            byte vendredi[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            byte samedi[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        };
        
        static_assert(sizeof(Programmation) == 42, "Programme hebdomadaire : 42 octets");

        Zone(uint8_t idZone, MqttManager& mqtt) : _idZone(idZone), _mqtt(mqtt) {}

        void begin();
        void loop();
        void setPilotageActif(bool actif);
        bool pilotageDisponible() const { return _pilotageActif; }
        bool appliquerPreset(const String& preset);
        bool appliquerConsigne(float temperature);
        void confirmerMode(MODE_ZONE mode, byte options);
        void confirmerMode() { confirmerMode(_mode, _modeOptions); }
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
        void refreshLastEnvoi() { _lastEnvoi = millis(); _programmationEnvoiEnAttente = false; }
        void refreshLastChange() { _lastChange = millis(); }
        
        SOURCE getSource() { return _source; }
        void setSource(SOURCE source) { _source = source; }

        Programmation& getProgrammation() { return _programmation; };
        void setProgrammation(const Programmation& programmation, bool recue = false) {
            // Ne pas écraser une modification du portail avant son envoi réussi.
            if(_programmationEnvoiEnAttente) return;
            memcpy(&_programmation, &programmation, sizeof(programmation));
            if(recue) _programmationRecue = true;
        }
        void modifierProgrammation(const Programmation& programmation) {
            memcpy(&_programmation, &programmation, sizeof(programmation));
            _programmationRecue = true;
            _programmationEnvoiEnAttente = _source != SOURCE::SATELLITE_VIRTUEL;
            if(_source == SOURCE::SATELLITE_VIRTUEL) _derogationFin = 0;
            refreshLastChange();
        }
        bool programmationEnvoiEnAttente() const { return _programmationEnvoiEnAttente; }
        bool programmationRecue() const { return _programmationRecue; }

    private:
        bool programmeLocal() const { return _source != SOURCE::CONNECT; }
        bool programmeDisponible() const;
        bool regimeProgramme(bool& confort, uint32_t* prochaineTransition = nullptr) const;
        void actualiserDecouverte();
        void publierMode();
        void actualiserConsigne();
        bool _pilotageActif = false;
        bool _mqttPret = false;
        bool _mqttConnecte = false;
        uint8_t _capacitesPubliees = 0xFF;
        uint32_t _derogationFin = 0;
        MODE_ZONE _modeObserve = MODE_ZONE::INCONNU;
        byte _optionsObservees = 0;
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
            MqttEntity preset;
            MqttEntity regime;
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

        // Octet « options » du bloc zone A154 (6e octet), au format de l'octet
        // A02F du satellite (voir Satellite.h : 0x20 Auto, 0x02 Dérogation,
        // 0x01 Confort, 0x04 état interne, 0x10 Hors Gel ; 0x40 Boost côté Connect).
        // Un Connect réel y recopie la dernière valeur A02F lue (ex. 0x25 en Auto) ;
        // l'émission du firmware conserve 0x04 pour Auto en attendant validation
        // sur chaudière. Seuls les bits 0x01, 0x02 et 0x40 sont interprétés ici.
        Programmation _programmation;
        // Programme confirmé par radio, défini sur le portail ou restauré de NVS.
        bool _programmationRecue = false;
        bool _programmationEnvoiEnAttente = false;

        Preferences _preferences;

        SOURCE _source = SOURCE::SATELLITE_PHYSIQUE;
        uint32_t _lastChange = 0;
        uint32_t _lastEnvoi = 0;
};
