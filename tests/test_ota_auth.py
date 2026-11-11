"""Check that OTA starts with the portal password, including the default."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ota = (ROOT / "src/OTA.h").read_text()
app = (ROOT / "src/App.cpp").read_text()

init_ota = re.search(r"void App::initOta\(\) \{\n(.*?)\n\}", app, re.S)[1]
assert "_cfg.portalPassword().c_str()" in init_ota

with tempfile.TemporaryDirectory(prefix="frisquet-ota-") as temp:
    temp = Path(temp)
    fixture = ROOT / "tests/fixtures/ota-auth"
    for name in ("ArduinoOTA.h", "Logs.h", "test.cpp"):
        shutil.copy(fixture / name, temp / name)
    (temp / "OTA.h").write_text(ota)
    binary = temp / "test"
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-fsanitize=address,undefined", "-I", str(temp),
                    str(temp / "test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

print("OTA auth tests passed")
