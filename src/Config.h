#pragma once
#include <Board.h>
#include <Preferences.h>
#include "NetworkManager.h"
#include "MQTT/MqttManager.h"
#include "Frisquet/NetworkID.h"

class Config {
    private:
        NetworkManager::Options _wifiOpts;
        MqttManager::Options _mqttOpts;
        NetworkID _networkId;

        Preferences _preferences;

        bool _useConnect = false;
        bool _useConnectPassive = false;
        bool _useSondeExterieure = false;
        bool _useDS18B20 = false;
        bool _useSatelliteZ1 = false;
        bool _useSatelliteZ2 = false;
        bool _useSatelliteZ3 = false;
        bool _useSatelliteVirtualZ1 = false;
        bool _useSatelliteVirtualZ2 = false;
        bool _useSatelliteVirtualZ3 = false;
        bool _useZone1 = true;
        bool _useZone2 = false;
        bool _useZone3 = false;
    public:
        Config();
        void load();
        void save();

        NetworkManager::Options& getWiFiOptions() { return _wifiOpts; }
        MqttManager::Options& getMQTTOptions() { return _mqttOpts; }
        NetworkID& getNetworkID() { return _networkId; }
        void setNetworkID(NetworkID networkId) { _networkId = networkId; }

        bool useConnect() { return _useConnect; }
        bool useConnect(bool useConnect) { _useConnect = useConnect; return _useConnect; }
        bool useConnectPassive() { return _useConnectPassive; }
        bool useConnectPassive(bool useConnectPassive) { _useConnectPassive = useConnectPassive; return _useConnectPassive; }
        bool useSondeExterieure() { return _useSondeExterieure; }
        bool useSondeExterieure(bool useSondeExterieure) { _useSondeExterieure = useSondeExterieure; return _useSondeExterieure; }
        bool useDS18B20() { return _useDS18B20; }
        bool useDS18B20(bool useDS18B20) { _useDS18B20 = useDS18B20; return _useDS18B20; }
        bool useSatelliteZ1() { return _useSatelliteZ1; }
        bool useSatelliteZ1(bool useSatelliteZ1) { _useSatelliteZ1 = useSatelliteZ1; return _useSatelliteZ1; }
        bool useSatelliteZ2() { return _useSatelliteZ2; }
        bool useSatelliteZ2(bool useSatelliteZ2) { _useSatelliteZ2 = useSatelliteZ2; return _useSatelliteZ2; }
        bool useSatelliteZ3() { return _useSatelliteZ3; }
        bool useSatelliteZ3(bool useSatelliteZ3) { _useSatelliteZ3 = useSatelliteZ3; return _useSatelliteZ3; }
        bool useSatelliteVirtualZ1() { return _useSatelliteVirtualZ1; };
        void useSatelliteVirtualZ1(bool useSatelliteVirtualZ1) { _useSatelliteVirtualZ1 = useSatelliteVirtualZ1; }
        bool useSatelliteVirtualZ2() { return _useSatelliteVirtualZ2; };
        void useSatelliteVirtualZ2(bool useSatelliteVirtualZ2) { _useSatelliteVirtualZ2 = useSatelliteVirtualZ2; }
        bool useSatelliteVirtualZ3() { return _useSatelliteVirtualZ3; };
        void useSatelliteVirtualZ3(bool useSatelliteVirtualZ3) { _useSatelliteVirtualZ3 = useSatelliteVirtualZ3; }
        bool useZone1() { return _useZone1; }
        bool useZone1(bool useZone1) { _useZone1 = useZone1; return _useZone1; }
        bool useZone2() { return _useZone2; }
        bool useZone2(bool useZone2) { _useZone2 = useZone2; return _useZone2; }
        bool useZone3() { return _useZone3; }
        bool useZone3(bool useZone3) { _useZone3 = useZone3; return _useZone3; }
};
