"""Préparer le changelog et sélectionner les notes/version de la CI, sans dépendance."""

import argparse
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
import re
import subprocess
import sys
from zoneinfo import ZoneInfo


ROOT = Path(__file__).resolve().parents[1]
CATEGORIES = ("Ajouts", "Corrections", "Changements", "Actions nécessaires après mise à jour")
TEMPLATE = "\n" + "\n\n".join("### " + name for name in CATEGORIES) + "\n\n"
HEADINGS = re.compile(r"^## (.+)$", re.MULTILINE)
RELEASE = re.compile(r"(\d{2}\.\d{2}\.\d{2}\.\d{4}) — (\d{4}-\d{2}-\d{2})")


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
    # Les catégories vides servent de modèle, mais ne sont pas publiées.
    body = re.sub(r"<!--.*?-->", "", body, flags=re.DOTALL)
    body = re.sub(r"^### [^\n]+\n(?=\s*(?:### |\Z))", "", body, flags=re.MULTILINE)
    return body.strip()


def has_notes(body):
    return bool(re.search(r"^- \S", clean_notes(body), re.MULTILINE))


def parse(text):
    headings = list(HEADINGS.finditer(text))
    if not headings or headings[0].group(1) != "À venir":
        raise ValueError("Le changelog doit commencer par la section « ## À venir ».")
    entries = []
    for i, heading in enumerate(headings[1:], 1):
        match = RELEASE.fullmatch(heading.group(1))
        if not match:
            raise ValueError("Titre de version invalide : " + heading.group(1))
        version, date = match.groups()
        if version_date(version).date().isoformat() != date:
            raise ValueError("La date ne correspond pas à la version " + version)
        end = headings[i + 1].start() if i + 1 < len(headings) else len(text)
        notes = clean_notes(text[heading.end():end])
        if not has_notes(notes):
            raise ValueError("La version " + version + " ne contient aucune note.")
        if entries and version >= entries[-1].version:
            raise ValueError("Les versions doivent être uniques et classées de la plus récente à la plus ancienne.")
        entries.append(Entry(version, date, notes))
    end = headings[1].start() if len(headings) > 1 else len(text)
    return text[headings[0].end():end], entries


def prepare(text, version):
    date = version_date(version).date().isoformat()
    pending, entries = parse(text)
    if not has_notes(pending):
        raise ValueError("La section « À venir » ne contient aucun changement à publier.")
    if entries and version <= entries[0].version:
        raise ValueError("La nouvelle version doit être postérieure à la dernière version préparée.")
    headings = list(HEADINGS.finditer(text))
    history = text[headings[1].start():] if len(headings) > 1 else ""
    prefix = text[:headings[0].end()]
    return (prefix + "\n" + TEMPLATE + "## " + version + " — " + date
            + "\n\n" + clean_notes(pending) + "\n\n" + history)


def release_candidate(current, previous):
    pending, entries = parse(current)
    old = parse(previous)[1] if previous else []
    if entries == old:
        return None
    if not entries or entries[1:] != old:
        raise ValueError("Ajouter une seule version par push, sans modifier les versions déjà archivées.")
    if old and entries[0].version <= old[0].version:
        raise ValueError("La nouvelle version doit être postérieure à la précédente.")
    if has_notes(pending):
        raise ValueError("La section « À venir » doit être vide pour publier une nouvelle version.")
    return entries[0]


def previous_changelog(before, root):
    if not re.fullmatch(r"[0-9a-f]{40}", before):
        raise ValueError("Le commit précédent doit être un SHA Git complet.")
    if before == "0" * 40:
        return ""
    subprocess.run(["git", "cat-file", "-e", before + "^{commit}"], cwd=root, check=True)
    present = subprocess.check_output(
        ["git", "ls-tree", "--name-only", before, "--", "CHANGELOG.md"], cwd=root, text=True)
    if not present.strip():
        return ""
    return subprocess.check_output(["git", "show", before + ":CHANGELOG.md"], cwd=root, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("prepare", help="Archiver « À venir » sous une nouvelle version")
    command.add_argument("version", nargs="?", help="AA.MM.JJ.HHmm ; par défaut, heure de Paris")
    command = commands.add_parser("notes", help="Extraire les notes d'une version")
    command.add_argument("version")
    commands.add_parser("check", help="Vérifier la structure du changelog")
    command = commands.add_parser("ci", help="Afficher version et autorisation de publication pour GitHub Actions")
    command.add_argument("--before", help="SHA de main avant le push ; absent sur PR et develop")
    args = parser.parse_args()
    path = ROOT / "CHANGELOG.md"
    text = path.read_text(encoding="utf-8")
    pending, entries = parse(text)
    if args.command == "prepare":
        version = args.version or datetime.now(ZoneInfo("Europe/Paris")).strftime("%y.%m.%d.%H%M")
        path.write_text(prepare(text, version), encoding="utf-8")
        print("Version préparée : " + version + ". Relire puis committer CHANGELOG.md avant la fusion sur main.")
    elif args.command == "notes":
        entry = next((entry for entry in entries if entry.version == args.version), None)
        if entry is None:
            raise ValueError("Version absente du changelog : " + args.version)
        print(entry.notes)
    elif args.command == "ci":
        entry = release_candidate(text, previous_changelog(args.before, ROOT)) if args.before else None
        version = entry.version if entry else datetime.now(ZoneInfo("Europe/Paris")).strftime("%y.%m.%d.%H%M")
        print("version=" + version)
        print("publish=" + str(entry is not None).lower())
    else:
        print("Changelog valide ; " + str(len(entries)) + " version(s) archivée(s).")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print("Changelog : " + str(error), file=sys.stderr)
        sys.exit(1)
