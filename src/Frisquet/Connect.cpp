#include "Connect.h"
#include "../Buffer.h"
#include <cstring>
#include <math.h>

void Connect::loadConfig() {
    bool checkMigration = false;

    getPreferences().begin("connectCfg", false);

    if(! getPreferences().isKey("idAssociation")) {
        checkMigration = true;
    }

    setIdAssociation(getPreferences().getUChar("idAssociation", 0xFF));

    getPreferences().end();
    delay(100);
    
    // Migration
    if(checkMigration && getPreferences().begin("net-conf", false)) {
        if(getPreferences().isKey("con_id")) {
            setIdAssociation(getPreferences().getUChar("con_id", 0xFF));
        }
        getPreferences().end();
        checkMigration = false;
    }
}

void Connect::saveConfig() {   
    getPreferences().begin("connectCfg", false);
    getPreferences().putUChar("idAssociation", getIdAssociation());
    getPreferences().end();
}

bool Connect::envoyerZone(Zone& zone, uint8_t rang) {
    const auto& d = zone.demande();
    const bool satellitePhysique = (zone.getIdZone() == ID_ZONE_1 && getConfig().useSatelliteZ1())
        || (zone.getIdZone() == ID_ZONE_2 && getConfig().useSatelliteZ2())
        || (zone.getIdZone() == ID_ZONE_3 && getConfig().useSatelliteZ3());
    auto& envoi = _envois[zone.getNumeroZone() - 1];
    if(envoi.revision != zone.revisionCommande() || envoi.generationRadio != radio().operationGeneration()) {
        envoi = Envoi{};
        envoi.revision = zone.revisionCommande();
        envoi.generationRadio = radio().operationGeneration();
    }
    if(envoi.tentative != zone.tentative()) {
        envoi.tentative = zone.tentative();
        envoi.prochainRang = 0;
        envoi.rangsEmis = 0;
    }
    if(rang < envoi.prochainRang) return false;
    if(!isfinite(d.confort) || !isfinite(d.reduit) || !isfinite(d.horsGel) || d.mode == Zone::INCONNU
        || ((d.options & 0x40) && !isfinite(d.boost))) {
        if(envoi.etat != EtatEnvoi::Incomplet)
            error("[CONNECT] Impossible d'envoyer la zone %d, configuration incomplète.", zone.getNumeroZone());
        envoi.etat = EtatEnvoi::Incomplet;
        return false;
    }
    if(satellitePhysique && !d.programmeConnu) {
        if(envoi.etat != EtatEnvoi::ProgrammeInconnu)
            info("[CONNECT] Zone %d en attente d'une programmation connue (radio ou mémoire).", zone.getNumeroZone());
        envoi.etat = EtatEnvoi::ProgrammeInconnu;
        return false;
    }

    struct {
        FrisquetRadio::RadioTrameHeader entete;
        FrisquetRadio::RadioTrameInit ecriture;
        Zone::BlocZone contenu;
    } trame{};
    static_assert(sizeof(trame) == 63, "Longueur de la commande de zone");
    envoi.prochainRang = rang + 1;
    const byte message = incrementIdMessage();
    trame.entete.idDestinataire = getConfig().boilerId();
    trame.entete.idExpediteur = getId();
    trame.entete.idAssociation = getIdAssociation();
    trame.entete.idMessage = message;
    trame.entete.idReception = zone.getIdZone();
    trame.entete.type = FrisquetRadio::INIT;
    trame.ecriture = {0xA154, 24, 0xA154, 24, sizeof(trame.contenu)};
    trame.contenu = zone.blocDemande();
    // Le gestionnaire traite l'accusé comme tout autre paquet reçu. La radio
    // reste disponible pour les autres échanges pendant toute l'attente.
    FrisquetRadio::interruptReceive = true;
    const auto resultat = radio().transmit(reinterpret_cast<byte*>(&trame), sizeof(trame));
    FrisquetRadio::receivedFlag = false;
    const auto reception = radio().startReceive();
    FrisquetRadio::interruptReceive = false;
    if(reception != RADIOLIB_ERR_NONE) error("[CONNECT] Réarmement de la réception impossible (code %d).", reception);
    if(resultat != RADIOLIB_ERR_NONE) {
        if(envoi.etat != EtatEnvoi::ErreurRadio)
            error("[CONNECT] Échec de l'émission de la zone %d (code %d).", zone.getNumeroZone(), resultat);
        envoi.etat = EtatEnvoi::ErreurRadio;
        return true; // Espacer aussi les erreurs d'émission.
    }
    if(envoi.etat != EtatEnvoi::Emis)
        info("[CONNECT] Envoi de la zone %d effectué, en attente de confirmation.", zone.getNumeroZone());
    envoi.etat = EtatEnvoi::Emis;
    logRadio(false, reinterpret_cast<const byte*>(&trame), sizeof(trame));
    envoi.messagesFenetre[rang] = message;
    envoi.rangsEmis |= 1U << rang;
    envoi.messages[message / 8] |= 1U << (message % 8);
    return true;
}

bool Connect::recevoirConfirmation(const FrisquetRadio::RadioTrameHeader& h, const byte* donnees, size_t taille) {
    if(h.type != FrisquetRadio::INIT || h.idExpediteur != getConfig().boilerId() || h.idDestinataire != getId()
        || h.idAssociation != getIdAssociation() || taille != 49 || donnees[0] != 48) return false;
    const byte id = h.idReception & 0x7f;
    if(id != ID_ZONE_1 && id != ID_ZONE_2 && id != ID_ZONE_3) return false;
    auto& zone = getZone(id);
    auto& envoi = _envois[zone.getNumeroZone() - 1];
    if((!zone.enAttente() && !zone.nonConfirmee()) || envoi.revision != zone.revisionCommande()
        || envoi.generationRadio != radio().operationGeneration()
        || !(envoi.messages[h.idMessage / 8] & (1U << (h.idMessage % 8)))
        || !zone.reconnaitreReglages(donnees + 1, taille - 1)) return false;
    info("[CONNECT] Zone %d : commande confirmée par la chaudière.", zone.getNumeroZone());
    return true;
}

