"""Publier et archiver automatiquement les notes des firmwares, sans dépendance."""

import argparse
from collections import Counter
from dataclasses import dataclass
from datetime import datetime, timedelta
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]
HEADINGS = re.compile(r"^## (.+)$", re.MULTILINE)
RELEASE = re.compile(r"(\d{2}\.\d{2}\.\d{2}\.\d{4}) — (\d{4}-\d{2}-\d{2})")
SOURCE_TITLES = {"À venir", "Notes intégrées à main"}
ARCHIVE_NAME = "CHANGELOG.md"
SOURCE_NAME = "changelog-source.md"
ENVIRONMENTS = ("heltec_wifi_lora_32_V3", "heltec_wifi_lora_32_V4",
                "heltec_wifi_lora_32_V4_3", "ttgo_lora32_v21")


@dataclass(frozen=True)
class Entry:
    version: str
    date: str
    notes: str


def version_date(version):
    if not re.fullmatch(r"\d{2}\.\d{2}\.\d{2}\.\d{4}", version):
        raise ValueError("Version attendue : AA.MM.JJ.HHmm.")
    yy, month, day, hm = version.split(".")
    return datetime(2000 + int(yy), int(month), int(day), int(hm[:2]), int(hm[2:]))


def clean_notes(body):
    body = re.sub(r"<!--.*?-->", "", body, flags=re.DOTALL)
    body = re.sub(r"^### [^\n]+\n(?=\s*(?:### |\Z))", "", body, flags=re.MULTILINE)
    return body.strip()


def parse(text):
    headings = list(HEADINGS.finditer(text))
    if not headings:
        raise ValueError("Le changelog ne contient aucune section.")
    source = headings[0].group(1) in SOURCE_TITLES
    pending = text[headings[0].end():headings[1].start() if len(headings) > 1 else len(text)] if source else ""
    entries = []
    for i in range(int(source), len(headings)):
        heading = headings[i]
        match = RELEASE.fullmatch(heading.group(1))
        if not match:
            raise ValueError("Titre de version invalide : " + heading.group(1))
        version, date = match.groups()
        if version_date(version).date().isoformat() != date:
            raise ValueError("La date ne correspond pas à la version " + version)
        end = headings[i + 1].start() if i + 1 < len(headings) else len(text)
        notes = clean_notes(text[heading.end():end])
        if not re.search(r"^- \S", notes, re.MULTILINE):
            raise ValueError("La version " + version + " ne contient aucune note.")
        if entries and version >= entries[-1].version:
            raise ValueError("Les versions doivent être uniques et classées de la plus récente à la plus ancienne.")
        entries.append(Entry(version, date, notes))
    return pending, entries


def note_blocks(body):
    """Garder les puces multilignes et leur catégorie, sans les catégories vides."""
    blocks = []
    category, lines = "Changements", []

    def finish():
        if lines:
            blocks.append((category, "\n".join(lines).strip()))
            lines.clear()

    for line in clean_notes(body).splitlines():
        if line.startswith("### "):
            finish()
            category = line[4:].strip()
        elif line.startswith("- ") and line[2:].strip():
            finish()
            lines.append(line.rstrip())
        elif lines and (not line.strip() or line.startswith((" ", "\t"))):
            lines.append(line.rstrip())
        elif line.strip():
            raise ValueError("Note attendue sous forme de puce : " + line)
    finish()
    return blocks


def new_notes(current, baseline=""):
    previous = Counter(note_blocks(parse(baseline)[0])) if baseline else Counter()
    sections = {}
    for category, block in note_blocks(parse(current)[0]):
        key = (category, block)
        if previous[key]:
            previous[key] -= 1
        else:
            sections.setdefault(category, []).append(block)
    return "\n\n".join("### " + category + "\n\n" + "\n".join(blocks)
                         for category, blocks in sections.items()) or (
        "### Changements\n\n- Mise à jour du firmware sans nouvelle note de changement.")


