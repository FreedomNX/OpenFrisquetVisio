"""Run the native log-handler harness and independently check its JSON output."""
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/Portal.cpp").read_text()
start = source.index("void Portal::handleGetLogs()")
end = source.index("void Portal::handleLogsPage()", start)

# Expected values are checked with Python's JSON parser, independently of the
# C++ implementation. The matching input fixtures live in portal_logs_test.cpp.
special = 'guillemets " / barre \\ / accent é 🌡 / ' + ''.join(chr(n) for n in range(1, 32))
entries = [f'{n:03d}: ' + 'a"\\é\t' * 35 for n in range(140)]
expected = {
    "empty": (200, []),
    "escapes": (200, [special]),
    "default": (200, entries[-100:]),
    "cap": (200, entries[-120:]),
    "small": (200, entries[-5:]),
    "zero": (200, entries[-100:]),
    "negative": (200, entries[-100:]),
    "filter": (200, entries[1::2][-30:]),
    "controls": (200, ["\x01" * 250] * 120),
    "radio_denied": (403, {}),
    "radio_allowed": (200, ["frame"]),
}

# Exercise the smaller LilyGo buffer as well as the Heltec limit.
for capacity in (80, 120):
    with tempfile.TemporaryDirectory(prefix="ofv-portal-logs-") as tmp:
        # Include the actual handler without pulling in ESP32-only dependencies.
        (Path(tmp) / "portal_logs_handler.inc").write_text(source[start:end])
        binary = Path(tmp) / "portal_logs_test"
        subprocess.run([
            "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
            "-DTEST_LOG_QUERY_LINES=" + str(capacity), "-I", tmp,
            str(ROOT / "tests/portal_logs_test.cpp"), "-o", str(binary),
        ], check=True)
        rows = subprocess.check_output([str(binary)], text=True).splitlines()
        assert len(rows) == len(expected)
        for row, (name, (expected_status, expected_body)) in zip(rows, expected.items()):
            actual_name, status, count, largest, body = row.split(' ', 4)
            assert actual_name == name and int(status) == expected_status
            bounded_body = expected_body[-capacity:] if isinstance(expected_body, list) else expected_body
            assert json.loads(body) == bounded_body, (capacity, name)
            if expected_status == 200:
                assert 0 < int(largest) <= 1024
                # Reject tiny per-token writes, especially for escaped lines.
                assert int(count) <= (len(body.encode()) + 511) // 512, name
            else:
                assert int(count) == 0

print('OK: JSON escaping, UTF-8, chunk boundaries, limits, filtering, access checks and bounded writes (80/120 lines)')
