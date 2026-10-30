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

bool Connect::envoyerZone(Zone& zone) {
    if(! estAssocie()) {
        return false;
    }

    if(isnan(zone.getTemperatureConfort()) || isnan(zone.getTemperatureReduit()) || isnan(zone.getTemperatureHorsGel()) || zone.getMode() == Zone::MODE_ZONE::INCONNU || zone.getNumeroZone() == 0) {
        error("[CONNECT] Impossible d'envoyer la zone %d, configuration incomplète.", zone.getNumeroZone());
        return false;
    }

    info("[CONNECT] Envoi de la zone %d.", zone.getNumeroZone());

    if(zone.getIdZone() == ID_ZONE_1 && getConfig().useSatelliteVirtualZ1()) {
        return true;
    } else if(zone.getIdZone() == ID_ZONE_2 && getConfig().useSatelliteVirtualZ2()) {
        return true;
    } else if(zone.getIdZone() == ID_ZONE_3 && getConfig().useSatelliteVirtualZ3()) {
        return true;
    }

    const bool satellitePhysique =
        (zone.getIdZone() == ID_ZONE_1 && getConfig().useSatelliteZ1()) ||
        (zone.getIdZone() == ID_ZONE_2 && getConfig().useSatelliteZ2()) ||
        (zone.getIdZone() == ID_ZONE_3 && getConfig().useSatelliteZ3());
    if(satellitePhysique && !zone.programmationRecue()) {
        info("[CONNECT] Zone %d en attente d'une programmation connue (radio ou mémoire).", zone.getNumeroZone());
        return false;
    }
    
    struct {
        temperature8 temperatureConfort;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
        temperature8 temperatureReduit;     // Début 5°C -> 0 = 50 = 5°C - MAX Confort
        temperature8 temperatureHorsGel;     // Début 5°C -> 0 = 50 = 5°C - MAX Hors gel
        uint8_t mode = 0x00; 
        byte modeOptions = 0b00000100;
        // Même format que l'octet A02F du satellite (voir Zone.h et Satellite.h).
        // Un Connect réel envoie ici la dernière valeur A02F lue (0x25 en Auto) ;
        // le firmware conserve 0x04 pour Auto en attendant validation sur chaudière.
        // Boost (0x40) et bits non interprétés conservés par Zone::setMode().
        byte inconnu1 = 0x00;
        Zone::Programmation programmation;
    } payload;
    static_assert(sizeof(payload) == 48, "La configuration zone doit contenir 48 octets");
    
    if(zone.boostActif()) {
        payload.temperatureConfort = std::min(30.0f, zone.getTemperatureConfort() + zone.getTemperatureBoost());
    } else {
        payload.temperatureConfort = zone.getTemperatureConfort();
    }
    payload.temperatureReduit = zone.getTemperatureReduit();
    payload.temperatureHorsGel = zone.getTemperatureHorsGel();
    payload.mode = zone.getMode();
    payload.modeOptions = zone.getModeOptions();
    payload.programmation = zone.getProgrammation();
    
    byte buff[RADIOLIB_SX126X_MAX_PACKET_LENGTH];
    size_t length = 0;
    int16_t err;

    uint8_t retry = 0;
    do {
        err = this->radio().sendInit(
            this->getId(), 
            ID_CHAUDIERE, 
            this->getIdAssociation(),
            this->incrementIdMessage(),
            zone.getIdZone(), 
            0xA154,
            0x0018,
            0xa154,
            0x0018,
            (byte*)&payload,
            sizeof(payload),
            buff,
            length
        );

        if(err != RADIOLIB_ERR_NONE) {
            delay(100);
            continue;
        }
        
        return true;
    } while(retry++ < 1);

    return false;
}

