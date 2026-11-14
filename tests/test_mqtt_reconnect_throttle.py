"""Check that MQTT reconnection attempts are spaced and never block the main loop in a tight retry."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
manager = (ROOT / 'src/MQTT/MqttManager.h').read_text()


def extract(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


loop = extract(manager, 'bool loop()')
retry = re.search(r'static constexpr uint32_t kRetryMs = (\d+);', manager)
assert retry, 'kRetryMs manquant'
retry_ms = int(retry.group(1))
assert 5000 <= retry_ms <= 60000, retry_ms

code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
struct String : std::string { using std::string::string; bool isEmpty() const { return empty(); } };
static uint32_t fakeNow = 0;
static uint32_t millis() { return fakeNow; }
struct FakeWiFi { bool up = true; bool isConnected() const { return up; } } WiFi;
struct FakeMqtt {
    bool online = false; int loops = 0;
    bool connected() const { return online; }
    bool loop() { ++loops; return online; }
};
struct Options { String host; };
struct Manager {
    Options _opts; FakeMqtt _mqtt; int attempts = 0; bool brokerUp = false;
    void reconnect() { ++attempts; _mqtt.online = brokerUp; }
    static constexpr uint32_t kRetryMs = ''' + str(retry_ms) + r''';
    uint32_t _lastAttemptMs = 0;
    bool _triedOnce = false;
''' + loop + r'''
};
constexpr uint32_t Manager::kRetryMs;

int main() {
    // Broker non configuré : aucune tentative.
    { Manager m; for (fakeNow = 0; fakeNow < 100000; fakeNow += 10) m.loop(); assert(m.attempts == 0); }
    // WiFi hors ligne : aucune tentative.
    { Manager m; m._opts.host = "192.0.2.10"; WiFi.up = false;
      for (fakeNow = 0; fakeNow < 100000; fakeNow += 10) m.loop(); assert(m.attempts == 0); WiFi.up = true; }
    // Broker injoignable : une tentative immédiate, puis une par intervalle.
    { Manager m; m._opts.host = "192.0.2.10";
      for (fakeNow = 0; fakeNow < 3 * Manager::kRetryMs; fakeNow += 10) m.loop();
      assert(m.attempts == 3); }
    // Broker joignable : connexion au premier essai, plus de tentative ensuite.
    { Manager m; m._opts.host = "192.0.2.10"; m.brokerUp = true;
      for (fakeNow = 0; fakeNow < 100000; fakeNow += 10) m.loop();
      assert(m.attempts == 1); assert(m._mqtt.loops > 0); }
    // Passage de millis() par 0 : l'intervalle reste respecté.
    { Manager m; m._opts.host = "192.0.2.10";
      fakeNow = UINT32_MAX - 1000; m.loop(); assert(m.attempts == 1);
      fakeNow = 2000; m.loop(); assert(m.attempts == 1);
      fakeNow = Manager::kRetryMs; m.loop(); assert(m.attempts == 2); }
    puts("OK: reconnexions MQTT espacées, sans broker ni WiFi aucune tentative, retour de millis() géré");
}
'''

with tempfile.TemporaryDirectory(prefix='frisquet-mqtt-throttle-') as directory:
    cpp = Path(directory) / 'test.cpp'
    cpp.write_text(code)
    binary = Path(directory) / 'test'
    subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
