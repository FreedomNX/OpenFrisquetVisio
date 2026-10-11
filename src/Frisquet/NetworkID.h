#pragma once

#include <Board.h>

struct NetworkID {
    NetworkID(uint8_t b0=0, uint8_t b1=0, uint8_t b2=0, uint8_t b3=0) {
        bytes[0] = b0; bytes[1] = b1; bytes[2] = b2; bytes[3] = b3;
    }

    NetworkID(uint8_t networkId[]) {
        memcpy(bytes, networkId, 4);
    }

    NetworkID(const NetworkID& networkId) {
        memcpy(bytes, networkId.bytes, 4);
    }

    bool isBroadcast() {
        return 
            bytes[0] == 0XFF &&
            bytes[1] == 0XFF &&
            bytes[2] == 0XFF &&
            bytes[3] == 0XFF;
    }

    uint8_t bytes[4] = {0xFF, 0xFF, 0xFF, 0xFF};

    void operator =(const NetworkID& networkId) {
        memcpy(bytes, networkId.bytes, 4);
    }
};