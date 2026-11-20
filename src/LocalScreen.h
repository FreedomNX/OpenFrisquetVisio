#pragma once
#include <Board.h>
#if defined(OFV_SCREEN_V3)
#include "ScreenNavigation.h"

class FrisquetManager;
class OFVNetworkManager;

class LocalScreen {
public:
    void begin(FrisquetManager& manager, OFVNetworkManager& network);
    void loop();
private:
    FrisquetManager* _manager = nullptr;
    OFVNetworkManager* _network = nullptr;
    ScreenNavigation _navigation;
    bool _present = false, _dirty = true, _uploading = false;
    bool _desiredPower = true, _power = false;
    uint8_t _page = 0;
    uint16_t _offset = 0;
    uint32_t _lastRender = 0;
    bool command(const uint8_t* bytes, size_t length);
    bool availableBus() const;
    void fail();
    void advance();
    void render(uint32_t now);
    void transfer();
};
#endif
