#pragma once

#include <Board.h>
#include <RadioLib.h>
#include <math.h>
#include "RadioSettings.h"
#include "RadioCrc.h"
#include <Preferences.h>

class Radio {

    public:
        static constexpr uint8_t MAX_RX_PACKET_LENGTH = RadioCrc::MAX_PAYLOAD;
        Radio();
        void init(float rxBandwidth = RadioSettings::DEFAULT_RX_BANDWIDTH,
                  bool lnaBypass = RadioSettings::DEFAULT_LNA_BYPASS);

        int16_t startReceive() { return _radio.startReceive(); }
        int16_t receive(uint8_t data[], size_t len);
        int16_t readData(uint8_t data[], size_t len);
        bool hasReceivedPacket();
        float getLastPacketRSSI() const { return _lastPacketRSSI; }
        float rssiInst() {
#if defined(RADIO_SX1276)
            return _radio.getRSSI();
#else
            return _radio.getRSSI(false);   // false = valeur instantanée, pas celle du dernier paquet
#endif
        }
        int16_t transmit(byte data[], size_t len);
        // Balayage : règle la fréquence, laisse l'AGC se stabiliser, lit le RSSI.
        float rssiAt(float mhz) {
            _radio.standby();
            _radio.setFrequency(mhz);
            _radio.startReceive();   // sans réception armée, l'AGC ne mesure rien
            delay(60);
            const float v = rssiInst();
            _radio.standby();
            return v;
        }
        void radioRestore() { _radio.standby(); _radio.setFrequency(868.96); startReceive(); }

        // --- Mode sniffer : sync word court pour capter un réseau inconnu ---
        void stopReceiveForSniff() { _radio.standby(); }
        int16_t setSniffSync(uint8_t* sync, size_t len, float mhz) {
            _radio.standby();
            _radio.setFrequency(mhz);
            int16_t st = _radio.setSyncWord(sync, len);
            if (st != RADIOLIB_ERR_NONE) return st;
#if defined(RADIO_SX1276)
            return _radio.startReceive();
#else
            return _radio.startReceiveWithLimit(RADIOLIB_SX126X_RX_TIMEOUT_INF);
#endif
        }
        size_t getPacketLength() const { return _radio.lastRxLength; }
        const uint8_t* lastRaw() const { return _radio.raw; }
        size_t lastRawLen() const { return _radio.rawLength; }
        int lastCrcStatus() const { return static_cast<int>(_radio.crcResult.status); }
        void onReceive(void (*func)()) { _radio.setPacketReceivedAction(func);}
        void setSyncWord(uint8_t* syncWord, size_t len) { _radio.setSyncWord(syncWord, len); }

    private:
        int16_t finishReceive(int16_t result, const char* operation, const uint8_t* data, size_t requestedLength);
        // Les chemins bloquant et asynchrone partagent la lecture et le CRC
        // propres à la puce ; les métadonnées survivent à l'effacement IRQ.
#if defined(RADIO_SX1276)
        using RadioChip = SX1276;
#else
        using RadioChip = SX1262;
#endif
        struct PacketRadio : public RadioChip {
            using RadioChip::RadioChip;
            size_t tunedLen = RadioCrc::RAW_LENGTH;
            bool   tunedVar = false;   // mode longueur variable (le 1er octet porte la longueur)
            uint8_t tunedPreamble = 16;
            uint16_t lastRxIrq = 0;
            size_t lastRxLength = 0;
            uint8_t lastRxOffset = 0;
            uint8_t raw[RadioCrc::RAW_LENGTH] = {};
            size_t rawLength = 0;
            RadioCrc::Result crcResult;
            int16_t startReceive() override;
            int16_t receive(uint8_t* data, size_t len, RadioLibTime_t timeout = 0) override;
            int16_t readData(uint8_t* data, size_t len) override;
#if !defined(RADIO_SX1276)
            int16_t startReceiveWithLimit(uint32_t timeout);
#endif
        };
        PacketRadio _radio;
        float _lastPacketRSSI = NAN;
    public:
        // Réglages ajustables à chaud (banc de test réception)
        // Persistance du banc de test : sans cela, un reboot remet la longueur fixe 68
        // et la réception des trames Frisquet (variable, 23/49 octets) retombe.
        void loadTuning() {
            Preferences p;
            if (!p.begin("radiotune", true)) return;
            _radio.tunedPreamble = p.getUChar("pre", 16);
            _radio.tunedLen = p.getUShort("len", RadioCrc::RAW_LENGTH);
            _radio.tunedVar = p.getBool("var", false);
            p.end();
        }
        void saveTuning() {
            Preferences p;
            if (!p.begin("radiotune", false)) return;
            p.putUChar("pre", _radio.tunedPreamble);
            p.putUShort("len", (uint16_t)_radio.tunedLen);
            p.putBool("var", _radio.tunedVar);
            p.end();
        }
        size_t tunedLen() const { return _radio.tunedLen; }
        bool tunedVar() const { return _radio.tunedVar; }
        uint8_t tunedPreamble() const { return _radio.tunedPreamble; }
        int16_t applyTuning(uint8_t preamble, size_t len, float bw) {
            _radio.standby();
            if (preamble >= 1 && preamble <= 32) {
                int16_t st = _radio.setPreambleLength(preamble);
                if (st != RADIOLIB_ERR_NONE) return st;
                _radio.tunedPreamble = preamble;
            }
            if (len == 1) { _radio.tunedVar = true; }
            else { _radio.tunedVar = false; if (len >= 8 && len <= 255) _radio.tunedLen = len; }
            saveTuning();
            if (bw > 0) {
                int16_t st = _radio.setRxBandwidth(bw);
                if (st != RADIOLIB_ERR_NONE) return st;
            }
            return startReceive();
        }
    private:
};
