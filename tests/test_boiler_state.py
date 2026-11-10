"""Check actual boiler state decoding and independent MQTT anomaly publication."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / 'src/Frisquet/Chaudiere.h').read_text()
source = (ROOT / 'src/Frisquet/Chaudiere.cpp').read_text()

def extract(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

publish = source[source.index('    _mqtt.publishState(_mqttEntities.etatChaudiere'):source.index('    const bool temperatureEcsDisponible')]
assert '_mqttEntities.modeFonctionnement' not in publish
assert '_mqtt.removeEntityDiscovery(*device, _mqttEntities.modeFonctionnement);' in source
code = r'''
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
using byte = unsigned char;
using boolean = bool;
class String { public:
    const char* value;
    String(const char* s):value(s){}
    const char* c_str()const{return value;}
    bool operator==(const char* s)const{return !strcmp(value,s);}
};
struct Mqtt {
    const char* states[2]={};
    void publishState(int id,const String& value){states[id]=value.c_str();}
};
class Chaudiere {public:
''' + extract(header, 'struct ETAT_CHAUDIERE') + ';\n' + extract(header, 'enum MODE_FONCTIONNEMENT') + r''';
    MODE_FONCTIONNEMENT _modeFonctionnement=FONCTIONNEMENT_INCONNU;
    uint16_t _modeFonctionnementRaw=0xFFFF;
    ETAT_CHAUDIERE _etatChaudiere;
    bool _etatChaudiereConnu=false;
    float ecs=NAN,heating=NAN;
    Mqtt _mqtt;
    struct {int etatChaudiere=0,anomalieChaudiere=1;} _mqttEntities;
    MODE_FONCTIONNEMENT getModeFonctionnement()const{return _modeFonctionnement;}
    float getPuissanceInstantaneeECS()const{return ecs;}
    float getPuissanceInstantaneeChauffage()const{return heating;}
    ETAT_CHAUDIERE getEtatChaudiere()const{return _etatChaudiere;}
    bool etatChaudiereConnu()const{return _etatChaudiereConnu;}
    void setModeFonctionnement(uint16_t);
    String getNomEtatChaudiere()const;
''' + extract(header, 'void setEtatChaudiere(') + '\nvoid publish(){\n' + publish + '\n}\n};\n'
code += extract(source, 'void Chaudiere::setModeFonctionnement(') + '\n'
code += extract(source, 'String Chaudiere::getNomEtatChaudiere(') + r'''
int main(){
    Chaudiere b;
    assert(b.getNomEtatChaudiere()=="Inconnu");b.publish();
    assert(!strcmp(b._mqtt.states[0],"Inconnu"));
    assert(!strcmp(b._mqtt.states[1],"None"));
    // Issue #44 : satellite seul, état reçu sans mode détaillé Connect.
    b.setEtatChaudiere(0x08);assert(b.getNomEtatChaudiere()=="Fonctionnement");
    b.publish();assert(!strcmp(b._mqtt.states[0],"Fonctionnement"));
    assert(!strcmp(b._mqtt.states[1],"OFF"));
    b.setEtatChaudiere(0x09);b.publish();
    assert(!strcmp(b._mqtt.states[0],"Fonctionnement"));assert(!strcmp(b._mqtt.states[1],"ON"));
    b.setEtatChaudiere(0x00);assert(b.getNomEtatChaudiere()=="Veille");
    b.setEtatChaudiere(0x20);assert(b.getNomEtatChaudiere()=="Veille");
    b.publish();assert(!strcmp(b._mqtt.states[1],"OFF"));
    b.setEtatChaudiere(0x28);assert(b.getNomEtatChaudiere()=="Fonctionnement");
    b.setModeFonctionnement(0x05);assert(b.getNomEtatChaudiere()=="Chauffage");
    b.setModeFonctionnement(0x07);assert(b.getNomEtatChaudiere()=="Chauffage ECS");
    // Production ECS observée en conditions réelles : corps de chauffe à 87 °C,
    // puissance ECS non nulle, bit fonctionnement à 0 (zone chauffage au repos).
    b.setEtatChaudiere(0x20);assert(b.getNomEtatChaudiere()=="Chauffage ECS");
    b.publish();assert(!strcmp(b._mqtt.states[0],"Chauffage ECS"));
    b.setEtatChaudiere(0x2D); // anomaly + heating disabled: ECS can still operate
    assert(b.getNomEtatChaudiere()=="Chauffage ECS");b.publish();
    assert(!strcmp(b._mqtt.states[0],"Chauffage ECS"));assert(!strcmp(b._mqtt.states[1],"ON"));
    b.setModeFonctionnement(0x05);assert(b.getNomEtatChaudiere()=="Veille");
    b.setEtatChaudiere(0x29);assert(b.getNomEtatChaudiere()=="Chauffage");
    assert(!(b.getEtatChaudiere().getLibelle()=="Anomalie"));
    // Instantaneous powers must never change the radio-derived state.
    const float powers[]={0,12,18,-1,NAN,INFINITY};
    for(float ecs:powers)for(float heating:powers){
        b.ecs=ecs;b.heating=heating;
        b.setEtatChaudiere(0x29);b.setModeFonctionnement(0x05);
        assert(b.getNomEtatChaudiere()=="Chauffage");b.publish();
        assert(!strcmp(b._mqtt.states[0],"Chauffage"));assert(!strcmp(b._mqtt.states[1],"ON"));
        b.setModeFonctionnement(0x07);assert(b.getNomEtatChaudiere()=="Chauffage ECS");
        // Relance ECS : le bit fonctionnement (zone chauffage) reste à 0.
        b.setEtatChaudiere(0x20);assert(b.getNomEtatChaudiere()=="Chauffage ECS");
        b.setModeFonctionnement(0x05);assert(b.getNomEtatChaudiere()=="Veille");
        b.setEtatChaudiere(0x28);b.setModeFonctionnement(0);assert(b.getNomEtatChaudiere()=="Fonctionnement");
    }
    b.setEtatChaudiere(0x20);b.publish();assert(!strcmp(b._mqtt.states[1],"OFF"));
    puts("OK: heating/DHW/idle/unknown, powers ignored, independent anomaly and recovery");
}
'''
manager = (ROOT / 'src/MQTT/MqttManager.h').read_text()
removal = extract(manager, 'void removeEntityDiscovery(')
cleanup = 'for (const auto& topic : _removedDiscoveryTopics) publishRaw(topic, "", 0, true);'
assert cleanup in manager[manager.index('  void reconnect()'):]
migration_code = r'''
#include <assert.h>
#include <string.h>
struct String {
    const char* value="";
    String()=default;String(const char* s):value(s){}
    bool operator==(const String& other)const{return !strcmp(value,other.value);}
};
struct Topics {
    String entries[4];unsigned count=0;
    const String* begin()const{return entries;}const String* end()const{return entries+count;}
    void push_back(const String& topic){assert(count<4);entries[count++]=topic;}
};
struct MqttDevice {struct {bool erased=false;void erase(const String&){erased=true;}} entities;};
struct MqttEntity {
    String id="modeFonctionnement";MqttDevice* device=nullptr;
    String discoveryTopic(){assert(device);return "homeassistant/sensor/openFrisquetVisio/modeFonctionnement/config";}
};
struct MqttManager {
    Topics _removedDiscoveryTopics;bool registered=false,online=false;unsigned published=0;
    bool isRegistered(MqttDevice*){return registered;}
    void registerDevice(MqttDevice&){registered=true;}
    bool publishRaw(const String& topic,const String& value,int qos,bool retain){
        if(!online)return false;
        assert(topic=="homeassistant/sensor/openFrisquetVisio/modeFonctionnement/config");
        assert(value=="" && qos==0 && retain);++published;return true;
    }
''' + removal + '\nvoid reconnect(){online=true;' + cleanup + r'''}
};
int main(){
    MqttManager manager;MqttDevice device;MqttEntity entity;
    manager.removeEntityDiscovery(device,entity);
    assert(manager.registered && device.entities.erased);
    assert(manager.published==0 && manager._removedDiscoveryTopics.count==1);
    manager.removeEntityDiscovery(device,entity);assert(manager._removedDiscoveryTopics.count==1);
    manager.reconnect();assert(manager.published==1);
    manager.online=false;manager.reconnect();assert(manager.published==2);
}
'''
with tempfile.TemporaryDirectory(prefix='frisquet-boiler-state-') as directory:
    cpp=Path(directory)/'test.cpp';cpp.write_text(code)
    binary=Path(directory)/'test'
    subprocess.run(['c++','-std=c++11','-fsanitize=address,undefined',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

with tempfile.TemporaryDirectory(prefix='frisquet-discovery-retirement-') as directory:
    cpp=Path(directory)/'test.cpp';cpp.write_text(migration_code)
    binary=Path(directory)/'test'
    subprocess.run(['c++','-std=c++11','-fsanitize=address,undefined',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('OK: obsolete discovery deletion is retained, deduplicated and replayed after connecting')
