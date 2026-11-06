"""Native checks of the actual Zone mode/override/schedule methods (ASan/UBSan)."""
from pathlib import Path
import subprocess
import tempfile
import re

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/Frisquet/Zone.cpp').read_text()
header = (ROOT / 'src/Frisquet/Zone.h').read_text()

def extract(signature, text=source):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

prefix = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <time.h>
using byte = unsigned char;
class String {
    char value[256] = {};
public:
    String() = default;
    String(unsigned n){snprintf(value,sizeof(value),"%u",n);}
    size_t length() const {return strlen(value);}
    String& operator+=(const String& other){assert(length()+other.length()<sizeof(value));strcat(value,other.value);return *this;}
    friend String operator+(String a,const String& b){a+=b;return a;}
    String(const char* s){assert(strlen(s)<sizeof(value));strcpy(value,s);}
    bool equalsIgnoreCase(const char* s) const {return !strcasecmp(value,s);}
    bool operator==(const String& s) const {return !strcmp(value,s.value);}
    const char* c_str() const {return value;}
};
namespace std {
    template<class T> class initializer_list {
        const T* data;size_t count;
        constexpr initializer_list(const T* p,size_t n):data(p),count(n){}
    public:
        constexpr initializer_list():data(nullptr),count(0){}
        const T* begin()const{return data;}const T* end()const{return data+count;}
    };
}
struct Fields {
    String keys[12],values[12];size_t count=0;
    void set(const String& key,const String& value){
        for(size_t i=0;i<count;++i)if(keys[i]==key){values[i]=value;return;}
        assert(count<12);keys[count]=key;values[count++]=value;
    }
    void erase(const String& key){for(size_t i=0;i<count;++i)if(keys[i]==key){keys[i]=keys[count-1];values[i]=values[--count];return;}}
    String get(const String& key){for(size_t i=0;i<count;++i)if(keys[i]==key)return values[i];return String();}
};
struct MqttTopic {String full;MqttTopic(){}MqttTopic(const String& s,int=0,bool=true):full(s){}};
struct MqttEntity {
    String value;Fields extraFields;MqttTopic availabilityTopic,stateTopic;
    void set(const String& key,const String& value){extraFields.set(key,value);}
    void setRaw(const String& key,const String& value){set(key,value);}
};
struct MqttDevice {String baseTopic="frisquet";};
struct MqttManager {
    MqttDevice device;unsigned discoveries=0;
    bool connected(){return true;}
    void publishState(MqttEntity& entity,const String& value){entity.value=value;}
    void publishAvailability(MqttEntity&,bool){}
    void registerEntity(MqttDevice&,MqttEntity&,bool){++discoveries;}
    MqttDevice* getDevice(const char*){return &device;}
    static String compose(std::initializer_list<String> parts){String out;for(const auto& part:parts){if(out.length())out+="/";out+=part;}return out;}
};
struct Preferences {};
uint32_t tick=1;
uint32_t millis(){return ++tick;}
unsigned saves=0,publishes=0;
time_t clockNow=0;
bool clockValid=false;
enum {timeNotSet,timeSet};
int timeStatus(){return clockValid?timeSet:timeNotSet;}
time_t now(){return clockNow;}
unsigned year(time_t t){return gmtime(&t)->tm_year+1900;}
unsigned weekday(time_t t){return gmtime(&t)->tm_wday+1;}
unsigned hour(time_t t){return gmtime(&t)->tm_hour;}
unsigned minute(time_t t){return gmtime(&t)->tm_min;}
unsigned second(time_t t){return gmtime(&t)->tm_sec;}
void info(const char*,...){}
namespace std { template<class T> T min(T a,T b){return a<b?a:b;} template<class T> T max(T a,T b){return a>b?a:b;} }
'''
class_code = re.sub(r'^#.*\n', '', header, flags=re.M).replace('    private:', '    public:')
methods = [
    'Zone::MODE_ZONE Zone::getMode(', 'void Zone::setMode(MODE_ZONE',
    'String Zone::getNomMode(', 'byte Zone::getModeOptions(', 'void Zone::setModeOptions(',
    'void Zone::activerBoost(', 'void Zone::desactiverBoost(', 'bool Zone::boostActif(',
    'bool Zone::confortActif(', 'bool Zone::reduitActif(', 'bool Zone::horsGelActif(',
    'bool Zone::derogationActive(', 'bool Zone::programmeDisponible(',
    'bool Zone::regimeProgramme(', 'void Zone::setPilotageActif(',
    'void Zone::actualiserConsigne(', 
    'bool Zone::appliquerPreset(', 'bool Zone::appliquerConsigne(', 'void Zone::loop(', 'void Zone::confirmerMode(MODE_ZONE',
    'void Zone::publierMode(', 'void Zone::actualiserDecouverte(',
]
for name in ['Confort','Reduit','HorsGel','Consigne','Boost']:
    methods += [f'void Zone::setTemperature{name}(', f'float Zone::getTemperature{name}(']
helpers = source[source.index('namespace {\nString nomPreset'):source.index('void Zone::loadConfig()')]
code = prefix + '\n#include "Frisquet/ZoneProgram.h"\n' + class_code + '\n' + helpers
code += '\n'.join(extract(method) for method in methods)
code += r'''
struct Config {
    bool connect=false,passive=false;
    bool virtualZ1=false;
    bool useConnect(){return connect;}
    bool useConnectPassive(){return passive;}
    bool useSatelliteVirtualZ1(){return virtualZ1;}
    bool useSatelliteVirtualZ2(){return false;}
    bool useSatelliteVirtualZ3(){return false;}
    bool useSatelliteZ1(){return true;}
    bool useSatelliteZ2(){return false;}
    bool useSatelliteZ3(){return false;}
};
constexpr int RADIOLIB_ERR_NONE=0, RADIOLIB_SX126X_MAX_PACKET_LENGTH=255;
constexpr byte ID_ZONE_1=8, ID_ZONE_2=9, ID_ZONE_3=10, ID_CHAUDIERE=0x80;
void delay(unsigned){}
void error(const char*,...){}
struct temperature8 {
    byte value=0;
    temperature8& operator=(float v){value=static_cast<byte>((v-5)*10);return *this;}
};
struct Radio {
    int16_t result=RADIOLIB_ERR_NONE;
    unsigned calls=0;
    byte sentMode=0,sentOptions=0;
    int16_t sendInit(byte,byte,byte,byte,byte,uint16_t,uint16_t,uint16_t,uint16_t,
                     byte* payload,uint8_t count,byte*,size_t&){
        ++calls;assert(count==48);sentMode=payload[3];sentOptions=payload[4];
        return result;
    }
};
struct Connect {
    bool associated=false;Config config;Radio transport;
    bool estAssocie(){return associated;}
    Config& getConfig(){return config;}
    Radio& radio(){return transport;}
    byte getId(){return 0x7E;}
    byte getIdAssociation(){return 0xAC;}
    byte incrementIdMessage(){return 1;}
    bool envoyerZone(Zone&);
};
'''
code += extract('bool Connect::envoyerZone(', (ROOT/'src/Frisquet/Connect.cpp').read_text())
code += extract('static String programmationReadOnlyReason(', (ROOT/'src/Portal.cpp').read_text())
code += r'''
void Zone::saveConfig(){++saves;}
void Zone::publishMqtt(){++publishes;actualiserDecouverte();publierMode();}
uint8_t Zone::getNumeroZone(){return 1;}
uint8_t Zone::getIdZone(){return _idZone;}
void initialize(Zone& z,Zone::SOURCE source){
    z.setSource(source);z.setPilotageActif(true);z._mqttPret=true;
    z.setTemperatureConfort(20);z.setTemperatureReduit(17);z.setTemperatureHorsGel(8);z.setTemperatureBoost(2);
}
int main(){
    // Bits and chronological week/day boundaries.
    uint8_t bytes[42]={};bytes[0]=1;bytes[41]=128;
    assert(ZoneProgram::comfort(bytes,0));assert(!ZoneProgram::comfort(bytes,1));
    assert(ZoneProgram::comfort(bytes,335));assert(ZoneProgram::comfort(bytes,336));
    assert(ZoneProgram::nextTransition(bytes,335,120)==3480);
    memset(bytes,0,42);assert(!ZoneProgram::nextTransition(bytes,0,0));
    memset(bytes,255,42);assert(!ZoneProgram::nextTransition(bytes,335,1799));
    for(unsigned slot=0;slot<336;++slot){
        memset(bytes,0,42);bytes[slot/8]=1U<<(slot%8);
        assert(ZoneProgram::comfort(bytes,slot));
        assert(ZoneProgram::nextTransition(bytes,slot,1799)==1);
    }
    MqttManager mqtt;
    // Sortie d'Auto : ne pas conserver le bit 0x20 dans les options A154.
    Zone transition(8,mqtt);
    for(byte rawAuto : {0x20, 0x21, 0x24, 0x25, 0x27}) {
        transition.setModeOptions(rawAuto);
        transition.setMode(Zone::CONFORT);
        assert(transition.getModeOptions()==0x01);
        transition.setModeOptions(rawAuto);
        transition.setMode(Zone::REDUIT);
        assert(transition.getModeOptions()==0x00);
        transition.setModeOptions(rawAuto);
        transition.setMode(Zone::HORS_GEL);
        assert(transition.getModeOptions()==0x10);
    }
    transition.setModeOptions(0xED); // Auto, Boost et bits non interprétés.
    transition.setMode(Zone::CONFORT);
    assert(transition.getModeOptions()==0xC9);
    transition.setModeOptions(0x25);
    transition.setMode(Zone::AUTO, true);
    assert(transition.getModeOptions()==0x25);
    Zone unknown(8,mqtt);unknown.setSource(Zone::SATELLITE_VIRTUEL);unknown.setPilotageActif(true);
    assert(!unknown.appliquerPreset("Confort permanent"));assert(unknown.getMode()==Zone::INCONNU);
    Zone z(8,mqtt);initialize(z,Zone::SATELLITE_VIRTUEL);
    z.actualiserDecouverte();
    assert(!strstr(z._mqttEntities.thermostat.extraFields.get("preset_modes").c_str(),"Dérogation"));
    assert(z._mqttEntities.thermostat.extraFields.get("mode_command_topic")=="frisquet/z1/thermostat/set");
    const unsigned discoveries=mqtt.discoveries;z.actualiserDecouverte();assert(mqtt.discoveries==discoveries);
    assert(!z.appliquerPreset("Auto"));assert(!z.appliquerPreset("Dérogation Confort"));
    assert(z.appliquerPreset("Confort permanent"));assert(z.getMode()==Zone::CONFORT);
    assert(z.getNomMode()=="Confort");assert(z.getTemperatureConsigne()==20);
    assert(z._mqttEntities.thermostat.value=="heat");
    assert(z.appliquerPreset("Réduit"));assert(z.getNomMode()=="Réduit");
    assert(z.getTemperatureConsigne()==17);assert(z._mqttEntities.regime.value=="Réduit");
    assert(z.appliquerPreset("Hors Gel"));assert(z.getTemperatureConsigne()==8);
    assert(z.appliquerPreset("Boost"));assert(z.getNomMode()=="Boost");
    assert(z.getTemperatureConsigne()==22);assert(z._mqttEntities.mode.value=="Boost");
    z.setTemperatureConfort(30);z.actualiserConsigne();assert(z.getTemperatureConsigne()==30);
    z.setTemperatureBoost(5);
    z.setTemperatureConfort(28);z.actualiserConsigne();assert(z.getTemperatureConsigne()==30);
    z.setTemperatureConfort(30);z.actualiserConsigne();assert(z.getTemperatureConsigne()==30);
    z.setTemperatureBoost(2);
    z.setTemperatureConfort(20);
    assert(z.appliquerConsigne(23));assert(z.getTemperatureConsigne()==23);assert(z.getTemperatureConfort()==21);
    assert(!z.appliquerConsigne(NAN));assert(!z.appliquerConsigne(31));assert(!z.appliquerConsigne(5));
    z.setTemperatureConfort(20);
    assert(z.appliquerPreset("none"));assert(!z.boostActif());assert(z.getMode()==Zone::CONFORT);
    assert(z.appliquerPreset("Boost"));
    const unsigned before=saves;assert(!z.appliquerPreset("invalid"));assert(saves==before);
    z.setPilotageActif(false);assert(!z.appliquerPreset("Réduit"));assert(z.boostActif());
    assert(!z.appliquerConsigne(19));
    assert(!z._mqttEntities.thermostat.extraFields.get("mode_command_topic").length());
    assert(!z._mqttEntities.thermostat.extraFields.get("temperature_command_topic").length());
    assert(!z._mqttEntities.thermostat.extraFields.get("preset_mode_command_topic").length());
    assert(!z._mqttEntities.thermostat.extraFields.get("preset_modes").length());
    assert(!z._mqttEntities.thermostat.extraFields.get("preset_mode_state_topic").length());
    z.setPilotageActif(true);
    Zone::Programmation program;memset(&program,0,sizeof(program));
    program.dimanche[0]=0x0C; // reduced 00:00-01:00, comfort 01:00-02:00
    z.modifierProgrammation(program);assert(!z.programmationEnvoiEnAttente());
    struct tm sunday={};sunday.tm_year=126;sunday.tm_mon=9;sunday.tm_mday=25;
    clockNow=timegm(&sunday)+15*60;clockValid=true;
    assert(z.appliquerPreset("Auto"));assert(!z.confortActif());assert(!z.boostActif());
    assert(z._mqttEntities.thermostat.value=="auto");
    assert(z.appliquerPreset("Dérogation Confort"));
    assert(strstr(z._mqttEntities.thermostat.extraFields.get("preset_modes").c_str(),"Dérogation Confort"));
    assert(z.derogationActive()&&z.confortActif());
    assert(z._mqttEntities.mode.value=="Dérogation Confort");
    const uint32_t end=z._derogationFin;
    assert(end==uint32_t(timegm(&sunday)+3600)); // not the 00:30 slot boundary
    clockNow+=15*60;z.loop();assert(z.derogationActive());
    // Restart with the persisted deadline: still expires at the same time.
    Zone restored(8,mqtt);initialize(restored,Zone::SATELLITE_VIRTUEL);
    restored.setProgrammation(program,true);restored.setMode(Zone::AUTO,true,true);restored._derogationFin=end;
    restored.loop();assert(restored.derogationActive());
    clockNow=end;restored.loop();assert(!restored.derogationActive());assert(restored.confortActif());
    assert(restored.appliquerPreset("Dérogation Réduit"));assert(!restored.confortActif());
    assert(restored.getTemperatureConsigne()==17);
    assert(restored.appliquerPreset("Auto"));assert(!restored.derogationActive());assert(restored.confortActif());
    clockNow=timegm(&sunday)+7200;restored.loop();assert(restored.reduitActif());
    assert(restored.getTemperatureConsigne()==17);
    // No clock: do not invent a schedule or expire an override.
    assert(restored.appliquerPreset("Dérogation Confort"));clockValid=false;
    clockNow+=7200;restored.loop();assert(restored.derogationActive());
    assert(!restored.appliquerPreset("Auto"));clockValid=true;restored.loop();
    // A constant week cannot provide a temporary override expiry.
    memset(&program,0,sizeof(program));z.setProgrammation(program,true);
    assert(z.appliquerPreset("Auto"));assert(!z.appliquerPreset("Dérogation Confort"));
    // Remote Connect keeps native override timing and confirms only radio feedback.
    Zone remote(8,mqtt);initialize(remote,Zone::CONNECT);
    remote.confirmerMode(Zone::AUTO,4);assert(remote._mqttEntities.mode.value=="Auto");
    assert(remote.appliquerPreset("Dérogation Confort"));assert(remote.derogationActive());
    assert(remote._mqttEntities.mode.value=="Auto");
    remote.confirmerMode(Zone::AUTO,7);assert(remote._mqttEntities.mode.value=="Dérogation Confort");
    remote.loop();assert(remote.derogationActive());
    remote.confirmerMode(Zone::CONFORT,1);assert(remote._mqttEntities.thermostat.value=="heat");
    // HA mode commands are confirmed on a successful A154 response alone.
    Connect sender;sender.associated=true;
    remote.setProgrammation(program,true);
    for(const char* preset : {"Confort", "Réduit", "Auto", "Dérogation Confort", "Boost", "Hors Gel"}) {
        assert(remote.appliquerPreset(preset));
        sender.transport.result=-1;
        const String previous=remote._mqttEntities.mode.value;
        assert(!sender.envoyerZone(remote));
        assert(remote._mqttEntities.mode.value==previous);
        sender.transport.result=RADIOLIB_ERR_NONE;
        assert(sender.envoyerZone(remote));
        assert(remote._mqttEntities.mode.value==preset);
        assert(remote._modeObserve==sender.transport.sentMode);
        assert(remote._optionsObservees==sender.transport.sentOptions);
    }
    assert(remote.appliquerPreset("Confort"));
    sender.associated=false;assert(!sender.envoyerZone(remote));
    assert(remote._mqttEntities.mode.value=="Hors Gel");
    sender.associated=true;sender.config.virtualZ1=true;
    const unsigned calls=sender.transport.calls;
    assert(sender.envoyerZone(remote));assert(sender.transport.calls==calls);
    assert(remote._mqttEntities.mode.value=="Hors Gel");
    // Physical override uses local programme timing, passive physical is read-only.
    Zone physical(8,mqtt);initialize(physical,Zone::SATELLITE_PHYSIQUE);
    physical.setPilotageActif(false);assert(!physical.appliquerPreset("Boost"));
    physical.confirmerMode(Zone::AUTO,6);assert(physical._mqttEntities.mode.value=="Dérogation Réduit");
    physical.setPilotageActif(true);assert(!physical.appliquerPreset("Auto"));
    assert(physical.appliquerPreset("Boost"));assert(physical._mqttEntities.mode.value=="Dérogation Réduit");
    physical.confirmerMode();assert(physical._mqttEntities.mode.value=="Boost");
    Config cfg;Connect bridge;
    assert(programmationReadOnlyReason(cfg,bridge,z,true)=="");
    assert(!(programmationReadOnlyReason(cfg,bridge,z,false)==""));
    assert(!(programmationReadOnlyReason(cfg,bridge,physical,true)==""));
    cfg.connect=true;cfg.passive=true;
    assert(!(programmationReadOnlyReason(cfg,bridge,remote,true)==""));
    cfg.passive=false;assert(!(programmationReadOnlyReason(cfg,bridge,remote,true)==""));
    bridge.associated=true;remote.setMode(Zone::CONFORT);
    assert(programmationReadOnlyReason(cfg,bridge,remote,true)=="");
    puts("OK: native modes, Boost once, local schedule/expiry/restart, remote confirmation, passive controls");
}
'''
with tempfile.TemporaryDirectory(prefix='frisquet-zone-modes-') as temp:
    cpp=Path(temp)/'test.cpp';cpp.write_text(code)
    binary=Path(temp)/'test'
    subprocess.run(['c++','-std=c++11','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(ROOT/'src'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