bool Connect::setVersionFirmware(const byte* response, size_t length) {
    const size_t prefixLength = sizeof(FrisquetRadio::RadioTrameHeader) + 1;
    BoilerFirmware version;
    if (!response || length != prefixLength + 4 || response[prefixLength - 1] != 4
        || !version.decode(response + prefixLength, 4)) return false;

    // Une lecture quotidienne invalide ne remplace pas une version déjà connue.
    _boilerFirmware = version;
    info("[CONNECT] Firmware chaudière %02u.%02u.%04u : puissances %s.",
         static_cast<unsigned>(_boilerFirmware.major), static_cast<unsigned>(_boilerFirmware.minor),
         static_cast<unsigned>(_boilerFirmware.build),
         _boilerFirmware.directPower() ? "en kW directs" : "divisées par 10");
    return true;
}

bool Connect::applyInformation(const byte* data, size_t length, const uint32_t* zoneChanges) {
    struct {
        FrisquetRadio::RadioTrameHeader header;
        uint8_t longueurDonnees;
        temperature16 temperatureAmbianteZ1;
        temperature16 temperatureAmbianteZ2;
        temperature16 temperatureAmbianteZ3;
        temperature16 temperatureConsigneDepartZ1;
        temperature16 temperatureConsigneDepartZ2;
        temperature16 temperatureConsigneDepartZ3;
        temperature16 temperatureConsigneZ1;
        temperature16 temperatureConsigneZ2;
        temperature16 temperatureConsigneZ3;
        temperature16 temperatureExterieure;
        temperature16 temperatureECS;
        temperature16 temperatureCDC;
        temperature16 temperatureDepartZ1;
        temperature16 temperatureDepartZ2;
        temperature16 temperatureDepartZ3;
        temperature16 temperatureSondeSecuriteCDC;
        temperature16 temperatureFumees;
        temperature16 puissanceInstantaneeECS;
        temperature16 puissanceInstantaneeChauffage;
        temperature16 temperatureInconnue5;
        pression16 pression;
        fword modeFonctionnement;
        byte inconnu;
        temperature16 inconnu2;
    } buff;

    if (!data || length < sizeof(buff)) return false;
    memcpy(&buff, data, sizeof(buff));
    const size_t prefix = sizeof(buff.header) + 1;
    if (buff.longueurDonnees < sizeof(buff) - prefix || buff.longueurDonnees > length - prefix) return false;

    if(getZone1().getSource() == Zone::SOURCE::CONNECT) {
        getZone1().setTemperatureAmbiante(buff.temperatureAmbianteZ1.toFloat());
        if (!getZone1().enAttente() && (!zoneChanges || getZone1().getLastChange() == zoneChanges[0]))
            getZone1().setTemperatureConsigne(buff.temperatureConsigneZ1.toFloat());
    }
    getZone1().setTemperatureDepart(buff.temperatureDepartZ1.toFloat());
    getZone1().setTemperatureConsigneDepart(buff.temperatureConsigneDepartZ1.toFloat());

    if(getZone2().getSource() == Zone::SOURCE::CONNECT) {
        getZone2().setTemperatureAmbiante(buff.temperatureAmbianteZ2.toFloat());
        if (!getZone2().enAttente() && (!zoneChanges || getZone2().getLastChange() == zoneChanges[1]))
            getZone2().setTemperatureConsigne(buff.temperatureConsigneZ2.toFloat());
    }
    getZone2().setTemperatureDepart(buff.temperatureDepartZ2.toFloat());
    getZone2().setTemperatureConsigneDepart(buff.temperatureConsigneDepartZ2.toFloat());

    if(getZone3().getSource() == Zone::SOURCE::CONNECT) {
        getZone3().setTemperatureAmbiante(buff.temperatureAmbianteZ3.toFloat());
        if (!getZone3().enAttente() && (!zoneChanges || getZone3().getLastChange() == zoneChanges[2]))
            getZone3().setTemperatureConsigne(buff.temperatureConsigneZ3.toFloat());
    }
    getZone3().setTemperatureDepart(buff.temperatureDepartZ3.toFloat());
    getZone3().setTemperatureConsigneDepart(buff.temperatureConsigneDepartZ3.toFloat());


    if(buff.temperatureExterieure.toFloat() != 129) {
        setTemperatureExterieure(buff.temperatureExterieure.toFloat());
    }
    setTemperatureECS(buff.temperatureECS.toFloat());
    setTemperatureCDC(buff.temperatureCDC.toFloat());
    _chaudiere.setTemperatureFumees(buff.temperatureFumees.toFloat()/2);
    _chaudiere.setPuissanceInstantaneeECS(_boilerFirmware.power(
        uint16_t(buff.puissanceInstantaneeECS.bytes[0]) << 8 | buff.puissanceInstantaneeECS.bytes[1]));
    _chaudiere.setPuissanceInstantaneeChauffage(_boilerFirmware.power(
        uint16_t(buff.puissanceInstantaneeChauffage.bytes[0]) << 8 | buff.puissanceInstantaneeChauffage.bytes[1]));
    _chaudiere.setModeFonctionnement(buff.modeFonctionnement.toUInt16());

    setPression(buff.pression.toFloat());
    return true;
}

bool Connect::applyConsumption(const byte* data, size_t length) {
    struct {
        FrisquetRadio::RadioTrameHeader header;
        uint8_t longueurDonnees;
        byte i1[18] = {0};
        fword consommationECS;
        fword consommationChauffage;
        byte i2[34] = {0};
    } buff;


    if (!data || length < sizeof(buff)) return false;
    memcpy(&buff, data, sizeof(buff));
    if (buff.longueurDonnees != 56 || length < 7U + buff.longueurDonnees) return false;
    setConsommationChauffage(buff.consommationChauffage.toInt16());
    setConsommationECS(buff.consommationECS.toInt16());
    return true;
}

