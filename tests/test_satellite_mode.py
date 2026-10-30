"""Native checks of Satellite::decodeModeFromRaw / encodeModeForSend (ASan/UBSan).

Valeurs de référence : octet « mode » écrit en A02F par un satellite Z1 physique,
captures radio des 19 au 30 octobre 2026, modes imposés depuis l'appli Connect.
"""
from pathlib import Path
import subprocess
import tempfile
import re

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/Frisquet/Satellite.cpp').read_text()
header = (ROOT / 'src/Frisquet/Satellite.h').read_text()

def extract(signature, text=source):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

# Constantes et énumération telles que déclarées dans la classe.
class_body = header[header.index('static constexpr uint8_t MODE_BIT_CONFORT'):header.index('Satellite(FrisquetRadio&')]

code = r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
class Satellite {
public:
''' + class_body + r'''
    MODE decodeModeFromRaw(uint8_t rawMode) const;
    uint8_t encodeModeForSend(MODE mode) const;
};
''' + extract('Satellite::MODE Satellite::decodeModeFromRaw(') + '\n' + extract('uint8_t Satellite::encodeModeForSend(') + r'''
int main() {
    Satellite s;
    using M = Satellite::MODE;
    // Auto : le bit 0x20 est le marqueur, le bit 0x04 varie sans changer le mode.
    assert(s.decodeModeFromRaw(0x25) == M::CONFORT_AUTO);   // 07:00-23:00, période confort
    assert(s.decodeModeFromRaw(0x21) == M::CONFORT_AUTO);   // ~3 h après redémarrage / retour en Auto
    assert(s.decodeModeFromRaw(0x24) == M::REDUIT_AUTO);    // 02:00-07:00, période réduit
    assert(s.decodeModeFromRaw(0x20) == M::REDUIT_AUTO);    // 23:00-02:00, période réduit
    // Dérogation en cours.
    assert(s.decodeModeFromRaw(0x22) == M::REDUIT_DEROGATION);  // dérogation Éco lancée depuis l'appli
    assert(s.decodeModeFromRaw(0x23) == M::CONFORT_DEROGATION);
    assert(s.decodeModeFromRaw(0x26) == M::REDUIT_DEROGATION);
    assert(s.decodeModeFromRaw(0x27) == M::CONFORT_DEROGATION);
    // Modes permanents : bit 0x20 absent.
    assert(s.decodeModeFromRaw(0x05) == M::CONFORT_PERMANENT);  // Confort permanent depuis l'appli
    assert(s.decodeModeFromRaw(0x01) == M::CONFORT_PERMANENT);
    assert(s.decodeModeFromRaw(0x00) == M::REDUIT_PERMANENT);   // Éco permanent depuis l'appli
    assert(s.decodeModeFromRaw(0x04) == M::REDUIT_PERMANENT);
    assert(s.decodeModeFromRaw(0x10) == M::HORS_GEL);
    // Bits non documentés : refus explicite.
    assert(s.decodeModeFromRaw(0x08) == M::INCONNU);
    assert(s.decodeModeFromRaw(0x11) == M::INCONNU);
    assert(s.decodeModeFromRaw(0x40) == M::INCONNU);
    assert(s.decodeModeFromRaw(0x80) == M::INCONNU);
    assert(s.decodeModeFromRaw(0xFF) == M::INCONNU);
    // Les valeurs émises sont celles d'un satellite physique et se relisent à l'identique.
    const M modes[] = {M::REDUIT_PERMANENT, M::CONFORT_PERMANENT, M::REDUIT_DEROGATION,
                       M::CONFORT_DEROGATION, M::REDUIT_AUTO, M::CONFORT_AUTO, M::HORS_GEL};
    for (M mode : modes) {
        assert(s.decodeModeFromRaw(s.encodeModeForSend(mode)) == mode);
    }
    assert(s.encodeModeForSend(M::CONFORT_AUTO) == 0x25);
    assert(s.encodeModeForSend(M::REDUIT_AUTO) == 0x24);
    assert(s.encodeModeForSend(M::CONFORT_PERMANENT) == 0x05);
    assert(s.encodeModeForSend(M::REDUIT_PERMANENT) == 0x00);
    assert(s.encodeModeForSend(M::REDUIT_DEROGATION) == 0x22);
    puts("OK: satellite A02F mode byte (auto bit 0x20, derogation 0x02, confort 0x01, 0x04 ignored)");
}
'''
with tempfile.TemporaryDirectory(prefix='frisquet-satellite-mode-') as temp:
    cpp = Path(temp) / 'test.cpp'; cpp.write_text(code)
    binary = Path(temp) / 'test'
    subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
