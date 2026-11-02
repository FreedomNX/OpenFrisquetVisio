#include "FrisquetRadio.h"
#include "../Buffer.h"
#include "../Logs.h"

volatile bool FrisquetRadio::receivedFlag = false;
volatile bool FrisquetRadio::interruptReceive = false;

void FrisquetRadio::purgeBufferedPackets() {
    const uint32_t now = millis();
    for (BufferedPacket& packet : _rxBuffer) {
        if (packet.used && (now - packet.timestamp > RX_PACKET_TTL_MS)) {
            if (packet.length >= sizeof(RadioTrameHeader)) {
                RadioTrameHeader header;
                memcpy(&header, packet.data, sizeof(header));
                info("[RADIO][BUFFER] Expiration paquet %d->%d msg=%d rec=%d type=0x%02X len=%d",
                    header.idExpediteur, header.idDestinataire, header.idMessage, header.idReception, header.type, packet.length);
            } else {
                info("[RADIO][BUFFER] Expiration paquet invalide len=%d", packet.length);
            }
            packet.used = false;
            packet.length = 0;
        }
    }
}

bool FrisquetRadio::bufferUnexpectedPacket(const byte* donnees, size_t length) {
    purgeBufferedPackets();

    if (length == 0 || length > RADIOLIB_SX126X_MAX_PACKET_LENGTH) {
        return false;
    }

    for (BufferedPacket& packet : _rxBuffer) {
        if (!packet.used) {
            packet.used = true;
            packet.timestamp = millis();
            packet.length = length;
            packet.rssi = getLastPacketRSSI();
            memcpy(packet.data, donnees, length);
            if (length >= sizeof(RadioTrameHeader)) {
                RadioTrameHeader header;
                memcpy(&header, donnees, sizeof(header));
                info("[RADIO][BUFFER] Insertion paquet %d->%d msg=%d rec=%d type=0x%02X len=%d",
                    header.idExpediteur, header.idDestinataire, header.idMessage, header.idReception, header.type, length);
            } else {
                info("[RADIO][BUFFER] Insertion paquet invalide len=%d", length);
            }
            return true;
        }
    }

    info("[RADIO][BUFFER] Buffer plein, paquet ignore len=%d", length);
    return false;
}

bool FrisquetRadio::popBufferedPacket(byte* donnees, size_t& length, float* rssi) {
    purgeBufferedPackets();

    // Dépiler la plus ancienne : avec un TTL de 4 s, une trame récente pourrait
    // sinon être rejouée avant une plus vieille et se faire écraser ensuite.
    BufferedPacket* plusAncien = nullptr;
    for (BufferedPacket& p : _rxBuffer) {
        if (!p.used) continue;
        if (!plusAncien || (int32_t)(p.timestamp - plusAncien->timestamp) < 0) plusAncien = &p;
    }
    if (plusAncien) {
        BufferedPacket& packet = *plusAncien;

        if (length == 0 || packet.length < length) {
            length = packet.length;
        }

        memcpy(donnees, packet.data, length);
        if (rssi) *rssi = packet.rssi;
        if (length >= sizeof(RadioTrameHeader)) {
            RadioTrameHeader header;
            memcpy(&header, donnees, sizeof(header));
            info("[RADIO][BUFFER] Depilement paquet %d->%d msg=%d rec=%d type=0x%02X len=%d",
                header.idExpediteur, header.idDestinataire, header.idMessage, header.idReception, header.type, length);
        } else {
            info("[RADIO][BUFFER] Depilement paquet invalide len=%d", length);
        }
        packet.used = false;
        packet.length = 0;
        return true;
    }

    return false;
}

bool FrisquetRadio::hasBufferedPacket() {
    purgeBufferedPackets();
    for (BufferedPacket& packet : _rxBuffer) {
        if (packet.used) {
            return true;
        }
    }
    return false;
}

