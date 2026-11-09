"""Check firmware BCD decoding, power scaling and the actual Connect version read."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/Frisquet/Connect.cpp').read_text()

def extract(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

loop = extract('void Connect::loop()')
assert loop.count('recupererVersionFirmware();') == 1
assert loop.index('useConnectPassive()') < loop.index('if (_initialReadPending)')
assert loop.index('_initialReadPending = false;') < loop.index('recupererVersionFirmware();') < loop.index('recupererInformations()')
active = extract('bool Connect::recupererInformations()')
assert active.count('_boilerFirmware.power(') == 2
passive = extract('bool Connect::handlePassiveReadResponse(')
assert passive.count('_boilerFirmware.power(') == 2
assert 'if (adresseMemoire == 0x7541)' in passive
assert 'return setVersionFirmware(buff, length);' in passive

code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "Frisquet/BoilerFirmware.h"
using byte = uint8_t;
constexpr int RADIOLIB_ERR_NONE = 0;
constexpr uint8_t ID_CHAUDIERE = 0x80;
template<typename... T> void info(const char*, T...) {}
template<typename... T> void error(const char*, T...) {}
struct FrisquetRadio {
    struct RadioTrameHeader { byte bytes[6]; };
    byte payload[4] = {0x19,0x02,0x00,0x00};
    size_t responseLength = 11;
    byte declaredLength = 4;
    int result = 0, calls = 0;
    int16_t sendAsk(uint8_t from,uint8_t to,uint8_t association,uint8_t message,
                    uint8_t reception,uint16_t address,uint16_t words,
                    byte* response,size_t& length,uint8_t attempts) {
        ++calls;
        assert(from==0x7E && to==ID_CHAUDIERE && association==0xAC && message==1);
        assert(reception==1 && address==0x7541 && words==2 && attempts==1);
        assert(length==11);
        response[6]=declaredLength;
        memcpy(response+7,payload,4);
        length=responseLength;
        return result;
    }
};
struct Config { bool passive=false;bool useConnectPassive(){return passive;} };
struct Connect {
    BoilerFirmware _boilerFirmware;
    FrisquetRadio radioMock;
    Config cfg;
    bool associated = true;
    Config& getConfig(){return cfg;}
    bool estAssocie(){return associated;}
    FrisquetRadio& radio(){return radioMock;}
    uint8_t getId(){return 0x7E;}
    uint8_t getIdAssociation(){return 0xAC;}
    uint8_t incrementIdMessage(){return 1;}
    bool recupererVersionFirmware();
    bool setVersionFirmware(const byte* response, size_t length);
};
''' + extract('bool Connect::recupererVersionFirmware()') + '\n' + extract('bool Connect::setVersionFirmware(') + r'''
int main() {
    BoilerFirmware version;
    assert(!version.known && version.power(123)==12.3f);
    const byte oldVersion[]={0x19,0x01,0x00,0x08};
    assert(version.decode(oldVersion,4));
    assert(version.major==19 && version.minor==1 && version.build==8);
    assert(!version.directPower() && version.power(200)==20.0f);
    const byte lastOld[]={0x19,0x01,0x99,0x99};
    assert(version.decode(lastOld,4) && !version.directPower());
    const byte newVersion[]={0x19,0x02,0x00,0x00};
    assert(version.decode(newVersion,4) && version.directPower() && version.power(20)==20.0f);
    assert(version.power(0)==0.0f);
    const byte future[]={0x20,0x00,0x12,0x34};
    assert(version.decode(future,4) && version.directPower() && version.build==1234);
    const byte ancient[]={0x18,0x99,0x99,0x99};
    assert(version.decode(ancient,4) && !version.directPower());
    for(unsigned i=0;i<4;++i) {
        byte invalid[]={0x19,0x02,0x00,0x00};
        invalid[i]=0xFA;
        assert(!version.decode(invalid,4) && !version.known && version.power(200)==20.0f);
    }
    const byte empty[4]={};
    assert(!version.decode(empty,4));
    assert(!version.decode(nullptr,4));
    assert(!version.decode(newVersion,3));
    assert(!version.decode(newVersion,5));
    Connect c;
    assert(c.recupererVersionFirmware() && c.radioMock.calls==1 && c._boilerFirmware.directPower());
    c.radioMock.result=-6;
    assert(!c.recupererVersionFirmware() && !c._boilerFirmware.known && c._boilerFirmware.power(200)==20.0f);
    c.radioMock.result=0;
    for(size_t length=0;length<11;++length) {
        c.radioMock.responseLength=length;
        assert(!c.recupererVersionFirmware() && !c._boilerFirmware.known);
    }
    c.radioMock.responseLength=11;c.radioMock.declaredLength=3;
    assert(!c.recupererVersionFirmware());
    c.radioMock.declaredLength=4;c.radioMock.payload[3]=0xFF;
    assert(!c.recupererVersionFirmware());
    const int calls=c.radioMock.calls;
    c.cfg.passive=true;
    assert(!c.recupererVersionFirmware() && c.radioMock.calls==calls);
    c.cfg.passive=false;c.associated=false;
    assert(!c.recupererVersionFirmware() && c.radioMock.calls==calls);
    // Capture réelle : demande 80 7E 6E 1C 01 03 75 41 00 02.
    const byte captured[]={0x7E,0x80,0x6E,0x1C,0x81,0x03,0x04,0x19,0x02,0x00,0x11};
    Connect passive;
    passive.cfg.passive=true;
    assert(passive.setVersionFirmware(captured,sizeof(captured)));
    assert(passive._boilerFirmware.major==19 && passive._boilerFirmware.minor==2 && passive._boilerFirmware.build==11);
    assert(passive._boilerFirmware.power(20)==20.0f && passive.radioMock.calls==0);
    assert(!passive.setVersionFirmware(nullptr,11));
    for(size_t length=0;length<sizeof(captured);++length) {
        assert(!passive.setVersionFirmware(captured,length));
        assert(passive._boilerFirmware.directPower());
    }
    byte invalid[sizeof(captured)];memcpy(invalid,captured,sizeof(captured));
    invalid[6]=3;
    assert(!passive.setVersionFirmware(invalid,sizeof(invalid)));
    invalid[6]=4;invalid[10]=0xFF;
    assert(!passive.setVersionFirmware(invalid,sizeof(invalid)) && passive._boilerFirmware.directPower());
    byte oldResponse[sizeof(captured)];memcpy(oldResponse,captured,sizeof(captured));
    memcpy(oldResponse+7,oldVersion,4);
    assert(passive.setVersionFirmware(oldResponse,sizeof(oldResponse)));
    assert(passive._boilerFirmware.power(200)==20.0f && passive.radioMock.calls==0);
    puts("OK: firmware BCD, threshold 19.02.0000, power scales, one startup read, passive capture 19.02.0011 without transmission, preserved known version and invalid/failed responses");
}
'''
with tempfile.TemporaryDirectory(prefix='ofv-boiler-firmware-') as temp:
    cpp = Path(temp) / 'test.cpp'
    cpp.write_text(code)
    binary = Path(temp) / 'test'
    subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I', str(ROOT / 'src'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
