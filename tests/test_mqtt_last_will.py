"""MQTT last will: Home Assistant sees the gateway offline after an abrupt loss.

Compiles the real MqttManager.h and MqttDevice.h with a broker stub that records
the last will and applies it when the client vanishes.
Run: python3 tests/test_mqtt_last_will.py
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/zone"
for libs in sorted((ROOT / ".pio/libdeps").glob("*/")):
    if (libs / "ArduinoJson/src/ArduinoJson.h").is_file():
        break
else:
    raise SystemExit("Installer les dépendances avec pio pkg install -e heltec_wifi_lora_32_V3.")

PUBSUB = r'''
#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include <functional>
#include <map>
// Broker: retained messages, connection state and the will of the session.
struct Broker {
    std::map<String, String> retained;
    bool online = false, connected = false;
    String willTopic, willMessage;
    byte willQos = 0;
    bool willRetain = false, hasWill = false;
    // The client vanishes without DISCONNECT: the broker publishes the will.
    void loseClient() {
        connected = false;
        if (hasWill && willRetain) retained[willTopic] = willMessage;
    }
};
extern Broker broker;
class PubSubClient {
public:
    void setClient(Client&) {}
    void setServer(const char*, uint16_t) {}
    void setKeepAlive(uint16_t) {}
    void setSocketTimeout(uint16_t) {}
    void setBufferSize(size_t) {}
    void setCallback(std::function<void(char*, byte*, unsigned)>) {}
    bool connected() const { return broker.connected; }
    bool loop() { return true; }
    bool connect(const char*, const char*, const char*, const char* willTopic, byte willQos,
                 bool willRetain, const char* willMessage, bool) {
        if (!broker.online) return false;
        broker.hasWill = willTopic != nullptr;
        broker.willTopic = willTopic ? willTopic : "";
        broker.willMessage = willMessage ? willMessage : "";
        broker.willQos = willQos;
        broker.willRetain = willRetain;
        broker.connected = true;
        return true;
    }
    bool subscribe(const char*) { return true; }
    bool publish(const char* topic, const byte* payload, size_t length, bool retain) {
        if (!broker.connected) return false;
        if (retain) broker.retained[topic] = std::string(reinterpret_cast<const char*>(payload), length);
        return true;
    }
};
'''

PROGRAM = r'''
#include <cassert>
#include "MQTT/MqttManager.h"
Broker broker;
WifiSimule WiFi;
uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }
uint32_t micros() { return clockMs * 1000; }
void delay(unsigned long ms) { clockMs += ms; }

static JsonArray availability(JsonDocument& doc, const MqttEntity& e) {
    doc.clear();
    assert(deserializeJson(doc, broker.retained.at(e.discoveryTopic())) == DeserializationError::Ok);
    return doc["availability"].as<JsonArray>();
}

int main() {
    const String deviceTopic = "frisquet/openFrisquetVisio/availability";
    {
        // No registered device: plain connection, no will.
        Client net; MqttManager mqtt(net); mqtt.begin(MqttManager::Options{});
        broker.online = true;
        mqtt.loop();
        assert(broker.connected && !broker.hasWill);
        broker = Broker{};
    }

    Client net; MqttManager mqtt(net); mqtt.begin(MqttManager::Options{});
    MqttDevice device; device.deviceId = "openFrisquetVisio"; device.baseTopic = "frisquet";
    mqtt.registerDevice(device);
    assert(device.availabilityTopic.full == deviceTopic);

    MqttEntity plain; plain.id = "pression"; plain.component = "sensor";
    plain.stateTopic = MqttTopic("frisquet/chaudiere/pression", 0, true);
    MqttEntity own; own.id = "temperatureECS"; own.component = "sensor";
    own.stateTopic = MqttTopic("frisquet/chaudiere/temperatureECS", 0, true);
    own.availabilityTopic = MqttTopic("frisquet/chaudiere/temperatureECS/availability", 0, true);
    MqttEntity same; same.id = "suiviZ1"; same.component = "sensor";
    same.availabilityTopic = device.availabilityTopic;
    for (MqttEntity* e : {&plain, &own, &same}) mqtt.registerEntity(device, *e, false);

    broker.online = true;
    mqtt.loop();
    // Last will on the device availability, as Home Assistant expects it.
    assert(broker.connected && broker.hasWill);
    assert(broker.willTopic == deviceTopic && broker.willMessage == "offline");
    assert(broker.willRetain && broker.willQos == 1);
    assert(broker.retained.at(deviceTopic) == "online");

    JsonDocument doc;
    // An entity without its own availability follows the device.
    JsonArray av = availability(doc, plain);
    assert(av.size() == 1 && av[0]["topic"] == deviceTopic.c_str());
    assert(av[0]["payload_not_available"] == "offline" && doc["availability_mode"].isNull());
    // Its own availability is combined with the device one: both must be online.
    av = availability(doc, own);
    assert(av.size() == 2 && av[0]["topic"] == deviceTopic.c_str());
    assert(av[1]["topic"] == "frisquet/chaudiere/temperatureECS/availability");
    assert(doc["availability_mode"] == "all");
    // Already the device topic: not listed twice.
    av = availability(doc, same);
    assert(av.size() == 1 && av[0]["topic"] == deviceTopic.c_str() && doc["availability_mode"].isNull());

    // Power cut: the broker marks the gateway offline; the next connection says online again.
    broker.loseClient();
    assert(broker.retained.at(deviceTopic) == "offline");
    clockMs += 60000;
    mqtt.loop();
    assert(broker.connected && broker.retained.at(deviceTopic) == "online");
    puts("ok");
}
'''

with tempfile.TemporaryDirectory(prefix="ofv-mqtt-will-") as directory:
    temp = Path(directory)
    (temp / "PubSubClient.h").write_text(PUBSUB)
    (temp / "test.cpp").write_text(PROGRAM)
    binary = temp / "test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-DBOARD_TTGO_LORA32", "-I", str(temp), "-I", str(FIXTURE),
                    "-I", str(ROOT / "include"), "-I", str(ROOT / "src"),
                    "-I", str(libs / "ArduinoJson/src"), str(temp / "test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, stdout=subprocess.DEVNULL)

print("OK: MQTT last will on the device availability, inherited by every discovered entity")
