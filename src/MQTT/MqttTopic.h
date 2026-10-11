#pragma once

#include <Board.h>

struct MqttTopic {
  String full;     // ex: "home/sensor/boiler/z1/state"
  uint8_t qos = 0;
  bool retain = true;

  MqttTopic() {}
  MqttTopic(const String& full, uint8_t qos = 0, bool retain = true)
    : full(full), qos(qos), retain(retain) {}
};
