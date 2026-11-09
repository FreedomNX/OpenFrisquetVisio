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

        if(donnees.header.idExpediteur == ID_CHAUDIERE && donnees.header.type == FrisquetRadio::MessageType::ASSOCIATION) {
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
            
            radio().setNetworkID(networkId);

            delay(5000); // Attente 5 secondes
            radio().startReceive();
            return true;
        }
    } while(millis() < timeout);

    // La tentative sur le réseau broadcast ne doit pas interrompre l'écoute
    // du réseau configuré si aucune association n'a abouti.
    radio().setNetworkID(getConfig().getNetworkID());
    radio().startReceive();
    return false;
}

bool FrisquetDevice::recupererDate() {
    if(! estAssocie()) {
        return false;
    }

    struct {
        FrisquetRadio::RadioTrameHeader header;
        uint8_t longueur;
        byte date[6] = {0};
    } donnees;
    
    size_t length;
    int16_t err;

    uint8_t retry = 0;
    do {
        length = 0;
        err = this->radio().sendAsk(
            this->getId(), 
            ID_CHAUDIERE, 
            this->getIdAssociation(),
            this->incrementIdMessage(),
            0x01,
            0xA02B,
            0x0003,
            (byte*)&donnees,
            length
        );

        if(err != RADIOLIB_ERR_NONE || length < sizeof(donnees)) {
            delay(100);
            continue;
        }

        Date date = donnees.date;
        setDate(date);
        
        return true;
    } while(retry++ < 10);

    return false;
}
