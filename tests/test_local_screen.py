"""Native PRG gestures and actual incremental OLED transfer (ASan/UBSan)."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/LocalScreen.cpp').read_text()

def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

code = r'''
#include "ScreenNavigation.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <cstdio>
using A = ScreenNavigation::Action;
constexpr uint8_t ADDRESS = 0x3C;
struct WireMock {
    std::vector<uint8_t> packet;
    std::vector<std::vector<uint8_t>> packets;
    bool failed = false;
    void beginTransmission(uint8_t a) {assert(a == ADDRESS); packet.clear();}
    void write(uint8_t value) {packet.push_back(value);}
    void write(const uint8_t* values, size_t length) {packet.insert(packet.end(), values, values + length);}
    int endTransmission() {packets.push_back(packet); return failed ? 4 : 0;}
} Wire;
struct Display {uint8_t buffer[1024] = {}; } display;
struct { Display* display; } Heltec{&display};
uint32_t clockMs = 0;
uint32_t millis() {return clockMs;}
constexpr uint8_t BUTTON = 0;
constexpr int LOW = 0;
int digitalRead(uint8_t) {return 1;}
struct FrisquetRadio {static bool receivedFlag;};
bool FrisquetRadio::receivedFlag = false;
struct Radio {
    bool occupied = false, packet = false, buffered = false;
    bool busy() {return occupied;}
    bool hasReceivedPacket() {return packet;}
    bool hasBufferedPacket() {return buffered;}
    bool hasPendingPacket() {return FrisquetRadio::receivedFlag || packet || buffered;}
};
struct Config {
    bool zones[3] = {true, false, true};
    uint16_t timeout = 60;
    bool useZone1() {return zones[0];}
    bool useZone2() {return zones[1];}
    bool useZone3() {return zones[2];}
    uint16_t displayTimeout() {return timeout;}
};
struct Manager {
    Radio r; Config cfg;
    Radio& radio() {return r;}
    Config& config() {return cfg;}
} manager;
struct LocalScreen {
    Manager* _manager = &manager;
    ScreenNavigation _navigation;
    bool _uploading = true, _power = false, _present = true, _dirty = true, _desiredPower = true;
    uint16_t _offset = 0;
    uint8_t _page = 0;
    uint32_t _lastRender = 0;
    unsigned errors = 0;
    bool command(const uint8_t*, size_t);
    void transfer();
    void loop();
    bool availableBus() const;
    void advance();
    void render(uint32_t now) {_lastRender = now; _dirty = false; _uploading = true; _offset = 0;}
    void fail() {++errors; _uploading = false; _present = false;}
};
''' + '\n'.join(method(signature) for signature in (
    'bool LocalScreen::command(', 'void LocalScreen::transfer(', 'bool LocalScreen::availableBus(',
    'void LocalScreen::advance(', 'void LocalScreen::loop(')) + r'''
int main() {
    ScreenNavigation nav;
    nav.begin(0, false, false);
    assert(nav.poll(100, true) == A::None && nav.poll(130, true) == A::None);
    assert(nav.poll(200, false) == A::None && nav.poll(230, false) == A::None);
    assert(nav.poll(580, false) == A::None && nav.poll(581, false) == A::Next);
    // A second press inside the double-click interval may be released later.
    nav.poll(1000, true); nav.poll(1030, true); nav.poll(1100, false); nav.poll(1130, false);
    nav.poll(1400, true); nav.poll(1430, true); nav.poll(1550, false);
    assert(nav.poll(1580, false) == A::Home);
    assert(nav.poll(2000, false) == A::None);
    nav.poll(2100, true); nav.poll(2130, true);
    assert(nav.poll(4129, true) == A::None && nav.poll(4130, true) == A::ToggleCycle);
    assert(nav.cycling() && nav.poll(5000, true) == A::None);
    nav.poll(5100, false); nav.poll(5130, false);
    assert(nav.poll(5600, false) == A::None);
    assert(nav.cycleDue(13130)); nav.pageChanged(13130); assert(!nav.cycleDue(13131));
    assert(!nav.shouldSleep(65129, 60) && nav.shouldSleep(65130, 60));
    assert(!nav.shouldSleep(100000, 0));
    nav.sleep(); assert(!nav.awake() && !nav.cycleDue(100000));
    nav.poll(100000, true); assert(nav.poll(100030, true) == A::Wake);
    nav.poll(100100, false); nav.poll(100130, false);
    assert(nav.poll(100500, false) == A::None); // Wake does not advance a page.
    ScreenNavigation boot; boot.begin(0, true, false);
    assert(boot.poll(3000, true) == A::None);
    boot.poll(3100, false); boot.poll(3130, false);
    assert(boot.poll(4000, false) == A::None);
    // Debounce and rollover: a bouncing switch must not cause a second event.
    ScreenNavigation wrap; wrap.begin(0xfffffff0U, false, false);
    wrap.poll(0xfffffff5U, true); wrap.poll(0xfffffffaU, false);
    assert(wrap.poll(40, false) == A::None);
    wrap.poll(100, true); wrap.poll(130, true); wrap.poll(200, false); wrap.poll(230, false);
    assert(wrap.poll(581, false) == A::Next);
    ScreenNavigation heldWrap; heldWrap.begin(0xfffffff0U, false, false);
    heldWrap.poll(0xfffffff1U, true); heldWrap.poll(15, true);
    assert(heldWrap.poll(2015, true) == A::ToggleCycle);

    for (size_t i = 0; i < sizeof(display.buffer); ++i) display.buffer[i] = i % 251;
    LocalScreen screen;
    unsigned turns = 0;
    while(screen._uploading) {
        const auto before = Wire.packets.size(); screen.transfer(); ++turns;
        assert(Wire.packets.size() == before + 1 && turns <= 34);
    }
    assert(turns == 34 && screen._power && screen.errors == 0);
    assert(Wire.packets.front() == std::vector<uint8_t>({0, 0x21, 0, 127, 0x22, 0, 7}));
    for (size_t packet = 1; packet <= 32; ++packet) {
        const auto& bytes = Wire.packets[packet];
        assert(bytes.size() == 33 && bytes[0] == 0x40);
        for (size_t i = 0; i < 32; ++i) assert(bytes[i+1] == display.buffer[(packet-1)*32+i]);
    }
    assert(Wire.packets.back() == std::vector<uint8_t>({0, 0xAF}));
    // Loop never uses I2C while radio work/packets are pending.
    LocalScreen running;
    const size_t before = Wire.packets.size();
    manager.r.occupied = true; running.loop(); manager.r.occupied = false;
    manager.r.packet = true; running.loop(); manager.r.packet = false;
    manager.r.buffered = true; running.loop(); manager.r.buffered = false;
    FrisquetRadio::receivedFlag = true; running.loop(); FrisquetRadio::receivedFlag = false;
    assert(Wire.packets.size() == before);
    running.loop(); assert(Wire.packets.size() == before + 1);
    running._present = false; running.loop(); assert(Wire.packets.size() == before + 1);
    // Only enabled zones are visited, including when all three are disabled.
    LocalScreen pages;
    for (uint8_t page : {1, 3, 4, 5, 0}) {pages.advance(); assert(pages._page == page);}
    manager.cfg.zones[0] = manager.cfg.zones[2] = false;
    pages.advance(); assert(pages._page == 4);
    // A disappeared OLED aborts on the first NACK, including during data transfer.
    for (uint16_t offset : {uint16_t(0), uint16_t(1), uint16_t(1026)}) {
        LocalScreen broken; broken._offset = offset; Wire.failed = true;
        broken.transfer(); assert(broken.errors == 1 && !broken._uploading);
    }
    puts("OK: PRG gestures, wake, inactivity, boot hold, rollover and bounded OLED transfers");
}
'''
with tempfile.TemporaryDirectory(prefix='ofv-screen-') as directory:
    temp = Path(directory)
    (temp / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-I', str(ROOT / 'src'), str(temp / 'test.cpp'), '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test')], check=True)
