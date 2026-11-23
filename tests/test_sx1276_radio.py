"""Exercise the actual SX1276 RX backend with a simulated FSK FIFO and IRQs."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/Radio.cpp').read_text()
backend = source.split('#if defined(RADIO_SX1276)\n', 1)[1].split('\n#else\n', 1)[0]
candidates = list((ROOT / '.pio/libdeps').glob('*/RadioLib/src/modules/SX127x/SX127x.h'))
if not candidates:
    raise SystemExit('Installer les dépendances PlatformIO avant ce test (RadioLib 7.8.1).')
registers = candidates[0].read_text()

def extract(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
constants = ''
for name in sorted(set(re.findall(r'\bRADIOLIB_SX127X_\w+', backend))):
    constants += re.search(r'^#define ' + name + r'\s+[^\n]+', registers, re.M)[0] + '\n'
code = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "RadioCrc.h"
using RadioLibTime_t=uint32_t;
using byte=uint8_t;
#define RADIO_SX1276
#define RADIOLIB_ERR_PACKET_TOO_LONG -4
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_ERR_RX_TIMEOUT -6
#define RADIOLIB_ERR_CRC_MISMATCH -7
#define RADIOLIB_ERR_WRONG_MODEM -20
#define RADIOLIB_IRQ_RX_DEFAULT_FLAGS 1
#define RADIOLIB_IRQ_RX_DEFAULT_MASK 1
''' + constants + r'''
static uint32_t clockMs=0;
uint32_t millis(){return clockMs;}
struct { void addLogf(const char*,const char*,...){} } logs;
void logRadioCrcData(const byte*,size_t,const char*){}
struct Hal {
    uint32_t ticks=0;bool reply=true;
    uint32_t millis(){return ticks++;}
    bool digitalRead(int){return reply;}
    void yield(){}
};
struct Module {
    Hal clock;Hal* hal=&clock;
    uint8_t fifo[64]={};size_t size=0,pos=0;bool keepInvalid=false;
    int getIrq(){return 26;}
    int16_t SPIsetRegValue(int reg,int value,int high,int low){
        assert(reg==RADIOLIB_SX127X_REG_PACKET_CONFIG_1);
        assert(value==RADIOLIB_SX127X_CRC_AUTOCLEAR_OFF && high==3 && low==3);
        keepInvalid=true;return 0;
    }
    uint8_t SPIreadRegister(int reg){assert(reg==RADIOLIB_SX127X_REG_FIFO && pos<size);return fifo[pos++];}
    void SPIreadRegisterBurst(int reg,size_t length,uint8_t* out){
        assert(reg==RADIOLIB_SX127X_REG_FIFO && pos+length<=size);
        memcpy(out,fifo+pos,length);pos+=length;
    }
};
class SX1276 {public:
    size_t sentLength=0;
    int16_t transmit(uint8_t*,size_t length){sentLength=length;return 0;}
    Module module;uint16_t irq=0;bool crc=false,ccitt=false,armed=false;uint8_t maximum=0;
    Module* getMod(){return &module;}
    int16_t standby(){armed=false;return 0;}
    int16_t getActiveModem(){return RADIOLIB_SX127X_FSK_OOK;}
    int16_t setCRC(bool enable,bool ibm){crc=enable;ccitt=!ibm;return 0;}
    int16_t variablePacketLengthMode(uint8_t max){maximum=max;return 0;}
    virtual int16_t startReceive(){return 0;}
    int16_t startReceive(int timeout,int,int,int length){assert(!timeout&&!length);armed=true;return 0;}
    virtual int16_t receive(uint8_t*,size_t,RadioLibTime_t=0){return 0;}
    virtual int16_t readData(uint8_t*,size_t){return 0;}
    float getRSSI(){return -50;}
    uint16_t getIrqFlags(){return irq;}
    int16_t clearIrqFlags(uint32_t flags){assert(flags==0xFFFF);irq=0;return 0;}
    size_t getPacketLength(bool=true){assert(false && "must not consume the FIFO length twice");return 0;}
};
class Radio {public:
    static constexpr size_t MAX_RX_PACKET_LENGTH=RadioCrc::MAX_PAYLOAD;
    struct PacketRadio:public SX1276 {
        uint16_t lastRxIrq=0;size_t lastRxLength=0;uint8_t lastRxOffset=0;
        uint8_t raw[RadioCrc::RAW_LENGTH]={};size_t rawLength=0;RadioCrc::Result crcResult;
        int16_t startReceive() override;
        int16_t receive(uint8_t*,size_t,RadioLibTime_t=0) override;
        int16_t readData(uint8_t*,size_t) override;
    };
    PacketRadio _radio;
    uint32_t _lastActivityMs=0, _lastValidReceptionMs=0;
    bool _hasValidReception=false;
    float _lastPacketRSSI=0;
    int16_t finishReceive(int16_t,const char*,const uint8_t*,size_t);
    bool hasReceivedPacket();
    int16_t transmit(byte*,size_t);
};
''' + backend + '\n' + extract('bool Radio::hasReceivedPacket(') + '\n' + extract('int16_t Radio::transmit(') + '\n' + extract('int16_t Radio::finishReceive(') + r'''
void packet(Radio::PacketRadio& radio,uint8_t length,bool valid=true){
    radio.module.pos=0;radio.module.size=length<=63?length+1:1;
    radio.module.fifo[0]=length;
    for(size_t i=1;i<radio.module.size;++i)radio.module.fifo[i]=i;
    radio.irq=(RADIOLIB_SX127X_FLAG_PAYLOAD_READY | (valid?RADIOLIB_SX127X_FLAG_CRC_OK:0))<<8;
}
int main(){
    Radio wrapper;uint8_t tx[64]={};
    wrapper._radio.irq=RADIOLIB_SX127X_FLAG_FIFO_OVERRUN<<8;
    assert(wrapper.hasReceivedPacket());
    wrapper._radio.irq=0;assert(!wrapper.hasReceivedPacket());
    wrapper._radio.armed=true;
    assert(wrapper.transmit(tx,64)==RADIOLIB_ERR_PACKET_TOO_LONG && wrapper._radio.armed);
    clockMs=100;
    assert(wrapper.transmit(tx,63)==0 && wrapper._radio.sentLength==63 && wrapper._radio.ccitt);
    assert(wrapper._lastActivityMs==100);
    clockMs=200;
    wrapper.finishReceive(0,"receive",tx,0);
    assert(wrapper._lastValidReceptionMs==200 && wrapper._lastActivityMs==200);
    clockMs=300;
    wrapper.finishReceive(RADIOLIB_ERR_CRC_MISMATCH,"readData",tx,0);
    assert(wrapper._lastValidReceptionMs==200 && wrapper._lastActivityMs==300);
    clockMs=400;
    wrapper.finishReceive(RADIOLIB_ERR_RX_TIMEOUT,"receive",tx,0);
    assert(wrapper._lastActivityMs==300);
    Radio::PacketRadio radio;
    assert(radio.startReceive()==0 && radio.armed && radio.crc && radio.ccitt);
    assert(radio.module.keepInvalid && radio.maximum==63);
    uint8_t output[65];memset(output,0xA5,sizeof(output));
    for(unsigned length=1;length<=63;++length){
        packet(radio,length);assert(radio.readData(output+1,0)==0);
        assert(radio.lastRxLength==length && radio.module.pos==length+1);
        assert(output[0]==0xA5 && output[length]==length);
    }
    assert(output[64]==0xA5); // 63-byte payload + both canaries
    // A second read without a new IRQ cannot accept the previous frame.
    assert(radio.readData(output,0)==RADIOLIB_ERR_RX_TIMEOUT && radio.lastRxLength==0);
    memset(output,0xA5,sizeof(output));packet(radio,63);
    assert(radio.readData(output+1,3)==0);
    assert(output[0]==0xA5 && output[4]==0xA5 && radio.module.pos==64);
    packet(radio,9,false);assert(radio.readData(output,0)==RADIOLIB_ERR_CRC_MISMATCH);
    assert(radio.lastRxLength==9 && radio.rawLength==10);
    packet(radio,9);assert(radio.readData(output,0)==0 && radio.lastRxLength==9);
    packet(radio,0);assert(radio.readData(output,0)==RADIOLIB_ERR_CRC_MISMATCH && !radio.lastRxLength);
    packet(radio,64);assert(radio.readData(output,0)==RADIOLIB_ERR_CRC_MISMATCH && !radio.lastRxLength);
    packet(radio,255);assert(radio.readData(output,0)==RADIOLIB_ERR_CRC_MISMATCH && !radio.lastRxLength);
    packet(radio,9);radio.irq|=RADIOLIB_SX127X_FLAG_FIFO_OVERRUN<<8;
    assert(radio.readData(output,0)==RADIOLIB_ERR_CRC_MISMATCH && radio.module.pos==0);
    radio.irq=RADIOLIB_SX127X_FLAG_FIFO_OVERRUN<<8;
    assert(radio.readData(output,0)==RADIOLIB_ERR_CRC_MISMATCH && !radio.irq);
    radio.module.clock.reply=false;radio.module.clock.ticks=0;
    assert(radio.receive(output,0)==RADIOLIB_ERR_RX_TIMEOUT);
    assert(radio.module.clock.ticks>=408 && !radio.armed);
    radio.module.clock.ticks=0;
    assert(radio.receive(output,0,25)==RADIOLIB_ERR_RX_TIMEOUT);
    assert(radio.module.clock.ticks>=25 && radio.module.clock.ticks<30);
    radio.module.clock.reply=true;packet(radio,49);
    assert(radio.receive(output,0)==0 && radio.lastRxLength==49);
    puts("OK: SX1276 CCITT, 1..63 bytes, cached length, bounded reads, CRC errors, overrun and timeout");
}
'''
with tempfile.TemporaryDirectory(prefix='frisquet-sx1276-') as directory:
    cpp=Path(directory)/'test.cpp';cpp.write_text(code)
    binary=Path(directory)/'test'
    subprocess.run(['c++','-std=c++11','-fsanitize=address,undefined','-I',str(ROOT/'src'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
