#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include <functional>
#include <map>

// Les scénarios observent les publications réellement produites par MqttManager.
extern std::map<String, String> publications;
extern std::map<String, unsigned> nombrePublications;
using ReceptionMqtt = std::function<void(char*, byte*, unsigned)>;
extern ReceptionMqtt receptionMqtt;
class PubSubClient {
public:
    void setClient(Client&) {}
    void setServer(const char*, uint16_t) {}
    void setKeepAlive(uint16_t) {}
    void setSocketTimeout(uint16_t) {}
    void setBufferSize(size_t) {}
    void setCallback(ReceptionMqtt callback) { receptionMqtt = callback; }
    bool connected() const { return true; }
    bool loop() { return true; }
    bool connect(const char*, const char*, const char*, const char*, byte, bool, const char*, bool) { return true; }
    bool subscribe(const char*) { return true; }
    bool publish(const char* sujet, const byte* texte, size_t taille, bool) {
        publications[sujet] = std::string(reinterpret_cast<const char*>(texte), taille);
        ++nombrePublications[sujet];
        return true;
    }
};