def archive(current, version, previous="", baseline=""):
    date = version_date(version).date().isoformat()
    old = parse(previous)[1] if previous else []
    if old and version <= old[0].version:
        raise ValueError("La version doit être postérieure au dernier historique publié.")
    entries = [Entry(version, date, new_notes(current, baseline)), *old]
    intro = ("# Historique des changements\n\n"
             "Historique généré après validation des firmwares. Les versions antérieures à ce\n"
             "fichier restent consultables dans les [releases GitHub]"
             "(https://github.com/FreedomNX/OpenFrisquetVisio/releases).\n\n")
    return intro + "\n\n".join("## " + entry.version + " — " + entry.date + "\n\n" + entry.notes
                                for entry in entries) + "\n"


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def ci_version(timestamp=None):
    moment = datetime.fromisoformat(timestamp.replace("Z", "+00:00")) if timestamp else datetime.now(ZoneInfo("Europe/Paris"))
    if moment.tzinfo is None:
        raise ValueError("L'horodatage CI doit inclure son fuseau.")
    version = moment.astimezone(ZoneInfo("Europe/Paris")).strftime("%y.%m.%d.%H%M")
    commit = os.environ.get("GITHUB_SHA")
    if commit:
        # Une relance du même workflow réutilise son tag. Deux pushes dans la
        # même minute reçoivent des versions distinctes, sans écraser une release.
        versions = []
        for tag in git("tag", "--merged", commit).splitlines():
            if re.fullmatch(r"\d{2}\.\d{2}\.\d{2}\.\d{4}", tag):
                try:
                    version_date(tag)
                except ValueError:
                    continue
                if tag >= version and git("rev-parse", tag + "^{commit}") == commit:
                    return tag
                versions.append(tag)
        if versions and max(versions) >= version:
            version = (version_date(max(versions)) + timedelta(minutes=1)).strftime("%y.%m.%d.%H%M")
    version_date(version)
    return version


def gh(*args):
    return subprocess.check_output(["gh", *args], cwd=ROOT, text=True).strip()