int16_t FrisquetRadio::sendAsk(
    uint8_t idExpediteur, 
    uint8_t idDestinataire, 
    uint8_t idAssociation, 
    uint8_t idMessage, 
    uint8_t idReception,
    fword adresseMemoire,
    fword tailleMemoire,
    byte* donneesReception,
    size_t& length,
    uint8_t retry
) {
    
    struct {
        RadioTrameHeader header;
        RadioTrameAsk body;
    } trame;

    trame.header.idExpediteur = idExpediteur;
    trame.header.idDestinataire = idDestinataire;
    trame.header.idAssociation = idAssociation;
    trame.header.idMessage = idMessage;
    trame.header.idReception = idReception;
    trame.header.type = FrisquetRadio::MessageType::READ;
    trame.body.adresseMemoire = adresseMemoire;
    trame.body.tailleMemoire = tailleMemoire;

    byte payload[sizeof(trame)];
    memcpy(payload, &trame, sizeof(payload));
    
    interruptReceive = true;
    uint8_t attempts = retry;
    int16_t err;
    do {
        delay(30);
        logRadio(false, (byte*)payload, sizeof(payload));
        err = this->transmit((byte*)&payload, sizeof(payload));
        if(err != RADIOLIB_ERR_NONE) {
            continue;
        }

        err = this->receiveExpected(idDestinataire, idExpediteur, idAssociation, idMessage, idReception|0x80, FrisquetRadio::MessageType::READ, donneesReception, length, retry);
        if(err == RADIOLIB_ERR_NONE || err == RADIOLIB_ERR_ADDRESS_NOT_FOUND) {
            break;
        }
    } while (--attempts > 0);

    interruptReceive = false;
    startReceive();
    return err;
}

int16_t FrisquetRadio::sendInit(
    uint8_t idExpediteur, 
    uint8_t idDestinataire, 
    uint8_t idAssociation, 
    uint8_t idMessage, 
    uint8_t idReception,
    fword adresseMemoireLecture,
    fword tailleMemoireLecture,
    fword adresseMemoireEcriture,
    fword tailleMemoireEcriture,
    byte* donneesEnvoi, 
    uint8_t longueurDonnees,
    byte* donneesReception, 
    size_t& length
) {

    struct {
        RadioTrameHeader header;
        RadioTrameInit body;
    } trame;

    trame.header.idExpediteur = idExpediteur;
    trame.header.idDestinataire = idDestinataire;
    trame.header.idAssociation = idAssociation;
    trame.header.idMessage = idMessage;
    trame.header.idReception = idReception;
    trame.header.type = FrisquetRadio::MessageType::INIT;
    trame.body.adresseMemoireLecture = adresseMemoireLecture;
    trame.body.tailleMemoireLecture = tailleMemoireLecture;
    trame.body.adresseMemoireEcriture = adresseMemoireEcriture;
    trame.body.tailleMemoireEcriture = tailleMemoireEcriture;
    trame.body.longueurDonneesEcriture = longueurDonnees;
    
    byte payload[sizeof(trame) + longueurDonnees];
    memcpy(payload, &trame, sizeof(trame));
    memcpy(&payload[sizeof(trame)], donneesEnvoi, longueurDonnees);

    interruptReceive = true;
    uint8_t retry = 0;
    int16_t err;

    do {
        logRadio(false, (byte*)payload, sizeof(payload));
        err = this->transmit(payload, sizeof(payload));
        if(err != RADIOLIB_ERR_NONE) {
            continue;
        }
        
        err = this->receiveExpected(idDestinataire, idExpediteur, idAssociation, idMessage, idReception|0x80, FrisquetRadio::MessageType::INIT, donneesReception, length);
        if(err == RADIOLIB_ERR_NONE) {
            break;
        }
        delay(30);
    } while (retry++ < 5);

    interruptReceive = false;
    startReceive();
    return err;
}