// Deux blocs contigus de 24 mots : année courante en 0xA063, précédente
// en 0xA07B. Pour chaque année : janvier à décembre, chauffage puis ECS en kWh.
struct ConsommationMensuelle {
    FrisquetRadio::RadioTrameHeader header;
    uint8_t longueurDonnees;
    fword mois[24];
};
static_assert(sizeof(ConsommationMensuelle) == 55, "Une année occupe 55 octets");
static_assert(sizeof(ConsommationMensuelle) <= Radio::MAX_RX_PACKET_LENGTH, "Respecter la capacité RX");

bool Connect::setConsommationAnnuelle(const byte* donnees, size_t length, bool anneePrecedente) {
    ConsommationMensuelle resp;
    if (!donnees || length < sizeof(resp)) return false;
    memcpy(&resp, donnees, sizeof(resp));
    if (resp.longueurDonnees != sizeof(resp.mois)) return false;
    auto& destination = anneePrecedente ? _consommationsAnneePrecedente : _consommationsMensuelles;
    int32_t chauffage = 0, ecs = 0;
    for (uint8_t mois = 0; mois < 12; ++mois) {
        destination.chauffage[mois] = resp.mois[mois * 2].toUInt16();
        destination.ecs[mois] = resp.mois[mois * 2 + 1].toUInt16();
        chauffage += destination.chauffage[mois];
        ecs += destination.ecs[mois];
    }
    destination.dateReception = timeStatus() == timeNotSet ? 0 : ::now();
    destination.disponibles = true;
    // L'archive ne remplace jamais les cumuls de l'année courante dans MQTT.
    if (!anneePrecedente) _chaudiere.setConsommationAnnuelle(chauffage, ecs);
    return true;
}

void Connect::setTemperatureExterieure(float temperature) {
    _chaudiere.setTemperatureExterieure(temperature);
}
void Connect::setTemperatureECS(float temperature) {
    _chaudiere.setTemperatureECS(temperature);
}
void Connect::setTemperatureCDC(float temperature) {
    _chaudiere.setTemperatureCDC(temperature);
}

float Connect::getTemperatureExterieure() {
    return _chaudiere.getTemperatureExterieure();
}
float Connect::getTemperatureECS() {
    return _chaudiere.getTemperatureECS();
}
float Connect::getTemperatureCDC() {
    return _chaudiere.getTemperatureCDC();
}

void Connect::setConsommationECS(int16_t consommation) {
    _chaudiere.setConsommationECS(consommation);
}
void Connect::setConsommationChauffage(int16_t consommation) {
    _chaudiere.setConsommationChauffage(consommation);
}
int16_t Connect::getConsommationChauffage() {
    return _chaudiere.getConsommationChauffage();
}
int16_t Connect::getConsommationECS() {
    return _chaudiere.getConsommationECS();
}

bool Connect::applyEcs(const byte* data, size_t length) {
    /*
    7E 80 AA 16 05 10 A0 F0 00 0D 1A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 11 // Eco horloge
    7E 80 AA 1A 05 10 A0 F0 00 0D 1A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 09 // Eco
    80 7E AA 03 05 10 A0 F0 00 0D 1A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 09 // Eco
    7E 80 AA 1D 05 10 A0 F0 00 0D 1A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 // max
    7E 80 AA 22 05 10 A0 F0 00 0D 1A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 19 // eco+
    */
    // Demande récupération courte : 80 7E AA 03 01 03 A0 FC 00 01
    // Réponse A0FC : en-tête, longueur 2, octet réservé, mode.
    if (!data || length < 9 || data[6] != 2) return false;
    setModeECSFromRaw(data[8]);
    return true;
}

Connect::MODE_ECS Connect::getModeECS() {
    return _chaudiere.getModeECS();
}
String Connect::getNomModeECS() {
    return _chaudiere.getNomModeECS();
}
bool Connect::setModeECS(MODE_ECS modeECS) {
    return _chaudiere.setModeECS(modeECS);
}
bool Connect::setModeECS(const String& modeECS) {
    return _chaudiere.setModeECS(modeECS);
}

void Connect::setModeECSFromRaw(uint8_t rawModeECS) {
    _modeECSFrameBits = rawModeECS & 0x81;
    uint8_t masked = rawModeECS & 0x7E;
    info("[CONNECT] modeECS reçu brut=0x%02X, masqué=0x%02X, bits trame=0x%02X", rawModeECS, masked, _modeECSFrameBits);
    setModeECS((MODE_ECS)masked);
}

uint8_t Connect::encodeModeECSForSend() {
    return ((uint8_t)getModeECS() & 0x7E) | _modeECSFrameBits;
}

void Connect::handleModeEcsCommand(const String& payload) {
    info("[CONNECT] Changement du mode  ECS : %s.", payload.c_str());
    if (getConfig().useConnectPassive()) {
        info("[CONNECT] Mode passif actif, envoi du mode ECS ignoré.");
        return;
    }
    if (!setModeECS(payload)) {
        error("[CONNECT] Mode ECS invalide : %s", payload.c_str());
        return;
    }
    if (radio().busy()) {
        // Une seule consigne différée : la plus récente remplace la précédente.
        // Aucune tentative ni delay() depuis le callback MQTT pendant RX.
        _requestedModeEcs = getModeECS();
        _modeEcsCommandPending = true;
        return;
    }
    _modeEcsCommandPending = false;
    if (envoyerModeECS()) {
        _chaudiere.publishModeEcs();
    }
}

bool Connect::sendPendingModeEcs() {
    if (!_modeEcsCommandPending || radio().busy()) return false;
    _modeEcsCommandPending = false;
    if (getConfig().useConnectPassive() || !estAssocie()) return false;
    setModeECS(_requestedModeEcs);
    if (envoyerModeECS()) _chaudiere.publishModeEcs();
    return true;
}

