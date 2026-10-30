#include "Satellite.h"
#include <math.h>
#include "../Buffer.h"

void Satellite::loadConfig() {
    getPreferences().begin((String("satCfgZ") + String(getNumeroZone())).c_str(), false);
    setIdAssociation(getPreferences().getUChar("idAssociation", 0xFF));
    getPreferences().end();
}

void Satellite::saveConfig() {   
    getPreferences().begin((String("satCfgZ") + String(getNumeroZone())).c_str(), false);
    getPreferences().putUChar("idAssociation", getIdAssociation());
    getPreferences().end();
}


void Satellite::begin(bool modeVirtuel) {
    _modeVirtuel = modeVirtuel;
    loadConfig();

    info("[SATELLITE Z%d] Initialisation du satellite [MODE %s].", _zone.getNumeroZone(), _modeVirtuel ? "VIRTUEL" : "PHYSIQUE");

    // Device commun
    MqttDevice* device = mqtt().getDevice("openFrisquetVisio");

    // SWITCH: Activation Écrasement consigne
    _mqttEntities.ecrasementConsigne.id = "ecrasementConsigneZ" + String(getNumeroZone());
    _mqttEntities.ecrasementConsigne.name = "Écrasement consigne Z" + String(getNumeroZone());
    _mqttEntities.ecrasementConsigne.component = "switch";
    _mqttEntities.ecrasementConsigne.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()), "ecrasementConsigne"}), 0, true);
    _mqttEntities.ecrasementConsigne.commandTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"ecrasementConsigne", "set"}), 0, true);
    _mqttEntities.ecrasementConsigne.set("icon", "mdi:tune-variant");
    mqtt().registerEntity(*device, _mqttEntities.ecrasementConsigne, true);
    mqtt().onCommand(_mqttEntities.ecrasementConsigne, [&](const String& payload){
        if(payload.equalsIgnoreCase("ON")) { 
            info("[SATELLITE %d] Activation de l'écrasement", getNumeroZone());
            setEcrasement(true);
        } else {
            info("[SATELLITE %d] Désactivation de l'écrasement", getNumeroZone());
            setEcrasement(false);
        }
        mqtt().publishState(_mqttEntities.ecrasementConsigne, payload);
    });

}

void Satellite::loop() {
    
    bool& firstLoop = _initialReadPending;
    const bool cdcGereeParConnect = getConfig().useConnect();
    if(firstLoop && !_modeVirtuel) {
        firstLoop = false;
        publishMqtt();
        _zone.publishMqtt();
    }

    uint32_t now = millis();

    if(! estAssocie() || !_modeVirtuel) {
        return;
    }

    if(firstLoop) {
        firstLoop = false;
        recupererInfosChaudiere();
        if (this->getId() == ID_ZONE_1 && !cdcGereeParConnect) {
            if (recupererTemperatureCDC()) {
                _lastRecuperationTemperatureCDC = millis();
            } else {
                error("[SATELLITE Z%d] Échec de la récupération initiale de la température CDC.", getNumeroZone());
            }
            _cdcSchedules[0].due(600, 180);
            _cdcSchedules[1].due(600, 480);
        }
        publishMqtt();
        if (this->getId() == ID_ZONE_1 && !cdcGereeParConnect) _chaudiere.publishMqtt();
        _zone.publishMqtt();
    }

    // Sans Connect : :03 et :08 de chaque tranche, hors créneaux des satellites.
    if (this->getId() == ID_ZONE_1 && !cdcGereeParConnect
        && (_cdcSchedules[0].due(600, 180) || _cdcSchedules[1].due(600, 480))) {
        info("[SATELLITE Z%d] Récupération de la température CDC...", getNumeroZone());
        if (recupererTemperatureCDC()) {
            _lastRecuperationTemperatureCDC = now;
            _chaudiere.publishMqtt();
        } else {
            _lastRecuperationTemperatureCDC = now <= 60000 ? 1 : now - 60000;
            error("[SATELLITE Z%d] Échec de la récupération de la température CDC.", getNumeroZone());
        }
        delay(100);
    }

    if ((_zone.getLastChange() > _zone.getLastEnvoi() && (_zone.getLastChange() + 15000) < now ) || now - _lastEnvoiConsigne >= 600000  ) { // dernier changement ou 10 minutes
        info("[SATELLITE Z%d] Envoi de la consigne.", getNumeroZone());
        _zone.refreshLastEnvoi();
        if(envoyerConsigne()) {
            incrementIdMessage(3);
            _lastEnvoiConsigne = now;
            _zone.publishMqtt();
            publishMqtt();
        } else {
            error("[SATELLITE Z%d] Echec de l'envoi.", getNumeroZone());
            _lastEnvoiConsigne = now <= 60000 ? 1 : now - 60000;
        }
    }
}

