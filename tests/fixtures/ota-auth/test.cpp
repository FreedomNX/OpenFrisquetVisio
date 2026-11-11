#include <assert.h>
#include "OTA.h"

int main() {
    for (const char* password : {"admin", "a'b$;\"&"}) {
        ArduinoOTA = ArduinoOTAStub();
        OTA ota;
        ota.begin("openfrisquetvisio", password);
        ota.loop();
        assert(ArduinoOTA.started);
        assert(ArduinoOTA.hostname == "openfrisquetvisio");
        assert(ArduinoOTA.passwordAtBegin == password);
        assert(ArduinoOTA.handled == 1);
    }
    return 0;
}
