"""Check component migration against a retained broker model, including reconnects."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
manager = (ROOT / 'src/MQTT/MqttManager.h').read_text()
device_header = (ROOT / 'src/MQTT/MqttDevice.h').read_text()


def extract(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


# All changing components must clean up before their active registration.
for filename, entity, mqtt in (
    ('Zone.cpp', 'temperatureAmbiante', 'mqtt()'),
    ('Chaudiere.cpp', 'tempExterieure', '_mqtt'),
    ('SondeExterieure.cpp', 'tempExterieure', 'mqtt()'),
):
    source = (ROOT / 'src/Frisquet' / filename).read_text()
    cleanup = (f'{mqtt}.removeEntityDiscovery(*device, _mqttEntities.{entity},\n'
               f'        _mqttEntities.{entity}.component == "number" ? "sensor" : "number");')
    registration = f'{mqtt}.registerEntity(*device, _mqttEntities.{entity}, true);'
    assert source.index(cleanup) < source.index(registration), filename

code = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <cstdio>
using String = std::string;
struct MqttTopic { String full; unsigned qos=0; bool retain=true; };
struct MqttDevice;
struct MqttEntity {
    String id, component, domain="homeassistant";
    MqttTopic commandTopic, stateTopic;
    const MqttDevice* device=nullptr;
    String discoveryTopic() const;
};
struct MqttDevice {
    String deviceId="openFrisquetVisio";
    std::map<String,MqttEntity*> entities;
};
''' + extract(device_header, 'inline String MqttEntity::discoveryTopic() const') + r'''
struct Client {
    bool online=false, allowConnect=false;
    bool connected() const { return online; }
    template<class... Args> bool connect(Args...) { online=allowConnect; return online; }
    void subscribe(const char*) {}
};
struct Manager {
    Client _mqtt;
    std::map<String,MqttDevice*> _devices;
    std::vector<String> _removedDiscoveryTopics;
    std::map<String,std::function<void(const String&)>> _commandHandlers;
    std::map<String,String>& broker;
''' + extract(manager, 'struct Options') + r''';
    Options _opts;
    explicit Manager(std::map<String,String>& retained):broker(retained) {}
    void registerDevice(MqttDevice& d) { _devices[d.deviceId]=&d; }
    void publishAvailability(const MqttDevice&, bool) {}
    bool publishRaw(const String& topic, const String& payload, unsigned qos=0, bool retain=true) {
        assert(qos==0 && retain);
        if (!_mqtt.online) return false;
        if (payload.empty()) broker.erase(topic); else broker[topic]=payload;
        return true;
    }
    bool publishEntityDiscovery(const MqttEntity& e) { return publishRaw(e.discoveryTopic(), "config"); }
'''
for signature in (
    'bool isRegistered(', 'void registerEntity(',
    'void removeEntityDiscovery(MqttDevice& d, MqttEntity& e)',
    'void removeEntityDiscovery(MqttDevice& d, const MqttEntity& e,',
    'void reconnect()', 'bool publishState(const MqttEntity& e, const String& payload)',
):
    code += extract(manager, signature) + '\n'
code += r'''
};
int main() {
    for (const char* id : {"temperatureAmbianteZ1", "temperatureAmbianteZ2",
                           "temperatureAmbianteZ3", "temperatureExterieure"}) {
        std::map<String,String> retained;
        const String root="homeassistant/";
        const String suffix=String("/openFrisquetVisio/")+id+"/config";
        const String sensor=root+"sensor"+suffix, number=root+"number"+suffix;
        retained[sensor]="old sensor"; retained[number]="old number";
        retained["unrelated/config"]="keep";
        // Each iteration simulates a reboot after a mode change, in both directions.
        for (const char* component : {"number", "sensor", "number"}) {
            Manager manager(retained); MqttDevice device; MqttEntity entity;
            entity.id=id; entity.component=component;
            entity.stateTopic.full=String("frisquet/")+id;
            const String obsolete=entity.component=="number"?"sensor":"number";
            const String activeTopic=root+component+suffix, oldTopic=root+obsolete+suffix;
            manager.removeEntityDiscovery(device,entity,obsolete);
            assert(entity.component==component && entity.device==nullptr);
            manager.registerEntity(device,entity);
            // Two exterior-temperature owners may register the same ID at boot.
            manager.removeEntityDiscovery(device,entity,obsolete);
            manager.registerEntity(device,entity);
            assert(manager._removedDiscoveryTopics.size()==1);
            assert(device.entities.at(id)==&entity);
            manager.reconnect(); // broker unavailable: deletion must remain queued
            assert(!manager._mqtt.connected());
            manager._mqtt.allowConnect=true; manager.reconnect();
            assert(retained.count(oldTopic)==0 && retained.at(activeTopic)=="config");
            assert(retained.at("unrelated/config")=="keep");
            assert(manager.publishState(entity,"19.5"));
            assert(retained.at(entity.stateTopic.full)=="19.5");
            // A reconnect must replay the cleanup and preserve the active entity.
            retained[oldTopic]="stale";
            manager._mqtt.online=false; manager.reconnect();
            assert(retained.count(oldTopic)==0 && retained.at(activeTopic)=="config");
            assert(device.entities.at(id)==&entity);
        }
    }
    // Custom discovery domains must also be preserved by the cleanup overload.
    std::map<String,String> retained; Manager manager(retained); MqttDevice device;
    MqttEntity custom; custom.id="temperature"; custom.domain="custom"; custom.component="number";
    manager.removeEntityDiscovery(device,custom,"sensor");
    assert(manager._removedDiscoveryTopics[0]=="custom/sensor/openFrisquetVisio/temperature/config");
    puts("OK: sensor/number cleanup in both directions, offline boot, reconnect, active registry/state preserved");
}
'''

with tempfile.TemporaryDirectory(prefix='frisquet-discovery-cleanup-') as directory:
    cpp = Path(directory) / 'test.cpp'
    cpp.write_text(code)
    binary = Path(directory) / 'test'
    subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