bool Connect::envoyerModeECS() {
    if (radio().busy()) return false;
    if(! estAssocie()) {
        return false;
    }

    info("[CONNECT] Envoi du mode  ECS.");
    
    struct {
        uint8_t i1 = 0x00;
        uint8_t modeECS = 0x00; 
    } payload;
    
    payload.modeECS = encodeModeECSForSend();
    info("[CONNECT] modeECS envoyé=0x%02X (mode=0x%02X, bits trame=0x%02X)", payload.modeECS, (uint8_t)getModeECS(), _modeECSFrameBits);
    
    byte buff[RADIOLIB_SX126X_MAX_PACKET_LENGTH];
    size_t length = 0;
    int16_t err;

    uint8_t retry = 0;
    do {
        err = this->radio().sendInit(
            this->getId(), 
            getConfig().boilerId(),
            this->getIdAssociation(),
            this->incrementIdMessage(),
            0x01, 
            0xA0FC,
            0x0001,
            0xA0FC,
            0x0001,
            (byte*)&payload,
            sizeof(payload),
            buff,
            length
        );

        if(err != RADIOLIB_ERR_NONE) {
            delay(100);
            continue;
        }
        
        info("[CONNECT] Envoi réussie.");
        return true;
    } while(retry++ < 1);

    info("[CONNECT] Échec de l'envoi.");
    return false;
}

bool Connect::handlePassiveReadResponse(uint16_t adresseMemoire, const byte* buff, size_t length) {
    const uint16_t addrInformations = 0x79E0;
    const uint16_t addrInformations2 = 0x79FC;
    const uint16_t addrConsommation = 0x7A18;
    const uint16_t addrDate = 0xA02B;
    const uint16_t addrModeEcs = 0xA0F0;
    const uint16_t addrConsommationAnnuelle = 0xA063;

    auto addrIsInformations = adresseMemoire == addrInformations || adresseMemoire == (addrInformations + 0x00C8);
    auto addrIsInformations2 = adresseMemoire == addrInformations2 || adresseMemoire == (addrInformations2 + 0x00C8);
    auto addrIsConsommation = adresseMemoire == addrConsommation || adresseMemoire == (addrConsommation + 0x00C8);
    auto addrIsDate = adresseMemoire == addrDate;

    if (adresseMemoire == 0x7541) {
        return setVersionFirmware(buff, length);
    }

    if (addrIsInformations) {
        debug("[CONNECT] Réception des informations passives du Connect.");
        struct {
            FrisquetRadio::RadioTrameHeader header;
            uint8_t longueurDonnees;
            byte offsetInformations[36]; // (0x79F2 - 0x79E0) * 2 octets
            temperature16 temperatureAmbianteZ1;
            temperature16 temperatureAmbianteZ2;
            temperature16 temperatureAmbianteZ3;
            temperature16 temperatureConsigneDepartZ1;
            temperature16 temperatureConsigneDepartZ2;
            temperature16 temperatureConsigneDepartZ3;
            temperature16 temperatureConsigneZ1;
            temperature16 temperatureConsigneZ2;
            temperature16 temperatureConsigneZ3;
            temperature16 temperatureExterieure;
        } resp;
        static_assert(sizeof(resp) == 63, "La réponse passive 79E0 doit contenir 63 octets");

        if (!buff || length < sizeof(resp)) {
            return false;
        }
        memcpy(&resp, buff, sizeof(resp));
        const size_t prefixLength = sizeof(resp.header) + 1;
        if (resp.longueurDonnees < sizeof(resp) - prefixLength
            || resp.longueurDonnees > length - prefixLength) return false;

        if (getZone1().getSource() == Zone::SOURCE::CONNECT) {
            getZone1().setTemperatureAmbiante(resp.temperatureAmbianteZ1.toFloat());
            getZone1().setTemperatureConsigne(resp.temperatureConsigneZ1.toFloat());
        }
        getZone1().setTemperatureConsigneDepart(resp.temperatureConsigneDepartZ1.toFloat());

        if (getZone2().getSource() == Zone::SOURCE::CONNECT) {
            getZone2().setTemperatureAmbiante(resp.temperatureAmbianteZ2.toFloat());
            getZone2().setTemperatureConsigne(resp.temperatureConsigneZ2.toFloat());
        }
        getZone2().setTemperatureConsigneDepart(resp.temperatureConsigneDepartZ2.toFloat());

        if (getZone3().getSource() == Zone::SOURCE::CONNECT) {
            getZone3().setTemperatureAmbiante(resp.temperatureAmbianteZ3.toFloat());
            getZone3().setTemperatureConsigne(resp.temperatureConsigneZ3.toFloat());
        }
        getZone3().setTemperatureConsigneDepart(resp.temperatureConsigneDepartZ3.toFloat());

        if(resp.temperatureExterieure.toFloat() != 129) {
            setTemperatureExterieure(resp.temperatureExterieure.toFloat());
        }
        // Les mesures suivantes ne sont pas couvertes par cette lecture.

        _lastRecuperationTemperatures = millis();
        publishMqtt();
        _zone1.publishMqtt();
        _zone2.publishMqtt();
        _zone3.publishMqtt();
        return true;
    }

    if (addrIsInformations2) {
        debug("[CONNECT] Réception des informations 2 passives du Connect.");
        struct {
            FrisquetRadio::RadioTrameHeader header;
            uint8_t longueurDonnees;
            // 0x79FC suit les dix mots de températures lus à partir de 0x79F2.
            temperature16 temperatureECS;
            temperature16 temperatureCDC;
            temperature16 temperatureDepartZ1;
            temperature16 temperatureDepartZ2;
            temperature16 temperatureDepartZ3;
            temperature16 temperatureSondeSecuriteCDC;
            temperature16 temperatureFumees;
            temperature16 puissanceInstantaneeECS;
            temperature16 puissanceInstantaneeChauffage;
            temperature16 temperatureInconnue5;
            pression16 pression;
            fword modeFonctionnement;
            byte inconnu;
            temperature16 inconnu2;
            byte inconnus[29];
        } resp;
        static_assert(sizeof(resp) == 63, "La réponse passive 79FC doit contenir 63 octets");

        if (!buff || length < sizeof(resp)) return false;
        memcpy(&resp, buff, sizeof(resp));
        const size_t prefixLength = sizeof(resp.header) + 1;
        if (resp.longueurDonnees < sizeof(resp) - prefixLength
            || resp.longueurDonnees > length - prefixLength) return false;

        setTemperatureECS(resp.temperatureECS.toFloat());
        setTemperatureCDC(resp.temperatureCDC.toFloat());
        getZone1().setTemperatureDepart(resp.temperatureDepartZ1.toFloat());
        getZone2().setTemperatureDepart(resp.temperatureDepartZ2.toFloat());
        getZone3().setTemperatureDepart(resp.temperatureDepartZ3.toFloat());
        _chaudiere.setTemperatureFumees(resp.temperatureFumees.toFloat() / 2);
        _chaudiere.setPuissanceInstantaneeECS(_boilerFirmware.power(
            uint16_t(resp.puissanceInstantaneeECS.bytes[0]) << 8 | resp.puissanceInstantaneeECS.bytes[1]));
        _chaudiere.setPuissanceInstantaneeChauffage(_boilerFirmware.power(
            uint16_t(resp.puissanceInstantaneeChauffage.bytes[0]) << 8 | resp.puissanceInstantaneeChauffage.bytes[1]));
        setPression(resp.pression.toFloat());
        _chaudiere.setModeFonctionnement(resp.modeFonctionnement.toUInt16());

        _lastRecuperationTemperatures = millis();
        publishMqtt();
        _zone1.publishMqtt();
        _zone2.publishMqtt();
        _zone3.publishMqtt();
        return true;
    }

    if (addrIsConsommation) {
        debug("[CONNECT] Réception des consommations passives du Connect.");
        struct {
            FrisquetRadio::RadioTrameHeader header;
            uint8_t longueurDonnees;
            byte i1[18] = {0};
            fword consommationECS;
            fword consommationChauffage;
            byte i2[34] = {0};
        } resp;

        if (length < sizeof(resp)) {
            return false;
        }
        memcpy(&resp, buff, sizeof(resp));

        setConsommationChauffage(resp.consommationChauffage.toInt16());
        setConsommationECS(resp.consommationECS.toInt16());
        _lastRecuperationConsommation = millis();
        publishMqtt();
        return true;
    }

    if(addrIsDate) {
        debug("[CONNECT] Réception de la date passive du Connect.");
        struct {
            FrisquetRadio::RadioTrameHeader header;
            uint8_t longueurDonnees;
            byte date[6] = {0};
            byte i1 = 0;
            byte i2 = 0;
        } resp;

        if (length < sizeof(resp)) {
            return false;
        }
        memcpy(&resp, buff, sizeof(resp));

        setDateRecue(resp.date);
        return true;
    }

    if (adresseMemoire == 0xA07B) {
        return setConsommationAnnuelle(buff, length, true);
    }

    if (adresseMemoire == addrConsommationAnnuelle) {
        debug("[CONNECT] Réception des consommations mensuelles passives du Connect.");
        if (!setConsommationAnnuelle(buff, length)) return false;
        _lastRecuperationConsommation = millis();
        publishMqtt();
        return true;
    }

    if (adresseMemoire == addrModeEcs) {
        struct {
            FrisquetRadio::RadioTrameHeader header;
            uint8_t longueurDonnees;
            byte offsetModeEcs[24]; // (0xA0FC - 0xA0F0) * 2 octets
            byte i1;
            byte modeECS;
        } resp;
        static_assert(sizeof(resp) == 33, "La réponse passive A0F0 doit contenir 33 octets");

        if (!buff || length < sizeof(resp)) return false;
        memcpy(&resp, buff, sizeof(resp));
        const size_t prefixLength = sizeof(resp.header) + 1;
        if (resp.longueurDonnees < sizeof(resp) - prefixLength
            || resp.longueurDonnees > length - prefixLength) return false;

        setModeECSFromRaw(resp.modeECS);
        _lastRecuperationModeECS = millis();
        publishMqtt();
        return true;
    }

    return false;
}

