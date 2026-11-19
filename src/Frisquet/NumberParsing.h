#pragma once
#include <math.h>
#include <stdlib.h>

namespace NumberParsing {
// Nombre décimal complet et fini : [+-]chiffres[.chiffres] ou [+-].chiffres.
// Refuse "", "21abc", "abc", "nan", "inf", "1e3" et l'hexadécimal, au lieu de
// les convertir en 0 ou en préfixe comme String::toFloat().
inline bool finiteDecimal(const char* text, float& out) {
    if (!text) return false;
    const char* p = text;
    if (*p == '+' || *p == '-') ++p;
    bool digits = false;
    while (*p >= '0' && *p <= '9') { ++p; digits = true; }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') { ++p; digits = true; }
    }
    if (!digits || *p != '\0') return false;
    const float value = strtof(text, nullptr);
    if (!isfinite(value)) return false;
    out = value;
    return true;
}
}