int16_t FrisquetRadio::sendAnswer(
    uint8_t idExpediteur, 
    uint8_t idDestinataire, 
    uint8_t idAssociation, 
    uint8_t idMessage, 
    uint8_t idReception,
    uint8_t type,
    byte* donneesEnvoi, 
    uint8_t longueurDonnees
) {

    FrisquetRadio::RadioTrameHeader header;

    header.idExpediteur = idExpediteur;
    header.idDestinataire = idDestinataire;
    header.idAssociation = idAssociation;
    header.idMessage = idMessage;
    header.idReception = idReception | 0x80;
    header.type = type;
    
    byte payload[RADIOLIB_SX126X_MAX_PACKET_LENGTH];
    WriteBuffer writeBuffer(payload);
    writeBuffer.putBytes((byte*)&header, sizeof(header));
    writeBuffer.putUInt8(longueurDonnees);
    writeBuffer.putBytes(donneesEnvoi, longueurDonnees);

    interruptReceive = true;
    uint8_t retry = 0;
    int16_t err;
    do {
        delay(30);
        logRadio(false, (byte*)payload, writeBuffer.getLength());
        err = this->transmit(payload, writeBuffer.getLength());
        if(err != RADIOLIB_ERR_NONE) {
            delay(10);
            continue;
        }
        
        break;
    } while (retry++ < 5);

    interruptReceive = false;
    startReceive();
    return err;
}

int16_t FrisquetRadio::receiveExpected(
    uint8_t idExpediteur, 
    uint8_t idDestinataire, 
    uint8_t idAssociation, 
    uint8_t idMessage, 
    uint8_t idReception,
    uint8_t type,
    byte* donnees, 
    size_t& length,
    uint8_t retry,
    bool useReadData,
    bool keepReceiving
) {
    if (retry == 0) return RADIOLIB_ERR_RX_TIMEOUT;
    interruptReceive = true;
    // Cette fonction prend en charge la réception en attente. Son événement
    // logiciel ne doit pas déclencher une deuxième lecture dans loop().
    receivedFlag = false;

    byte buff[RADIOLIB_SX126X_MAX_PACKET_LENGTH];
    int16_t err;
    RadioTrameHeader radioTrameHeader;

    do {
        if (keepReceiving) {
            // Ne pas interrompre une réponse déjà en cours de réception.
            // Même fenêtre que receive() à 25 kbit/s : 5 * 255 * 8 / 25000.
            const uint32_t waitStarted = millis();
            while (!hasReceivedPacket() && millis() - waitStarted < 408U) {
                delay(1);
            }
            if (!hasReceivedPacket()) {
                err = RADIOLIB_ERR_RX_TIMEOUT;
                continue;
            }
            receivedFlag = false;
            err = this->readData(buff, 0);
            // readData() place la radio en veille. Réarmer avant le traitement,
            // y compris après un CRC invalide, puis attendre une nouvelle IRQ.
            const int16_t restartError = startReceive();
            if (restartError != RADIOLIB_ERR_NONE) {
                err = restartError;
                break;
            }
        } else if(useReadData) {
            // Le tampon peut déjà contenir la réponse reçue pendant le traitement
            // du satellite. Le lire une seule fois : readData() n'attend aucun
            // nouveau paquet. Les tours suivants doivent attendre une réception.
            useReadData = false;
            err = this->readData(buff, 0);
        } else {
            err = this->receive(buff, 0);
        }

        if(err != RADIOLIB_ERR_NONE) {
            continue;
        }

        size_t len = this->getPacketLength();

        if(len < sizeof(radioTrameHeader)) {
            err = RADIOLIB_ERR_RX_TIMEOUT;
            continue;
        }
        
        logRadio(true, (byte*)buff, len, getLastPacketRSSI());

        memcpy(&radioTrameHeader, buff, sizeof(radioTrameHeader));
        if( radioTrameHeader.idExpediteur == idExpediteur && 
            radioTrameHeader.idDestinataire == idDestinataire && 
            radioTrameHeader.idMessage == idMessage &&
            radioTrameHeader.idReception ==  idReception &&
            radioTrameHeader.type ==  type
        ) {
            if(length == 0 || len < length) {
                length = len;   
            }
            memcpy(donnees, buff, length);
            break;
        } else {
            if(radioTrameHeader.type != type && radioTrameHeader.type == 0x83) {
                err = RADIOLIB_ERR_ADDRESS_NOT_FOUND;
                break;
            }
            bufferUnexpectedPacket(buff, len);
            err = RADIOLIB_ERR_RX_TIMEOUT;
        }
    } while (--retry > 0);


    interruptReceive = false;
    if (!keepReceiving) startReceive();
    return err;
}

void FrisquetRadio::setNetworkID(NetworkID networkID) {
    this->setSyncWord(networkID.bytes, sizeof(networkID.bytes));
}
