"""Check decimal portal arguments with the real parser under sanitizers.

Run: python3 tests/test_portal_numeric_args.py
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/Portal.cpp").read_text()

# strtoul/strtol must not keep `end` inside a temporary `_srv.arg(...)`.
assert not re.search(r"strto[u]?l\s*\(\s*_srv\.arg\([^)]*\)\.c_str\(\)\s*,\s*&", source)
for name, maximum in (("len", 256), ("max", 512), ("step", 256)):
    assert f'parseDecimalArg(_srv.arg("{name}"), 1, {maximum}, ' in source


def extract(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


parser = extract("static bool parseDecimalArg(")
program = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Always heap-allocated, like a long Arduino String: a dangling `end`
// would be reported as heap-use-after-free.
class String {
public:
    String(const char* text) : _data(new char[strlen(text) + 1]) { strcpy(_data, text); }
    String(const String& other) : String(other._data) {}
    ~String() { delete[] _data; }
    String& operator=(const String&) = delete;
    const char* c_str() const { return _data; }
    bool isEmpty() const { return _data[0] == '\0'; }
private:
    char* _data;
};

static String arg(const char* text) { return String(text); }

''' + parser + r'''

int main() {
    uint16_t value = 7;
    assert(parseDecimalArg(arg("1"), 1, 256, value) && value == 1);
    assert(parseDecimalArg(arg("256"), 1, 256, value) && value == 256);
    assert(parseDecimalArg(arg("512"), 1, 512, value) && value == 512);
    const char* rejected[] = {"", "0", "257", "12x", "x12", "-1", "1.5",
                              "99999999999999999999999999999999"};
    for (const char* text : rejected) {
        value = 7;
        assert(!parseDecimalArg(arg(text), 1, 256, value));
        assert(value == 7);
    }
    puts("OK: decimal portal arguments, bounds and temporary lifetime");
}
'''

with tempfile.TemporaryDirectory(prefix="frisquet-portal-args-") as temp:
    cpp = Path(temp) / "portal_numeric_args.cpp"
    binary = Path(temp) / "portal_numeric_args"
    cpp.write_text(program)
    subprocess.run([
        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-fno-omit-frame-pointer", "-g", str(cpp), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
