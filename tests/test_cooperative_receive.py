"""Tester les observations coopératives avec une radio et une horloge simulées."""

from cooperative_radio_fixture import ROOT, FIXTURE, write_radio_fixture
import subprocess
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory(prefix="ofv-cooperative-radio-") as directory:
    temp = Path(directory)
    write_radio_fixture(temp)
    binary = temp / "test"
    subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-parameter",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(temp),
            "-I",
            str(FIXTURE),
            "-I",
            str(ROOT / "src"),
            str(FIXTURE / "receive.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
