"""Transforme les pages de web/ en ressources C++ embarquées dans le firmware.

Importé par tools/generate_web_assets.py sous PlatformIO, et directement par
tests/test_web_assets.py.
"""
import gzip
import hashlib
import os
import re

STATIC_ASSETS = (
    ("THEME_CSS", "theme.css", "text/css; charset=utf-8"),
    ("THEME_JS", "theme.js", "application/javascript; charset=utf-8"),
    ("INDEX_HTML", "index.html", "text/html; charset=utf-8"),
    ("SETTINGS_HTML", "settings.html", "text/html; charset=utf-8"),
    ("PROGRAMMING_HTML", "programming.html", "text/html; charset=utf-8"),
    ("LOGS_HTML", "logs.html", "text/html; charset=utf-8"),
    ("MEMORY_HTML", "memory.html", "text/html; charset=utf-8"),
    ("RADIO_LOGS_HTML", "radio-logs.html", "text/html; charset=utf-8"),
    ("LOGO_PNG", "logo.png", "image/png"),
)

# login.html porte {{LOGIN_ERROR}}, que le serveur remplit à chaque refus de connexion.
# Il ne peut pas écrire dans un contenu compressé, ce fichier part donc tel quel.
RAW_ASSETS = (
    ("LOGIN_HTML", "login.html"),
)

VERSION_PLACEHOLDER = "{{OPENFRISQUETVISIO_VERSION}}"
PLACEHOLDER = re.compile(r"\{\{[A-Z_]+\}\}")


def firmware_version(env):
    """Version posée dans l’environnement de build par tools/version.py."""
    version = env.get("OPENFRISQUETVISIO_VERSION")
    if version is None:
        raise SystemExit(
            "tools/version.py doit tourner avant generate_web_assets.py :"
            " vérifier l’ordre de extra_scripts dans platformio.ini"
        )
    return version


def read_source(web_dir, filename):
    with open(os.path.join(web_dir, filename), "r", encoding="utf-8") as source:
        return source.read()


def static_content(web_dir, filename, content_type, version):
    """Contenu servi : la version y est inscrite au build, la page ne change donc
    plus d’une requête à l’autre et peut être compressée puis revalidée.
    """
    # Seuls les types texte déclarent un charset. Les autres, comme le logo
    # PNG, ne portent aucun marqueur et partent tels quels.
    if "charset=" not in content_type:
        with open(os.path.join(web_dir, filename), "rb") as source:
            return source.read()
    content = read_source(web_dir, filename).replace(VERSION_PLACEHOLDER, version)
    unfilled = PLACEHOLDER.search(content)
    if unfilled is not None:
        raise ValueError(filename + " contient " + unfilled.group(0) + ", que le build ne sait pas remplir")
    return content.encode("utf-8")


def gzip_bytes(content):
    """Sans date dans l’en-tête gzip, le même contenu donne les mêmes octets,
    donc le même ETag, d’un build à l’autre.
    """
    return gzip.compress(content, compresslevel=9, mtime=0)


def etag_literal(body):
    """Empreinte des octets envoyés : elle ne change qu’avec eux, ce qui permet au
    navigateur de garder sa copie d’une mise à jour à l’autre.

    @see https://www.rfc-editor.org/rfc/rfc9110#section-8.8.3
    """
    return '"\\"' + hashlib.sha1(body).hexdigest() + '\\""'


def byte_lines(body):
    rows = (body[start:start + 16] for start in range(0, len(body), 16))
    return ",\n".join("  " + ", ".join("0x%02x" % byte for byte in row) for row in rows)


def static_asset_declaration(web_dir, symbol, filename, content_type, version):
    body = gzip_bytes(static_content(web_dir, filename, content_type, version))
    return (
        "static const uint8_t " + symbol + "_GZIP[] PROGMEM = {\n"
        + byte_lines(body)
        + "\n};\n"
        + "static const StaticAsset " + symbol + " = {"
        + symbol + "_GZIP, sizeof(" + symbol + "_GZIP), "
        + etag_literal(body) + ', "' + content_type + '"};\n'
    )


def raw_delimiter(content):
    digest = hashlib.sha1(content.encode("utf-8")).hexdigest().upper()
    for length in range(8, 13):
        delimiter = "OFV" + digest[:length]
        if ")" + delimiter + '"' not in content:
            return delimiter
    raise ValueError("Impossible de créer un délimiteur C++ pour la ressource HTML")


def raw_asset_declaration(web_dir, symbol, filename):
    content = read_source(web_dir, filename)
    delimiter = raw_delimiter(content)
    return (
        "static const char " + symbol + "[] PROGMEM = R\""
        + delimiter + "(" + content + ")" + delimiter + "\";\n"
    )


def render_header(web_dir, version):
    declarations = [
        static_asset_declaration(web_dir, symbol, filename, content_type, version)
        for symbol, filename, content_type in STATIC_ASSETS
    ] + [
        raw_asset_declaration(web_dir, symbol, filename)
        for symbol, filename in RAW_ASSETS
    ]

    return (
        "// Fichier généré automatiquement depuis web/. Ne pas modifier.\n"
        "#pragma once\n\n"
        "#include <Arduino.h>\n\n"
        "namespace WebAssets {\n\n"
        "struct StaticAsset {\n"
        "  const uint8_t* gzipData;\n"
        "  size_t gzipSize;\n"
        "  const char* etag;\n"
        "  const char* contentType;\n"
        "};\n\n"
        + "\n".join(declarations)
        + "\n}  // namespace WebAssets\n"
    )


def write_if_changed(path, content):
    try:
        with open(path, "r", encoding="utf-8") as current_file:
            if current_file.read() == content:
                return
    except FileNotFoundError:
        pass

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as output:
        output.write(content)


def generate(project_dir, version):
    """Écrit src/generated/WebAssets.h depuis web/, seulement si son contenu change."""
    header = render_header(os.path.join(project_dir, "web"), version)
    write_if_changed(os.path.join(project_dir, "src", "generated", "WebAssets.h"), header)
