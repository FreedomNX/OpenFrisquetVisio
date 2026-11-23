#include "materiel.h"
#include <cstdarg>
#include "Frisquet/Connect.h"

uint32_t tempsMs = 0;
time_t heureChaudiere = 0;
bool heureConnue = false;
uint32_t millis() { return tempsMs; }
void delay(unsigned long duree) { tempsMs += duree; }
void setTime(int h, int m, int s, int j, int mois, int an) {
    std::tm date{};
    date.tm_year = 100 + an;
    date.tm_mon = mois - 1;
    date.tm_mday = j;
    date.tm_hour = h;
    date.tm_min = m;
    date.tm_sec = s;
    heureChaudiere = timegm(&date);
    heureConnue = true;
}

WifiSimule WiFi;
ReceptionMqtt receptionMqtt;
std::map<String, String> publications;
std::map<String, unsigned> nombrePublications;
bool courtierDisponible = true;
String sujetRefuse;
std::map<String, Preferences::Espace> Preferences::memoire;
unsigned Preferences::ecritures = 0;
std::vector<std::vector<byte>> emissions;
std::vector<byte> reception;
std::vector<String> journal;
Radio::Radio() = default;
int16_t Radio::transmit(byte* trame, size_t taille) {
    emissions.emplace_back(trame, trame + taille);
    return RADIOLIB_ERR_NONE;
}
bool Radio::hasReceivedPacket() { return !reception.empty(); }
int16_t Radio::PacketRadio::startReceive() { return RADIOLIB_ERR_NONE; }
int16_t Radio::PacketRadio::receive(byte*, size_t, RadioLibTime_t) { return RADIOLIB_ERR_RX_TIMEOUT; }
int16_t Radio::PacketRadio::readData(byte* data, size_t taille) {
    if (reception.empty()) return RADIOLIB_ERR_RX_TIMEOUT;
    lastRxLength = reception.size();
    assert(lastRxLength <= RADIOLIB_SX126X_MAX_PACKET_LENGTH);
    const size_t copie = taille ? std::min(taille, lastRxLength) : lastRxLength;
    std::memcpy(data, reception.data(), copie);
    reception.clear();
    return RADIOLIB_ERR_NONE;
}
int16_t Radio::receive(byte* data, size_t taille) { return _radio.receive(data, taille); }
int16_t Radio::readData(byte* data, size_t taille) { return _radio.readData(data, taille); }

void debug(const char*, ...) {}
void error(const char*, ...) {}
void info(const char* format, ...) {
    char ligne[224];
    va_list args;
    va_start(args, format);
    std::vsnprintf(ligne, sizeof(ligne), format, args);
    va_end(args);
    journal.emplace_back(ligne);
}
void logRadio(bool, const byte*, size_t, float) {}

void warning(const String&) {}