bool Connect::onReceive(byte* donnees, size_t length) {
    ReadBuffer readBuffer(donnees, length);

    FrisquetRadio::RadioTrameHeader header;
    if(readBuffer.remainingLength() < sizeof(header)) { return false; }

    readBuffer.getBytes((byte*)&header, sizeof(header));

    bool passive = getConfig().useConnectPassive();
    if(! estAssocie() && !passive) {
        return false;
    }

    if (header.type == FrisquetRadio::MessageType::READ) {
        if (passive && header.idExpediteur == getId() && BoilerIdentity::valid(header.idDestinataire)) {
            if(readBuffer.remainingLength() < sizeof(FrisquetRadio::RadioTrameAsk)) { return false; }
            FrisquetRadio::RadioTrameAsk requete;
            readBuffer.getBytes((byte*)&requete, sizeof(requete));

            size_t lengthRx = 0;
            byte buffRx[RADIOLIB_SX126X_MAX_PACKET_LENGTH];
            int16_t err = radio().receiveExpected(
                header.idDestinataire,
                getId(),
                header.idAssociation,
                header.idMessage,
                header.idReception | 0x80,
                header.type,
                buffRx,
                lengthRx,
                15,
                false,
                true
            );
            if (err != RADIOLIB_ERR_NONE) {
                return false;
            }

            if (lengthRx < sizeof(header) + 1 || buffRx[2] != header.idAssociation
                || buffRx[sizeof(header)] != lengthRx - sizeof(header) - 1) return false;
            confirmerChaudiere(header.idDestinataire);

            setIdAssociation(header.idAssociation);
            setIdMessage(header.idMessage);
            saveConfig();

            debug("[CONNECT] Réception réponse passive lecture adresse 0x%04X", requete.adresseMemoire.toUInt16());
            return handlePassiveReadResponse(requete.adresseMemoire.toUInt16(), buffRx, lengthRx);
        }
        return false;
    }

    if(header.type == FrisquetRadio::MessageType::INIT) {
        if(header.isAck()) return recevoirConfirmation(header, donnees + sizeof(header), length - sizeof(header));
        FrisquetRadio::RadioTrameInit requete;
        if(readBuffer.remainingLength() < sizeof(requete)) { return false; }
        readBuffer.getBytes((byte*)&requete, sizeof(requete));

        if(requete.adresseMemoireEcriture.toUInt16() == 0xA154 && requete.tailleMemoireEcriture.toUInt16() == 0x0018) { // Modification Zone
            info("[CONNECT] Réception trame Zone (idExpediteur=%d idReception(raw)=%d)", header.idExpediteur, header.idReception);

            Zone::BlocZone donneesZone;
            if(requete.longueurDonneesEcriture != sizeof(donneesZone) || readBuffer.remainingLength() < sizeof(donneesZone)) { return false; }
            readBuffer.getBytes((byte*)&donneesZone, sizeof(donneesZone));

            const byte zoneId = header.idReception & 0x7F;
            if(zoneId != ID_ZONE_1 && zoneId != ID_ZONE_2 && zoneId != ID_ZONE_3) {
                error("[CONNECT] Impossible de mettre à jour la zone, identifiant invalide : %d.", zoneId);
                return false;
            }
            if(!passive && (header.idExpediteur != getConfig().boilerId() || header.idDestinataire != getId()
                || header.idAssociation != getIdAssociation())) return false;
            // Un mode inconnu n'est pas appliqué à la zone. Le Connect actif accuse
            // quand même l'écriture : sans son accusé, la chaudière n'accuse pas le
            // satellite, qui répète son envoi puis signale une perte de connexion.
            const bool modeConnu = donneesZone.mode >= Zone::AUTO && donneesZone.mode <= Zone::HORS_GEL;
            if(!modeConnu) {
                error("[CONNECT] Zone %d : mode 0x%02X inconnu (options 0x%02X), réglages non appliqués.",
                      zoneId, donneesZone.mode, donneesZone.options);
                if(passive) return false;
            } else {
                auto& zone = getZone(zoneId);
                zone.setProgrammation(donneesZone.programme, true);
                zone.setMode(static_cast<Zone::MODE_ZONE>(donneesZone.mode));
                zone.setModeOptions(donneesZone.options);
                zone.setTemperatureReduit(donneesZone.reduit.toFloat());
                zone.setTemperatureHorsGel(donneesZone.horsGel.toFloat());
                if(!zone.boostActif()) zone.setTemperatureConfort(donneesZone.confort.toFloat());
                if(passive || !zone.reconnaitreReglages(reinterpret_cast<const byte*>(&donneesZone), sizeof(donneesZone))) {
                    info("[CONNECT] Mise à jour zone %d (id %d), publication MQTT locale.", zone.getNumeroZone(), zoneId);
                    zone.confirmerMode();
                    zone.saveConfig();
                    zone.publishMqtt();
                } else {
                    info("[CONNECT] Zone %d : commande confirmée par la relecture A154 de la chaudière.", zone.getNumeroZone());
                }
            }

            if (passive) {
                publishMqtt();
                return true;
            }

            uint8_t retry = 0;
            int16_t err;
            
            info("[CONNECT] Envoi accusé de réception");

            do {
                err = radio().sendAnswer(
                    header.idDestinataire, 
                    header.idExpediteur, 
                    header.idAssociation, 
                    header.idMessage, 
                    header.idReception, 
                    header.type,
                    (byte*)&donneesZone,
                    sizeof(donneesZone)
                );
                if(err != RADIOLIB_ERR_NONE) {
                    delay(100);
                    continue;
                }

                publishMqtt();

                return true;
            } while (retry++ < 5);
        }
    }

    return false;
}


