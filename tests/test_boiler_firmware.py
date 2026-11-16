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
active = extract('bool Connect::applyInformation(')
assert active.count('_boilerFirmware.power(') == 2
passive = extract('bool Connect::handlePassiveReadResponse(')
assert passive.count('_boilerFirmware.power(') == 2
assert 'if (adresseMemoire == 0x7541)' in passive
assert 'return setVersionFirmware(buff, length);' in passive


with tempfile.TemporaryDirectory(prefix='ofv-boiler-firmware-') as temp:
    cpp = ROOT / 'tests/fixtures/cooperative-radio/firmware.cpp'
    (Path(temp) / 'firmware-methods.inc').write_text(
        extract('bool Connect::recupererVersionFirmware()') + '\n'
        + extract('bool Connect::setVersionFirmware(')
    )
    binary = Path(temp) / 'test'
    subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I', temp, '-I', str(ROOT / 'src'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
