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
        void confirmerMode() { confirmerMode(_reglages.mode, _reglages.options); }
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

        struct Reglages {
            float confort = NAN, reduit = NAN, horsGel = NAN, boost = NAN, consigne = NAN;
            MODE_ZONE mode = INCONNU;
            byte options = 0x05;
            Programmation programme;
            bool programmeConnu = false;
        };
        enum class Objet : uint8_t { Aucun, Consigne, Confort, Reduit, HorsGel, Boost, Mode, Programme };
        enum class Livraison : uint8_t { Repos, Attente, Confirmee, Echec };
        static constexpr uint8_t MAX_FENETRES = 5;
        void observerStatut(uint32_t reception, byte association);
        void oublierStatut();
        byte associationSatellite() const { return _associationSatellite; }
        uint8_t tentative() const { return _tentative; }
        int rangEmission(uint32_t maintenant) const;
        bool enAttente() const { return _livraison == Livraison::Attente; }
        bool nonConfirmee() const { return _livraison == Livraison::Echec; }
        Reglages demande() const;
        uint32_t revisionCommande() const { return _revisionCommande; }
        bool reessayer();
        void actualiserLivraison();
        void confirmerCommande();
        struct BlocZone {
            // Profils codés entre 5 et 30 °C par pas de 0,5 °C, Boost inclus dans Confort.
            temperature8 confort, reduit, horsGel;
            // Options A154 : le Connect réel a été observé à 0x25 en Auto (octet A02F).
            // La sélection Auto du firmware conserve 0x04 et le bit de régime en attendant validation sur chaudière.
            byte mode = INCONNU, options = 0, reserve = 0;
            Programmation programme;
        };
        static_assert(sizeof(BlocZone) == 48, "Taille du bloc A154");
        BlocZone blocDemande() const;
        bool reconnaitreReglages(const byte* bloc, size_t taille);
        String suiviJson() const;
        Programmation& getProgrammation() { return _reglages.programme; }
        const Programmation& programmeEdite() const {
            return programmationEnvoiEnAttente() || programmationNonConfirmee() ? _demande.reglages.programme : _reglages.programme;
        }
        void setProgrammation(const Programmation& programme, bool recue = false) {
            _reglages.programme = programme;
            if(recue) _reglages.programmeConnu = true;

        }
        void modifierProgrammation(const Programmation& programme) {
            modifier(Objet::Programme, [&]() { setProgrammation(programme, true); return true; });
        }
        bool programmationEnvoiEnAttente() const { return enAttente() && (_demande.champs & ProgrammeModifie); }
        bool programmationNonConfirmee() const { return nonConfirmee() && (_demande.champs & ProgrammeModifie); }
        bool programmeEditeConnu() const { return programmationRecue() || programmationEnvoiEnAttente() || programmationNonConfirmee(); }
        bool programmationRecue() const { return _reglages.programmeConnu; }

    private:
        enum Champ : uint8_t {
            ConfortModifie = 1, ReduitModifie = 2, HorsGelModifie = 4,
            BoostModifie = 8, ModeModifie = 16, ProgrammeModifie = 32
        };
        struct Demande {
            Reglages reglages;
            Objet objet = Objet::Aucun;
            uint8_t champs = 0;
        } _demande;
        Reglages _reglages;
        Livraison _livraison = Livraison::Repos;
        uint8_t _tentative = 0;
        enum class Fenetre : uint8_t { SansStatut, CycleEnCours, CycleSuivant };
        Fenetre _fenetre = Fenetre::SansStatut;
        bool _statutConnu = false;
        byte _associationSatellite = 0;
        uint32_t _statut = 0, _ouverture = 0, _debutPeriode = 0;
        uint32_t _revisionCommande = 0;
        bool planifierFenetre(uint32_t maintenant);
        void demarrerCommande();
        uint8_t champModifie(Objet objet);
        float valeurObjet(const Reglages& reglages) const;
        template<class F> bool modifier(Objet objet, F appliquer) {
            // Les éditeurs de mode et de température travaillent sur les mêmes
            // réglages. Une zone distante ne les publie qu'après confirmation.
            const Reglages confirmes = _reglages;
            const bool distante = _source == CONNECT;
            const bool garderSemaine = distante && programmationNonConfirmee();
            if(distante && enAttente()) _reglages = demande();
            else if(garderSemaine) setProgrammation(_demande.reglages.programme, true);
            if(!appliquer()) { _reglages = confirmes; return false; }
            if(distante) {
                // Chaque bloc A154 contient la semaine entière, même pour une consigne.
                if(!enAttente()) _demande.champs = garderSemaine ? ProgrammeModifie : 0;
                _demande.champs |= champModifie(objet);
                _demande.reglages = _reglages;
                _demande.objet = objet;
                _reglages = confirmes;
                demarrerCommande();
            } else {
                refreshLastChange();
                if(objet == Objet::Programme) _derogationFin = 0;
            }
            saveConfig();
            if(_mqttPret) publishMqtt();
            return true;
        }
        bool definirPreset(const String& preset);
        bool definirConsigne(float temperature);
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
        float _temperatureDepart = NAN;
        float _temperatureConsigneDepart = NAN;
        float _temperatureAmbiante = NAN;

        Preferences _preferences;

        SOURCE _source = SOURCE::SATELLITE_PHYSIQUE;
        uint32_t _lastChange = 0;
        uint32_t _lastEnvoi = 0;
};
