"""Tester les observations satellite différées avec des données radio et MQTT synthétiques."""

from pathlib import Path
from cooperative_radio_fixture import extract, stripped
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "tests/fixtures/satellite-observation"


sat = (ROOT / "src/Frisquet/Satellite.cpp").read_text()
zone = (ROOT / "src/Frisquet/Zone.cpp").read_text()
header = (ROOT / "src/Frisquet/FrisquetRadio.h").read_text()
with tempfile.TemporaryDirectory(prefix="ofv-zone-replay-") as tmp:
    tmp = Path(tmp)
    # Utiliser les trames et le lecteur de tampon du firmware ; seul le résultat de l’observation est simulé.
    wire = ""
    for signature in [
        "enum MessageType",
        "struct RadioTrameHeader",
        "struct RadioTrameInit",
    ]:
        wire += extract(header, signature) + ";\n"
    (tmp / "wire-types.inc").write_text(wire)
    (tmp / "buffer.inc").write_text(stripped(ROOT / "src/Buffer.h"))
    (tmp / "classes.inc").write_text(
        stripped(ROOT / "src/Frisquet/Zone.h").replace("private:", "public:")
        + re.sub(
            r"^#.*\n", "", (ROOT / "src/Frisquet/Satellite.h").read_text(), flags=re.M
        ).replace("private:", "public:")
    )
    methods = zone[zone.index("namespace {") : zone.index("void Zone::loadConfig")]
    for signature in [
        "Zone::MODE_ZONE Zone::getMode(",
        "void Zone::setMode(MODE_ZONE",
        "byte Zone::getModeOptions(",
        "void Zone::setModeOptions(",
        "void Zone::desactiverBoost(",
        "bool Zone::confortActif(",
        "bool Zone::derogationActive(",
        "void Zone::confirmerMode(MODE_ZONE",
        "void Zone::publierMode(",
    ]:
        methods += extract(zone, signature) + "\n"
    for signature in [
        "bool Satellite::onReceive(",
        "Satellite::MODE Satellite::getMode(",
        "void Satellite::setMode(",
        "Satellite::MODE Satellite::decodeModeFromRaw(",
        "uint8_t Satellite::encodeModeForSend(",
        "bool Satellite::setModeFromRaw(",
    ]:
        methods += extract(sat, signature) + "\n"
    for signature in [
        "void Satellite::pollObservation(",
        "bool Satellite::applyObservation(",
    ]:
        methods += extract(sat, signature) + "\n"
    (tmp / "methods.inc").write_text(methods)
    binary = tmp / "replay"
    subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-reorder",
            "-Wno-unused-variable",
            "-Wno-switch",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(tmp),
            str(HERE / "replay.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
