// Native harness for the real Portal::handleGetLogs(), run by test_portal_logs.py.
// Minimal Arduino/server/log substitutes keep the test independent of ESP32.
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

class String {
    std::string value;
public:
    String() = default;
    String(const char* s) : value(s) {}
    String(const std::string& s) : value(s) {}
    size_t length() const { return value.size(); }
    const char* c_str() const { return value.c_str(); }
    int toInt() const { return atoi(value.c_str()); }
    bool equalsIgnoreCase(const char* other) const {
        std::string a = value, b = other;
        for (char& c : a) c = std::tolower(static_cast<unsigned char>(c));
        for (char& c : b) c = std::tolower(static_cast<unsigned char>(c));
        return a == b;
    }
};

constexpr int CONTENT_LENGTH_UNKNOWN = -1;
struct Server {
    std::map<std::string, String> args;
    std::vector<std::string> chunks;
    std::string body;
    int status = 0;

    bool hasArg(const char* key) { return args.count(key); }
    String arg(const char* key) { return args.at(key); }
    void setContentLength(int n) { assert(n == CONTENT_LENGTH_UNKNOWN); }
    void send(int code, const char*, const char* text) {
        status = code;
        body = text;
    }
    void sendContent(const char* text, size_t size) {
        assert(size > 0);
        chunks.emplace_back(text, size);
        body.append(text, size);
    }
};

struct Logs {
    static constexpr size_t kMaxFormattedLen = 256;
    struct Line {
        std::string level, text;
        void format(char* out, size_t size) const {
            snprintf(out, size, "%s", text.c_str());
        }
    };
    std::vector<Line> entries;

    size_t getLines(Line* out, size_t capacity, size_t limit, const char* level) {
        std::vector<Line> filtered;
        for (const auto& line : entries) {
            if (!level || line.level == level) filtered.push_back(line);
        }
        size_t count = std::min({capacity, limit, filtered.size()});
        std::copy(filtered.end() - count, filtered.end(), out);
        return count;
    }
} logs;

#ifndef TEST_LOG_QUERY_LINES
#define TEST_LOG_QUERY_LINES 120
#endif
static constexpr size_t kMaxQueryLines = TEST_LOG_QUERY_LINES;
static Logs::Line s_logLines[kMaxQueryLines];
static volatile bool s_logsBusy = false;

struct Portal {
    Server _srv;
    bool developer = false;
    bool requireDeveloperMode() {
        if (developer) return true;
        _srv.send(403, "application/json", "{}");
        return false;
    }
    void handleGetLogs();
};

// The runner extracts this from src/Portal.cpp; no handler implementation is
// duplicated in the test.
#include "portal_logs_handler.inc"

static void run(const char* name, const std::map<std::string, String>& args = {},
                bool developer = false) {
    Portal portal;
    portal._srv.args = args;
    portal.developer = developer;
    portal.handleGetLogs();
    assert(!s_logsBusy);

    size_t largest = 0;
    for (const auto& chunk : portal._srv.chunks) {
        largest = std::max(largest, chunk.size());
    }
    // Python parses the JSON body and checks it against independent expectations.
    std::cout << name << ' ' << portal._srv.status << ' '
              << portal._srv.chunks.size() << ' ' << largest << ' '
              << portal._srv.body << '\n';
}

int main() {
    run("empty");

    std::string special = "guillemets \" / barre \\ / accent é 🌡 / ";
    for (char c = 1; c < 32; ++c) special += c;
    logs.entries = {{"INFO", special}};
    run("escapes");

    logs.entries.clear();
    for (unsigned n = 0; n < 140; ++n) {
        char number[8];
        snprintf(number, sizeof(number), "%03u: ", n);
        std::string text = number;
        for (unsigned repeat = 0; repeat < 35; ++repeat) text += "a\"\\é\t";
        logs.entries.push_back({n % 2 ? "INFO" : "DEBUG", text});
    }
    run("default");
    run("cap", {{"limit", "500"}});
    run("small", {{"limit", "5"}});
    run("zero", {{"limit", "0"}});
    run("negative", {{"limit", "-1"}});
    run("filter", {{"limit", "30"}, {"level", "INFO"}});

    logs.entries.assign(120, {"INFO", std::string(250, '\x01')});
    run("controls", {{"limit", "120"}});

    logs.entries = {{"RADIO", "frame"}};
    run("radio_denied", {{"level", "rAdIo"}});
    run("radio_allowed", {{"level", "RADIO"}}, true);

    s_logsBusy = true;
    Portal busy;
    busy.handleGetLogs();
    assert(busy._srv.status == 429 && busy._srv.chunks.empty() && s_logsBusy);
}