void Connect::begin() {

    loadConfig();
}

void Connect::loop() {
    if (sendPendingModeEcs()) return;
    if (getConfig().useConnectPassive() || !estAssocie() || radio().busy()
        || _readKind != ReadKind::None) return;

    // Une collecte initiale, y compris dans les fenêtres réservées ; pas de
    // rattrapage en rafale après un échec ou une annulation de configuration.
    if (_initialReadPending) {
        // Rester en attente jusqu'à la fin : une annulation doit permettre
        // de recommencer avec le réseau et l'association courants.
        startReads(ReadKind::Firmware, ReadKind::Ecs);
        return;
    }
    if (RadioSchedule::quietWindow(RadioSchedule::clockSeconds())) return;
    // Trois lectures par tranche de 10 minutes : :03, :06 et :08.
    if (_temperatureSchedules[0].due(600, 180)
        || _temperatureSchedules[1].due(600, 360)
        || _temperatureSchedules[2].due(600, 480)) {
        startReads(ReadKind::Information, ReadKind::Information);
        return;
    }
    // Créneaux horaires distincts pour les lectures moins fréquentes.
    if (_consommationSchedule.due(3600, 300)) {
        startReads(ReadKind::Consumption, ReadKind::PreviousAnnual);
        return;
    }
    if (_ecsSchedule.due(3600, 420)) {
        startReads(ReadKind::Ecs, ReadKind::Ecs);
        return;
    }
}

void Connect::startReads(ReadKind first, ReadKind last) {
    if (first == ReadKind::Firmware) _boilerFirmware = BoilerFirmware();
    _lastRead = last;
    _readKind = first;
    _readAttempts = 0;
    _readAssociation = getIdAssociation();
    _readBoiler = getConfig().boilerId();
    _readGeneration = radio().operationGeneration();
    _readDeadline = millis();
    switch (first) {
        case ReadKind::Firmware: info("[CONNECT] Récupération initiale des informations..."); break;
        case ReadKind::Information: info("[CONNECT] Récupération des températures..."); break;
        case ReadKind::Consumption: info("[CONNECT] Récupération des consommations..."); break;
        case ReadKind::Ecs: info("[CONNECT] Récupération du mode ECS..."); break;
        case ReadKind::Annual:
        case ReadKind::PreviousAnnual:
        case ReadKind::None: break;
    }
    pollReads();
}

