"""Check that OTA starts with the portal password, including the default."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ota = (ROOT / "src/OTA.h").read_text()
app = (ROOT / "src/App.cpp").read_text()

init_ota = re.search(r"void App::initOta\(\) \{\n(.*?)\n\}", app, re.S)[1]
assert "_cfg.portalPassword().c_str()" in init_ota

stub_ota = r'''
#pragma once
#include <functional>
#include <string>

enum { U_FLASH, U_SPIFFS };
typedef int ota_error_t;

struct String {
    String& operator=(const char*) { return *this; }
};

struct ArduinoOTAStub {
    std::string hostname, password, passwordAtBegin;
    bool started = false;
    int handled = 0;
    ArduinoOTAStub& setHostname(const char* value) { hostname = value; return *this; }
    ArduinoOTAStub& setPassword(const char* value) { password = value; return *this; }
    ArduinoOTAStub& setTimeout(int) { return *this; }
    ArduinoOTAStub& onStart(std::function<void()>) { return *this; }
    ArduinoOTAStub& onEnd(std::function<void()>) { return *this; }
    ArduinoOTAStub& onProgress(std::function<void(unsigned int, unsigned int)>) { return *this; }
    ArduinoOTAStub& onError(std::function<void(ota_error_t)>) { return *this; }
    int getCommand() { return U_FLASH; }
    void begin() { started = true; passwordAtBegin = password; }
    void handle() { ++handled; }
};
ArduinoOTAStub ArduinoOTA;
'''

stub_logs = r'''
#pragma once
void info(const char*) {}
void error(const char*) {}
'''

harness = r'''
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
'''

with tempfile.TemporaryDirectory(prefix="frisquet-ota-") as temp:
    temp = Path(temp)
    (temp / "ArduinoOTA.h").write_text(stub_ota)
    (temp / "Logs.h").write_text(stub_logs)
    (temp / "OTA.h").write_text(ota)
    (temp / "test.cpp").write_text(harness)
    binary = temp / "test"
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-fsanitize=address,undefined", "-I", str(temp),
                    str(temp / "test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

print("OTA auth tests passed")