void Satellite::publishMqtt() {
    mqtt().publishState(*mqtt().getDevice("openFrisquetVisio")->getEntity("ecrasementConsigneZ" + String(getNumeroZone())), getEcrasement() ? "ON" : "OFF");
    if(this->getId() == ID_ZONE_1) { // Seulement sur Z1 (leader)
        _chaudiere.publishMqtt();
    }
}

bool Satellite::recupererInfosChaudiere() {
    if(! estAssocie() || getNumeroZone() == 0) {
        return false;
    }

    struct donneesZones_t {
        FrisquetRadio::RadioTrameHeader header;
        uint8_t longueur;
        temperature16 temperatureExterieure;
        byte i1[2];
        uint8_t date[6];  // format reçu "YY MM DD hh mm ss"
        byte etatChaudiere; // mode chaudière à valider 0X20 Hors gel 0x24 = Hors gel contact sec 0X28 = Fonctionnement
        uint8_t jourSemaine; // format wday
        temperature16 temperatureAmbianteZ1;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        temperature16 temperatureConsigneZ1;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        byte i2 = 0x00;
        uint8_t modeZ1 = 0x00;                       // 0x05 auto - 0x06 confort - 0x07 reduit - 0x08 hors gel
        byte i3[4] = {0x00, 0xC6, 0x00, 0xC6};
        temperature16 temperatureAmbianteZ2;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        temperature16 temperatureConsigneZ2;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        byte i4 = 0x00;
        uint8_t modeZ2 = 0x00;                       // 0x05 auto - 0x06 confort - 0x07 reduit - 0x08 hors gel
        byte i5[4] = {0x00, 0x00, 0x00, 0x00};
        temperature16 temperatureAmbianteZ3;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        temperature16 temperatureConsigneZ3;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        byte i6 = 0x00;
        uint8_t modeZ3 = 0x00;                       // 0x05 auto - 0x06 confort - 0x07 reduit - 0x08 hors gel
        byte i7[4] = {0x00, 0x00, 0x00, 0x00};
    } donneesZones;

    size_t length = 0;
    int16_t err;

    info("[Satellite %d] Récupération des informations chaudière.", _zone.getNumeroZone());
    
    uint8_t retry = 0;
    do {
        length = sizeof(donneesZones_t);
        err = this->radio().sendAsk(
            this->getId(), 
            ID_CHAUDIERE, 
            this->getIdAssociation(),
            this->incrementIdMessage(),
            0x01,
            0xA029,
            0x0015,
            (byte*)&donneesZones,
            length
        );
        
        if(err != RADIOLIB_ERR_NONE) {
            delay(30);
            continue;
        }

        setEtatChaudiere(donneesZones.etatChaudiere);
        Date date = donneesZones.date;
        setDate(date);
        
        return true;
    } while(retry++ < 1);

    return false;
}

bool Satellite::recupererTemperatureCDC() {
    if(! estAssocie() || getNumeroZone() == 0) {
        return false;
    }

    struct donneesTemperatureCDC_t {
        FrisquetRadio::RadioTrameHeader header;
        uint8_t longueurDonnees;
        temperature16 temperatureCDC;
    } donneesTemperatureCDC;

    size_t length = sizeof(donneesTemperatureCDC_t);
    int16_t err;

    uint8_t retry = 0;
    do {
        err = this->radio().sendAsk(
            this->getId(),
            ID_CHAUDIERE,
            this->getIdAssociation(),
            this->incrementIdMessage(),
            0x01,
            0xA05E,
            0x0001,
            (byte*)&donneesTemperatureCDC,
            length
        );

        if(err != RADIOLIB_ERR_NONE) {
            delay(30);
            continue;
        }

        float temperatureCDC = donneesTemperatureCDC.temperatureCDC.toFloat();
        _chaudiere.setTemperatureCDC(temperatureCDC);
        debug("[SATELLITE Z%d] Température CDC : %.1f°C", getNumeroZone(), temperatureCDC);
        return true;
    } while(retry++ < 1);

    return false;
}