void Connect::pollReads() {
    if (_readKind == ReadKind::None) return;
    if (!getConfig().useConnect() || getConfig().useConnectPassive() || !estAssocie()
        || getConfig().boilerId() != _readBoiler
        || getIdAssociation() != _readAssociation || radio().operationGeneration() != _readGeneration) {
        if (radio().ownsRead(getId())) radio().cancelRead();
        _readKind = ReadKind::None;
        return;
    }
    if (!radio().ownsRead(getId())) {
        // Les commandes récentes passent entre les lectures. Une autre écoute
        // peut occuper la radio pendant les 100 ms d'intervalle.
        if (radio().busy() || _modeEcsCommandPending || static_cast<int32_t>(millis() - _readDeadline) < 0
            || FrisquetRadio::receivedFlag || radio().hasReceivedPacket() || radio().hasBufferedPacket()) return;
        uint16_t address = 0, words = 0;
        switch (_readKind) {
            case ReadKind::Firmware: address = 0x7541; words = 2; break;
            case ReadKind::Information: address = 0x79F2 + getConfig().boilerMemoryOffset(); words = 0x1C; break;
            case ReadKind::Consumption: address = 0x7A18 + getConfig().boilerMemoryOffset(); words = 0x1C; break;
            case ReadKind::Annual: address = 0xA063; words = 0x18; break;
            case ReadKind::PreviousAnnual: address = 0xA07B; words = 0x18; break;
            case ReadKind::Ecs: address = 0xA0FC; words = 1; break;
            case ReadKind::None: return;
        }
        FrisquetRadio::RadioTrameHeader request;
        request.idExpediteur = getId();
        request.idDestinataire = getConfig().boilerId();
        request.idAssociation = getIdAssociation();
        request.idMessage = incrementIdMessage();
        request.idReception = 1;
        if (radio().beginRead(getId(), request, address, words, 5)) {
            if (_readKind == ReadKind::Information && !_readAttempts) {
                // Protéger seulement les commandes reçues pendant cette lecture,
                // sans figer les observations ultérieures si leur envoi échoue.
                _readZoneChanges[0] = _zone1.getLastChange();
                _readZoneChanges[1] = _zone2.getLastChange();
                _readZoneChanges[2] = _zone3.getLastChange();
            }
            ++_readAttempts;
        }
        return;
    }

    radio().pollRead();
    if (!radio().readReady(getId())) return;
    byte data[RADIOLIB_SX126X_MAX_PACKET_LENGTH];
    size_t length = sizeof(data);
    const int16_t result = radio().takeRead(getId(), data, length);

    bool success = false;
    if (result == RADIOLIB_ERR_NONE) {
        switch (_readKind) {
            case ReadKind::Firmware: success = setVersionFirmware(data, length); break;
            case ReadKind::Information: success = applyInformation(data, length, _readZoneChanges); break;
            case ReadKind::Consumption: success = applyConsumption(data, length); break;
            case ReadKind::Annual: success = setConsommationAnnuelle(data, length); break;
            case ReadKind::PreviousAnnual: success = setConsommationAnnuelle(data, length, true); break;
            case ReadKind::Ecs: success = applyEcs(data, length); break;
            case ReadKind::None: return;
        }
    }
    // Même deuxième tentative externe et nouvel idMessage qu'avant. Les
    // erreurs de format ne modifient aucun état et suivent aussi ce chemin.
    if (!success && _readKind != ReadKind::Firmware && _readAttempts < 2) {
        _readDeadline = millis() + (_readKind == ReadKind::Ecs ? 10U : 100U);
        return;
    }
    completeRead(success, result);
}

void Connect::completeRead(bool success, int16_t result) {
    // Seule la collecte de démarrage peut commencer tant que ce drapeau est actif.
    const bool initial = _initialReadPending;
    if (success) {
        switch (_readKind) {
            case ReadKind::Information:
                _lastRecuperationTemperatures = millis();
                _zone1.publishMqtt();
                _zone2.publishMqtt();
                _zone3.publishMqtt();
                break;
            case ReadKind::Consumption: _lastRecuperationConsommation = millis(); break;
            case ReadKind::Ecs: _lastRecuperationModeECS = millis(); break;
            case ReadKind::Annual: break;
            case ReadKind::PreviousAnnual: break;
            case ReadKind::Firmware: break;
            case ReadKind::None: return;
        }
        // Une commande ECS arrivée pendant la lecture publiera son résultat
        // après envoi ; ne pas intercaler ici le mode antérieur à cette commande.
        // L'année précédente ne modifie aucune valeur MQTT.
        if (_readKind != ReadKind::Firmware && _readKind != ReadKind::PreviousAnnual
            && !(_readKind == ReadKind::Ecs && _modeEcsCommandPending)) publishMqtt();
    } else {
        switch (_readKind) {
            case ReadKind::Firmware:
                error("[CONNECT] Version chaudière indisponible ou invalide (code=%d) : puissances divisées par 10.", result);
                break;
            case ReadKind::Information:
                error(initial ? "[CONNECT] Échec de la récupération initiale des températures."
                    : "[CONNECT] Échec de la récupération des températures.");
                break;
            case ReadKind::Consumption:
                error(initial ? "[CONNECT] Échec de la récupération initiale des consommations."
                    : "[CONNECT] Échec de la récupération des consommations.");
                break;
            case ReadKind::Annual:
                error(initial ? "[CONNECT] Échec de la récupération initiale des consommations mensuelles."
                    : "[CONNECT] Échec de la récupération des consommations mensuelles.");
                break;
            case ReadKind::PreviousAnnual:
                error("[CONNECT] Échec de la récupération de l'année précédente.");
                break;
            case ReadKind::Ecs:
                error(initial ? "[CONNECT] Échec de la récupération initiale du mode ECS."
                    : "[CONNECT] Récupération impossible");
                break;
            case ReadKind::None: return;
        }
    }
    if (_readKind != _lastRead) {
        const uint32_t gap = success || _readKind == ReadKind::Firmware ? 100U : 200U;
        _readKind = static_cast<ReadKind>(static_cast<uint8_t>(_readKind) + 1);
        _readAttempts = 0;
        // L'ancienne boucle attendait aussi 100 ms après le dernier échec,
        // avant les 100 ms séparant deux lectures. Aucun sommeil ici.
        _readDeadline = millis() + gap;
    } else {
        _readKind = ReadKind::None;
        if (initial) {
            _initialReadPending = false;
            // Pas de rattrapage immédiat des créneaux traversés au démarrage.
            _temperatureSchedules[0].due(600, 180);
            _temperatureSchedules[1].due(600, 360);
            _temperatureSchedules[2].due(600, 480);
            _consommationSchedule.due(3600, 300);
            _ecsSchedule.due(3600, 420);
        }
    }
}

