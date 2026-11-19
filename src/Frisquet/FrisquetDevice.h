#pragma once

#include <Board.h>
#include "FrisquetRadio.h"
#include <Preferences.h>
#include "../MQTT/MqttManager.h"
#include "../Config.h"
#include "BoilerIdentity.h"

#include <TimeLib.h>

#define ID_ZONE_1 0x08
#define ID_ZONE_2 0x09
#define ID_ZONE_3 0x0A
#define ID_SONDE_EXTERIEURE 0x20
#define ID_CONNECT 0x7E

class FrisquetDevice {
    public:
        uint8_t getId() { return _idAppareil; };
        bool estAssocie() {
            return _idAssociation != 0xFF;
        }

        Date& getDate() { return _date; }
        void setDate(Date& date){ _date = date; setTime(_date.heure, _date.minute, _date.seconde, _date.jour, _date.mois, _date.annee); }
        // Date BCD reçue d'une trame (YY MM DD hh mm ss) : l'horloge n'est réglée
        // que si elle est valide, comme pour la lecture explicite de recupererDate().
        bool setDateRecue(const byte* bcd) {
            if (!BoilerIdentity::validDate(bcd)) {
                debug("[RADIO] Date reçue invalide ignorée : %02X %02X %02X %02X %02X %02X.",
                      bcd[0], bcd[1], bcd[2], bcd[3], bcd[4], bcd[5]);
                return false;
            }
            byte copie[6];
            memcpy(copie, bcd, sizeof(copie));
            Date date(copie);
            setDate(date);
            return true;
        }
        bool recupererDate();
        
        void setIdAssociation(uint8_t idAssociation) { _idAssociation = idAssociation; };
        uint8_t getIdAssociation() { return _idAssociation; };
        bool associer(NetworkID& networkId, uint8_t& idAssociation);

    protected:
        FrisquetDevice(FrisquetRadio& radio, Config& cfg, MqttManager& mqtt, uint8_t idAppareil, uint8_t idAssociation = 0xFF) : _radio(radio), _mqtt(mqtt), _cfg(cfg), _idAppareil(idAppareil), _idAssociation(idAssociation) {}
        FrisquetRadio& getRadio() { return _radio; }

        uint8_t getIdMessage() { return _idMessage; };
        void setIdMessage(uint8_t idMessage) { _idMessage = idMessage; };
        uint8_t incrementIdMessage(uint8_t increment = 1) { _idMessage += increment; return _idMessage; };

        FrisquetRadio& radio() { return _radio; }
        MqttManager& mqtt() { return _mqtt; }

        Preferences& getPreferences() { return _preferences; }
        Config& getConfig() { return _cfg; }
        void confirmerChaudiere(uint8_t id);

        void loadConfig() {}
        void saveConfig() {}

        void begin() {};
        void loop() {};

    private:

        FrisquetRadio& _radio;
        MqttManager& _mqtt;

        Config& _cfg;
        Preferences _preferences;

        uint8_t _idAssociation = 0xFF;
        uint8_t _idAppareil = 0x00;
        uint8_t _idMessage = 0;

        Date _date;
};
