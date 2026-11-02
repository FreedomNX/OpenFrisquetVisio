#pragma once

#include "Frisquet/FrisquetRadio.h"
#include "Config.h"
#include "MQTT/MqttManager.h"
#include "Frisquet/SondeExterieure.h"
#include "Frisquet/Connect.h"
#include "Logs.h"
#include "DS18B20.h"
#include "Frisquet/Satellite.h"
#include "Frisquet/Zone.h"
#include "Frisquet/Chaudiere.h"

class FrisquetManager {
public:
  FrisquetManager(FrisquetRadio& radio, Config& cfg, MqttManager& mqtt);

  void begin();
  void loop();

  void initMqtt();
  void initDS18B20();

  FrisquetRadio& radio() { return _radio; }
  MqttManager& mqtt() { return _mqtt; }
  Config& config() { return _cfg; }
  Connect& connect() { return _connect; }
  Chaudiere& chaudiere() { return _chaudiere; }
  Zone& zone1() { return _zone1; }
  SondeExterieure& sondeExterieure() { return _sondeExterieure; }
  Satellite& satelliteZ1() { return _satelliteZ1; }
  Satellite& satelliteZ2() { return _satelliteZ2; }
  Satellite& satelliteZ3() { return _satelliteZ3; }

  bool recupererNetworkID();
  void setHunt(bool on) { _hunt = on; if (on) _lastHunt = 0; }
  bool huntActive() const { return _hunt; }

private:
  FrisquetRadio& _radio;
  Config&        _cfg;
  MqttManager&   _mqtt;
  uint32_t       _lastRssiSample = 0;
  bool           _hunt = false;      // chasse au NetworkID en tâche de fond
  uint32_t       _lastHunt = 0;

  Zone _zone1;
  Zone _zone2;
  Zone _zone3;

  Chaudiere _chaudiere;
  SondeExterieure _sondeExterieure;
  Connect _connect;

  Satellite _satelliteZ1;
  Satellite _satelliteZ2;
  Satellite _satelliteZ3;

  DS18B20* _ds18b20;

  void onRadioReceive();


  bool _envoiZ1 = false;
  bool _envoiZ2 = false;
  bool _envoiZ3 = false;

  // MQTT
  MqttDevice _device;

  
};