// L’écoute des statuts sert aussi au Connect utilisé sans émulation de satellite.
void Connect::verifierContexteEcoute() {
    if(_generationEcoute == radio().operationGeneration()) return;
    _generationEcoute = radio().operationGeneration();
    _zone1.oublierStatut();
    _zone2.oublierStatut();
    _zone3.oublierStatut();
}

void Connect::observerSatellite(const byte* donnees, size_t taille, uint32_t reception) {
    if(taille < sizeof(FrisquetRadio::RadioTrameHeader)) return;
    FrisquetRadio::RadioTrameHeader h;
    memcpy(&h, donnees, sizeof(h));
    if(h.idDestinataire != getConfig().boilerId() || h.type != FrisquetRadio::INIT
        || (h.idExpediteur != ID_ZONE_1 && h.idExpediteur != ID_ZONE_2 && h.idExpediteur != ID_ZONE_3)) return;
    Zone& zone = getZone(h.idExpediteur);
    if(zone.getSource() != Zone::CONNECT) return;
    verifierContexteEcoute();
    const byte* contenu = donnees + sizeof(h);
    if(h.idReception == 1 && taille == sizeof(h) + sizeof(FrisquetRadio::RadioTrameInit) + 8) {
        FrisquetRadio::RadioTrameInit requete;
        memcpy(&requete, contenu, sizeof(requete));
        const uint16_t consigne = 0xA02F + 5 * (zone.getNumeroZone() - 1);
        const bool blocZones = requete.adresseMemoireLecture.toUInt16() == 0xA029 && requete.tailleMemoireLecture.toUInt16() == 21;
        // Comme Satellite::applyObservation, reconnaître aussi l’écriture de consigne
        // accompagnée d’une lecture d’un seul mot (A033 sur un ancien satellite Z1).
        const bool unMot = requete.tailleMemoireLecture.toUInt16() == 1;
        if((blocZones || unMot) && requete.adresseMemoireEcriture.toUInt16() == consigne && requete.tailleMemoireEcriture.toUInt16() == 4
            && requete.longueurDonneesEcriture == 8) zone.observerStatut(reception, h.idAssociation);
        return;
    }
    // L’accusé satellite → chaudière arrête les émissions de cette fenêtre.
    // L’accusé final chaudière → Connect est traité par recevoirConfirmation().
    if(h.idReception != (getId() | 0x80) || h.idAssociation != zone.associationSatellite() || !zone.enAttente()
        || taille != sizeof(h) + 1 + sizeof(Zone::BlocZone) || contenu[0] != sizeof(Zone::BlocZone)) return;
    auto& envoi = _envois[zone.getNumeroZone() - 1];
    if(envoi.revision != zone.revisionCommande() || envoi.tentative != zone.tentative()
        || envoi.generationRadio != _generationEcoute) return;
    const auto attendu = zone.blocDemande();
    for(uint8_t rang = 0; rang < 3; ++rang) {
        if((envoi.rangsEmis & (1U << rang)) && envoi.messagesFenetre[rang] == h.idMessage
            && memcmp(contenu + 1, &attendu, sizeof(attendu)) == 0) {
            envoi.prochainRang = 3;
            return;
        }
    }
}

void Connect::envoiZones() {
    verifierContexteEcoute();
    if(!_zone1.enAttente() && !_zone2.enAttente() && !_zone3.enAttente()) return;
    if(getConfig().useConnectPassive() || !estAssocie() || radio().busy() || _readKind != ReadKind::None
        || FrisquetRadio::receivedFlag || radio().hasReceivedPacket() || radio().hasBufferedPacket()) return;
    const uint32_t maintenant = millis();
    if(_aDejaEmis && maintenant - _lastEnvoiZone < 1000) return;
    // Les créneaux d’émission programmés ne concernent que les satellites virtuels.
    const uint32_t heure = RadioSchedule::clockSeconds();
    if((getConfig().useSatelliteZ1() && getConfig().useSatelliteVirtualZ1() && RadioSchedule::satelliteWindow(heure, 1))
        || (getConfig().useSatelliteZ2() && getConfig().useSatelliteVirtualZ2() && RadioSchedule::satelliteWindow(heure, 2))
        || (getConfig().useSatelliteZ3() && getConfig().useSatelliteVirtualZ3() && RadioSchedule::satelliteWindow(heure, 3))) return;
    Zone* zones[] = {&_zone1, &_zone2, &_zone3};
    const bool actives[] = {getConfig().useZone1(), getConfig().useZone2(), getConfig().useZone3()};
    for(uint8_t i = 0; i < 3; ++i) {
        const uint8_t index = (_prochaineZone + i) % 3;
        Zone& zone = *zones[index];
        if(!actives[index] || zone.getSource() != Zone::CONNECT || !zone.enAttente()) continue;
        const int rang = zone.rangEmission(maintenant);
        if(rang >= 0 && envoyerZone(zone, rang)) {
            _aDejaEmis = true;
            _lastEnvoiZone = maintenant;
            _prochaineZone = (index + 1) % 3;
            return;
        }
    }
}

void Connect::publishMqtt() {
    _chaudiere.publishMqtt();
}

void Connect::setPression(float pression) {
    _chaudiere.setPression(pression);
}

float Connect::getPression() {
    return _chaudiere.getPression();
}
