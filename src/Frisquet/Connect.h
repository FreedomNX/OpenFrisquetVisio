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
        void pollReads();

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

        struct ConsommationsMensuelles {
            uint16_t chauffage[12] = {};
            uint16_t ecs[12] = {};
            // Heure locale synchronisée avec la chaudière ; 0 si inconnue.
            uint32_t dateReception = 0;
            bool disponibles = false;
        };
        const ConsommationsMensuelles& getConsommationsMensuelles() const { return _consommationsMensuelles; }
        const ConsommationsMensuelles& getConsommationsAnneePrecedente() const { return _consommationsAnneePrecedente; }

        void envoiZones();
        void observerSatellite(const byte* donnees, size_t taille, uint32_t reception);
        bool envoyerModeECS();

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
        bool sendPendingModeEcs();

        bool onReceive(byte* donnees, size_t length);

        void publishMqtt();
    private:
        Zone& _zone1;
        Zone& _zone2;
        Zone& _zone3;
        Chaudiere& _chaudiere;
        ConsommationsMensuelles _consommationsMensuelles;
        ConsommationsMensuelles _consommationsAnneePrecedente;

        void setTemperatureExterieure(float temperature);
        void setTemperatureECS(float temperature);
        void setTemperatureCDC(float temperature);

        void setConsommationECS(int16_t consommation);
        void setConsommationChauffage(int16_t consommation);
        bool setConsommationAnnuelle(const byte* donnees, size_t length, bool anneePrecedente = false);
        bool applyInformation(const byte* data, size_t length, const uint32_t* zoneChanges = nullptr);
        bool applyBoilerStatus(const byte* data, size_t length);
        bool applyConsumption(const byte* data, size_t length);
        bool applyEcs(const byte* data, size_t length);

        bool setVersionFirmware(const byte* response, size_t length);
        BoilerFirmware _boilerFirmware;

        void setPression(float pression);
        float getPression();
        bool handlePassiveReadResponse(uint16_t adresseMemoire, const byte* buff, size_t length);
        void setModeECSFromRaw(uint8_t rawModeECS);
        uint8_t encodeModeECSForSend();

        bool envoyerZone(Zone& zone, uint8_t rang);
        void verifierContexteEcoute();
        uint32_t _generationEcoute = 0;
        bool recevoirConfirmation(const FrisquetRadio::RadioTrameHeader& entete, const byte* donnees, size_t taille);
        enum class EtatEnvoi : uint8_t { Initial, Incomplet, ProgrammeInconnu, ErreurRadio, Emis };
        struct Envoi {
            EtatEnvoi etat = EtatEnvoi::Initial;
            uint32_t revision = 0, generationRadio = 0;
            byte messages[32] = {};
            byte tentative = 0, prochainRang = 0, rangsEmis = 0;
            byte messagesFenetre[3] = {};
        } _envois[3];
        uint8_t _prochaineZone = 0;
        bool _aDejaEmis = false;
        uint32_t _lastRecuperationTemperatures = 0;
        uint32_t _lastRecuperationModeECS = 0;
        uint32_t _lastRecuperationConsommation = 0;
        uint32_t _lastEnvoiZone = 0;
        RadioSchedule _temperatureSchedules[3];
        RadioSchedule _consommationSchedule;
        RadioSchedule _ecsSchedule;
        bool _initialReadPending = true;
        enum class ReadKind : uint8_t { Firmware, Information, Status, Consumption, Annual, PreviousAnnual, Ecs, None };
        void startReads(ReadKind first, ReadKind last);
        void completeRead(bool success, int16_t result);
        // Une collecte parcourt un intervalle contigu de lectures ; None signifie repos.
        ReadKind _readKind = ReadKind::None, _lastRead = ReadKind::None;
        uint8_t _readAttempts = 0, _readAssociation = 0, _readBoiler = 0;
        uint32_t _readDeadline = 0, _readGeneration = 0;
        uint32_t _readZoneChanges[3] = {};
        uint8_t _modeECSFrameBits = 0x01;
        bool _modeEcsCommandPending = false;
        MODE_ECS _requestedModeEcs = MODE_ECS::INCONNU;
};
