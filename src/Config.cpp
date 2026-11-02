#include "Config.h"
#include "Logs.h"

// Helpers IP <-> String
static String ipToStr(const IPAddress& ip){
  if (!ip) return "";
  return String(ip[0])+"."+ip[1]+"."+ip[2]+"."+ip[3];
}
static IPAddress strToIp(const String& s){
  IPAddress ip; if (s.length() && ip.fromString(s)) return ip; return IPAddress();
}

Config::Config() {}

void Config::load() {

  bool checkMigration = false;

  if(!_preferences.begin("sysconfig", false)) {
    error("[CONFIG] Impossible de charger la configuration.");
  }

  // WIFI
  _developerMode = _preferences.getBool("developerMode", false);
  _portalPassword = _preferences.getString("portalPass", "admin");
  if (_portalPassword.isEmpty()) _portalPassword = "admin";
  _radioRxBandwidth = _preferences.getFloat("radioRxBw", RadioSettings::DEFAULT_RX_BANDWIDTH);
  if (!RadioSettings::validRxBandwidth(_radioRxBandwidth)) {
    _radioRxBandwidth = RadioSettings::DEFAULT_RX_BANDWIDTH;
  }
  _radioLnaBypass = _preferences.getBool("radioBypass", RadioSettings::DEFAULT_LNA_BYPASS);
  _wifiOpts.hostname      = _preferences.getString("wifiHostname", "esp32-device");
  _wifiOpts.ssid          = _preferences.getString("wifiSsid", "");

  _wifiOpts.password      = _preferences.getString("wifiPass", "");
  _wifiOpts.useStaticIp   = _preferences.getBool("wifiStatic", false);
  _wifiOpts.localIp       = strToIp(_preferences.getString("wifiIp", ""));
  _wifiOpts.gateway       = strToIp(_preferences.getString("wifiGw", ""));
  _wifiOpts.subnet        = strToIp(_preferences.getString("wifiMask", ""));
  _wifiOpts.dns1          = strToIp(_preferences.getString("wifiDns1", "1.1.1.1"));
  _wifiOpts.dns2          = strToIp(_preferences.getString("wifiDns2", "8.8.8.8"));
  _wifiOpts.autoReconnect = _preferences.getBool("wifiAutoRec", true);
  _wifiOpts.firstConnectTimeoutMs = _preferences.getULong("wifiFirstTo", 15000);
  _wifiOpts.reconnectMinMs        = _preferences.getULong("wifiRecMin", 3000);
  _wifiOpts.reconnectMaxMs        = _preferences.getULong("wifiRecMax", 60000);
  _wifiOpts.wifiSleep     = _preferences.getBool("wifiSleep", false);

  // MQTT
  _mqttOpts.clientId      = _preferences.getString("mqttClientId", "OpenFrisquetVisio");
  _mqttOpts.host          = _preferences.getString("mqttHost", "192.168.1.10");
  _mqttOpts.port          = _preferences.getUShort("mqttPort", 1883);
  _mqttOpts.username      = _preferences.getString("mqttUser", "");
  _mqttOpts.password      = _preferences.getString("mqttPass", "");
  _mqttOpts.baseTopic     = _preferences.getString("mqttBase", "frisquet");
  _mqttOpts.keepAliveSec  = _preferences.getUShort("mqttKeep", 60);
  _mqttOpts.cleanSession  = _preferences.getBool("mqttClean", true);

  // Frisquet
  if(_preferences.isKey("networkID")) {
    _preferences.getBytes("networkID", &_networkId, sizeof(NetworkID));
  } else {
    checkMigration = true;
  }

  _useConnect = _preferences.getBool("useConnect", false);
  _useConnectPassive = _preferences.getBool("useConnectPass", false);
  _useSondeExterieure = _preferences.getBool("useSondeExt", false);
  _useDS18B20 = _preferences.getBool("useDS18B20", false);
  _useSatelliteZ1 = _preferences.getBool("useSatelliteZ1", false);
  _useSatelliteZ2 = _preferences.getBool("useSatelliteZ2", false);
  _useSatelliteZ3 = _preferences.getBool("useSatelliteZ3", false);
  _useSatelliteVirtualZ1 = _preferences.getBool("useSatVirtuelZ1", false);
  _useSatelliteVirtualZ2 = _preferences.getBool("useSatVirtuelZ2", false);
  _useSatelliteVirtualZ3 = _preferences.getBool("useSatVirtuelZ3", false);
  _useZone1 = _preferences.getBool("useZone1", true);
  _useZone2 = _preferences.getBool("useZone2", false);
  _useZone3 = _preferences.getBool("useZone3", false);

  _preferences.end();
  delay(100);

  // Migration : seulement si sysconfig n'a pas encore de NetworkID.
  if(checkMigration && _preferences.begin("net-conf", true)) {
    if(_preferences.isKey("net_id")) {
      _preferences.getBytes("net_id", &_networkId, sizeof(NetworkID));
    }
    _preferences.end();
    checkMigration = false;
  }
}

