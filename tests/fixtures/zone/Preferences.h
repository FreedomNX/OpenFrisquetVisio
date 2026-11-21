#pragma once
#include <Arduino.h>
#include <cassert>
#include <map>
#include <vector>

class Preferences {
    using Donnees = std::vector<byte>;
    using Espace = std::map<String, Donnees>;
    static std::map<String, Espace> memoire;
    Espace* espace = nullptr;

    template<class T> T lire(const char* cle, T defaut) const {
        T valeur;
        return getBytes(cle, &valeur, sizeof(T)) == sizeof(T) ? valeur : defaut;
    }
    template<class T> size_t ecrire(const char* cle, T valeur) {
        return putBytes(cle, &valeur, sizeof(T));
    }
public:
    static unsigned ecritures;
    static void effacer() { memoire.clear(); ecritures = 0; }
    bool begin(const char* nom, bool) { espace = &memoire[nom]; return true; }
    void end() { espace = nullptr; }
    bool isKey(const char* cle) const { return espace->count(cle) != 0; }
    void remove(const char* cle) { espace->erase(cle); }
    size_t getBytesLength(const char* cle) const { return isKey(cle) ? espace->at(cle).size() : 0; }
    size_t getBytes(const char* cle, void* cible, size_t capacite) const {
        const size_t taille = getBytesLength(cle);
        if (!taille || taille > capacite) return 0;
        std::memcpy(cible, espace->at(cle).data(), taille);
        return taille;
    }
    size_t putBytes(const char* cle, const void* source, size_t taille) {
        const auto* octets = static_cast<const byte*>(source);
        (*espace)[cle] = Donnees(octets, octets + taille);
        ++ecritures;
        return taille;
    }
    bool getBool(const char* k, bool v = false) const { return lire(k, v); }
    byte getUChar(const char* k, byte v = 0) const { return lire(k, v); }
    uint16_t getUShort(const char* k, uint16_t v = 0) const { return lire(k, v); }
    uint32_t getUInt(const char* k, uint32_t v = 0) const { return lire(k, v); }
    uint32_t getULong(const char* k, uint32_t v = 0) const { return lire(k, v); }
    float getFloat(const char* k, float v = NAN) const { return lire(k, v); }
    String getString(const char* k, const char* v) const {
        if (!isKey(k)) return v;
        const auto& octets = espace->at(k);
        return std::string(octets.begin(), octets.end());
    }
    void putBool(const char* k, bool v) { ecrire(k, v); }
    void putUChar(const char* k, byte v) { ecrire(k, v); }
    void putUShort(const char* k, uint16_t v) { ecrire(k, v); }
    void putUInt(const char* k, uint32_t v) { ecrire(k, v); }
    void putULong(const char* k, uint32_t v) { ecrire(k, v); }
    void putFloat(const char* k, float v) { ecrire(k, v); }
    void putString(const char* k, const String& v) { putBytes(k, v.data(), v.size()); }
};