bool Satellite::envoyerTemperatureAmbiante() {
    if(! estAssocie() || getNumeroZone() == 0) {
        return false;
    }

    if(isnan(_zone.getTemperatureAmbiante()) || _zone.getNumeroZone() == 0) {
        error("[SATELLITE Z%d] Impossible d'envoyer la température ambiante, configuration incomplète.", getNumeroZone());
        return false;
    }

    struct donneesSatellite_t {
        temperature16 temperatureAmbiante;
    } payload;

    struct donneesZones_t {
        FrisquetRadio::RadioTrameHeader header;
        uint8_t longueur;
        temperature16 temperatureExterieure;
        byte i1[2];
        uint8_t date[6];  // format reçu "YY MM DD hh mm ss"
        byte etatChaudiere; // mode chaudière à valider 0X20 Hors gel 0x24 = Hors gel contact sec 0X28 = Fonctionnement
        uint8_t jourSemaine; // format wday
        temperature16 temperatureAmbianteZ1;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        temperature16 temperatureConsigneZ1;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        byte i2 = 0x00;
        uint8_t modeZ1 = 0x00;                       // 0x05 auto - 0x06 confort - 0x07 reduit - 0x08 hors gel
        byte i3[4] = {0x00, 0xC6, 0x00, 0xC6};
        temperature16 temperatureAmbianteZ2;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        temperature16 temperatureConsigneZ2;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        byte i4 = 0x00;
        uint8_t modeZ2 = 0x00;                       // 0x05 auto - 0x06 confort - 0x07 reduit - 0x08 hors gel
        byte i5[4] = {0x00, 0x00, 0x00, 0x00};
        temperature16 temperatureAmbianteZ3;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        temperature16 temperatureConsigneZ3;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        byte i6 = 0x00;
        uint8_t modeZ3 = 0x00;                       // 0x05 auto - 0x06 confort - 0x07 reduit - 0x08 hors gel
        byte i7[4] = {0x00, 0x00, 0x00, 0x00};
    } donneesZones;

    
    payload.temperatureAmbiante = _zone.getTemperatureAmbiante();

    size_t length = 0;
    int16_t err;

    info("[Satellite %d] Envoi de la température ambiante %0.2f", _zone.getNumeroZone(), payload.temperatureAmbiante.toFloat());
    
    uint8_t retry = 0;
    do {
        length = sizeof(donneesZones_t);
        err = this->radio().sendInit(
            this->getId(), 
            ID_CHAUDIERE, 
            this->getIdAssociation(),
            this->incrementIdMessage(),
            0x01, 
            0xA029,
            0x0015,
            0xA02F + (0x0005 * (getNumeroZone() - 1)),
            0x0001,
            (byte*)&payload,
            sizeof(payload),
            (byte*)&donneesZones,
            length
        );
        
        if(err != RADIOLIB_ERR_NONE) {
            delay(30);
            continue;
        }

        setEtatChaudiere(donneesZones.etatChaudiere);
        Date date = donneesZones.date;
        setDate(date);
        
        return true;
    } while(retry++ < 1);

    return false;
}

