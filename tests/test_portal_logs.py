"""Compile the real log handler; verify JSON, filtering and bounded HTTP writes."""
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/Portal.cpp").read_text()
start = source.index("void Portal::handleGetLogs()")
end = source.index("void Portal::handleLogsPage()", start)
handler = source[start:end]

prefix = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
class String {
    std::string value;
public:
    String() = default;
    String(const char* s): value(s) {}
    String(const std::string& s): value(s) {}
    size_t length() const { return value.size(); }
    const char* c_str() const { return value.c_str(); }
    int toInt() const { return atoi(value.c_str()); }
    bool equalsIgnoreCase(const char* other) const {
        std::string a=value,b=other;
        for(char& c:a)c=std::tolower(static_cast<unsigned char>(c));
        for(char& c:b)c=std::tolower(static_cast<unsigned char>(c));
        return a==b;
    }
};
constexpr int CONTENT_LENGTH_UNKNOWN=-1;
struct Server {
    std::map<std::string,String> args;
    std::vector<std::string> chunks;
    std::string body;
    int status=0;
    bool hasArg(const char* key) { return args.count(key); }
    String arg(const char* key) { return args.at(key); }
    void setContentLength(int n) { assert(n==CONTENT_LENGTH_UNKNOWN); }
    void send(int code,const char*,const char* text) { status=code;body=text; }
    void sendContent(const char* text,size_t size) {
        assert(size>0);chunks.emplace_back(text,size);body.append(text,size);
    }
};
struct Logs {
    static constexpr size_t kMaxFormattedLen=256;
    struct Line {
        std::string level,text;
        void format(char* out,size_t size) const { snprintf(out,size,"%s",text.c_str()); }
    };
    std::vector<Line> entries;
    size_t getLines(Line* out,size_t capacity,size_t limit,const char* level) {
        std::vector<Line> filtered;
        for(const auto& line:entries)if(!level||line.level==level)filtered.push_back(line);
        size_t count=std::min({capacity,limit,filtered.size()});
        std::copy(filtered.end()-count,filtered.end(),out);
        return count;
    }
} logs;
static constexpr size_t kMaxQueryLines=120;
static Logs::Line s_logLines[kMaxQueryLines];
static volatile bool s_logsBusy=false;
struct Portal {
    Server _srv;
    bool developer=false;
    bool requireDeveloperMode() {
        if(developer)return true;
        _srv.send(403,"application/json","{}");return false;
    }
    void handleGetLogs();
};
void print(const char* name,Portal& portal) {
    size_t largest=0;for(const auto& chunk:portal._srv.chunks)largest=std::max(largest,chunk.size());
    std::cout<<name<<' '<<portal._srv.status<<' '<<portal._srv.chunks.size()<<' '<<largest<<' '<<portal._srv.body<<'\n';
}
'''


def literal(text):
    return '"' + ''.join(f'\\{byte:03o}' for byte in text.encode()) + '"'


cases = []
cases.append(("empty", [], {}, False, 200, []))
special = 'guillemets " / barre \\ / accent é 🌡 / ' + ''.join(chr(n) for n in range(1, 32))
cases.append(("escapes", [("INFO", special)], {}, False, 200, [special]))
entries = [("INFO" if n % 2 else "DEBUG", f'{n:03d}: ' + 'a"\\é\t' * 35) for n in range(140)]
for name, args, expected in (
    ("default", {}, entries[-100:]),
    ("cap", {"limit": "500"}, entries[-120:]),
    ("small", {"limit": "5"}, entries[-5:]),
    ("zero", {"limit": "0"}, entries[-100:]),
    ("negative", {"limit": "-1"}, entries[-100:]),
    ("filter", {"limit": "30", "level": "INFO"}, [e for e in entries if e[0] == "INFO"][-30:]),
):
    cases.append((name, entries, args, False, 200, [text for _, text in expected]))
cases.append(("controls", [("INFO", "\x01" * 250)] * 120, {"limit": "120"}, False, 200, ["\x01" * 250] * 120))
cases.append(("radio_denied", [("RADIO", "frame")], {"level": "rAdIo"}, False, 403, {}))
cases.append(("radio_allowed", [("RADIO", "frame")], {"level": "RADIO"}, True, 200, ["frame"]))
main = ['int main(){']
for name, lines, args, developer, _, _ in cases:
    main.append('{ logs.entries.clear(); Portal portal;')
    main.extend(f'logs.entries.push_back({{{literal(level)},{literal(text)}}});' for level, text in lines)
    main.extend(f'portal._srv.args[{literal(key)}]={literal(value)};' for key, value in args.items())
    main.append(f'portal.developer={str(developer).lower()};portal.handleGetLogs();assert(!s_logsBusy);print("{name}",portal);}}')
main.append('s_logsBusy=true;Portal busy;busy.handleGetLogs();assert(busy._srv.status==429&&busy._srv.chunks.empty()&&s_logsBusy);return 0;}')
with tempfile.TemporaryDirectory(prefix="ofv-portal-logs-") as tmp:
    cpp = Path(tmp) / "test.cpp"
    binary = Path(tmp) / "test"
    cpp.write_text(prefix + handler + '\n'.join(main))
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-fsanitize=address,undefined', str(cpp), '-o', str(binary)], check=True)
    output = subprocess.check_output([str(binary)], text=True)
    rows = output.splitlines()
    assert len(rows) == len(cases)
    for row, (name, _, _, _, expected_status, expected_body) in zip(rows, cases):
        actual_name, status, count, largest, body = row.split(' ', 4)
        assert actual_name == name and int(status) == expected_status
        assert json.loads(body) == expected_body, name
        if expected_status == 200:
            assert 0 < int(largest) <= 1024
            # Tiny per-token writes would violate this, especially for escaped lines.
            assert int(count) <= (len(body.encode()) + 511) // 512, name
        else:
            assert int(count) == 0
print('OK: JSON escaping, UTF-8, chunk boundaries, limits, filtering, access checks and bounded writes')
