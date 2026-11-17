#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include "RadioCrc.h"
using byte = uint8_t;
constexpr int timeNotSet = 0;
bool clockKnown = false;
uint32_t clockSeconds = 0;
int timeStatus() { return clockKnown ? 1 : timeNotSet; }
uint32_t now() { return clockSeconds; }
#include "monthly-types.inc"
struct Chaudiere {
    int32_t chauffage = -1, ecs = -1;
    void setConsommationAnnuelle(int32_t h, int32_t e) { chauffage = h; ecs = e; }
};
struct Connect {
#include "monthly-state.inc"
    Chaudiere _chaudiere;
    bool setConsommationAnnuelle(const byte*, size_t, bool = false);
};
#include "monthly-method.inc"

int main() {
    Connect connect;
    const auto& current = connect.getConsommationsMensuelles();
    const auto& previous = connect.getConsommationsAnneePrecedente();
    assert(!current.disponibles && !previous.disponibles);
    ConsommationMensuelle reply;
    reply.longueurDonnees = 48;
    const auto bytes = reinterpret_cast<const byte*>(&reply);
    static_assert(sizeof(reply) == 55 && sizeof(reply) <= RadioCrc::MAX_PAYLOAD);
    assert(sizeof(current) == 56);
    assert(!connect.setConsommationAnnuelle(nullptr, sizeof(reply)));
    assert(connect.setConsommationAnnuelle(bytes, sizeof(reply)));
    assert(current.disponibles && current.chauffage[0] == 0 && current.dateReception == 0);
    assert(!previous.disponibles && connect._chaudiere.chauffage == 0);
    clockKnown = true;
    clockSeconds = 1794829800;
    assert(current.dateReception == 0); // Pas de datation rétroactive.
    for (unsigned i = 0; i < 12; ++i) {
        reply.mois[2 * i] = fword(1000 + i);
        reply.mois[2 * i + 1] = fword(10 + i);
    }
    assert(connect.setConsommationAnnuelle(bytes, sizeof(reply)));
    const auto annualHeat = connect._chaudiere.chauffage, annualEcs = connect._chaudiere.ecs;
    assert(annualHeat == 12066 && annualEcs == 186);
    for (auto& word : reply.mois) word = fword(65535);
    assert(connect.setConsommationAnnuelle(bytes, sizeof(reply), true));
    assert(previous.disponibles && previous.dateReception == current.dateReception);
    assert(previous.chauffage[11] == 65535 && previous.ecs[11] == 65535);
    assert(current.chauffage[11] == 1011 && current.ecs[11] == 21);
    assert(connect._chaudiere.chauffage == annualHeat && connect._chaudiere.ecs == annualEcs);
    const auto oldDate = previous.dateReception;
    ++clockSeconds;
    for (bool archive : {false, true}) {
        for (size_t n = 0; n < sizeof(reply); ++n)
            assert(!connect.setConsommationAnnuelle(bytes, n, archive));
        for (unsigned length = 0; length < 256; ++length) {
            if (length == 48) continue;
            reply.longueurDonnees = length;
            assert(!connect.setConsommationAnnuelle(bytes, sizeof(reply), archive));
            assert(previous.dateReception == oldDate && previous.ecs[11] == 65535);
            assert(current.dateReception == oldDate && current.chauffage[11] == 1011);
            assert(connect._chaudiere.chauffage == annualHeat && connect._chaudiere.ecs == annualEcs);
        }
        reply.longueurDonnees = 48;
    }
    for (auto& word : reply.mois) word = fword(uint16_t(0));
    assert(connect.setConsommationAnnuelle(bytes, sizeof(reply), true));
    assert(previous.disponibles && previous.ecs[11] == 0 && previous.dateReception == clockSeconds);
    assert(connect._chaudiere.chauffage == annualHeat);
    clockSeconds = 1798761600;
    assert(current.dateReception == oldDate); // Le changement d'année ne réétiquette aucun cache.
    Connect restarted;
    assert(!restarted.getConsommationsMensuelles().disponibles);
    assert(!restarted.getConsommationsAnneePrecedente().disponibles);
    // Les requêtes des deux années sont vérifiées dans le séquencement Connect.
    puts("OK : réponses sous la limite RX, caches indépendants et MQTT courant préservé");
}
