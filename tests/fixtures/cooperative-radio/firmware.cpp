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

};
struct Connect {
    BoilerFirmware _boilerFirmware;
    bool setVersionFirmware(const byte* response, size_t length);
};

#include "firmware-methods.inc"

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
    // Capture réelle : demande 80 7E 6E 1C 01 03 75 41 00 02.
    const byte captured[]={0x7E,0x80,0x6E,0x1C,0x81,0x03,0x04,0x19,0x02,0x00,0x11};
    Connect passive;
    assert(passive.setVersionFirmware(captured,sizeof(captured)));
    assert(passive._boilerFirmware.major==19 && passive._boilerFirmware.minor==2 && passive._boilerFirmware.build==11);
    assert(passive._boilerFirmware.power(20)==20.0f);
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
    assert(passive._boilerFirmware.power(200)==20.0f);
    puts("OK: firmware BCD, threshold 19.02.0000, power scales, passive capture 19.02.0011, preserved known version and invalid/failed responses");
}
