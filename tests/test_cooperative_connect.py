"""Tester les collectes Connect et la progression des autres services."""

from pathlib import Path
import tempfile
from connect_fixture import run_connect_fixture

with tempfile.TemporaryDirectory(prefix="ofv-cooperative-connect-") as directory:
    run_connect_fixture(Path(directory), "connect.cpp", extra_methods=(
        "void Connect::startReads(",
        "void Connect::pollReads(",
        "void Connect::completeRead(",
        "void Connect::loop(",
        "void Connect::handleModeEcsCommand(",
        "bool Connect::sendPendingModeEcs(",
    ), services=True)