def publish(version, artifacts, output):
    version_date(version)
    repo = os.environ["GH_REPO"]
    commit = os.environ["GITHUB_SHA"]
    files = [artifacts / ("firmware-" + env + ".bin") for env in ENVIRONMENTS]
    files.append(artifacts / "SHA256SUMS")
    if any(not path.is_file() or not path.stat().st_size for path in files):
        raise ValueError("Les quatre firmwares et SHA256SUMS doivent être présents et non vides.")
    pages = json.loads(gh("api", "--paginate", "--slurp", "repos/" + repo + "/releases"))
    releases = [release for page in pages for release in page]
    existing = next((release for release in releases if release["tag_name"] == version), None)
    required = {path.name for path in files} | {ARCHIVE_NAME, SOURCE_NAME}
    if existing:
        target = existing["target_commitish"] if existing["draft"] else gh(
            "api", "repos/" + repo + "/commits/" + version, "--jq", ".sha")
        if target != commit:
            raise ValueError("Cette version désigne déjà un autre commit ; aucun écrasement effectué.")
        if not existing["draft"]:
            if not required.issubset({asset["name"] for asset in existing["assets"]}):
                raise ValueError("La release publiée est incomplète ; elle a été préservée.")
            print("Release déjà publiée : " + version)
            return

    def release_order(release):
        try:
            return version_date(release["tag_name"])
        except ValueError:
            return datetime.min

    previous = None
    for release in sorted(releases, key=release_order, reverse=True):
        if release["draft"] or release["prerelease"] or release["tag_name"] == version:
            continue
        # L'historique ne doit pas inclure les releases d'une autre branche.
        if subprocess.run(["git", "merge-base", "--is-ancestor", release["tag_name"], commit],
                          cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
            previous = release
            break
    output.mkdir(parents=True, exist_ok=True)
    old_archive = old_source = ""
    if previous:
        assets = {asset["name"] for asset in previous["assets"]}
        available = assets & {ARCHIVE_NAME, SOURCE_NAME}
        if available and len(available) != 2:
            raise ValueError("La précédente release ne contient pas un historique complet.")
        if available:
            directory = output / "previous"
            directory.mkdir(exist_ok=True)
            gh("release", "download", previous["tag_name"], "--repo", repo,
               "--pattern", ARCHIVE_NAME, "--pattern", SOURCE_NAME, "--dir", str(directory), "--clobber")
            old_archive = (directory / ARCHIVE_NAME).read_text(encoding="utf-8")
            old_source = (directory / SOURCE_NAME).read_text(encoding="utf-8")
            entries = parse(old_archive)[1]
            if not entries or entries[0].version != previous["tag_name"]:
                raise ValueError("L'historique téléchargé ne correspond pas à la précédente release.")
    current = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    full = archive(current, version, old_archive, old_source)
    (output / ARCHIVE_NAME).write_text(full, encoding="utf-8")
    (output / SOURCE_NAME).write_text(current, encoding="utf-8")
    notes = parse(full)[1][0].notes
    run_url = os.environ["GITHUB_SERVER_URL"] + "/" + repo + "/actions/runs/" + os.environ["GITHUB_RUN_ID"]
    notes += ("\n\n---\n\nVersion " + version + ", compilée depuis main, commit " + commit
              + ".\nTests Python et Node et compilations des quatre cartes réussis.\n\n"
              + "Exécution CI : " + run_url + "\n\n"
              + "Télécharger le firmware-<environnement>.bin correspondant à la carte pour une mise à jour OTA.\n"
              + "Pour un premier flash USB, utiliser Upload avec PlatformIO en mode esptool : il écrit\n"
              + "automatiquement bootloader, partitions, boot_app0 et application.\n"
              + "Procédure : https://github.com/" + repo + "/blob/" + commit + "/README.md\n\n"
              + "CHANGELOG.md contient l'historique versionné ; SHA256SUMS permet de vérifier les binaires.\n")
    notes_path = output / "release-notes.md"
    notes_path.write_text(notes, encoding="utf-8")
    uploads = [str(path) for path in [*files, output / ARCHIVE_NAME, output / SOURCE_NAME]]
    # Les fichiers sont tous joints au brouillon avant de rendre la release visible.
    # Une relance peut terminer uniquement notre propre brouillon, jamais écraser
    # les fichiers d'une release déjà publiée.
    if existing:
        gh("release", "upload", version, *uploads, "--repo", repo, "--clobber")
        gh("release", "edit", version, "--repo", repo, "--notes-file", str(notes_path))
    else:
        # Réserver le tag sans force avant publication : une création concurrente
        # ne doit jamais faire pointer le tag vers un autre firmware.
        subprocess.run(["git", "push", "origin", commit + ":refs/tags/" + version], cwd=ROOT, check=True)
        gh("release", "create", version, *uploads, "--repo", repo, "--target", commit,
           "--title", version, "--notes-file", str(notes_path), "--draft", "--latest=false")
    latest = gh("api", "repos/" + repo + "/git/ref/heads/main", "--jq", ".object.sha") == commit
    gh("release", "edit", version, "--repo", repo, "--draft=false", "--latest=" + str(latest).lower())
    print("Release et changelog publiés : " + version)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check", help="Vérifier les notes rédigées dans le dépôt")
    command = commands.add_parser("ci", help="Choisir la version commune des quatre firmwares")
    command.add_argument("--timestamp", help="Date ISO de création du workflow, stable lors des relances")
    command = commands.add_parser("archive", help="Générer l'historique sans publier ni modifier la source")
    command.add_argument("version")
    command.add_argument("--previous", type=Path)
    command.add_argument("--baseline", type=Path)
    command.add_argument("--output", required=True, type=Path)
    command = commands.add_parser("publish", help="Publier les firmwares validés et leur historique sur GitHub")
    command.add_argument("version")
    command.add_argument("--artifacts", required=True, type=Path)
    command.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    pending, _ = parse(text)
    note_blocks(pending)
    if args.command == "ci":
        print("version=" + ci_version(args.timestamp))
    elif args.command == "archive":
        previous = args.previous.read_text(encoding="utf-8") if args.previous else ""
        baseline = args.baseline.read_text(encoding="utf-8") if args.baseline else ""
        args.output.write_text(archive(text, args.version, previous, baseline), encoding="utf-8")
    elif args.command == "publish":
        publish(args.version, args.artifacts.resolve(), args.output.resolve())
    else:
        print("Changelog valide.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        print("Changelog : " + str(error), file=sys.stderr)
        sys.exit(1)
