"""Vérifier les réponses Connect avant toute modification des valeurs publiées."""

from pathlib import Path
import tempfile
from connect_fixture import run_connect_fixture

with tempfile.TemporaryDirectory(prefix="ofv-connect-decoding-") as directory:
    run_connect_fixture(Path(directory), "decoding.cpp")
