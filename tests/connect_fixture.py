"""Préparer les méthodes Connect partagées par les scénarios natifs."""

from cooperative_radio_fixture import ROOT, FIXTURE, extract, stripped, write_radio_fixture
import subprocess


def run_connect_fixture(directory, scenario, extra_methods=(), services=False):
    write_radio_fixture(directory, full_types=True)
    (directory / "connect-declarations.inc").write_text(
        stripped(ROOT / "src/Frisquet/RadioSchedule.h")
    )
    (directory / "connect-class.inc").write_text(
        stripped(ROOT / "src/Frisquet/Connect.h").replace("private:", "public:")
    )
    source = (ROOT / "src/Frisquet/Connect.cpp").read_text()
    signatures = (
        "bool Connect::setVersionFirmware(",
        "bool Connect::applyInformation(",
        "bool Connect::applyConsumption(",
        "bool Connect::applyEcs(",
        "bool Connect::setConsommationAnnuelle(",
        "void Connect::setTemperatureExterieure(",
        "void Connect::setTemperatureECS(",
        "void Connect::setTemperatureCDC(",
        "void Connect::setPression(",
        "void Connect::setConsommationECS(",
        "void Connect::setConsommationChauffage(",
        "Connect::MODE_ECS Connect::getModeECS(",
        "bool Connect::setModeECS(MODE_ECS",
        "bool Connect::setModeECS(const String&",
        "void Connect::setModeECSFromRaw(",
    )
    (directory / "connect-methods.inc").write_text(
        extract(source, "struct ConsommationMensuelle {") + ";\n"
        + "\n\n".join(extract(source, s) for s in (*signatures, *extra_methods))
    )
    if services:
        (directory / "services-methods.inc").write_text("\n\n".join(
            extract((ROOT / path).read_text(), signature)
            for path, signature in (
                ("src/FrisquetManager.cpp", "void FrisquetManager::loop()"),
                ("src/App.cpp", "void App::loop()"),
            )
        ))
    binary = directory / "test"
    subprocess.run([
        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-misleading-indentation",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(directory), "-I", str(FIXTURE), "-I", str(ROOT / "src"),
        str(FIXTURE / scenario), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
