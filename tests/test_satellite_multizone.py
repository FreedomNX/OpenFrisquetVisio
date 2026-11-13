"""Replay physical satellite setpoints through the actual onReceive method."""
from pathlib import Path
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


code = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "Buffer.h"
#include "Frisquet/BoilerIdentity.h"
using byte = uint8_t;
constexpr int RADIOLIB_ERR_NONE = 0;
constexpr unsigned RADIOLIB_SX126X_MAX_PACKET_LENGTH = 255;
void info(const char*, ...) {}
void error(const char*, ...) {}
void delay(unsigned) {}
''' + '\n'.join(extract('src/Frisquet/Utils.h', signature) + ';' for signature in (
    'struct fword', 'struct temperature16', 'struct Date')) + r'''
struct FrisquetRadio {
''' + '\n'.join(extract('src/Frisquet/FrisquetRadio.h', signature) + ';' for signature in (
    'enum MessageType', 'struct RadioTrameHeader', 'struct RadioTrameInit')) + r'''
    std::vector<byte> reply;
    unsigned waits=0;
    int receiveExpected(byte from, byte to, byte association, byte message,
                        byte reception, byte type, byte* out, size_t& length, unsigned, bool) {
        ++waits;
        assert(reply[0]==to && reply[1]==from && reply[2]==association);
        assert(reply[3]==message && reply[4]==reception && reply[5]==type);
        length=reply.size();memcpy(out,reply.data(),length);return 0;
    }
};
struct Zone {
    enum class SOURCE { CONNECT, SATELLITE_PHYSIQUE };
    enum class MODE_ZONE { INCONNU, CONFORT };
    struct Programmation {byte data[42];};
    float ambient=NAN,setpoint=NAN;
    unsigned publishes=0,saves=0;
    SOURCE getSource(){return SOURCE::SATELLITE_PHYSIQUE;}
    MODE_ZONE getMode(){return MODE_ZONE::CONFORT;}
    byte getModeOptions(){return 0;}
    unsigned getLastChange(){return 0;}
    unsigned getLastEnvoi(){return 0;}
    void setMode(MODE_ZONE){}
    void setModeOptions(byte){}
    void confirmerMode(){}
    void setProgrammation(Programmation&,bool){}
    void saveConfig(){++saves;}
    void publishMqtt(){++publishes;}
    void setTemperatureAmbiante(float t){ambient=t;}
    void setTemperatureConsigne(float t){setpoint=t;}
    float getTemperatureConsigne(){return setpoint;}
};
struct Satellite {
    enum MODE : byte {INCONNU=255};
    bool _modeVirtuel=false,overwrite=false;
    Zone _zone;FrisquetRadio transport;
    byte number,association=255,message=0;
    Satellite(byte n):number(n){}
    byte getNumeroZone(){return number;}
    byte getId(){return 7+number;}
    byte getIdMessage(){return message;}
    byte getIdAssociation(){return association;}
    void setIdMessage(byte v){message=v;}
    void setIdAssociation(byte v){association=v;}
    void incrementIdMessage(byte v){message+=v;}
    FrisquetRadio& radio(){return transport;}
    bool getEcrasement(){return overwrite;}
    void confirmerChaudiere(byte){}
    void setEtatChaudiere(byte){}
    void setDate(Date&){}
    bool setModeFromRaw(byte){return true;}
    MODE getMode(){return INCONNU;}
    bool envoyerConsigne(){return true;}
    void saveConfig(){}
    void publishMqtt(){}
    bool onReceive(byte*,size_t);
};
''' + extract('src/Frisquet/Satellite.cpp', 'bool Satellite::onReceive(') + r'''
int main(){
    // Real zone-1 request and boiler reply from the 12 November capture.
    const std::vector<byte> request={
        0x80,0x08,0x3F,0xE8,0x01,0x17,0xA0,0x29,0x00,0x15,
        0xA0,0x2F,0x00,0x04,0x08,0x00,0xCA,0x00,0xAA,0x00,0x00,0x00,0x00};
    const std::vector<byte> reply={
        0x08,0x80,0x3F,0xE8,0x81,0x17,0x2A,0x00,0x7E,0x00,0x00,
        0x26,0x11,0x12,0x12,0x54,0x49,0x08,0x04,
        0x00,0xCA,0x00,0xAA,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0xC7,0x00,0xA0,0x00,0x04,0x00,0xC6,0x00,0xC6,
        0x00,0xC3,0x00,0xAF,0x00,0x00,0x00,0x00,0x00,0x00};
    // Zones 2/3 requests are derived from the existing TX memory layout.
    for(byte zone=1;zone<=3;++zone)for(byte boiler: {0x80,0x84}) {
        Satellite s(zone);auto frame=request;s.transport.reply=reply;
        frame[0]=boiler;frame[1]=s.getId();frame[11]=0x2F+5*(zone-1);
        s.transport.reply[0]=s.getId();s.transport.reply[1]=boiler;
        assert(s.onReceive(frame.data(),frame.size()));
        assert(s.transport.waits==1 && s._zone.publishes==1);
        assert(s._zone.ambient==20.2f && s._zone.setpoint==17.0f);
        assert(s.association==0x3F && s.message==0xE8);
        // Duplicate setpoints must not wait for the same response again.
        assert(!s.onReceive(frame.data(),frame.size()));assert(s.transport.waits==1);
        // Another zone's address, malformed payload and ACK are ignored.
        for(unsigned mutation: {0U,1U,2U,3U}) {
            Satellite rejected(zone);auto invalid=frame;
            if(mutation==0)invalid[11]=zone==1 ? 0x34 : 0x2F;
            if(mutation==1)invalid[14]=7;
            if(mutation==2)invalid[13]=3;
            if(mutation==3)invalid[4]=0x81;
            assert(!rejected.onReceive(invalid.data(),invalid.size()));
            assert(rejected.transport.waits==0 && rejected._zone.publishes==0);
        }
        for(size_t size=0;size<frame.size();++size) {
            Satellite truncated(zone);
            assert(!truncated.onReceive(frame.data(),size));
            assert(truncated.transport.waits==0);
        }
    }
    puts("OK: physical Z1/Z2/Z3 setpoints, 0x80/0x84, duplicates and invalid requests");
}
'''

with tempfile.TemporaryDirectory(prefix='ofv-multizone-') as directory:
    root = Path(directory)
    (root / 'Board.h').write_text(
        '#pragma once\n#include <stdint.h>\n#include <stddef.h>\n'
        '#include <string.h>\nusing byte=uint8_t;\n')
    (root / 'test.cpp').write_text(code)
    binary = root / 'test'
    subprocess.run([
        'c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable',
        '-Wno-deprecated-copy', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I', str(root), '-I', str(ROOT / 'src'), str(root / 'test.cpp'), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
