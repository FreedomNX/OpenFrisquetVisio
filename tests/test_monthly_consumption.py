"""Vérifier le cache mensuel avec le décodeur réellement utilisé par Connect."""

from pathlib import Path
import json
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
    portal = (ROOT / "src/Portal.cpp").read_text()
    (temp / "monthly-json.inc").write_text(extract(portal, "static String jsonConsommationsMensuelles("))
    subprocess.run([
        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(temp), str(fixture / "serialization.cpp"), "-o", str(binary),
    ], check=True)
    values = [json.loads(line) for line in subprocess.check_output([str(binary)], text=True).splitlines()]
    assert values[0] is None
    assert values[1] == {"year": None, "month": None, "receivedAt": None, "complete": False, "heating": [0] * 12, "hotWater": [0] * 12}
    assert values[2] == {"year": 2026, "month": 11, "receivedAt": "16/11/2026 11:50:00", "complete": False,
                         "heating": list(range(1000, 1012)), "hotWater": list(range(10, 22))}
    assert values[3]["year"] == 2026
    assert values[3]["month"] == 12
    assert values[3]["receivedAt"] == "31/12/2026 23:59:59"
    assert values[3]["heating"][-1] == 65535
    assert values[4]["year"] == 2027
    assert values[4]["month"] == 1
    assert values[4]["receivedAt"] == "01/01/2027 00:00:00"
    assert values[5]["year"] == 2025 and values[5]["month"] == 12 and values[5]["complete"] is True
    assert values[5]["receivedAt"] == "16/11/2026 11:50:00"
    assert values[5]["heating"][-1] == 65535
    assert values[6]["year"] == 2026 and values[6]["complete"] is True
    assert values[7]["year"] is None and values[7]["month"] is None and values[7]["complete"] is True
    assert values[8] is None
    print("OK : JSON des deux années, année précédente complète et dates conservées")
