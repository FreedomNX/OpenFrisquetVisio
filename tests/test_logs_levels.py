"""Check that every level offered by the logs page filter is actually logged.

The server compares the level exactly (Logs::Line::levelEquals), so an option
that does not match a level used by addLog() always returns an empty list.

Run: python3 tests/test_logs_levels.py
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
page = (ROOT / "web/logs.html").read_text(encoding="utf-8")
select = re.search(r"<select id='level'>(.*?)</select>", page, re.S)
assert select, "level selector not found in web/logs.html"
options = [value for value in re.findall(r"<option value='([^']*)'", select.group(1)) if value]
assert options, "no level option found"

levels = set()
for path in (ROOT / "src").rglob("*"):
    if path.suffix in (".cpp", ".h"):
        levels.update(re.findall(r'addLog\("([A-Z]+)"', path.read_text(encoding="utf-8", errors="replace")))

missing = [option for option in options if option not in levels]
assert not missing, f"filter options never logged: {missing} (logged levels: {sorted(levels)})"
print("OK: logs filter options match logged levels", options)
