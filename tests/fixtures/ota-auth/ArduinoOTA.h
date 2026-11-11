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
