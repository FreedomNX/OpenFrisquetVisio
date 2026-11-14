"""Native tests of satellite slots, the actual loop and bounded radio retries."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def extract(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


radio = re.sub(r'^#.*\n', '', (ROOT / 'src/Frisquet/FrisquetRadio.h').read_text(), flags=re.M)
satellite_fields = (ROOT / 'src/Frisquet/Satellite.h').read_text().split(
    '        RadioSchedule _consigneSchedule;')[1].split('        MODE decodeModeFromRaw')[0]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <vector>
using byte=uint8_t;
uint32_t tick=0,wallOffset=0;
bool synced=true;
uint32_t millis(){return tick;}
void delay(uint32_t ms){tick+=ms;}
enum {timeNotSet,timeSet};
int timeStatus(){return synced ? timeSet : timeNotSet;}
uint32_t now(){return wallOffset+tick/1000;}
void info(const char*,...){}
void error(const char*,...){}
void logRadio(bool,byte*,size_t,float=0){}
constexpr int RADIOLIB_ERR_NONE=0,RADIOLIB_ERR_RX_TIMEOUT=-6;
constexpr int RADIOLIB_ERR_TX_TIMEOUT=-5,RADIOLIB_ERR_ADDRESS_NOT_FOUND=-7;
constexpr size_t RADIOLIB_SX126X_MAX_PACKET_LENGTH=255;
struct NetworkID {byte bytes[4];};
''' + extract('src/Frisquet/Utils.h', 'struct fword') + r''';
#include "Frisquet/RadioSchedule.h"
struct Radio {
    bool success=true;
    unsigned rearms=0;
    std::vector<uint32_t> sent;
    byte reply[7]={};
    int transmit(byte* data,size_t n){
        assert(n>=6);sent.push_back(tick);
        memcpy(reply,data,6);reply[0]=data[1];reply[1]=data[0];reply[4]|=0x80;
        delay(12);return 0;
    }
    int startReceive(){++rearms;return 0;}
    bool hasReceivedPacket(){return success;}
    int readData(byte* out,size_t){memcpy(out,reply,7);return 0;}
    int receive(byte* out,size_t n){
        if(success)return readData(out,n);
        delay(408);return RADIOLIB_ERR_RX_TIMEOUT;
    }
    size_t getPacketLength(){return 7;}
    float getLastPacketRSSI(){return -100;}
};
''' + radio + r'''
volatile bool FrisquetRadio::receivedFlag=false;
volatile bool FrisquetRadio::interruptReceive=false;
bool FrisquetRadio::bufferUnexpectedPacket(const byte*,size_t){return false;}
''' + '\n'.join(extract('src/Frisquet/FrisquetRadio.cpp', method) for method in (
    'int16_t FrisquetRadio::sendAsk(', 'int16_t FrisquetRadio::sendInit(',
    'int16_t FrisquetRadio::receiveExpected(')) + r'''
constexpr byte ID_ZONE_1=8;
struct Zone {
    enum class MODE_ZONE {INCONNU,CONFORT};
    float ambient=20,setpoint=19;
    MODE_ZONE mode=MODE_ZONE::CONFORT;
    unsigned confirmations=0;
    uint32_t lastChange=0,lastEnvoi=0;
    float getTemperatureAmbiante(){return ambient;}
    float getTemperatureConsigne(){return setpoint;}
    MODE_ZONE getMode(){return mode;}
    uint32_t getLastChange(){return lastChange;}
    uint32_t getLastEnvoi(){return lastEnvoi;}
    void refreshLastEnvoi(){++confirmations;lastEnvoi=millis();}
    void publishMqtt(){}
};
struct Config {bool connect=true;bool useConnect(){return connect;}};
struct Boiler {void publishMqtt(){}};
struct Satellite {
    Zone _zone;Config cfg;Boiler _chaudiere;FrisquetRadio radioMock;
    byte zone;
    bool associated=true;
    unsigned dates=0,infos=0,consignes=0;
    Satellite(byte n):zone(n){_modeVirtuel=true;}
    byte getId(){return 7+zone;}
    byte getNumeroZone(){return zone;}
    bool estAssocie(){return associated;}
    Config& getConfig(){return cfg;}
    FrisquetRadio& radio(){return radioMock;}
    void publishMqtt(){}
    void incrementIdMessage(byte){}
    bool exchange(bool write){
        byte response[255]={};size_t length=sizeof(response);byte payload[8]={};
        if(write)return radio().sendInit(getId(),0x80,1,4,1,0xA029,21,
            0xA02F+5*(zone-1),4,payload,8,response,length)==0;
        return radio().sendAsk(getId(),0x80,1,4,1,0xA029,21,response,length)==0;
    }
    bool recupererDate(){++dates;return exchange(false);}
    bool recupererInfosChaudiere(){++infos;return exchange(false);}
    bool envoyerConsigne(){++consignes;return exchange(true);}
    bool recupererTemperatureCDC(){return true;}
    void loop();
    RadioSchedule _consigneSchedule;
''' + satellite_fields + '\n};\n' + extract('src/Frisquet/Satellite.cpp', 'void Satellite::loop(') + r'''
int main(){
    // Exact boundaries, separated by 24 seconds, every ten minutes.
    for(byte zone=1;zone<=3;++zone){
        const uint32_t begin=30+24*(zone-1),end=begin+24;
        for(uint32_t sec=0;sec<1200;++sec){
            const bool expected=sec%600>=begin && sec%600<end;
            assert(RadioSchedule::satelliteWindow(sec,zone)==expected);
        }
        RadioSchedule schedule;
        assert(!schedule.dueSatelliteAt(begin-1,1000,zone));
        assert(schedule.dueSatelliteAt(begin,2000,zone));
        assert(!schedule.dueSatelliteAt(begin+1,3000,zone));
        assert(!schedule.dueSatelliteAt(end,26000,zone));
        assert(schedule.dueSatelliteAt(begin+600,602000,zone));
        assert(!schedule.dueSatelliteAt(begin+1200,603000,zone)); // clock jump
        RadioSchedule wrap;
        assert(wrap.dueSatelliteAt(begin,UINT32_MAX-1000,zone));
        assert(wrap.dueSatelliteAt(begin+600,598999,zone));
    }
    assert(!RadioSchedule::satelliteWindow(30,0));
    assert(!RadioSchedule::satelliteWindow(30,4));
    RadioSchedule readings;
    assert(!readings.dueAt(30,0,600,30)); // protected slots remain silent
    assert(readings.dueAt(180,1000,600,180));
    assert(!readings.dueAt(181,2000,600,180));
    assert(readings.dueAt(780,601000,600,180));

    // Actual radio retries stop at the deadline, including ACK waits.
    for(bool write: {false,true})for(uint32_t start: {0U,UINT32_MAX-100U}){
        tick=start;FrisquetRadio radio;radio.success=false;
        byte payload[8]={},response[255]={};size_t length=255;
        {
            FrisquetRadio::TransmitWindow window(radio,1000);
            const int result=write
                ? radio.sendInit(8,0x80,1,4,1,0xA029,21,0xA02F,4,payload,8,response,length)
                : radio.sendAsk(8,0x80,1,4,1,0xA029,21,response,length);
            assert(result!=0 && !radio.sent.empty());
            for(uint32_t at:radio.sent)assert(uint32_t(at-start)<900);
            assert(uint32_t(tick-start)<=1030);
            const auto count=radio.sent.size();
            delay(1100);assert(radio.transmit(payload,8)!=0);
            assert(radio.sent.size()==count);
        }
        assert(radio.transmit(payload,8)==0); // scope restored for other devices
    }

    // Real Satellite::loop: bootstrap, one success per cycle, retries after
    // failures only inside the zone's slot, and incomplete data reads.
    for(byte zone=1;zone<=3;++zone)for(bool success: {false,true}){
        tick=0;wallOffset=0;Satellite s(zone);s.radioMock.success=success;
        for(uint32_t step=0;step<13000;++step){
            s.loop();delay(100);
            if(tick>=1300000)break;
        }
        assert(!s.radioMock.sent.empty());
        for(uint32_t at:s.radioMock.sent)
            assert(RadioSchedule::satelliteWindow(at/1000,zone));
        if(success)assert(s._zone.confirmations==3 && s.dates==1);
        else assert(s._zone.confirmations==0 && s.dates>3);
    }
    tick=0;Satellite incomplete(2);incomplete._zone.ambient=NAN;
    tick=54000;incomplete.loop();incomplete.loop();
    assert(incomplete.dates==1 && incomplete.infos==1 && incomplete.consignes==0);
    incomplete.loop();assert(incomplete.infos==1);
    // A setpoint failure is retried on the next loop in the same window.
    tick=30000;Satellite retrying(1);retrying._initialReadPending=false;
    retrying.radioMock.success=false;retrying.loop();
    assert(retrying.consignes==1 && retrying._zone.confirmations==0);
    retrying.radioMock.success=true;retrying.loop();
    assert(retrying.consignes==2 && retrying._zone.confirmations==1);
    retrying.loop();assert(retrying.consignes==2);
    tick=630000;retrying.loop();assert(retrying.consignes==3);
    // A late start cannot transmit into Z2, and resumes in the next Z1 slot.
    tick=52000;Satellite late(1);late._initialReadPending=false;
    late.radioMock.success=false;late.loop();
    for(auto at:late.radioMock.sent)assert(at<54000);
    tick=54000;const auto count=late.radioMock.sent.size();late.loop();
    assert(late.radioMock.sent.size()==count);
    tick=630000;late.radioMock.success=true;late.loop();
    assert(late._zone.confirmations==1);
    // Manual changes bypass the slots, including before initial discovery.
    // They do not shift the next periodic slot and do not flood on failure.
    for(byte zone=1;zone<=3;++zone){
        tick=200000;Satellite manual(zone);manual._zone.lastChange=tick;
        assert(!RadioSchedule::satelliteWindow(tick/1000,zone));
        manual.loop();assert(manual.consignes==1 && manual.dates==0);
        assert(manual._zone.confirmations==1);
        manual.loop();assert(manual.consignes==1);
        tick=210000;manual._zone.lastChange=tick;manual.loop();
        assert(manual.consignes==2 && manual._zone.confirmations==2);
        tick=(600+30+24*(zone-1))*1000;manual.loop();
        assert(manual.consignes==3); // original fixed slot

        tick=200000;Satellite failed(zone);failed._initialReadPending=false;
        failed._zone.lastChange=tick;failed.radioMock.success=false;
        failed.loop();assert(failed.consignes==1 && failed._zone.confirmations==0);
        failed.loop();assert(failed.consignes==1);
        tick=(600+30+24*(zone-1))*1000;failed.radioMock.success=true;failed.loop();
        assert(failed.consignes==2 && failed._zone.confirmations==1);
    }
    tick=30000;Satellite sameSlot(1);sameSlot._zone.lastChange=tick;
    sameSlot.loop();sameSlot.loop();assert(sameSlot.consignes==1);
    tick=0;Satellite physical(1);physical._modeVirtuel=false;physical.loop();
    assert(physical.radioMock.sent.empty());
    synced=false;tick=30000;assert(RadioSchedule::clockSeconds()==30);
    puts("OK: Z1 :30, Z2 :54, Z3 1:18; slots, retries, deadlines, startup, incomplete data, wrap and clock jumps");
}
'''

with tempfile.TemporaryDirectory(prefix='ofv-satellite-slots-') as directory:
    temp = Path(directory)
    (temp / 'Arduino.h').write_text('#pragma once\n')
    (temp / 'TimeLib.h').write_text('#pragma once\n')
    (temp / 'test.cpp').write_text(code)
    binary = temp / 'test'
    subprocess.run([
        'c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-copy',
        '-Wno-vla', '-Wno-unused-parameter',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I', str(temp), '-I', str(ROOT / 'src'), str(temp / 'test.cpp'), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
