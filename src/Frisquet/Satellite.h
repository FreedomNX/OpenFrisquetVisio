#pragma once

#include "FrisquetDevice.h"
#include <Preferences.h>
#include "Logs.h"
#include "Zone.h"
#include "Chaudiere.h"
#include "RadioSchedule.h"

class Satellite : public FrisquetDevice {
    
    public:
        // Octet « mode » écrit par le satellite en A02F (4e mot), observé sur
        // satellite physique piloté par le boîtier et par l'appli Connect :
        //   0x20 Auto (programme en cours), absent en Confort/Réduit permanent ;
        //   0x02 Dérogation en cours ;
        //   0x01 Confort (1) / Réduit (0) ;
        //   0x04 état interne sans rapport avec le mode : effacé pendant ~3 h
        //        après un changement (passage programmé en réduit, réécriture du
        //        programme, redémarrage, retour en Auto) puis remis seul ;
        //   0x10 Hors Gel.
        static constexpr uint8_t MODE_BIT_CONFORT = 0x01;
        static constexpr uint8_t MODE_BIT_DEROGATION = 0x02;
        static constexpr uint8_t MODE_BIT_ETAT_INTERNE = 0x04;
        static constexpr uint8_t MODE_BIT_AUTO = 0x20;
        static constexpr uint8_t MODE_BITS_CONNUS = MODE_BIT_CONFORT | MODE_BIT_DEROGATION | MODE_BIT_ETAT_INTERNE | MODE_BIT_AUTO;

        // Valeurs émises : celles observées sur un satellite physique en régime
        // établi (Auto, Confort permanent, Réduit permanent, dérogation réduit) ;
        // la dérogation confort reprend la valeur demandée par le Connect.
        enum MODE : uint8_t {
            REDUIT_PERMANENT = 0x00,
            CONFORT_PERMANENT = 0x05,
            REDUIT_DEROGATION = 0x22,
            CONFORT_DEROGATION = 0x23,
            REDUIT_AUTO = 0x24,
            CONFORT_AUTO = 0x25,
            HORS_GEL = 0x10,
            INCONNU = 0xFF,
        };

        Satellite(FrisquetRadio& radio, Config& cfg, MqttManager& mqtt, Zone& zone, Chaudiere& chaudiere)
            : FrisquetDevice(radio, cfg, mqtt, zone.getIdZone()), _zone(zone), _chaudiere(chaudiere) {}
        void loadConfig();
        void saveConfig();

        void loop();
        void begin(bool modeVirtuel = false);
        void publishMqtt();

        bool envoyerConsigne();
        bool envoyerTemperatureAmbiante();
        bool recupererInfosChaudiere();
        bool recupererTemperatureCDC();

        bool onReceive(byte* donnees, size_t length);

        MODE getMode();
        void setMode(MODE mode);
        void setModeFromRaw(uint8_t rawMode);
        String getNomMode();

        void setEcrasement(bool ecrasement) {
            _ecrasement = ecrasement;
            if(_zone.getSource() == Zone::SOURCE::SATELLITE_PHYSIQUE) _zone.setPilotageActif(ecrasement);
        }
        bool getEcrasement() { return _ecrasement; }

        void setEtatChaudiere(byte etatChaudiere) { _chaudiere.setEtatChaudiere(etatChaudiere); }
        Chaudiere::ETAT_CHAUDIERE getEtatChaudiere() { return _chaudiere.getEtatChaudiere(); }

        uint8_t getNumeroZone() {
            return _zone.getNumeroZone();
        }

    private:
        Zone& _zone;
        Chaudiere& _chaudiere;

        uint32_t _lastEnvoiConsigne = 0;
        uint32_t _lastRecuperationTemperatureCDC = 0;
        RadioSchedule _cdcSchedules[2];
        bool _initialReadPending = true;
        bool _modeVirtuel = false;
        bool _ecrasement = false;

        MODE decodeModeFromRaw(uint8_t rawMode) const;
        uint8_t encodeModeForSend(MODE mode) const;
        struct {
            MqttEntity ecrasementConsigne;
        } _mqttEntities;
};
