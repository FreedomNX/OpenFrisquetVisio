import hashlib
import os


Import("env")

PROJECT_DIR = env.subst("$PROJECT_DIR")
WEB_DIR = os.path.join(PROJECT_DIR, "web")
OUTPUT_PATH = os.path.join(PROJECT_DIR, "src", "generated", "WebAssets.h")

ASSETS = (
    ("THEME_CSS", "theme.css"),
    ("THEME_JS", "theme.js"),
    ("LOGIN_HTML", "login.html"),
    ("INDEX_HTML", "index.html"),
    ("SETTINGS_HTML", "settings.html"),
    ("PROGRAMMING_HTML", "programming.html"),
    ("LOGS_HTML", "logs.html"),
    ("MEMORY_HTML", "memory.html"),
    ("RADIO_LOGS_HTML", "radio-logs.html"),
)

BINARY_ASSETS = (
    ("LOGO_PNG", "logo.png"),
)


def raw_delimiter(content):
    digest = hashlib.sha1(content.encode("utf-8")).hexdigest().upper()
    for length in range(8, 13):
        delimiter = "OFV" + digest[:length]
        if ")" + delimiter + '"' not in content:
            return delimiter
    raise ValueError("Impossible de créer un délimiteur C++ pour la ressource HTML")


def render_header():
    declarations = []
    for symbol, filename in ASSETS:
        path = os.path.join(WEB_DIR, filename)
        with open(path, "r", encoding="utf-8") as source:
            content = source.read()
        delimiter = raw_delimiter(content)
        declarations.append(
            "static const char "
            + symbol
            + "[] PROGMEM = R\""
            + delimiter
            + "("
            + content
            + ")"
            + delimiter
            + "\";\n"
        )

    binary_declarations = []
    for symbol, filename in BINARY_ASSETS:
        path = os.path.join(WEB_DIR, filename)
        with open(path, "rb") as source:
            content = source.read()
        rows = []
        for offset in range(0, len(content), 16):
            chunk = content[offset : offset + 16]
            rows.append("  " + ", ".join("0x%02X" % value for value in chunk))
        binary_declarations.append(
            "static const uint8_t " + symbol + "[] PROGMEM = {\n"
            + ",\n".join(rows)
            + "\n};\n"
            + "static constexpr size_t " + symbol + "_SIZE = sizeof(" + symbol + ");\n"
        )

    return (
        "// Fichier généré automatiquement depuis web/. Ne pas modifier.\n"
        "#pragma once\n\n"
        "#include <Arduino.h>\n\n"
        "namespace WebAssets {\n\n"
        + "\n".join(declarations)
        + "\n"
        + "\n".join(binary_declarations)
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


write_if_changed(OUTPUT_PATH, render_header())
