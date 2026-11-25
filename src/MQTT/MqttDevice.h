#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include "MqttTopic.h"
#include "MqttEntity.h"

struct MqttDevice {
  // Identité du device (affichée dans HA)
  String deviceId = "Device";           // stable + unique
  String name     = "Generic MQTT Device";
  String model    = "Custom";
  String manufacturer = "Community";
  String swVersion;                     // optionnel
  String hwVersion;                     // optionnel

  // BaseTopic commun (facilite la compositon des topics)
  String baseTopic = "devices";

  // Availability du device (birth/LWT applicatif)
  MqttTopic availabilityTopic;          // si vide, manager en générera un par défaut
  String payloadAvailable   = "online";
  String payloadNotAvailable = "offline";

  // Champs dynamiques pour le bloc device (HA)
  // ex: "suggested_area", "configuration_url", etc.
  std::map<String, String> extraFields;

  // Entités déclarées sous ce device (remplies par le manager)
  std::map<String, MqttEntity*> entities;

  // Construit le bloc "device" dans un objet JSON HA
  void buildDeviceBlock(JsonObject dev) const {
    JsonArray ids = dev["ids"].to<JsonArray>();
    ids.add(deviceId);
    dev["name"] = name;
    dev["mdl"]  = model;
    dev["mf"]   = manufacturer;
    if (swVersion.length()) dev["sw"] = swVersion;
    if (hwVersion.length()) dev["hw"] = hwVersion;

    // Champs dynamiques pour le device
    for (auto& kv : extraFields) {
      const auto& k = kv.first;
      const auto& v = kv.second;
      if (v == "true" || v == "false") dev[k] = (v == "true");
      else dev[k] = v; // si besoin: setRaw côté appelant
    }
  }

  MqttEntity* getEntity(String id) {
    auto it = entities.find(id);
    if (it != entities.end()) {
      return it->second;
    }

    // Create, store and return a new entity to avoid leaking anonymous allocations
    MqttEntity* e = new MqttEntity();
    e->id = id;
    entities[id] = e;
    return e;
  }
};

// --- impl des méthodes dépendantes de MqttDevice ---

inline String MqttEntity::discoveryTopic() const {
  const String dom = domain.length() ? domain : "homeassistant";
  const String dev = device ? device->deviceId : String("Device");
  return dom + "/" + component + "/" + dev + "/" + id + "/config";
}

inline void MqttEntity::buildDiscoveryJson(JsonDocument& doc) const {
  const String devId = device ? device->deviceId : String("Device");
  doc["uniq_id"] = devId + "_" + id;
  doc["name"]    = name;


  if (stateTopic.full.length() && component != "climate") doc["state_topic"] = stateTopic.full;
  if (commandTopic.full.length())    doc["command_topic"] = commandTopic.full;
  if (attributesTopic.full.length()) doc["json_attributes_topic"] = attributesTopic.full;

  // Disponibilité de l'appareil (« offline » publié par la dernière volonté MQTT),
  // combinée à celle propre à l'entité : l'entité n'est disponible que si les deux le sont.
  const bool deviceAvailability = device && device->availabilityTopic.full.length()
      && device->availabilityTopic.full != availabilityTopic.full;
  if (deviceAvailability || availabilityTopic.full.length()) {
    JsonArray av = doc["availability"].to<JsonArray>();
    if (deviceAvailability) {
      JsonObject a = av.add<JsonObject>();
      a["topic"]   = device->availabilityTopic.full;
      a["payload_available"]     = device->payloadAvailable;
      a["payload_not_available"] = device->payloadNotAvailable;
    }
    if (availabilityTopic.full.length()) {
      JsonObject a = av.add<JsonObject>();
      a["topic"]   = availabilityTopic.full;
      a["payload_available"]     = payloadAvailable;
      a["payload_not_available"] = payloadNotAvailable;
    }
    if (deviceAvailability && availabilityTopic.full.length()) doc["availability_mode"] = "all";
  }

  // Champs dynamiques
  for (auto& kv : extraFields) {
    const String& key = kv.first;
    const String& val = kv.second;
    if (key.endsWith("_template")) {
      doc[key] = val;
    } else if (val == "true" || val == "false") {
      doc[key] = (val == "true");
    } else if (val.length() && (val[0] == '[' || val[0] == '{')) {
      // tableau/objet JSON bruts
      JsonDocument tmp;
      if (deserializeJson(tmp, val) == DeserializationError::Ok) {
        doc[key] = tmp.as<JsonVariant>();
      } else {
        doc[key] = val; // fallback string
      }
    } else if (isNumber(val)) {
      doc[key] = val.toFloat();
    } else {
      doc[key] = val;
    }
  }

  // Bloc device hérité du parent
  MqttDevice d;
  if (device) {
    JsonObject dev = doc["device"].to<JsonObject>();
    device->buildDeviceBlock(dev);
  }
}