bool Satellite::envoyerConsigne() {
    if(! estAssocie() || getNumeroZone() == 0) {
        return false;
    }

    if(isnan(_zone.getTemperatureAmbiante()) || isnan(_zone.getTemperatureConsigne()) || _zone.getMode() == Zone::MODE_ZONE::INCONNU || _zone.getNumeroZone() == 0) {
        error("[SATELLITE Z%d] Impossible d'envoyer la consigne, configuration incomplète.", getNumeroZone());
        return false;
    }

    struct donneesSatellite_t {
        temperature16 temperatureAmbiante;
        temperature16 temperatureConsigne;
        uint8_t i1 = 0x00; 
        uint8_t mode = 0x00; // 0x01 Confort, 0x02 Reduit, etc.
        fword options = (uint16_t)0x0000;
    } payload;

    struct donneesZones_t {
        FrisquetRadio::RadioTrameHeader header;
        uint8_t longueur;
        temperature16 temperatureExterieure;
        byte i1[2];
        uint8_t date[6];  // format reçu "YY MM DD hh mm ss"
        byte etatChaudiere; // mode chaudière à valider 0X20 Hors gel 0x24 = Hors gel contact sec 0X28 = Fonctionnement
        uint8_t jourSemaine; // format wday
        temperature16 temperatureAmbianteZ1;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        temperature16 temperatureConsigneZ1;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        byte i2 = 0x00;
        uint8_t modeZ1 = 0x00;                       // 0x05 auto - 0x06 confort - 0x07 reduit - 0x08 hors gel
        byte i3[4] = {0x00, 0xC6, 0x00, 0xC6};
        temperature16 temperatureAmbianteZ2;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        temperature16 temperatureConsigneZ2;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        byte i4 = 0x00;
        uint8_t modeZ2 = 0x00;                       // 0x05 auto - 0x06 confort - 0x07 reduit - 0x08 hors gel
        byte i5[4] = {0x00, 0x00, 0x00, 0x00};
        temperature16 temperatureAmbianteZ3;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        temperature16 temperatureConsigneZ3;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        byte i6 = 0x00;
        uint8_t modeZ3 = 0x00;                       // 0x05 auto - 0x06 confort - 0x07 reduit - 0x08 hors gel
        byte i7[4] = {0x00, 0x00, 0x00, 0x00};
    } donneesZones;

    
    payload.temperatureAmbiante = _zone.getTemperatureAmbiante();
    payload.temperatureConsigne = _zone.getTemperatureConsigne();
    switch(_zone.getMode()) {
        case Zone::MODE_ZONE::AUTO :
            if(_zone.confortActif()) {
                payload.mode =  _zone.derogationActive() ? MODE::CONFORT_DEROGATION : MODE::CONFORT_AUTO;
            } else {
                payload.mode =  _zone.derogationActive() ? MODE::REDUIT_DEROGATION : MODE::REDUIT_AUTO;
            }
            break;
        case Zone::MODE_ZONE::CONFORT :
            payload.mode = MODE::CONFORT_PERMANENT;
            break;
        case Zone::MODE_ZONE::REDUIT :
            payload.mode = MODE::REDUIT_PERMANENT;
            break;
        case Zone::MODE_ZONE::HORS_GEL :
            payload.mode = MODE::HORS_GEL;
            break;
        default:
            info("[SATELLITE Z%d] Mode inconnu, impossible d'envoyer la consigne.", getNumeroZone());
            return false;
    }

    if(getEtatChaudiere().arretChauffage) {
        payload.mode = MODE::HORS_GEL;
        payload.temperatureConsigne = isnan(_zone.getTemperatureHorsGel()) ? 8.0f : _zone.getTemperatureHorsGel();
    } 

    const MODE modeEnvoye = (MODE)payload.mode;
    payload.mode = encodeModeForSend(modeEnvoye);
    
    size_t length = 0;
    int16_t err;

    info("[Satellite %d] Envoi de la consigne %0.2f, amb %0.2f, mode %s %d.", _zone.getNumeroZone(), payload.temperatureConsigne.toFloat(), _zone.getTemperatureAmbiante(), _zone.getNomMode(), payload.mode);
    
    uint8_t retry = 0;
    do {
        length = sizeof(donneesZones_t);
        err = this->radio().sendInit(
            this->getId(), 
            ID_CHAUDIERE, 
            this->getIdAssociation(),
            this->incrementIdMessage(),
            0x01, 
            0xA029,
            0x0015,
            0xA02F + (0x0005 * (getNumeroZone() - 1)),
            0x0004,
            (byte*)&payload,
            sizeof(payload),
            (byte*)&donneesZones,
            length
        );
        
        if(err != RADIOLIB_ERR_NONE) {
            delay(30);
            continue;
        }

        setEtatChaudiere(donneesZones.etatChaudiere);
        if(modeEnvoye == MODE::HORS_GEL) _zone.confirmerMode(Zone::MODE_ZONE::HORS_GEL, 0x10);
        else _zone.confirmerMode();
        debug("[SATELLITE Z%d] Retour état chaudière : 0x%02X", getNumeroZone(), donneesZones.etatChaudiere);
        debug("[SATELLITE Z%d] Retour état chaudière : %s", getNumeroZone(), getEtatChaudiere().getLibelle().c_str());
        Date date = donneesZones.date;
        setDate(date);
        
        return true;
    } while(retry++ < 1);

    return false;
}

