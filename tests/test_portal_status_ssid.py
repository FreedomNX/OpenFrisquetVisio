"""/api/status escapes the SSID, so the response stays valid JSON.

Run: python3 tests/test_portal_status_ssid.py
"""
from pathlib import Path
import json
import subprocess
import tempfile

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


status = extract("void Portal::handleStatus(")
assert '"\\"ssid\\":\\""        + jsonEscape(ssid) + ' in status
assert "+ ssid +" not in status

# The real jsonEscape(), on SSIDs a router accepts (up to 32 bytes, any byte).
ssids = ['MaBox', 'Box "salon"', 'C:\\wifi', 'Café', 'tab\there', 'x\x01y']
program = r'''
#include <cstdint>
#include <cstdio>
#include <string>
struct String : std::string {
    using std::string::string;
    String() = default;
    String(const std::string& s) : std::string(s) {}
    unsigned length() const { return size(); }
    void reserve(unsigned n) { std::string::reserve(n); }
};
''' + extract("static String jsonEscape(") + r'''
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) printf("{\"ssid\":\"%s\"}\n", jsonEscape(String(argv[i])).c_str());
}
'''

with tempfile.TemporaryDirectory(prefix="ofv-status-ssid-") as directory:
    temp = Path(directory)
    (temp / "test.cpp").write_text(program)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", str(temp / "test.cpp"), "-o", str(temp / "test")], check=True)
    output = subprocess.run([str(temp / "test"), *ssids], check=True, capture_output=True, text=True).stdout
    assert [json.loads(line)["ssid"] for line in output.splitlines()] == ssids

print("OK: /api/status escapes the SSID (quotes, backslash, control characters, UTF-8)")
