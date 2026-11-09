"""Compiler le test natif de reprise radio avec les méthodes du firmware."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/pairing-recovery"


def extract(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


radio = (ROOT / "src/Frisquet/FrisquetRadio.h").read_text()
types = [extract(radio, signature) + ";" for signature in (
    "enum MessageType", "struct RadioTrameHeader",
)]
methods = [extract((ROOT / path).read_text(), signature) for path, signature in (
    ("src/Frisquet/FrisquetRadio.cpp", "void FrisquetRadio::setNetworkID("),
    ("src/Frisquet/FrisquetDevice.cpp", "bool FrisquetDevice::associer("),
    ("src/FrisquetManager.cpp", "bool FrisquetManager::recupererNetworkID("),
)]

with tempfile.TemporaryDirectory(prefix="ofv-pairing-recovery-") as directory:
    temp = Path(directory)
    # Les inclusions générées proviennent du firmware, sans copie des méthodes
    # dans le test. Les types NetworkID et ReadBuffer sont inclus directement.
    (temp / "radio_types.inc").write_text("\n\n".join(types) + "\n")
    (temp / "production_methods.inc").write_text("\n\n".join(methods) + "\n")
    binary = temp / "test"
    subprocess.run([
        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-variable", "-Wno-deprecated-copy",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(temp), "-I", str(FIXTURE), "-I", str(ROOT / "src"),
        str(FIXTURE / "test.cpp"), "-o", str(binary),
    ], check=True)
    results = [subprocess.run([str(binary), scenario]).returncode
               for scenario in ("device", "manager")]
    assert results == [0, 0], results
