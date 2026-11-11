"""Exercise real discovery methods with simulated radio exchanges (ASan/UBSan)."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def extract(path, signature):
    text = (ROOT / path).read_text()
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

radio_types = '\n'.join(extract('src/Frisquet/FrisquetRadio.h', signature) + ';' for signature in (
    'enum MessageType', 'struct RadioTrameHeader', 'struct RadioTrameAsk'))
config_methods = '\n'.join(extract('src/Config.h', signature) for signature in (
    'uint8_t boilerId()', 'bool boilerIdKnown()', 'uint16_t boilerMemoryOffset()', 'bool boilerId(uint8_t id)'))
passive = extract('src/Frisquet/Connect.cpp', 'bool Connect::onReceive(')
# Keep the actual passive READ path; INIT decoding belongs to other tests.
passive = passive[:passive.index('    if(header.type == FrisquetRadio::MessageType::INIT)')] + '    return false;\n}'
code = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "Frisquet/BoilerIdentity.h"
#include "Frisquet/NetworkID.h"
#include "Buffer.h"
using byte=uint8_t;
constexpr int RADIOLIB_ERR_NONE=0;
constexpr size_t RADIOLIB_SX126X_MAX_PACKET_LENGTH=255;
constexpr byte ID_CONNECT=0x7E;
uint32_t clockMs=0;
uint32_t millis(){return clockMs;}
void delay(uint32_t ms){clockMs+=ms;}
void info(const char*,...){}
void error(const char*,...){}
void debug(const char*,...){}
void logRadio(bool,byte*,size_t,float=0){}
std::string byteArrayToHexString(byte*,size_t){return "synthetic";}
''' + extract('src/Frisquet/Utils.h', 'struct fword') + ';\n' + extract('src/Frisquet/Utils.h', 'struct Date') + r''';
struct Config {
    uint8_t _boilerId=0x80;bool _boilerIdKnown=false,passive=false;
    unsigned saves=0;
    NetworkID network{1,2,3,4};
    bool useConnectPassive(){return passive;}
    NetworkID& getNetworkID(){return network;}
    void setNetworkID(NetworkID n){network=n;}
    void save(){++saves;}
''' + config_methods + r'''
};
struct FrisquetRadio {
''' + radio_types + r'''
    uint8_t responding=0x80;
    int result=0,mutate=-1;
    bool shortDate=false,badPassive=false;
    std::vector<byte> asked;
    std::deque<std::vector<byte>> packets;
    std::vector<std::vector<byte>> sent;
    size_t length=0;
    unsigned rearms=0,passiveWaits=0;
    byte expectedSender=0;
    NetworkID network;
    void setNetworkID(NetworkID n){network=n;}
    int startReceive(){++rearms;return 0;}
    int receive(byte* out,size_t){clockMs+=408;if(packets.empty())return -6;
        auto p=packets.front();packets.pop_front();length=p.size();memcpy(out,p.data(),length);return 0;}
    size_t getPacketLength(){return length;}
    float getLastPacketRSSI(){return -50;}
    int transmit(byte* data,size_t n){sent.emplace_back(data,data+n);return 0;}
    int sendAsk(byte from,byte to,byte assoc,byte message,byte reception,uint16_t address,
                uint16_t words,byte* out,size_t& n,uint8_t attempts){
        asked.push_back(to);assert(address==0xA02B && words==3 && n==13 && reception==1 && attempts==2);
        if(to!=responding || result!=0)return result ? result : -6;
        byte response[]={from,to,assoc,message,0x81,0x03,6,0x26,0x11,0x11,0x12,0x34,0x56};
        if(mutate>=0)response[mutate]=0xFF;
        memcpy(out,response,13);n=shortDate ? 12 : 13;return 0;
    }
    int receiveExpected(byte from,byte to,byte assoc,byte message,byte reception,byte type,
                        byte* out,size_t& n,uint8_t,bool,bool){
        ++passiveWaits;expectedSender=from;
        byte response[]={to,from,assoc,message,reception,type,4,0x19,0x02,0,0x11};
        if(badPassive)response[2]^=1;
        memcpy(out,response,sizeof(response));n=sizeof(response);return result;
    }
};
struct FrisquetDevice {
    FrisquetRadio& transport;Config& cfg;
    byte device=ID_CONNECT,association=55,sequence=0;
    unsigned dateUpdates=0;
    Date date;
    FrisquetRadio& radio(){return transport;}
    Config& getConfig(){return cfg;}
    byte getId(){return device;}
    byte getIdAssociation(){return association;}
    void setIdAssociation(byte a){association=a;}
    void setIdMessage(byte m){sequence=m;}
    byte incrementIdMessage(){return ++sequence;}
    bool estAssocie(){return association!=0xFF;}
    void setDate(Date& d){date=d;++dateUpdates;}
    void confirmerChaudiere(uint8_t);
    bool recupererDate();
    bool associer(NetworkID&,uint8_t&);
};
struct Connect:FrisquetDevice {
    Connect(FrisquetRadio& r,Config& c):FrisquetDevice{r,c}{}
    unsigned handled=0,deviceSaves=0;
    void saveConfig(){++deviceSaves;}
    bool handlePassiveReadResponse(uint16_t address,const byte*,size_t){assert(address==0x7541);++handled;return true;}
    bool onReceive(byte*,size_t);
};
struct FrisquetManager {
    FrisquetRadio& transport;Config& cfg;
    FrisquetRadio& radio(){return transport;}
    Config& config(){return cfg;}
    bool recupererNetworkID();
};
''' + '\n'.join(extract(path, signature) for path, signature in (
    ('src/Frisquet/FrisquetDevice.cpp', 'void FrisquetDevice::confirmerChaudiere('),
    ('src/Frisquet/FrisquetDevice.cpp', 'bool FrisquetDevice::recupererDate('),
    ('src/Frisquet/FrisquetDevice.cpp', 'bool FrisquetDevice::associer('),
    ('src/FrisquetManager.cpp', 'bool FrisquetManager::recupererNetworkID('),
)) + '\n' + passive + r'''
int main(){
    for(byte id: {0x80,0x84})for(byte device: {0x7E,0x08,0x09,0x0A}){
        Config cfg;FrisquetRadio radio;radio.responding=id;
        FrisquetDevice d{radio,cfg};d.device=device;
        assert(d.recupererDate() && cfg.boilerId()==id && cfg.boilerIdKnown());
        assert(d.dateUpdates==1 && d.date.annee==26 && d.date.mois==11 && d.date.minute==34);
        assert(cfg.saves==1 && cfg.boilerMemoryOffset()==(id==0x84 ? 0xC8 : 0));
        assert(radio.asked==(id==0x80 ? std::vector<byte>{0x80} : std::vector<byte>{0x80,0x84}));
        radio.asked.clear();assert(d.recupererDate() && cfg.saves==1 && radio.asked==std::vector<byte>{id});
        // A replaced boiler is discovered in the reverse direction as well.
        radio.responding=BoilerIdentity::alternate(id);radio.asked.clear();
        assert(d.recupererDate() && cfg.boilerId()==radio.responding && cfg.saves==2);
        assert(radio.asked==std::vector<byte>({id,radio.responding}));
    }
    for(int failure: {-7,-6,0}){
        Config cfg;cfg.boilerId(0x84);FrisquetRadio radio;radio.responding=0x84;radio.result=failure;
        radio.shortDate=(failure==0);FrisquetDevice d{radio,cfg};
        assert(!d.recupererDate() && cfg.boilerId()==0x84 && cfg.saves==0 && d.dateUpdates==0);
    }
    for(int field=0;field<13;++field){
        Config cfg;FrisquetRadio radio;radio.mutate=field;FrisquetDevice d{radio,cfg};
        assert(!d.recupererDate() && !cfg.boilerIdKnown() && cfg.saves==0);
    }
    {
        Config cfg;cfg.passive=true;FrisquetRadio radio;FrisquetDevice d{radio,cfg};
        assert(!d.recupererDate() && radio.asked.empty());
        cfg.passive=false;d.association=0xFF;assert(!d.recupererDate() && radio.asked.empty());
        d.association=55;cfg.network=NetworkID(255,255,255,255);
        assert(!d.recupererDate() && radio.asked.empty());
    }
    // Association and NetworkID-only discovery both learn either source identity.
    for(byte id: {0x80,0x84})for(bool manager: {false,true}){
        Config cfg;FrisquetRadio radio;
        radio.packets.push_back({0x7E,id,55,42,1,0x41,4,5,6,7,8});
        FrisquetDevice d{radio,cfg};NetworkID network;uint8_t assoc=0;
        assert((manager ? FrisquetManager{radio,cfg}.recupererNetworkID() : d.associer(network,assoc)));
        assert(cfg.boilerId()==id && cfg.boilerIdKnown() && radio.rearms==1);
        if(manager){assert(cfg.saves==1 && cfg.network.bytes[0]==5 && radio.sent.empty());}
        else {assert(assoc==55 && network.bytes[0]==5 && radio.sent.size()==10);
              for(auto& frame:radio.sent)assert(frame[0]==id && frame[1]==0x7E);}
    }
    {
        Config cfg;cfg.boilerId(0x84);FrisquetRadio radio;
        // Unknown source and malformed announcements must not change the identity.
        radio.packets.push_back({0x7E,0x20,55,42,1,0x41,4,5,6,7,8});
        radio.packets.push_back({0x7E,0x80,55,42,1,0x41,3,5,6,7,8});
        FrisquetDevice d{radio,cfg};NetworkID network;uint8_t assoc=99;
        assert(!d.associer(network,assoc) && cfg.boilerId()==0x84 && assoc==99);
        assert(!memcmp(radio.network.bytes,cfg.network.bytes,4) && radio.rearms==1);
    }
    // Existing passive Connect installations learn from a correlated reply, without sending.
    for(byte id: {0x80,0x84}){
        Config cfg;cfg.passive=true;FrisquetRadio radio;Connect c(radio,cfg);c.association=0xFF;
        byte request[]={id,0x7E,55,42,1,3,0x75,0x41,0,2};
        assert(c.onReceive(request,sizeof(request)) && cfg.boilerId()==id && cfg.saves==1 && c.association==55);
        assert(radio.expectedSender==id && radio.asked.empty() && radio.sent.empty() && c.handled==1);
        assert(c.onReceive(request,sizeof(request)) && cfg.saves==1);
        radio.badPassive=true;request[0]=BoilerIdentity::alternate(id);
        assert(!c.onReceive(request,sizeof(request)) && cfg.boilerId()==id && cfg.saves==1);
        radio.badPassive=false;radio.result=-7;
        assert(!c.onReceive(request,sizeof(request)) && cfg.boilerId()==id);
    }
    const byte leap[]={0x24,0x02,0x29,0x23,0x59,0x59};assert(BoilerIdentity::validDate(leap));
    const byte invalid[]={0x25,0x02,0x29,0x23,0x59,0x59};assert(!BoilerIdentity::validDate(invalid));
    puts("OK: 0x80/0x84, Connect and virtual zones, fallback, invalid replies, pairing, passive learning without TX, memory offset and retained identity");
}
'''
with tempfile.TemporaryDirectory(prefix='ofv-boiler-id-') as directory:
    root=Path(directory)
    (root/'Board.h').write_text('#pragma once\n#include <stdint.h>\n#include <stddef.h>\n#include <string.h>\nusing byte=uint8_t;\n')
    (root/'test.cpp').write_text(code)
    binary=root/'test'
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-variable',
                    '-Wno-missing-field-initializers','-Wno-deprecated-copy',
                    '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                    '-I',str(root),'-I',str(ROOT/'src'),str(root/'test.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
