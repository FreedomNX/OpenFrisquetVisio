"""Run tools/web_assets.py outside PlatformIO and check the header it writes.

Run: python3 tests/test_web_assets.py
"""
from pathlib import Path
import gzip
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from web_assets import firmware_version, generate  # noqa: E402

VERSION = "26.01.02.0304"
HEADER = "src/generated/WebAssets.h"

# Compressed assets the portal serves, by symbol in the generated header.
STATIC_FILENAMES = {
    "THEME_CSS": "theme.css", "THEME_JS": "theme.js", "INDEX_HTML": "index.html",
    "SETTINGS_HTML": "settings.html", "PROGRAMMING_HTML": "programming.html",
    "LOGS_HTML": "logs.html", "MEMORY_HTML": "memory.html",
    "RADIO_LOGS_HTML": "radio-logs.html", "LOGO_PNG": "logo.png",
}

STATIC_ASSET = re.compile(
    r'static const uint8_t (\w+)_GZIP\[\] PROGMEM = \{\n(.*?)\n\};\n'
    r'static const StaticAsset \1 = \{\1_GZIP, sizeof\(\1_GZIP\), "\\"([0-9a-f]{40})\\"", "([^"]+)"\};',
    re.S,
)
RAW_ASSET = re.compile(r'static const char (\w+)\[\] PROGMEM = R"(\w+)\((.*?)\)\2";', re.S)


def project_copy(parent, name):
    """A project holding only web/, so the header lands outside the repo."""
    project_dir = Path(parent) / name
    shutil.copytree(ROOT / "web", project_dir / "web")
    return project_dir


def served_bytes(filename, content_type):
    source = (ROOT / "web" / filename).read_bytes()
    if "charset=" not in content_type:
        return source
    return source.decode("utf-8").replace("{{OPENFRISQUETVISIO_VERSION}}", VERSION).encode("utf-8")


with tempfile.TemporaryDirectory() as temp:
    first = project_copy(temp, "first")
    generate(first, VERSION)
    header = (first / HEADER).read_text()

    # Same sources, same version: same bytes, so the ETag survives a rebuild.
    second = project_copy(temp, "second")
    generate(second, VERSION)
    assert (second / HEADER).read_text() == header

    static_assets = {
        symbol: (bytes(int(byte, 16) for byte in re.findall(r"0x([0-9a-f]{2})", byte_text)), etag, content_type)
        for symbol, byte_text, etag, content_type in STATIC_ASSET.findall(header)
    }
    assert static_assets.keys() == STATIC_FILENAMES.keys(), static_assets.keys()

    for symbol, (body, etag, content_type) in static_assets.items():
        assert gzip.decompress(body) == served_bytes(STATIC_FILENAMES[symbol], content_type), symbol
        assert etag == hashlib.sha1(body).hexdigest(), symbol
        # Bytes 4 to 7 of a gzip header hold its creation time, which would
        # change the ETag on every build. @see https://www.rfc-editor.org/rfc/rfc1952#section-2.3
        assert body[4:8] == bytes(4), symbol

    assert static_assets["LOGO_PNG"][2] == "image/png"
    assert VERSION in gzip.decompress(static_assets["INDEX_HTML"][0]).decode("utf-8")

    raw_assets = RAW_ASSET.findall(header)
    assert [(symbol, content) for symbol, _, content in raw_assets] == [
        ("LOGIN_HTML", (ROOT / "web/login.html").read_text())
    ]

    # The header has to compile as the firmware includes it.
    include_dir = Path(temp) / "include"
    include_dir.mkdir()
    (include_dir / "Arduino.h").write_text("#include <cstddef>\n#include <cstdint>\n#define PROGMEM\n")
    (include_dir / "check.cpp").write_text('#include "WebAssets.h"\n')
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                    "-I", str(include_dir), "-I", str(first / "src/generated"),
                    str(include_dir / "check.cpp")], check=True)

    # Out of order in platformio.ini: version.py has not set the version yet.
    assert firmware_version({"OPENFRISQUETVISIO_VERSION": VERSION}) == VERSION
    try:
        firmware_version({})
        raise AssertionError("missing version accepted")
    except SystemExit as error:
        assert "tools/version.py" in str(error), error

    # A marker the build does not know would reach the browser unfilled.
    unknown_marker = project_copy(temp, "unknown-marker")
    with open(unknown_marker / "web/index.html", "a", encoding="utf-8") as page:
        page.write("{{INCONNU}}")
    try:
        generate(unknown_marker, VERSION)
        raise AssertionError("unknown marker accepted")
    except ValueError as error:
        assert "index.html" in str(error) and "{{INCONNU}}" in str(error), error

print("OK: web assets round-trip, stable ETags, raw login page, header compiles, ordering and marker errors")
