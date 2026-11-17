#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

class String : public std::string {
public:
    using std::string::string;
    String(const std::string& value) : std::string(value) {}
    String(int value) : std::string(std::to_string(value)) {}
};
struct Connect {
#include "monthly-state.inc"
};
int year(time_t date) { return gmtime(&date)->tm_year + 1900; }
int month(time_t date) { return gmtime(&date)->tm_mon + 1; }
int day(time_t date) { return gmtime(&date)->tm_mday; }
int hour(time_t date) { return gmtime(&date)->tm_hour; }
int minute(time_t date) { return gmtime(&date)->tm_min; }
int second(time_t date) { return gmtime(&date)->tm_sec; }
#include "monthly-json.inc"

int main() {
    Connect::ConsommationsMensuelles data;
    puts(jsonConsommationsMensuelles(data).c_str());
    data.disponibles = true;
    puts(jsonConsommationsMensuelles(data).c_str());
    data.dateReception = 1794829800;
    for (unsigned month = 0; month < 12; ++month) {
        data.chauffage[month] = 1000 + month;
        data.ecs[month] = 10 + month;
    }
    puts(jsonConsommationsMensuelles(data).c_str());
    data.dateReception = 1798761599; // 31 décembre 2026, 23:59:59 (heure chaudière).
    data.chauffage[11] = 65535;
    puts(jsonConsommationsMensuelles(data).c_str());
    data.dateReception = 1798761600; // Le passage à janvier ne dépend pas du navigateur.
    puts(jsonConsommationsMensuelles(data).c_str());
    data.dateReception = 1794829800;
    puts(jsonConsommationsMensuelles(data, true).c_str());
    data.dateReception = 1798761600;
    puts(jsonConsommationsMensuelles(data, true).c_str());
    data.dateReception = 0;
    puts(jsonConsommationsMensuelles(data, true).c_str());
    data.disponibles = false;
    puts(jsonConsommationsMensuelles(data, true).c_str());
}