bool Satellite::onReceive(byte* donnees, size_t length) { 
    if(length < sizeof(FrisquetRadio::RadioTrameHeader)) {
        return false;
    }
    ReadBuffer readBuffer(donnees, length);
    FrisquetRadio::RadioTrameHeader* header = (FrisquetRadio::RadioTrameHeader*) readBuffer.getBytes(sizeof(FrisquetRadio::RadioTrameHeader));

    //info("[SATELLITE Z] Interception envoi consigne.", header->type);
    if(!_modeVirtuel) {

        // Les échanges de consigne de 23 octets ne contiennent pas le programme.
        // Attendre une écriture complète A154 : paramètres (6) + programme (42).
        if(header->type == FrisquetRadio::MessageType::INIT &&
           header->idExpediteur == getId() && !header->isAck() &&
           readBuffer.remainingLength() >= sizeof(FrisquetRadio::RadioTrameInit)) {
            ReadBuffer configurationBuffer = readBuffer;
            FrisquetRadio::RadioTrameInit requete;
            configurationBuffer.getBytes((byte*)&requete, sizeof(requete));
            if(requete.adresseMemoireEcriture.toUInt16() == 0xA154 &&
               requete.tailleMemoireEcriture.toUInt16() == 0x0018 &&
               requete.longueurDonneesEcriture == 48 && configurationBuffer.remainingLength() >= 48) {
                byte parametres[6];
                configurationBuffer.getBytes(parametres, sizeof(parametres));
                Zone::Programmation programmation;
                configurationBuffer.getBytes((byte*)&programmation, sizeof(programmation));
                _zone.setProgrammation(programmation, true);
                _zone.saveConfig();
                info("[SATELLITE Z%d] Programmation hebdomadaire reçue.", getNumeroZone());
                return true;
            }
        }

        if(length == 23 && header->type == FrisquetRadio::MessageType::INIT && header->idExpediteur == this->getId() && header->idMessage != getIdMessage()) { // Réception en écoute
            FrisquetRadio::RadioTrameInit* requete = (FrisquetRadio::RadioTrameInit*) readBuffer.getBytes(sizeof(FrisquetRadio::RadioTrameInit));
            if(requete->adresseMemoireEcriture.toUInt16() == 0xA02F && requete->tailleMemoireEcriture.toUInt16() == 0x0004) { // Envoi consigne

                struct donneesSatellite_t {
                    temperature16 temperatureAmbiante; 
                    temperature16 temperatureConsigne;
                    uint8_t i1 = 0x00; 
                    uint8_t mode = 0x00; // 0x01 Confort, 0x02 Reduit, etc.
                    uint8_t i2[2] = {0};
                }* donneesSatellite = (donneesSatellite_t*) readBuffer.getBytes(sizeof(*donneesSatellite));
                
                //info("[SATELLITE Z%d] Interception envoi consigne.", getNumeroZone());

                uint8_t retry = 0;
                int16_t err;
                
                size_t lengthRx = 0;
                byte buffZones[RADIOLIB_SX126X_MAX_PACKET_LENGTH];

                if(! getEcrasement()) {
                    delay(300);

                    err = radio().receiveExpected(
                        ID_CHAUDIERE,
                        this->getId(),
                        header->idAssociation, 
                        header->idMessage,
                        header->idReception|0x80,
                        header->type,
                        (byte*) buffZones,
                        lengthRx,
                        15, true
                    );
                    
                    if(err != RADIOLIB_ERR_NONE) {
                        return false;
                    }

                    readBuffer = ReadBuffer(buffZones, lengthRx);

                    struct donneesZones_t {
                        FrisquetRadio::RadioTrameHeader header;
                        uint8_t longueur;
                        temperature16 temperatureExterieure;
                        byte i1[2];
                        uint8_t date[6];  // format reçu "YY MM DD hh mm ss"
                        byte etatChaudiere; // mode chaudière à valider
                        uint8_t jourSemaine; // format wday
                        temperature16 temperatureAmbianteZ1;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
                        temperature16 temperatureConsigneZ1;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
                        byte i2 = 0x00;
                        uint8_t modeZ1 = 0x00;                       // 0x05 auto - 0x06 confort - 0x07 reduit - 0x08 hors gel
                        byte i3[4] = {0x00, 0xC6, 0x00, 0xC6};
                        temperature16 temperatureAmbianteZ2;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
                        temperature16 temperatureConsigneZ2;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
                        byte i4 = 0x00;
                        uint8_t modeZ2 = 0x00;                       // 0x05 auto - 0x06 confort - 0x07 reduit - 0x08 hors gel
                        byte i5[4] = {0x00, 0x00, 0x00, 0x00};
                        temperature16 temperatureAmbianteZ3;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
                        temperature16 temperatureConsigneZ3;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
                        byte i6 = 0x00;
                        uint8_t modeZ3 = 0x00;                       // 0x05 auto - 0x06 confort - 0x07 reduit - 0x08 hors gel
                        byte i7[4] = {0x00, 0x00, 0x00, 0x00};
                    }* donneesZones = (donneesZones_t*)readBuffer.getBytes(sizeof(donneesZones_t));

                    if(lengthRx < sizeof(donneesZones_t)) {
                        return false;
                    }

                    setEtatChaudiere(donneesZones->etatChaudiere);
                    Date date = donneesZones->date;
                    setDate(date);
                }

                setIdAssociation(header->idAssociation);
                setIdMessage(header->idMessage);
                _zone.setTemperatureAmbiante(donneesSatellite->temperatureAmbiante.toFloat());
                
                if(! getEcrasement()) {
                    const auto modeDemande = _zone.getMode();
                    const byte optionsDemandees = _zone.getModeOptions();
                    const bool commandeConnectEnAttente = _zone.getSource() == Zone::SOURCE::CONNECT
                        && _zone.getLastChange() > _zone.getLastEnvoi();
                    setModeFromRaw(donneesSatellite->mode);
                    if(_zone.getSource() == Zone::SOURCE::SATELLITE_PHYSIQUE) {
                        _zone.desactiverBoost();
                        _zone.confirmerMode();
                    }
                    if(commandeConnectEnAttente) {
                        _zone.setMode(modeDemande);
                        _zone.setModeOptions(optionsDemandees);
                    }
                    _zone.setTemperatureConsigne(donneesSatellite->temperatureConsigne.toFloat());
                    saveConfig();
                    _zone.saveConfig();
                    publishMqtt();
                    _zone.publishMqtt();
                    return true;
                }

                if(getMode() == MODE::INCONNU) {
                    setModeFromRaw(donneesSatellite->mode);
                }
                if(isnan(_zone.getTemperatureConsigne())) {
                    _zone.setTemperatureConsigne(donneesSatellite->temperatureConsigne.toFloat());
                }

                info("[SATELLITE Z%d] Écrasement de données.", getNumeroZone());

                incrementIdMessage(3);

                if(!this->envoyerConsigne()) {
                    error("[SATELLITE Z%d] Échec de l'écrasement.", getNumeroZone());
                } else {
                    info("[SATELLITE Z%d] Écrasement réussie.", getNumeroZone());
                }

                saveConfig();
                _zone.saveConfig();
                publishMqtt();
                _zone.publishMqtt();
                return true;
            }
        }
    } else {
        if(header->idAssociation != getIdAssociation() || header->isAck()) {
            return false;
        }

        //radio().sendAnswer(getId(), header->idExpediteur, header->idAssociation, header->idMessage, header->idReception, header->type, {}, 0);
        return true;
    }

    return false;
}

