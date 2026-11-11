#include "FrisquetDevice.h"
#include "../Buffer.h"

bool FrisquetDevice::associer(NetworkID& networkId, uint8_t& idAssociation) {

    byte buff[RADIOLIB_SX126X_MAX_PACKET_LENGTH];
    size_t buffLength = 0;
    int16_t err;
    
    radio().setNetworkID({0xFF, 0xFF, 0xFF, 0xFF}); // Broadcast

    uint8_t retry = 0;
    unsigned long timeout = millis() + 30000;
    do {
        err = radio().receive(buff, 0);
        if(err != RADIOLIB_ERR_NONE) {
            continue;
        }

        buffLength = radio().getPacketLength(); 
        if (buffLength != 11) {
            continue;
        }

        struct {
            FrisquetRadio::RadioTrameHeader header;
            uint8_t length;
            NetworkID networkID;
        } donnees;

        logRadio(true, (byte*)buff, buffLength);

        ReadBuffer readBuffer = ReadBuffer(buff, buffLength);
        readBuffer.getBytes((byte*)&donnees, sizeof(donnees));

        if(BoilerIdentity::valid(donnees.header.idExpediteur) &&
           donnees.header.type == FrisquetRadio::MessageType::ASSOCIATION &&
           donnees.length == 4 && !donnees.header.isAck() && !donnees.networkID.isBroadcast()) {
            info("[DEVICE] Réception trame d'association");

            struct {
                FrisquetRadio::RadioTrameHeader header;
                NetworkID networkID;
            } confirmPayload;
            
            donnees.header.answer(confirmPayload.header);
            confirmPayload.header.idExpediteur = this->getId();
            confirmPayload.networkID = donnees.networkID;

            info("[DEVICE] Récupération du NetworkID : %s.", byteArrayToHexString((byte*)&donnees.networkID, sizeof(NetworkID)).c_str());
            info("[DEVICE] Récupération de l'association ID : %s.", byteArrayToHexString((byte*)&donnees.header.idAssociation, 1).c_str());

            logRadio(false, (byte*)&confirmPayload, sizeof(confirmPayload));

            for(uint8_t i = 0; i < 10; i++) {
                err = radio().transmit((byte*)&confirmPayload, sizeof(confirmPayload));
                if (err != RADIOLIB_ERR_NONE) {
                    continue;
                }
                delay(30);
            }

            idAssociation = donnees.header.idAssociation;
            networkId = confirmPayload.networkID;
            // Le point d'entrée d'association sauvegarde ensuite le réseau et l'identité ensemble.
            getConfig().boilerId(donnees.header.idExpediteur);
            
            radio().setNetworkID(networkId);

            delay(5000); // Attente 5 secondes
            radio().startReceive();
            return true;
        }
    } while(millis() < timeout);

    radio().setNetworkID(getConfig().getNetworkID());
    radio().startReceive();
    return false;
}

void FrisquetDevice::confirmerChaudiere(uint8_t id) {
    if (getConfig().boilerId(id)) {
        getConfig().save();
        info("[RADIO] Identifiant chaudière confirmé : 0x%02X.", id);
    }
}

bool FrisquetDevice::recupererDate() {
    if (!estAssocie() || getConfig().getNetworkID().isBroadcast()
        || (getId() == ID_CONNECT && getConfig().useConnectPassive())) return false;

    struct Response {
        FrisquetRadio::RadioTrameHeader header;
        uint8_t longueur;
        byte date[6];
    };

    const uint8_t preferred = getConfig().boilerId();
    const uint8_t candidates[] = {preferred, BoilerIdentity::alternate(preferred)};
    for (uint8_t candidate : candidates) {
        Response donnees = {};
        size_t length = sizeof(donnees);
        const uint8_t message = incrementIdMessage();
        const int16_t err = radio().sendAsk(getId(), candidate, getIdAssociation(), message,
            0x01, 0xA02B, 0x0003, (byte*)&donnees, length, 2);

        if (err != RADIOLIB_ERR_NONE || length != sizeof(donnees) || donnees.longueur != 6
            || donnees.header.idExpediteur != candidate || donnees.header.idDestinataire != getId()
            || donnees.header.idAssociation != getIdAssociation() || donnees.header.idMessage != message
            || donnees.header.idReception != 0x81 || donnees.header.type != FrisquetRadio::MessageType::READ
            || !BoilerIdentity::validDate(donnees.date)) continue;

        confirmerChaudiere(candidate);
        Date date = donnees.date;
        setDate(date);
        return true;
    }
    error("[RADIO] Lecture de l'heure sans réponse valide de 0x80 ou 0x84 ; identifiant conservé (0x%02X).", preferred);
    return false;
}
