#pragma once

#include <Arduino.h>
#include <functional>
#include "../MQTT/MqttManager.h"
#include "../Config.h"

class Chaudiere {
public:
    struct ETAT_CHAUDIERE {
        ETAT_CHAUDIERE() : fonctionnement(false), arretChauffage(false) {}
        ETAT_CHAUDIERE(byte etat) {
            set(etat);
        }
        void set(byte etat) {
            anomalie = (etat & 0b00000001) != 0;
            fonctionnement = (etat & 0b00001000) != 0;
            arretChauffage = (etat & 0b00000100) != 0;
        }
        byte toByte() {
            byte etat = 0;
            if(anomalie) etat |= 0b00000001;
            if(fonctionnement) etat |= 0b00001000;
            if(arretChauffage) etat |= 0b00000100;
            return etat;
        }

        bool anomalie = false;
        bool fonctionnement = false;
        bool arretChauffage = false;

        String getLibelle() {
            if(arretChauffage) {
                return "Arrêt chauffage";
            } else if(!fonctionnement) {
                return "Veille";
            } else {
                return "Fonctionnement";
            }
        }

        boolean anomalieDetecte() {
            return anomalie;
        }
    };

    enum MODE_ECS : uint8_t {
        INCONNU = 0XFF,
        STOP = 0x28,
        MAX = 0x00,
        ECO = 0x08,
        ECO_HORAIRES = 0x10,
        ECOPLUS = 0x18,
        ECOPLUS_HORAIRES = 0x20
    };

    enum MODE_FONCTIONNEMENT : uint8_t {
        FONCTIONNEMENT_INCONNU = 0xFF,
        CHAUFFAGE_RADIATEURS = 0x05,
        CHAUFFAGE_ECS = 0x07
    };

    Chaudiere(MqttManager& mqtt, Config& cfg) : _mqtt(mqtt), _cfg(cfg) {}

    void begin(std::function<void(const String&)> modeEcsCommandCb = {});
    void publishMqtt();
    void publishModeEcs();

    void setTemperatureExterieure(float temperature);
    void setTemperatureECS(float temperature);
    void setTemperatureCDC(float temperature);
    void setTemperatureFumees(float temperature) { _temperatureFumees = temperature; }
    void setPuissanceInstantaneeECS(float puissance) { _puissanceInstantaneeECS = puissance; }
    void setPuissanceInstantaneeChauffage(float puissance) { _puissanceInstantaneeChauffage = puissance; }

    float getTemperatureExterieure() const { return _temperatureExterieure; }
    float getTemperatureECS() const { return _temperatureECS; }
    float getTemperatureCDC() const { return _temperatureCDC; }
    float getTemperatureFumees() const { return _temperatureFumees; }
    float getPuissanceInstantaneeECS() const { return _puissanceInstantaneeECS; }
    float getPuissanceInstantaneeChauffage() const { return _puissanceInstantaneeChauffage; }

    void setConsommationVeille(int16_t chauffage, int16_t ecs, uint32_t jour);
    // Nombre de jours depuis le 01/01/1970 dans le calendrier de la chaudière, ou 0.
    uint32_t getJourConsommation() const { return _jourConsommation; }
    int16_t getConsommationChauffage() const { return _consommationGazChauffage; }
    int16_t getConsommationECS() const { return _consommationGazECS; }

    // Cumul de l'année en cours (somme des compteurs mensuels 0xA063).
    void setConsommationAnnuelle(int32_t chauffage, int32_t ecs);
    int32_t getConsommationAnnuelleChauffage() const { return _consommationAnnuelleChauffage; }
    int32_t getConsommationAnnuelleECS() const { return _consommationAnnuelleECS; }

    bool setModeECS(MODE_ECS modeECS);
    bool setModeECS(const String& modeECS);
    MODE_ECS getModeECS() const { return _modeECS; }
    String getNomModeECS() const;

    void setModeFonctionnement(uint16_t modeFonctionnement);
    MODE_FONCTIONNEMENT getModeFonctionnement() const { return _modeFonctionnement; }
    uint16_t getModeFonctionnementRaw() const { return _modeFonctionnementRaw; }
    String getNomModeFonctionnement() const;
    String getNomEtatChaudiere() const;
    bool etatChaudiereConnu() const { return _etatChaudiereConnu; }

    void setPression(float pression);
    float getPression() const { return _pression; }

    void setEtatChaudiere(byte etatChaudiere) { _etatChaudiere.set(etatChaudiere); _etatChaudiereConnu = true; }
    ETAT_CHAUDIERE getEtatChaudiere() const { return _etatChaudiere; }

private:
    MqttManager& _mqtt;
    Config& _cfg;

    float _temperatureECS = NAN;
    float _temperatureCDC = NAN;
    float _temperatureFumees = NAN;
    float _temperatureExterieure = NAN;
    float _puissanceInstantaneeECS = NAN;
    float _puissanceInstantaneeChauffage = NAN;
    float _pression = NAN;

    uint32_t _jourConsommation = 0;
    int16_t _consommationGazECS = -1;
    int16_t _consommationGazChauffage = -1;

    MODE_ECS _modeECS = MODE_ECS::INCONNU;
    MODE_FONCTIONNEMENT _modeFonctionnement = MODE_FONCTIONNEMENT::FONCTIONNEMENT_INCONNU;
    uint16_t _modeFonctionnementRaw = 0xFFFF;

    struct PublicationVeille {
        MqttTopic sujetNumerique;
        int16_t derniereValeurBrute = -1;
        int16_t valeurDatee = -1;
        uint32_t jour = 0;
        bool enAttente = false;

        void recevoir(int16_t valeur, uint32_t nouveauJour) {
            if (valeur < 0 || !nouveauJour || nouveauJour < jour) return;
            if (valeur == valeurDatee && nouveauJour == jour) return;
            valeurDatee = valeur;
            jour = nouveauJour;
            enAttente = true;
        }
    };
    PublicationVeille _publicationChauffage, _publicationEcs;
    void publierConsommationVeille(const MqttEntity& entite, int16_t valeur, PublicationVeille& publication);

    int32_t _consommationAnnuelleChauffage = -1;
    int32_t _consommationAnnuelleECS = -1;
    int32_t _lastPubConsommationAnnuelleChauffage = -1;
    int32_t _lastPubConsommationAnnuelleECS = -1;
    ETAT_CHAUDIERE _etatChaudiere;
    bool _etatChaudiereConnu = false;

    struct {
        MqttEntity etatChaudiere;
        MqttEntity anomalieChaudiere;
        MqttEntity modeFonctionnement;
        MqttEntity modeECS;
        MqttEntity tempECS;
        MqttEntity tempCDC;
        MqttEntity tempFumees;
        MqttEntity tempExterieure;
        MqttEntity puissanceInstantaneeECS;
        MqttEntity puissanceInstantaneeChauffage;
        MqttEntity consommationChauffage;
        MqttEntity consommationECS;
        MqttEntity consommationAnnuelleChauffage;
        MqttEntity consommationAnnuelleECS;
        MqttEntity pression;
    } _mqttEntities;
};
