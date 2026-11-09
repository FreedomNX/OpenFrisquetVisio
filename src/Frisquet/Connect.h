#pragma once

#include "FrisquetDevice.h"
#include <Preferences.h>
#include "../Logs.h"
#include "Zone.h"
#include "Chaudiere.h"
#include "RadioSchedule.h"
#include "BoilerFirmware.h"

class Connect : public FrisquetDevice {
    
    public:
        Connect(FrisquetRadio& radio, Config& cfg, MqttManager& mqtt, Zone& zone1, Zone& zone2, Zone& zone3, Chaudiere& chaudiere) : 
            FrisquetDevice(radio, cfg, mqtt, ID_CONNECT),
            _zone1(zone1),
            _zone2(zone2),
            _zone3(zone3),
            _chaudiere(chaudiere) {}
        void loadConfig();
        void saveConfig();

        void begin();
        void loop();

        using MODE_ECS = Chaudiere::MODE_ECS;

        Zone& getZone1() { return _zone1; }
        Zone& getZone2() { return _zone2; }
        Zone& getZone3() { return _zone3; }
        Zone& getZone(uint8_t idZone) { 
            switch (idZone) {
                default:
                case ID_ZONE_1: return _zone1;
                case ID_ZONE_2: return _zone2;
                case ID_ZONE_3: return _zone3;
            }
        }
        Chaudiere& chaudiere() { return _chaudiere; }

        bool envoyerZone(Zone& zone);
        bool envoyerModeECS();
        bool recupererInformations();
        bool recupererConsommation();
        bool recupererConsommationAnnuelle();
        bool recupererModeECS();

        float getTemperatureExterieure();
        float getTemperatureECS();
        float getTemperatureCDC();

        int16_t getConsommationECS();
        int16_t getConsommationChauffage();

        MODE_ECS getModeECS();
        bool setModeECS(MODE_ECS modeECS);
        bool setModeECS(const String& modeECS);
        String getNomModeECS();
        void handleModeEcsCommand(const String& payload);

        bool onReceive(byte* donnees, size_t length);

        void publishMqtt();
    private:
        Zone& _zone1;
        Zone& _zone2;
        Zone& _zone3;
        Chaudiere& _chaudiere;

        void setTemperatureExterieure(float temperature);
        void setTemperatureECS(float temperature);
        void setTemperatureCDC(float temperature);

        void setConsommationECS(int16_t consommation);
        void setConsommationChauffage(int16_t consommation);
        bool setConsommationAnnuelle(const byte* donnees, size_t length);
        bool recupererVersionFirmware();
        bool setVersionFirmware(const byte* response, size_t length);
        BoilerFirmware _boilerFirmware;

        void setPression(float pression);
        float getPression();
        bool handlePassiveReadResponse(uint16_t adresseMemoire, const byte* buff, size_t length);
        void setModeECSFromRaw(uint8_t rawModeECS);
        uint8_t encodeModeECSForSend();

        void envoiZones();

        bool _envoiZ1 = false;
        bool _envoiZ2 = false;
        bool _envoiZ3 = false;

        uint32_t _lastRecuperationTemperatures = 0;
        uint32_t _lastRecuperationModeECS = 0;
        uint32_t _lastRecuperationConsommation = 0;
        uint32_t _lastEnvoiZone = 0;
        RadioSchedule _temperatureSchedules[3];
        RadioSchedule _consommationSchedule;
        RadioSchedule _ecsSchedule;
        bool _initialReadPending = true;
        uint8_t _modeECSFrameBits = 0x01;
};
