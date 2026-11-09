"""Check the real PlatformIO version script with and without a CI override."""
import os
from pathlib import Path
import re
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'tools/version.py').read_text()


def run_version(root, version):
    env = {}
    namespace = {'__file__': str(root / 'tools/version.py'), 'env': env,
                 'Import': lambda name: None}
    with patch.dict(os.environ):
        os.environ.pop('OPENFRISQUETVISIO_VERSION', None)
        if version is not None:
            os.environ['OPENFRISQUETVISIO_VERSION'] = version
        exec(compile(source, 'tools/version.py', 'exec'), namespace)
    return env['OPENFRISQUETVISIO_VERSION']


with tempfile.TemporaryDirectory(prefix='ofv-version-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    readme = root / 'README.md'
    header = root / 'src/Version.h'
    readme.write_text('Firmware\n**Version :** ancienne\n')
    for version in ('26.11.03', '26.10.27.1007', '26.10.22.1250'):
        # Separate matrix jobs must embed exactly the same supplied version.
        for _ in range(4):
            assert run_version(root, version) == version
            assert f'#define OPENFRISQUETVISIO_VERSION "{version}"' in header.read_text()
            assert readme.read_text() == f'Firmware\n**Version :** {version}\n'
    generated = run_version(root, None)
    assert re.fullmatch(r'\d{2}\.\d{2}\.\d{2}\.\d{4}', generated)
    before = (header.read_text(), readme.read_text())
    for invalid in ('', 'main-42-1', '26.11.09.1830\n', '26.11.09"'):
        try:
            run_version(root, invalid)
        except ValueError:
            pass
        else:
            raise AssertionError(f'Invalid version accepted: {invalid!r}')
        assert (header.read_text(), readme.read_text()) == before

print('OK: shared CI version, date/date-time formats, embedded header, README, local fallback and invalid overrides')
