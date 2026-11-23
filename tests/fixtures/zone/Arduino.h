#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <strings.h>

using byte = uint8_t;
using boolean = bool;
using std::isfinite;
using std::isnan;

class String : public std::string {
public:
    using std::string::string;
    String() = default;
    String(const std::string& texte) : std::string(texte) {}
    explicit String(unsigned nombre) : std::string(std::to_string(nombre)) {}
    bool isEmpty() const { return empty(); }
    bool endsWith(const String& suffixe) const {
        return size() >= suffixe.size() && compare(size() - suffixe.size(), suffixe.size(), suffixe) == 0;
    }
    bool equalsIgnoreCase(const char* texte) const { return strcasecmp(c_str(), texte) == 0; }
    float toFloat() const { return std::strtof(c_str(), nullptr); }
    // Interface de sortie std::string reconnue par ArduinoJson.
    using std::string::append;
    String& append(const char* texte) { std::string::append(texte); return *this; }
};

inline String operator+(const std::string& texte, byte nombre) { return texte + std::to_string(nombre); }

uint32_t millis();
void delay(unsigned long duree);
inline char* dtostrf(double valeur, int largeur, int decimales, char* tampon) {
    std::sprintf(tampon, "%*.*f", largeur, decimales, valeur);
    return tampon;
}
