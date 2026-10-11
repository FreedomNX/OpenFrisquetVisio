#pragma once

#include <Board.h>
#include <RadioLib.h>

class Radio {

    public:
        Radio();
        void init();

        int16_t startReceive() { return _radio.startReceive(); }
#if defined(BOARD_TTGO_LORA32)
        // SX127x (FSK) calcule le timeout de réception à partir de len : avec len=0 il ne laisse
        // que ~15 ms à la chaudière pour répondre. On s'aligne sur le SX126x (255 octets, ~400 ms).
        int16_t receive(uint8_t data[], size_t len) { return _radio.receive(data, len == 0 ? 255 : len); }
#else
        int16_t receive(uint8_t data[], size_t len) { return _radio.receive(data, len); }
#endif
        int16_t readData(uint8_t data[], size_t len) { return _radio.readData(data, len); }
        int16_t transmit(byte data[], size_t len) { return _radio.transmit(data, len); }
        // Longueur du dernier paquet lu. Sur SX127x (FSK), getPacketLength(true) dépile un octet
        // de la FIFO : on ne doit donc relire la longueur qu'avant readData(), jamais après.
#if defined(BOARD_TTGO_LORA32)
        size_t getPacketLength() { return _radio.getPacketLength(false); }
#else
        size_t getPacketLength() { return _radio.getPacketLength(); }
#endif
        void onReceive(void (*func)()) { _radio.setPacketReceivedAction(func);}
        void setSyncWord(uint8_t* syncWord, size_t len) { _radio.setSyncWord(syncWord, len); }

    private:
        RADIO_CHIP _radio;
};

