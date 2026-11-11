"""Tester les commandes ECS différées et la progression des services."""

from cooperative_radio_fixture import ROOT, FIXTURE, extract
import subprocess
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory(prefix="ofv-cooperative-services-") as directory:
    temp = Path(directory)
    source = (ROOT / "src/Frisquet/Connect.cpp").read_text()
    signatures = (
        "void Connect::handleModeEcsCommand(",
        "bool Connect::sendPendingModeEcs(",
        "void Connect::loop()",
    )
    (temp / "commands-methods.inc").write_text(
        "\n\n".join(extract(source, s) for s in signatures)
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
            "-Wno-unused-variable",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(temp),
            "-I",
            str(FIXTURE),
            "-I",
            str(ROOT / "src"),
            str(FIXTURE / "commands.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)

with tempfile.TemporaryDirectory(prefix="ofv-cooperative-services-") as directory:
    temp = Path(directory)
    methods = [
        extract((ROOT / path).read_text(), signature)
        for path, signature in (
            ("src/FrisquetManager.cpp", "void FrisquetManager::loop()"),
            ("src/App.cpp", "void App::loop()"),
        )
    ]
    (temp / "services-methods.inc").write_text("\n\n".join(methods))
    binary = temp / "test"
    subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-parameter",
            "-Wno-unused-variable",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(temp),
            "-I",
            str(FIXTURE),
            "-I",
            str(ROOT / "src"),
            str(FIXTURE / "services.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
