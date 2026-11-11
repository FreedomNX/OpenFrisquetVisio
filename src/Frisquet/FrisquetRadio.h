#pragma once
#include "../Radio.h"
#include "Utils.h"
#include "NetworkID.h"

class FrisquetRadio : public Radio {
    public: 
    static constexpr uint8_t RX_BUFFER_SIZE = 6;
    static constexpr uint32_t RX_PACKET_TTL_MS = 1000;

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

    // Une seule observation passive, sans allocation ni callback réentrant.
    // Le propriétaire conserve la réservation jusqu'à takeObservation().
    static constexpr int16_t ERR_BUSY = -32000;
    bool beginObservation(uint8_t owner, const RadioTrameHeader& expected);
    void pollObservation();
    bool observationReady(uint8_t owner) const;
    bool ownsObservation(uint8_t owner) const;
    int16_t takeObservation(uint8_t owner, byte* data, size_t& length);
    void cancelObservation();
    void cancelOperations();
    bool suspended() const { return _suspended != 0; }
    bool busy() const {
        return _suspended || _observation.phase != ObservationPhase::Idle;
    }
    enum class Pause : uint8_t { ArduinoOta = 1, HttpOta = 2, Reboot = 4 };
    void suspend(Pause reason, bool suspended) {
        const uint8_t previous = _suspended;
        const uint8_t bit = static_cast<uint8_t>(reason);
        if (suspended) _suspended |= bit;
        else _suspended &= ~bit;
        if (!previous && _suspended) cancelOperations();
        if (previous && !_suspended) { receivedFlag = false; startReceive(); }
    }

    bool popBufferedPacket(byte* donnees, size_t& length, float* rssi = nullptr);
    bool hasBufferedPacket();

    static volatile bool receivedFlag;
    static volatile bool interruptReceive;

    private:
    enum class ObservationPhase : uint8_t { Idle, Guard, Receive, Ready };
    struct {
        ObservationPhase phase = ObservationPhase::Idle;
        uint8_t owner = 0;
        RadioTrameHeader expected;
        uint8_t remaining = 0;
        uint32_t deadline = 0;
        int16_t result = RADIOLIB_ERR_NONE;
        size_t length = 0;
    } _observation;
    byte _operationData[RADIOLIB_SX126X_MAX_PACKET_LENGTH] = {};
    uint8_t _suspended = 0;
    void finishObservation(int16_t result);
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
