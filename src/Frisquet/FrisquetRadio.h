#pragma once
#include "../Radio.h"
#include "Utils.h"
#include "NetworkID.h"

class FrisquetRadio : public Radio {
    public: 
    static constexpr uint8_t RX_BUFFER_SIZE = 6;
    static constexpr uint32_t RX_PACKET_TTL_MS = 1000;

    // Limiter toutes les tentatives d'un échange au créneau satellite.
    class TransmitWindow {
    public:
        TransmitWindow(FrisquetRadio& radio, uint32_t duration) : _radio(radio) {
            _radio._txWindowStarted = millis();
            _radio._txWindowDuration = duration;
            _radio._txWindowActive = true;
        }
        ~TransmitWindow() { _radio._txWindowActive = false; }
        TransmitWindow(const TransmitWindow&) = delete;
        TransmitWindow& operator=(const TransmitWindow&) = delete;
    private:
        FrisquetRadio& _radio;
    };

    bool transmitWindowOpen(uint32_t margin = 0) const {
        const uint32_t elapsed = millis() - _txWindowStarted;
        return !_txWindowActive || (elapsed < _txWindowDuration &&
            _txWindowDuration - elapsed > margin);
    }

    int16_t transmit(byte data[], size_t len) {
        // Une trame de 63 octets à 25 kbit/s dure moins de 30 ms sur l'air.
        // Réserver 100 ms pour sa préparation et sa transmission.
        if (!transmitWindowOpen(100)) return RADIOLIB_ERR_TX_TIMEOUT;
        return Radio::transmit(data, len);
    }

    enum MessageType : byte {
        READ = 0x03,
        INIT = 0x17,
        ASSOCIATION = 0x41
    };
    
    struct RadioTrameHeader {
        byte idDestinataire = 0x00;
        byte idExpediteur = 0x00;
        byte idAssociation = 0x00;
        byte idMessage = 0x00;
        byte idReception = 0x01; //0x01 => Si message direct, sinon id destinataire finale (exemple envoi au satellite Z1 via chaudière) + 0x80 si accusé réception
        byte type = 0x00;

        void answer(RadioTrameHeader& radioTrameHeader) {
            radioTrameHeader.idExpediteur = idDestinataire;
            radioTrameHeader.idDestinataire = idExpediteur;
            radioTrameHeader.idAssociation = idAssociation;
            radioTrameHeader.idMessage = idMessage;
            radioTrameHeader.idReception = idReception | 0x80;
            radioTrameHeader.type = type;
        }

        bool isAck() {
            return idReception >= 0x80;
        }
    };

    struct RadioTrameAsk {
        fword adresseMemoire;
        fword tailleMemoire;
    };

    struct RadioTrameInit {
        fword adresseMemoireLecture;
        fword tailleMemoireLecture;
        fword adresseMemoireEcriture;
        fword tailleMemoireEcriture;
        uint8_t longueurDonneesEcriture;
    };

    int16_t sendAsk(
        uint8_t idExpediteur, 
        uint8_t idDestinataire, 
        uint8_t idAssociation, 
        uint8_t idMessage, 
        uint8_t idReception,
        fword adresseMemoire,
        fword tailleMemoire,
        byte* donneesReception,
        size_t& length,
        uint8_t retry = 5
    );

    int16_t sendInit(
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
    );

    int16_t sendAnswer(
        uint8_t idExpediteur, 
        uint8_t idDestinataire, 
        uint8_t idAssociation, 
        uint8_t idMessage, 
        uint8_t idReception,
        uint8_t type,
        byte* donneesEnvoi, 
        uint8_t longueurDonnees
    );
    
    int16_t receiveExpected(
            uint8_t idExpediteur, 
            uint8_t idDestinataire, 
            uint8_t idAssociation, 
            uint8_t idMessage, 
            uint8_t idReception, 
            uint8_t type,
            byte* donnees, 
            size_t& length,
            uint8_t retry = 5,
            bool useReadData = false,
            bool keepReceiving = false // L'écoute continue est déjà armée par le manager.
        );

    void setNetworkID(NetworkID networkID);

    bool popBufferedPacket(byte* donnees, size_t& length, float* rssi = nullptr);
    bool hasBufferedPacket();

    static volatile bool receivedFlag;
    static volatile bool interruptReceive;

    private:
    bool _txWindowActive = false;
    uint32_t _txWindowStarted = 0;
    uint32_t _txWindowDuration = 0;
    struct BufferedPacket {
        bool used = false;
        uint32_t timestamp = 0;
        size_t length = 0;
        float rssi = NAN;
        byte data[RADIOLIB_SX126X_MAX_PACKET_LENGTH] = {0};
    };

    void purgeBufferedPackets();
    bool bufferUnexpectedPacket(const byte* donnees, size_t length);
    BufferedPacket _rxBuffer[RX_BUFFER_SIZE];
};