void Config::save() {
  if(!_preferences.begin("sysconfig", false)) {
    error("[CONFIG] Impossible de sauvegarder la configuration.");
  }

  // WIFI
  _preferences.putBool("developerMode", _developerMode);
  _preferences.putString("portalPass", _portalPassword);
  _preferences.putFloat("radioRxBw", _radioRxBandwidth);
  _preferences.putBool("radioBypass", _radioLnaBypass);
  _preferences.putString("wifiHostname", _wifiOpts.hostname);
  _preferences.putString("wifiSsid",     _wifiOpts.ssid);
  
  _preferences.putString("wifiPass",     _wifiOpts.password);
  _preferences.putBool  ("wifiStatic",   _wifiOpts.useStaticIp);
  _preferences.putString("wifiIp",       ipToStr(_wifiOpts.localIp));
  _preferences.putString("wifiGw",       ipToStr(_wifiOpts.gateway));
  _preferences.putString("wifiMask",     ipToStr(_wifiOpts.subnet));
  _preferences.putString("wifiDns1",     ipToStr(_wifiOpts.dns1));
  _preferences.putString("wifiDns2",     ipToStr(_wifiOpts.dns2));
  _preferences.putBool  ("wifiAutoRec",  _wifiOpts.autoReconnect);
  _preferences.putULong ("wifiFirstTo",  _wifiOpts.firstConnectTimeoutMs);
  _preferences.putULong ("wifiRecMin",   _wifiOpts.reconnectMinMs);
  _preferences.putULong ("wifiRecMax",   _wifiOpts.reconnectMaxMs);
  _preferences.putBool  ("wifiSleep",    _wifiOpts.wifiSleep);

  // MQTT
  _preferences.putString("mqttClientId", _mqttOpts.clientId);
  _preferences.putString("mqttHost",     _mqttOpts.host);
  _preferences.putUShort("mqttPort",     _mqttOpts.port);
  _preferences.putString("mqttUser",     _mqttOpts.username);
  _preferences.putString("mqttPass",     _mqttOpts.password);
  _preferences.putString("mqttBase",     _mqttOpts.baseTopic);
  _preferences.putUShort("mqttKeep",     _mqttOpts.keepAliveSec);
  _preferences.putBool  ("mqttClean",    _mqttOpts.cleanSession);

  // Frisquet
  _preferences.putBytes("networkID", &_networkId, sizeof(NetworkID));
  _preferences.putBool("useConnect", _useConnect);
  _preferences.putBool("useConnectPass", _useConnectPassive);
  _preferences.putBool("useSondeExt", _useSondeExterieure);
  _preferences.putBool("useDS18B20", _useDS18B20);
  _preferences.putBool("useSatelliteZ1", _useSatelliteZ1);
  _preferences.putBool("useSatelliteZ2", _useSatelliteZ2);
  _preferences.putBool("useSatelliteZ3", _useSatelliteZ3);
  _preferences.putBool("useSatVirtuelZ1", _useSatelliteVirtualZ1);
  _preferences.putBool("useSatVirtuelZ2", _useSatelliteVirtualZ2);
  _preferences.putBool("useSatVirtuelZ3", _useSatelliteVirtualZ3);
  _preferences.putBool("useZone1", _useZone1);
  _preferences.putBool("useZone2", _useZone2);
  _preferences.putBool("useZone3", _useZone3);

  _preferences.end();
}
