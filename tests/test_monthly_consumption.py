"""Vérifier le cache mensuel avec le décodeur réellement utilisé par Connect."""

from pathlib import Path
import subprocess
import tempfile
from cooperative_radio_fixture import ROOT, extract

fixture = ROOT / "tests/fixtures/monthly-consumption"
header = (ROOT / "src/Frisquet/Connect.h").read_text()
source = (ROOT / "src/Frisquet/Connect.cpp").read_text()

with tempfile.TemporaryDirectory(prefix="ofv-monthly-") as directory:
    temp = Path(directory)
    (temp / "monthly-state.inc").write_text(
        extract(header, "struct ConsommationsMensuelles") + ";\n"
        + extract(header, "const ConsommationsMensuelles& getConsommationsMensuelles()") + "\n"
        + extract(header, "const ConsommationsMensuelles& getConsommationsAnneePrecedente()") + "\n"
        + "\n".join(line for line in header.splitlines() if "ConsommationsMensuelles _consommations" in line)
    )
    (temp / "monthly-types.inc").write_text(
        extract((ROOT / "src/Frisquet/Utils.h").read_text(), "struct fword") + ";\n"
        + "namespace FrisquetRadio {\n"
        + extract((ROOT / "src/Frisquet/FrisquetRadio.h").read_text(), "struct RadioTrameHeader")
        + ";\n}\n" + extract(source, "struct ConsommationMensuelle") + ";\n"
    )
    (temp / "monthly-method.inc").write_text(extract(source, "bool Connect::setConsommationAnnuelle("))
    (temp / "monthly-query.inc").write_text(extract(source, "bool Connect::recupererConsommationAnnuelle("))
    binary = temp / "test"
    subprocess.run([
        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(temp), "-I", str(ROOT / "src"), str(fixture / "decoding.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
