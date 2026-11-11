"""Tester les handlers du portail avec la réservation radio et des requêtes synthétiques."""

from cooperative_radio_fixture import ROOT, FIXTURE, extract, write_radio_fixture
import subprocess
import tempfile
from pathlib import Path

source = (ROOT / "src/Portal.cpp").read_text()
# Les trois handlers doivent refuser le mode développeur désactivé avant
# d'exposer l'état radio ; leurs gardes sont exécutées dans la fixture C++.
developer_handlers = []
for name in ("handleSendRadio", "handleMemoryRead", "handleMemoryScan"):
    handler = extract(source, f"void Portal::{name}()")
    # Seul le préambule est nécessaire : le décodage des commandes reste hors périmètre.
    developer_handlers.append(handler[:handler.index("  if (_srv.method()")] + handler[-1:])

signatures = (
    "static bool isValidIPv4(",
    "static bool parseBoolArg(",
    "bool Portal::requireRadioAvailable(",
    "bool Portal::requireDeveloperMode(",
    "void Portal::handlePostConfig(",
    "void Portal::handlePairConnect(",
    "void Portal::handlePairSondeExt(",
    "void Portal::handlePairSatelliteZ1(",
    "void Portal::handlePairSatelliteZ2(",
    "void Portal::handlePairSatelliteZ3(",
    "void Portal::handleRecupNetworkId(",
)
with tempfile.TemporaryDirectory(prefix="ofv-cooperative-portal-") as directory:
    temp = Path(directory)
    write_radio_fixture(temp)
    (temp / "portal-methods.inc").write_text(
        "\n\n".join([extract(source, signature) for signature in signatures] + developer_handlers)
    )
    binary = temp / "test"
    subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-parameter",
            "-Wno-sign-compare",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(temp),
            "-I",
            str(FIXTURE),
            "-I",
            str(ROOT / "src"),
            str(FIXTURE / "portal.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary), "config"], check=True)
    subprocess.run([str(binary), "pairing"], check=True)
