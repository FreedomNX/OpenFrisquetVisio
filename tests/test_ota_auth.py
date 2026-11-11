"""Exécuter les vrais callbacks OTA, l'authentification et le cycle des pauses radio."""
from cooperative_radio_fixture import ROOT, FIXTURE as RADIO_FIXTURE, extract, write_radio_fixture
FIXTURE = ROOT / "tests/fixtures/ota-auth"
from pathlib import Path
import shutil
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix="ofv-ota-") as directory:
    temp = Path(directory)
    write_radio_fixture(temp)
    shutil.copy(ROOT / "src/OTA.h", temp / "OTA.h")
    shutil.copy(FIXTURE / "Logs.h", temp / "Logs.h")
    (temp / "app-ota-methods.inc").write_text(
        extract((ROOT / "src/App.cpp").read_text(), "void App::initOta()")
    )
    source = (ROOT / "src/Portal.cpp").read_text()
    (temp / "portal-ota-methods.inc").write_text("\n\n".join(
        extract(source, signature) for signature in (
            "void Portal::onAuthenticated(", "void Portal::cancelHttpUpdate(",
            "void Portal::handleUpdate()", "void Portal::handleUpdateUpload(",
            "bool Portal::scheduleReboot(",
        )
    ))
    binary = temp / "test"
    subprocess.run([
        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(temp),
        "-I", str(FIXTURE), "-I", str(RADIO_FIXTURE), "-I", str(ROOT / "src"), str(FIXTURE / "test.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