String Satellite::getNomMode() {
    switch(this->getMode()) {
        case MODE::CONFORT_AUTO:
            return "Auto - Confort";
        case MODE::REDUIT_AUTO:
            return "Auto - Réduit";
        case MODE::CONFORT_PERMANENT:
            return "Confort";
        case MODE::REDUIT_PERMANENT:
            return "Réduit";
        case MODE::CONFORT_DEROGATION:
            return "Confort - Dérog";
        case MODE::REDUIT_DEROGATION:
            return "Réduit - Dérog";
    }

    return "Inconnu";
}

Satellite::MODE Satellite::getMode() {
    switch(_zone.getMode()) {
        case Zone::MODE_ZONE::AUTO :
            if(_zone.confortActif()) {
                return _zone.derogationActive() ? MODE::CONFORT_DEROGATION : MODE::CONFORT_AUTO;
            } else {
                return _zone.derogationActive() ? MODE::REDUIT_DEROGATION : MODE::REDUIT_AUTO;
            }
        case Zone::MODE_ZONE::CONFORT :
            return MODE::CONFORT_PERMANENT;
        case Zone::MODE_ZONE::REDUIT :
            return MODE::REDUIT_PERMANENT;
        case Zone::MODE_ZONE::HORS_GEL :
            return MODE::HORS_GEL;
        default:
            return MODE::INCONNU;
    }
}