bool Connect::recupererInformations() {
    if(! estAssocie()) {
        return false;
    }

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

    size_t length;
    int16_t err;

    uint8_t retry = 0;
    do {
        length = sizeof(buff);
        err = this->radio().sendAsk(
            this->getId(), 
            ID_CHAUDIERE, 
            this->getIdAssociation(),
            this->incrementIdMessage(),
            0x01,
            0x79F2 + (ID_CHAUDIERE == 0x84 ? 0xC8 : 0x00),
            0x001C,
            (byte*)&buff,
            length
        );

        if(err != RADIOLIB_ERR_NONE) {
            delay(100);
            continue;
        }

        if(getZone1().getSource() == Zone::SOURCE::CONNECT) {
            getZone1().setTemperatureAmbiante(buff.temperatureAmbianteZ1.toFloat());
            getZone1().setTemperatureConsigne(buff.temperatureConsigneZ1.toFloat());
        }
        getZone1().setTemperatureDepart(buff.temperatureDepartZ1.toFloat());
        getZone1().setTemperatureConsigneDepart(buff.temperatureConsigneDepartZ1.toFloat());

        if(getZone2().getSource() == Zone::SOURCE::CONNECT) {
            getZone2().setTemperatureAmbiante(buff.temperatureAmbianteZ2.toFloat());
            getZone2().setTemperatureConsigne(buff.temperatureConsigneZ2.toFloat());
        }
        getZone2().setTemperatureDepart(buff.temperatureDepartZ2.toFloat());
        getZone2().setTemperatureConsigneDepart(buff.temperatureConsigneDepartZ2.toFloat());

        if(getZone3().getSource() == Zone::SOURCE::CONNECT) {
            getZone3().setTemperatureAmbiante(buff.temperatureAmbianteZ3.toFloat());
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
        _chaudiere.setPuissanceInstantaneeECS(buff.puissanceInstantaneeECS.toFloat());
        _chaudiere.setPuissanceInstantaneeChauffage(buff.puissanceInstantaneeChauffage.toFloat());
        _chaudiere.setModeFonctionnement(buff.modeFonctionnement.toUInt16());

        setPression(buff.pression.toFloat());
        return true;
    } while(retry++ < 1);

    return false;
}

bool Connect::recupererConsommation() {
    if(! estAssocie()) {
        return false;
    }

    struct {
        FrisquetRadio::RadioTrameHeader header;
        uint8_t longueurDonnees;
        byte i1[18] = {0};
        fword consommationECS;
        fword consommationChauffage;
        byte i2[34] = {0};
    } buff;
    

    size_t length;
    int16_t err;

    uint8_t retry = 0;
    do {
        length = sizeof(buff);
        err = this->radio().sendAsk(
            this->getId(), 
            ID_CHAUDIERE, 
            this->getIdAssociation(),
            this->incrementIdMessage(),
            0x01,
            0x7A18 + (ID_CHAUDIERE == 0x84 ? 0xC8 : 0x00),
            0x001C,
            (byte*)&buff,
            length
        );

        if(err != RADIOLIB_ERR_NONE) {
            delay(100);
            continue;
        }
        
        setConsommationChauffage(buff.consommationChauffage.toInt16());
        setConsommationECS(buff.consommationECS.toInt16());
        
        return true;
    } while(retry++ < 1);

    return false;
}

// Consommation mensuelle de l'année en cours en kWh, de janvier à décembre :
// chauffage puis ECS pour chaque mois (24 mots à partir de 0xA063).
// 0xA07B contient l'année précédente.
struct ConsommationMensuelle {
    FrisquetRadio::RadioTrameHeader header;
    uint8_t longueurDonnees;
    fword mois[24];
};
static_assert(sizeof(ConsommationMensuelle) == 55, "La réponse A063 doit contenir 55 octets");

bool Connect::recupererConsommationAnnuelle() {
    if(! estAssocie()) {
        return false;
    }

    ConsommationMensuelle buff;

    size_t length;
    int16_t err;

    uint8_t retry = 0;
    do {
        length = sizeof(buff);
        err = this->radio().sendAsk(
            this->getId(), 
            ID_CHAUDIERE, 
            this->getIdAssociation(),
            this->incrementIdMessage(),
            0x01,
            0xA063,
            0x0018,
            (byte*)&buff,
            length
        );

        if(err != RADIOLIB_ERR_NONE) {
            delay(100);
            continue;
        }

        return setConsommationAnnuelle((byte*)&buff, length);
    } while(retry++ < 1);

    return false;
}

bool Connect::setConsommationAnnuelle(const byte* donnees, size_t length) {
    ConsommationMensuelle resp;
    if (!donnees || length < sizeof(resp)) return false;
    memcpy(&resp, donnees, sizeof(resp));
    if (resp.longueurDonnees != sizeof(resp.mois)) return false;

    int32_t chauffage = 0;
    int32_t ecs = 0;
    for (uint8_t mois = 0; mois < 12; mois++) {
        chauffage += resp.mois[mois * 2].toInt16();
        ecs += resp.mois[mois * 2 + 1].toInt16();
    }
    _chaudiere.setConsommationAnnuelle(chauffage, ecs);
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

bool Connect::recupererModeECS() {
    if(! estAssocie()) {
        return false;
    }


    /*
    7E 80 AA 16 05 10 A0 F0 00 0D 1A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 11 // Eco horloge
    7E 80 AA 1A 05 10 A0 F0 00 0D 1A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 09 // Eco
    80 7E AA 03 05 10 A0 F0 00 0D 1A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 09 // Eco
    7E 80 AA 1D 05 10 A0 F0 00 0D 1A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 // max
    7E 80 AA 22 05 10 A0 F0 00 0D 1A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 19 // eco+
    */
    // Demande récupération courte : 80 7E AA 03 01 03 A0 FC 00 01
    struct {
        FrisquetRadio::RadioTrameHeader header;
        uint8_t longueurDonnees;
        byte i1;
        byte modeECS;
    } buff;
    

    size_t length = 0;
    int16_t err;

    uint8_t retry = 0;
    do {
        //length = sizeof(buff);
        err = this->radio().sendAsk(
            this->getId(), 
            ID_CHAUDIERE, 
            this->getIdAssociation(),
            this->incrementIdMessage(),
            0x01,
            0xA0FC,
            0x0001,
            (byte*)&buff,
            length
        );

        if(err != RADIOLIB_ERR_NONE) {
            delay(10);
            continue;
        }
        
        setModeECSFromRaw(buff.modeECS);

        return true;
    } while(retry++ < 1);

    return false;
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
    if (envoyerModeECS()) {
        _chaudiere.publishModeEcs();
    }
}

bool Connect::envoyerModeECS() {
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
            ID_CHAUDIERE, 
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
        _chaudiere.setPuissanceInstantaneeECS(resp.puissanceInstantaneeECS.toFloat());
        _chaudiere.setPuissanceInstantaneeChauffage(resp.puissanceInstantaneeChauffage.toFloat());
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

        Date date = resp.date;
        setDate(date);
        return true;
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
        if (passive && header.idExpediteur == getId() && header.idDestinataire == ID_CHAUDIERE) {
            if(readBuffer.remainingLength() < sizeof(FrisquetRadio::RadioTrameAsk)) { return false; }
            FrisquetRadio::RadioTrameAsk requete;
            readBuffer.getBytes((byte*)&requete, sizeof(requete));

            size_t lengthRx = 0;
            byte buffRx[RADIOLIB_SX126X_MAX_PACKET_LENGTH];
            int16_t err = radio().receiveExpected(
                ID_CHAUDIERE,
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

            setIdAssociation(header.idAssociation);
            setIdMessage(header.idMessage);
            saveConfig();

            debug("[CONNECT] Réception réponse passive lecture adresse 0x%04X", requete.adresseMemoire.toUInt16());
            return handlePassiveReadResponse(requete.adresseMemoire.toUInt16(), buffRx, lengthRx);
        }
        return false;
    }

    if(header.type == FrisquetRadio::MessageType::INIT) {
        FrisquetRadio::RadioTrameInit requete;
        if(readBuffer.remainingLength() < sizeof(requete)) { return false; }
        readBuffer.getBytes((byte*)&requete, sizeof(requete));

        if(requete.adresseMemoireEcriture.toUInt16() == 0xA154 && requete.tailleMemoireEcriture.toUInt16() == 0x0018) { // Modification Zone
            info("[CONNECT] Réception trame Zone (idExpediteur=%d idReception(raw)=%d)", header.idExpediteur, header.idReception);

            struct {
                temperature8 temperatureConfort;    // Début 5°C -> 0 = 50 = 5°C - MAX 30°C
                temperature8 temperatureReduit;     // Début 5°C -> 0 = 50 = 5°C - MAX Confort
                temperature8 temperatureHorsGel;     // Début 5°C -> 0 = 50 = 5°C - MAX Hors gel
                uint8_t mode = 0x00; 
                byte modeOptions = 0b00000100;
                byte inconnu1 = 0x00;
                Zone::Programmation programmation;
            } donneesZone;

            if(requete.longueurDonneesEcriture != sizeof(donneesZone) || readBuffer.remainingLength() < sizeof(donneesZone)) { return false; }
            readBuffer.getBytes((byte*)&donneesZone, sizeof(donneesZone));

            // Le champ `idReception` peut contenir le bit 0x80 (ACK) : Ajout d'un masque
            uint8_t zoneId = header.idReception & 0x7F;
            
            if(getZone(zoneId).getNumeroZone() == 0) {
                error("[CONNECT] Impossible de mettre à jour la zone %d, numéro de zone invalide.", zoneId);
                return false;
            }

            getZone(zoneId).setProgrammation(donneesZone.programmation, true);
            getZone(zoneId).confirmerMode((Zone::MODE_ZONE)donneesZone.mode, donneesZone.modeOptions);
            // Conserver une commande locale en attente pendant l'acquisition.
            if(!getZone(zoneId).programmationEnvoiEnAttente() && getZone(zoneId).getLastChange() <= getZone(zoneId).getLastEnvoi()) {
                getZone(zoneId).setMode((Zone::MODE_ZONE)donneesZone.mode);
                getZone(zoneId).setModeOptions(donneesZone.modeOptions);
                getZone(zoneId).setTemperatureReduit(donneesZone.temperatureReduit.toFloat());
                getZone(zoneId).setTemperatureHorsGel(donneesZone.temperatureHorsGel.toFloat());
                if(getZone(zoneId).boostActif()) {
                    //getZone(zoneId).setTemperatureBoost(donneesZone.temperatureConfort.toFloat());
                } else {
                    getZone(zoneId).setTemperatureConfort(donneesZone.temperatureConfort.toFloat());
                }
            }

            //Sauvegarde de la conf de la zone en NVs
            getZone(zoneId).saveConfig();
            info("[CONNECT] Mise à jour zone %d (id %d), publication MQTT locale.", getZone(zoneId).getNumeroZone(), zoneId );
            getZone(zoneId).publishMqtt();

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
    if (getConfig().useConnectPassive() || !estAssocie()) return;

    // Exception au planning : une collecte complète au démarrage, même dans
    // une fenêtre réservée. Les échecs seront repris aux créneaux ordinaires.
    if (_initialReadPending) {
        _initialReadPending = false;
        info("[CONNECT] Récupération initiale des informations...");
        if (recupererInformations()) {
            _lastRecuperationTemperatures = millis();
        } else {
            error("[CONNECT] Échec de la récupération initiale des températures.");
        }
        delay(100);
        if (recupererConsommation()) {
            _lastRecuperationConsommation = millis();
        } else {
            error("[CONNECT] Échec de la récupération initiale des consommations.");
        }
        delay(100);
        if (!recupererConsommationAnnuelle()) {
            error("[CONNECT] Échec de la récupération initiale des consommations mensuelles.");
        }
        delay(100);
        if (recupererModeECS()) {
            _lastRecuperationModeECS = millis();
        } else {
            error("[CONNECT] Échec de la récupération initiale du mode ECS.");
        }
        publishMqtt();
        _zone1.publishMqtt();
        _zone2.publishMqtt();
        _zone3.publishMqtt();
        // Consommer les créneaux actuellement ouverts pour éviter une lecture
        // identique immédiatement après la collecte initiale.
        _temperatureSchedules[0].due(600, 180);
        _temperatureSchedules[1].due(600, 360);
        _temperatureSchedules[2].due(600, 480);
        _consommationSchedule.due(3600, 300);
        _ecsSchedule.due(3600, 420);
        return;
    }
    if (RadioSchedule::quietWindow(RadioSchedule::clockSeconds())) return;

    // Trois lectures par tranche de 10 minutes : :03, :06 et :08.
    if (_temperatureSchedules[0].due(600, 180)
        || _temperatureSchedules[1].due(600, 360)
        || _temperatureSchedules[2].due(600, 480)) {
        info("[CONNECT] Récupération des températures...");
        if (recupererInformations()) {
            _lastRecuperationTemperatures = millis();
            publishMqtt();
            _zone1.publishMqtt();
            _zone2.publishMqtt();
            _zone3.publishMqtt();
        } else {
            error("[CONNECT] Échec de la récupération des températures.");
        }
        return;
    }

    // Créneaux horaires distincts pour les lectures moins fréquentes.
    if (_consommationSchedule.due(3600, 300)) {
        info("[CONNECT] Récupération des consommations...");
        if (recupererConsommation()) {
            _lastRecuperationConsommation = millis();
        } else {
            error("[CONNECT] Échec de la récupération des consommations.");
        }
        delay(100);
        if (!recupererConsommationAnnuelle()) {
            error("[CONNECT] Échec de la récupération des consommations mensuelles.");
        }
        publishMqtt();
        return;
    }
    if (_ecsSchedule.due(3600, 420)) {
        info("[CONNECT] Récupération du mode ECS...");
        if (recupererModeECS()) {
            _lastRecuperationModeECS = millis();
            publishMqtt();
        } else {
            error("[CONNECT] Récupération impossible");
        }
        return;
    }

    const uint32_t now = millis();
    if (now - _lastEnvoiZone >= 30000 || _lastEnvoiZone == 0) {
        envoiZones();
    }
}

void Connect::envoiZones() {
    if(estAssocie()) {
        if(getConfig().useZone1() && getZone1().getSource() == Zone::SOURCE::CONNECT && (_zone1.programmationEnvoiEnAttente() || _zone1.getLastChange() > _zone1.getLastEnvoi())) {
            info("[CONNECT] Envoi de la zone 1.");
            if(envoyerZone(_zone1)) {
                info("[CONNECT] Envoi réussi !");
                _zone1.publishMqtt();
                _zone1.refreshLastEnvoi();
                _envoiZ1 = false;
            }
        }
        if(getConfig().useZone2() && getZone2().getSource() == Zone::SOURCE::CONNECT && (_zone2.programmationEnvoiEnAttente() || _zone2.getLastChange() > _zone2.getLastEnvoi())) {
            info("[CONNECT] Envoi de la zone 2 (id=%d numero=%d).", _zone2.getIdZone(), _zone2.getNumeroZone());
            if(envoyerZone(_zone2)) {
                info("[CONNECT] Envoi réussi !");
                _zone2.publishMqtt();
                _zone2.refreshLastEnvoi();
                _envoiZ2 = false;
            }
        }
        if(getConfig().useZone3() && getZone3().getSource() == Zone::SOURCE::CONNECT && (_zone3.programmationEnvoiEnAttente() || _zone3.getLastChange() > _zone3.getLastEnvoi())) {
            info("[CONNECT] Envoi de la zone 3 (id=%d numero=%d).", _zone3.getIdZone(), _zone3.getNumeroZone());
            if(envoyerZone(_zone3)) {
                info("[CONNECT] Envoi réussi !");
                _zone3.publishMqtt();
                _zone3.refreshLastEnvoi();
                _envoiZ3 = false;
            }
        }
        _lastEnvoiZone = millis();
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
