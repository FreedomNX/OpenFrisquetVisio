"""Exercer la préparation et la sélection CI dans un dépôt Git temporaire."""

from pathlib import Path
from datetime import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
from zoneinfo import ZoneInfo


ROOT = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, ROOT / "tools/changelog.py", "check"], check=True)


with tempfile.TemporaryDirectory(prefix="ofv-changelog-") as directory:
    root = Path(directory)
    (root / "tools").mkdir()
    shutil.copy2(ROOT / "tools/changelog.py", root / "tools/changelog.py")
    changelog = root / "CHANGELOG.md"

    def git(*args):
        return subprocess.check_output(["git", *args], cwd=root, text=True).strip()

    def commit():
        git("add", "CHANGELOG.md")
        git("-c", "user.name=Test", "-c", "user.email=test@example.invalid",
            "commit", "--quiet", "-m", "Fixture changelog")
        return git("rev-parse", "HEAD")

    def cli(*args, success=True):
        result = subprocess.run([sys.executable, "tools/changelog.py", *args],
                                cwd=root, text=True, capture_output=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result.stdout

    git("init", "--quiet")
    (root / "baseline").write_text("Version antérieure au changelog\n")
    git("add", "baseline")
    git("-c", "user.name=Test", "-c", "user.email=test@example.invalid",
        "commit", "--quiet", "-m", "Sans changelog")
    before_changelog = git("rev-parse", "HEAD")
    changelog.write_text("# Historique\n\n## À venir\n\n### Ajouts\n- Première fonction (#10).\n"
                         "\n### Corrections\n\n### Changements\n"
                         "\n### Actions nécessaires après mise à jour\n", encoding="utf-8")
    before_prepare = commit()
    assert "publish=false" in cli("ci", "--before", before_changelog)
    assert "publish=false" in cli("ci", "--before", before_prepare)

    # Une préparation vide/invalide ne doit pas abîmer le fichier.
    for invalid in ("26.02.30.1200", "26.11.24.2500", "26.11.24.0955\npublish=true", "v1.0.0"):
        original = changelog.read_bytes()
        cli("prepare", invalid, success=False)
        assert changelog.read_bytes() == original

    cli("prepare", "26.11.24.0955")
    first = changelog.read_text(encoding="utf-8")
    assert "## 26.11.24.0955 — 2026-11-24" in first
    notes = cli("notes", "26.11.24.0955")
    assert "- Première fonction (#10)." in notes and "### Corrections" not in notes
    assert first.count("Première fonction") == 1
    # La même exécution CI conserve la version, sans modifier le changelog.
    for _ in range(2):
        assert cli("ci", "--before", before_prepare) == "version=26.11.24.0955\npublish=true\n"
        assert changelog.read_text(encoding="utf-8") == first
    # PR et develop ne publient pas et utilisent une version de compilation.
    assert re.fullmatch(r"version=\d{2}\.\d{2}\.\d{2}\.\d{4}\npublish=false\n", cli("ci"))
    cli("prepare", "26.11.24.1000", success=False)  # À venir est vide.
    cli("notes", "26.11.01.0000", success=False)
    released = commit()
    assert "publish=false" in cli("ci", "--before", released)

    # Une nouvelle PR remplit À venir sans republier la version précédente.
    changelog.write_text(first.replace("### Corrections", "### Corrections\n- Deuxième correction (#11).", 1), encoding="utf-8")
    assert "publish=false" in cli("ci", "--before", released)
    for duplicate in ("26.11.24.0955", "26.11.23.0955"):
        original = changelog.read_bytes()
        cli("prepare", duplicate, success=False)
        assert changelog.read_bytes() == original
    cli("prepare", "26.11.25.0900")
    second = changelog.read_text(encoding="utf-8")
    assert cli("ci", "--before", released) == "version=26.11.25.0900\npublish=true\n"
    assert "Deuxième correction" in cli("notes", "26.11.25.0900")
    assert "Première fonction" not in cli("notes", "26.11.25.0900")
    assert cli("notes", "26.11.24.0955") == notes
    # Un push avec plusieurs versions, une archive réécrite ou des notes non
    # intégrées à la version ne doit pas être publié silencieusement.
    cli("ci", "--before", before_prepare, success=False)
    changelog.write_text(second.replace("Première fonction", "Ancienne note réécrite"), encoding="utf-8")
    cli("ci", "--before", released, success=False)
    changelog.write_text(second.replace("### Ajouts", "### Ajouts\n- Changement oublié.", 1), encoding="utf-8")
    cli("ci", "--before", released, success=False)
    changelog.write_text(second.replace("2026-11-25", "2026-11-26"), encoding="utf-8")
    cli("check", success=False)
    changelog.write_text(first, encoding="utf-8")
    assert "publish=true" in cli("ci", "--before", "0" * 40)
    cli("ci", "--before", "invalid", success=False)
    cli("ci", "--before", "f" * 40, success=False)

    # La date par défaut suit Paris et ne dépend pas du fuseau du processus.
    changelog.write_text("# Historique\n\n## À venir\n\n### Ajouts\n- Nouvelle fonction.\n", encoding="utf-8")
    start = datetime.now(ZoneInfo("Europe/Paris")).strftime("%y.%m.%d.%H%M")
    cli("prepare")
    end = datetime.now(ZoneInfo("Europe/Paris")).strftime("%y.%m.%d.%H%M")
    chosen = re.search(r"^## (\d{2}\.\d{2}\.\d{2}\.\d{4})", changelog.read_text(), re.MULTILINE).group(1)
    assert start <= chosen <= end

    # Exécuter la vraie étape de publication, en remplaçant uniquement gh.
    # Aucune release n'est créée ; vérifier son texte et les quatre fichiers.
    workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")

    def workflow_shell(name):
        step = workflow.split("      - name: " + name + "\n", 1)[1]
        match = re.search(r"        run: \|\n((?: {10}[^\n]*\n|\n)+)", step)
        return textwrap.dedent(match.group(1))

    mockbin = root / "mockbin"
    mockbin.mkdir()
    gh = mockbin / "gh"
    gh.write_text('''#!/usr/bin/env python3
import json, os, pathlib, sys
args = sys.argv[1:]
if args[0] == 'api':
    print(os.environ['GITHUB_SHA'])
else:
    assert args[:2] == ['release', 'create']
    notes = pathlib.Path(args[args.index('--notes-file')+1]).read_text()
    pathlib.Path('published.json').write_text(json.dumps({'args':args,'notes':notes}))
''')
    gh.chmod(0o755)
    artifacts = root / "firmware"
    artifacts.mkdir()
    for env in ("heltec_wifi_lora_32_V3", "heltec_wifi_lora_32_V4", "heltec_wifi_lora_32_V4_3", "ttgo_lora32_v21"):
        (artifacts / ("firmware-" + env + ".bin")).write_bytes(b"firmware de test")
    (artifacts / "SHA256SUMS").write_text("sommes de test\n")
    env = dict(os.environ, PATH=str(mockbin) + os.pathsep + os.environ["PATH"],
               FIRMWARE_VERSION=chosen, RUNNER_TEMP=str(root), GH_REPO="test/ofv",
               GITHUB_SHA="1" * 40, GITHUB_SERVER_URL="https://github.com",
               GITHUB_REPOSITORY="test/ofv", GITHUB_RUN_ID="123")
    subprocess.run(["bash", "-e", "-c", workflow_shell("Publier dans GitHub Releases")],
                   cwd=root, env=env, check=True)
    published = json.loads((root / "published.json").read_text())
    assert published["args"][2] == chosen
    assert len([arg for arg in published["args"] if arg.endswith(".bin")]) == 4
    assert published["notes"].count("- Nouvelle fonction.") == 1
    assert "boot_app0" in published["notes"] and "SHA256SUMS" in published["notes"]
    assert "### Corrections" not in published["notes"]

    # Les sorties de la vraie étape version pilotent les artefacts et la release.
    output = root / "github-output"
    changelog.write_text(first, encoding="utf-8")
    env.update(GITHUB_OUTPUT=str(output), GITHUB_EVENT_NAME="push", GITHUB_REF="refs/heads/main",
               BEFORE_SHA=before_prepare)
    subprocess.run(["bash", "-e", "-c", workflow_shell("Définir une version commune aux quatre cartes")],
                   cwd=root, env=env, check=True)
    assert output.read_text() == "version=26.11.24.0955\npublish=true\n"
    output.write_text("")
    env["BEFORE_SHA"] = released
    subprocess.run(["bash", "-e", "-c", workflow_shell("Définir une version commune aux quatre cartes")],
                   cwd=root, env=env, check=True)
    assert "publish=false" in output.read_text()

print("OK: préparation, dates, historique conservé, extraction, version CI, publication unique et erreurs sans écrasement")
