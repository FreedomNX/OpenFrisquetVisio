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

    // Une seule observation passive, sans allocation ni callback réentrant.
    // Le propriétaire conserve la réservation jusqu'à takeObservation().
    static constexpr int16_t ERR_BUSY = -32000;
    bool beginObservation(uint8_t owner, const RadioTrameHeader& expected);
    void pollObservation();
    bool observationReady(uint8_t owner) const;
    bool ownsObservation(uint8_t owner) const;
    int16_t takeObservation(uint8_t owner, byte* data, size_t& length);
    void cancelObservation();
    // Lecture active : seule la courte émission reste synchrone, suivie
    // immédiatement du réarmement RX. Gardes et réceptions rendent la main.
    bool beginRead(uint8_t owner, const RadioTrameHeader& request,
                   fword address, fword words, uint8_t retry = 5);
    void pollRead();
    bool ownsRead(uint8_t owner) const;
    bool readReady(uint8_t owner) const;
    int16_t takeRead(uint8_t owner, byte* data, size_t& length);
    // Toute annulation invalide le contexte, même entre deux transactions.
    void cancelRead();
    void cancelOperations();
    uint32_t operationGeneration() const { return _operationGeneration; }
    bool suspended() const { return _suspended != 0; }
    bool busy() const {
        return _suspended || _observation.phase != ObservationPhase::Idle || _read.phase != ReadPhase::Idle;
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
    bool _txWindowActive = false;
    uint32_t _txWindowStarted = 0;
    uint32_t _txWindowDuration = 0;
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
    enum class ReadPhase : uint8_t { Idle, Guard, Receive, Ready };
    struct {
        ReadPhase phase = ReadPhase::Idle;
        uint8_t owner = 0;
        struct {
            RadioTrameHeader header;
            RadioTrameAsk body;
        } request;
        RadioTrameHeader expected;
        uint8_t retry = 0, sendsRemaining = 0, receivesRemaining = 0;
        uint32_t deadline = 0;
        int16_t result = RADIOLIB_ERR_NONE;
        size_t length = 0;
    } _read;
    // Ces deux opérations s'excluent ; aucun deuxième tampon de 255 octets.
    byte _operationData[RADIOLIB_SX126X_MAX_PACKET_LENGTH] = {};
    uint32_t _operationGeneration = 0;
    void finishRead(int16_t result);
    void retryRead(int16_t result);
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
