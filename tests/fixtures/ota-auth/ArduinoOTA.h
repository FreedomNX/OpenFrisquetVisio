#pragma once
#include <functional>
#include <string>

enum { U_FLASH, U_SPIFFS };
typedef int ota_error_t;

using String = std::string;

struct ArduinoOTAStub {
    std::string hostname, password, passwordAtBegin;
    bool started = false;
    int handled = 0;
    std::function<void()> start, end;
    std::function<void(ota_error_t)> failure;
    ArduinoOTAStub& setHostname(const char* value) { hostname = value; return *this; }
    ArduinoOTAStub& setPassword(const char* value) { password = value; return *this; }
    ArduinoOTAStub& setTimeout(int) { return *this; }
    ArduinoOTAStub& onStart(std::function<void()> callback) { start = callback; return *this; }
    ArduinoOTAStub& onEnd(std::function<void()> callback) { end = callback; return *this; }
    ArduinoOTAStub& onProgress(std::function<void(unsigned int, unsigned int)>) { return *this; }
    ArduinoOTAStub& onError(std::function<void(ota_error_t)> callback) { failure = callback; return *this; }
    int getCommand() { return U_FLASH; }
    void begin() { started = true; passwordAtBegin = password; }
    void handle() { ++handled; }
};
ArduinoOTAStub ArduinoOTA;
