"""Wi-Fi and MQTT passwords are write-only in the portal API.

Run: python3 tests/test_portal_secrets.py
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/Portal.cpp").read_text()


def extract(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


get_config = extract("void Portal::handleGetConfig(")
# The response only says whether a password is stored.
assert '"\\"wifiPass\\":' not in get_config and '"\\"mqttPass\\":' not in get_config
assert not re.search(r"jsonEscape\(String\((w|m)\.password\)\)", get_config)
assert re.findall(r"[wm]\.password[^;]*", get_config) == [
    'w.password.length() ? "true" : "false") + ","',
    'm.password.length() ? "true" : "false") + ","',
]
assert '\\"wifiPassConfigured\\":' in get_config and '\\"mqttPassConfigured\\":' in get_config

post_config = extract("void Portal::handlePostConfig(")
for field, target in (("wifiPass", "w"), ("mqttPass", "m")):
    # Empty or missing: keep. Explicit request: clear. Otherwise: replace.
    assert (f'if (_srv.hasArg("{field}") && !_srv.arg("{field}").isEmpty()) '
            f'{target}.password = _srv.arg("{field}");') in post_config
    assert f'else if (parseBoolArg(_srv.arg("{field}Clear"), false)) {target}.password = "";' in post_config
    assert len(re.findall(rf"\b{target}\.password\s*=", post_config)) == 2

# No other route serialises these passwords.
assert not re.search(r"(wifiPass|mqttPass)\\\":\\\"\" \+", source)
print("OK: Wi-Fi and MQTT passwords are never returned; empty keeps, clear is explicit")
