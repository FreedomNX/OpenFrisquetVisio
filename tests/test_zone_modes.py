"""Compiler et exécuter les scénarios de zone sur la machine de test."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/zone"
for libs in sorted((ROOT / ".pio/libdeps").glob("*/")):
    if (libs / "ArduinoJson/src/ArduinoJson.h").is_file() and (libs / "RadioLib/src/RadioLib.h").is_file():
        break
else:
    raise SystemExit("Installer les dépendances avec pio pkg install -e heltec_wifi_lora_32_V3.")

with tempfile.TemporaryDirectory() as directory:
    binary = Path(directory) / "zone"
    subprocess.run([
        "c++", "-std=c++11", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-fno-sanitize-recover=undefined",
        "-DBOARD_TTGO_LORA32", "-I", FIXTURE, "-I", ROOT / "include", "-I", ROOT / "src",
        "-I", libs / "ArduinoJson/src", "-I", libs / "RadioLib/src",
        FIXTURE / "scenarios.cpp", FIXTURE / "materiel.cpp",
        ROOT / "src/Config.cpp", ROOT / "src/Frisquet/Zone.cpp", ROOT / "src/Frisquet/Connect.cpp",
        ROOT / "src/Frisquet/HistoriqueVeille.cpp", ROOT / "src/Frisquet/Satellite.cpp", ROOT / "src/Frisquet/Chaudiere.cpp", ROOT / "src/Frisquet/FrisquetRadio.cpp",
        ROOT / "src/Frisquet/FrisquetDevice.cpp", ROOT / "src/Frisquet/Utils.cpp",
        "-o", binary,
    ], check=True)
    subprocess.run([binary, FIXTURE / "echanges-20261118.txt"], cwd=directory, check=True)

    montage = Path(directory) / "montage"
    subprocess.run([
        "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I", FIXTURE, "-I", ROOT / "src",
        FIXTURE / "montage.cpp", ROOT / "src/Frisquet/StockageHistorique.cpp", "-o", montage,
    ], check=True)
    for cas in ("vierge", "occupee", "panne"):
        subprocess.run([montage, cas], check=True)