void Satellite::setMode(Satellite::MODE mode) { 
    switch(mode) {
        case MODE::CONFORT_AUTO :
            _zone.setMode(Zone::MODE_ZONE::AUTO, true);
            break;
        case MODE::CONFORT_DEROGATION :
            _zone.setMode(Zone::MODE_ZONE::AUTO, true, true);
            break;
        case MODE::REDUIT_AUTO :
            _zone.setMode(Zone::MODE_ZONE::AUTO, false);
            break;
        case MODE::REDUIT_DEROGATION :
            _zone.setMode(Zone::MODE_ZONE::AUTO, false, true);
            break;
        case MODE::CONFORT_PERMANENT :
            _zone.setMode(Zone::MODE_ZONE::CONFORT);
            break;
        case MODE::REDUIT_PERMANENT :
            _zone.setMode(Zone::MODE_ZONE::REDUIT);
            break;
        case MODE::HORS_GEL :
            _zone.setMode(Zone::MODE_ZONE::HORS_GEL);
            break;
    }
}

Satellite::MODE Satellite::decodeModeFromRaw(uint8_t rawMode) const {
    if (rawMode == MODE::HORS_GEL) {
        return MODE::HORS_GEL;
    }
    if (rawMode & ~MODE_BITS_CONNUS) {
        return MODE::INCONNU;
    }

    // Le bit 0x04 est un état interne du satellite : il ne distingue pas
    // Auto de permanent et n'est pas interprété.
    const bool confort = (rawMode & MODE_BIT_CONFORT) != 0;
    if (rawMode & MODE_BIT_DEROGATION) {
        return confort ? MODE::CONFORT_DEROGATION : MODE::REDUIT_DEROGATION;
    }
    if (rawMode & MODE_BIT_AUTO) {
        return confort ? MODE::CONFORT_AUTO : MODE::REDUIT_AUTO;
    }
    return confort ? MODE::CONFORT_PERMANENT : MODE::REDUIT_PERMANENT;
}

uint8_t Satellite::encodeModeForSend(Satellite::MODE mode) const {
    return (uint8_t)mode;
}

void Satellite::setModeFromRaw(uint8_t rawMode) {
    MODE decodedMode = decodeModeFromRaw(rawMode);

    if (decodedMode == MODE::INCONNU) {
        char hexMode[5];
        snprintf(hexMode, sizeof(hexMode), "%02X", rawMode);
        warning(String("[SATELLITE Z") + String(getNumeroZone()) + "] Mode satellite inconnu: 0x" + String(hexMode));
        return;
    }

    setMode(decodedMode);
}
