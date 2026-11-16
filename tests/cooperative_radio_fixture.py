"""Préparer les inclusions du firmware utilisées par les tests C++ coopératifs."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/cooperative-radio"


def extract(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def stripped(path):
    return re.sub(r"^#.*\n", "", path.read_text(), flags=re.M)


def write_radio_fixture(directory):
    types = stripped(ROOT / "src/Frisquet/Utils.h")
    types = extract(types, "struct fword") + ";"
    (directory / "radio-types.inc").write_text(types + "\n")
    declarations = stripped(ROOT / "src/Buffer.h")
    declarations += stripped(ROOT / "src/Frisquet/FrisquetRadio.h")
    (directory / "radio-declarations.inc").write_text(declarations)
    source = (ROOT / "src/Frisquet/FrisquetRadio.cpp").read_text()
    signatures = (
        "bool FrisquetRadio::beginObservation(",
        "bool FrisquetRadio::ownsObservation(",
        "bool FrisquetRadio::observationReady(",
        "void FrisquetRadio::pollObservation(",
        "void FrisquetRadio::finishObservation(",
        "int16_t FrisquetRadio::takeObservation(",
        "void FrisquetRadio::cancelObservation(",
        "void FrisquetRadio::setNetworkID(",
        "void FrisquetRadio::cancelOperations(",
        "void FrisquetRadio::purgeBufferedPackets(",
        "bool FrisquetRadio::bufferUnexpectedPacket(",
        "bool FrisquetRadio::popBufferedPacket(",
        "bool FrisquetRadio::hasBufferedPacket(",
        "int16_t FrisquetRadio::sendAsk(",
        "int16_t FrisquetRadio::sendInit(",
        "int16_t FrisquetRadio::sendAnswer(",
        "int16_t FrisquetRadio::receiveExpected(",
    )
    (directory / "radio-methods.inc").write_text(
        "\n\n".join(extract(source, signature) for signature in signatures) + "\n"
    )
