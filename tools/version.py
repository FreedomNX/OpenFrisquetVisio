import os
import re
from datetime import datetime


try:
    script_dir = os.path.dirname(__file__)
    ROOT = os.path.abspath(os.path.join(script_dir, ".."))
except NameError:
    # SCons may not define __file__; fall back to project root from env if available
    scons_env = globals().get("env")
    if scons_env is not None:
        ROOT = scons_env.Dir("#").get_abspath()
    else:
        ROOT = os.getcwd()
HEADER_PATH = os.path.join(ROOT, "src", "Version.h")
README_PATH = os.path.join(ROOT, "README.md")


def generate_version():
    # Format: YY.MM.DD.HHmm
    return datetime.now().strftime("%y.%m.%d.%H%M")


def write_if_changed(path, content):
    try:
        with open(path, "r", encoding="utf-8") as f:
            current = f.read()
        if current == content:
            return False
    except FileNotFoundError:
        pass
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    return True


def update_version_header(version):
    content = (
        "#pragma once\n"
        "\n"
        "#define OPENFRISQUETVISIO_VERSION \"" + version + "\"\n"
    )
    return write_if_changed(HEADER_PATH, content)


def update_readme(version):
    if not os.path.exists(README_PATH):
        return False
    with open(README_PATH, "r", encoding="utf-8") as f:
        content = f.read()
    updated = re.sub(
        r"^\*\*Version :\*\* .*$",
        "**Version :** " + version,
        content,
        flags=re.MULTILINE,
    )
    if updated == content:
        return False
    with open(README_PATH, "w", encoding="utf-8") as f:
        f.write(updated)
    return True


def main():
    version = generate_version()
    update_version_header(version)
    update_readme(version)


main()
