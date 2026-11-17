#include "connect-model.h"
// App conserve son délai de 10 ms ; aucune autre attente n'est permise.
void delay(uint32_t ms) {
    assert(ms == 10);
    clockMs += ms;
}

#include "connect-declarations.inc"
#include "Frisquet/BoilerFirmware.h"
#include "connect-class.inc"
#include "connect-methods.inc"

void Connect::publishMqtt() {
    ++_chaudiere.publishes;
    _chaudiere.publishedModes.push_back(_chaudiere.mode);
}
void Connect::envoiZones() {
    ++_chaudiere.zoneWrites;
    _lastEnvoiZone = millis();
}
bool Connect::envoyerModeECS() {
    assert(!radio().busy());
    ++_chaudiere.writes;
    _chaudiere.lastSent = getModeECS();
    return true;
}
struct Service {
    unsigned calls = 0;
    void loop() { ++calls; }
    void pollObservation() {}
};
struct FrisquetManager {
    FrisquetRadio _radio;
    Config _cfg;
    MqttManager mqtt;
    Chaudiere boiler;
    Zone _zone1, _zone2, _zone3;
    Connect _connect{_radio, _cfg, mqtt, _zone1, _zone2, _zone3, boiler};
    Service _sondeExterieure, _satelliteZ1, _satelliteZ2, _satelliteZ3;
    unsigned dispatches = 0;
    void onRadioReceive() {
        ++dispatches;
        byte data[255];
        size_t n = sizeof(data);
        if (_radio.hasBufferedPacket())
            _radio.popBufferedPacket(data, n);
        else if (_radio.hasReceivedPacket())
            _radio.readData(data, 0);
        FrisquetRadio::receivedFlag = false;
    }
    void loop();
};
struct App {
    Service _ota, portal, _mqtt;
    Service *_portal = &portal;
    FrisquetManager _frisquetManager;
    void loop();
};

#include "services-methods.inc"

using R = FrisquetRadio;
using Kind = Connect::ReadKind;
void put(std::vector<byte> &b, size_t offset, uint16_t n) {
    b[offset] = n >> 8;
    b[offset + 1] = n;
}
uint16_t address(const R &r) {
    auto &p = r.sent.back();
    return (p[6] << 8) | p[7];
}
Radio::Packet reply(const R &r) {
    auto &p = r.sent.back();
    R::RadioTrameHeader h, answer;
    memcpy(&h, p.data(), sizeof(h));
    h.answer(answer);
    size_t bytes = 2 * ((p[8] << 8) | p[9]);
    Radio::Packet result;
    result.data.resize(7 + bytes);
    memcpy(result.data.data(), &answer, sizeof(answer));
    result.data[6] = bytes;
    if (address(r) == 0x7541) {
        result.data[7] = 0x19;
        result.data[8] = 0x02;
        result.data[9] = 0;
        result.data[10] = 0x11;
    }
    if (address(r) == 0x79f2 || address(r) == 0x7aba) {
        put(result.data, 7, 215);
        put(result.data, 19, 205);
        put(result.data, 25, 1290);
    }
    if (address(r) == 0x7a18 || address(r) == 0x7ae0) {
        put(result.data, 25, 123);
        put(result.data, 27, 456);
    }
    if (address(r) == 0xa063 || address(r) == 0xa07b)
        for (unsigned i = 0; i < 12; ++i) {
            put(result.data, 7 + i * 4, address(r) == 0xa063 ? 10 : 20);
            put(result.data, 9 + i * 4, 2);
        }
    if (address(r) == 0xa0fc)
        result.data[8] = 9;
    return result;
}
void tick(FrisquetManager &m, uint32_t t) {
    clockMs = t;
    m.loop();
    assert(clockMs == t);
}
void answer(FrisquetManager &m, uint32_t t) {
    m._radio.packets.push_back(reply(m._radio));
    tick(m, t);
}
int main() {
    FrisquetRadio::receivedFlag = false;
    clockMs = 0;
    seconds = 0;
    FrisquetManager boot;
    boot._zone2.source = Zone::SOURCE::SATELLITE;
    tick(boot, 0);
    assert(boot._radio.busy() && boot._sondeExterieure.calls == 0);
    const uint16_t addresses[] = {0x7541, 0x79f2, 0x7a18, 0xa063, 0xa07b, 0xa0fc};
    uint32_t start = 0;
    for (unsigned i = 0; i < 6; ++i) {
        tick(boot, start + 29);
        assert(boot._radio.sent.size() == i);
        tick(boot, start + 30);
        assert(address(boot._radio) == addresses[i]);
        answer(boot, start + 40);
        if (i < 5) {
            const uint32_t gap = 100;
            if (gap) {
                tick(boot, start + 40 + gap - 1);
                assert(!boot._radio.busy());
            }
            start += 40 + gap;
            tick(boot, start);
            assert(boot._radio.busy());
        }
    }
    assert(boot._connect._readKind == Kind::None && boot._radio.sent.size() == 6);
    assert(boot._connect._boilerFirmware.directPower() &&
           boot._connect._boilerFirmware.build == 11);
    assert(boot._zone1.ambient == 21.5f && boot._zone1.setpoint == 20.5f &&
           boot._zone2.ambient == -999);
    assert(boot.boiler.outdoor == -999 && boot.boiler.heat == 456 && boot.boiler.consEcs == 123);
    assert(boot.boiler.annualHeat == 120 && boot.boiler.annualEcs == 24 &&
           boot.boiler.mode == Chaudiere::MODE_ECS::ECO);
    // L'année précédente remplit son cache sans remplacer les cumuls MQTT ci-dessus.
    const auto &previousYear = boot._connect.getConsommationsAnneePrecedente();
    assert(previousYear.disponibles && previousYear.chauffage[0] == 20 && previousYear.ecs[11] == 2);

    // Chaque modèle utilise sa destination et son décalage mémoire, y compris
    // après la lecture de version. Vérifier les dix octets réellement émis.
    for (byte boiler : {0x80, 0x84}) {
        FrisquetManager model;
        model._cfg.boiler = boiler;
        seconds = 0;
        tick(model, 0);
        const uint16_t offset = boiler == 0x84 ? 0xc8 : 0;
        const uint16_t registers[] = {0x7541, uint16_t(0x79f2 + offset),
                                      uint16_t(0x7a18 + offset), 0xa063, 0xa07b, 0xa0fc};
        const byte words[] = {2, 0x1c, 0x1c, 0x18, 0x18, 1};
        // Ni la version ni l'année précédente ne modifient une valeur MQTT.
        const unsigned published[] = {0, 1, 2, 3, 3, 4};
        for (unsigned i = 0; i < 6; ++i) {
            const uint32_t begin = i * 140;
            if (i) tick(model, begin);
            tick(model, begin + 30);
            const std::vector<byte> expected = {boiler, 0x7e, 55, byte(i + 1), 1, 3,
                byte(registers[i] >> 8), byte(registers[i]), 0, words[i]};
            assert(model._radio.sent.back() == expected);
            answer(model, begin + 40);
            assert(model.boiler.publishes == published[i]);
            assert(model._zone1.publishes == (i ? 1U : 0U));
            assert(model._zone2.publishes == (i ? 1U : 0U));
            assert(model._zone3.publishes == (i ? 1U : 0U));
        }
        assert(model._connect._readKind == Kind::None && !model._radio.busy());
        assert(model._zone1.ambient == 21.5f && model.boiler.heat == 456);
    }

    // La lecture de version au démarrage dispose d’une émission et d’une réception.
    // Un échec conserve les puissances divisées par dix et poursuit les lectures initiales.
    FrisquetManager versionTimeout;
    seconds = 0;
    tick(versionTimeout, 0);
    tick(versionTimeout, 30);
    assert(address(versionTimeout._radio) == 0x7541);
    tick(versionTimeout, 438);
    assert(versionTimeout._radio.sent.size() == 1 &&
           !versionTimeout._connect._boilerFirmware.known);
    tick(versionTimeout, 537);
    assert(!versionTimeout._radio.busy());
    tick(versionTimeout, 538);
    tick(versionTimeout, 568);
    assert(address(versionTimeout._radio) == 0x79f2);
    auto power = reply(versionTimeout._radio);
    put(power.data, 41, 200);
    put(power.data, 43, 300);
    versionTimeout._radio.packets.push_back(power);
    tick(versionTimeout, 580);
    assert(versionTimeout.boiler.powerEcs == 20.0f && versionTimeout.boiler.powerHeat == 30.0f);
    assert(boot._connect.applyInformation(power.data.data(), power.data.size()));
    assert(boot.boiler.powerEcs == 200.0f && boot.boiler.powerHeat == 300.0f);

    FrisquetManager invalidVersion;
    seconds = 0;
    tick(invalidVersion, 0);
    tick(invalidVersion, 30);
    auto invalid = reply(invalidVersion._radio);
    invalid.data[10] = 0xFA;
    invalidVersion._radio.packets.push_back(invalid);
    tick(invalidVersion, 40);
    assert(!invalidVersion._connect._boilerFirmware.known &&
           invalidVersion._radio.sent.size() == 1);
    FrisquetManager passiveVersion;
    passiveVersion._cfg.passive = true;
    seconds = 0;
    tick(passiveVersion, 0);
    assert(passiveVersion._radio.sent.empty() && !passiveVersion._radio.busy());

    FrisquetManager unassociated;
    unassociated._connect.association = 255;
    seconds = 0;
    tick(unassociated, 0);
    assert(unassociated._radio.sent.empty() && !unassociated._connect._boilerFirmware.known);
    assert(!unassociated._radio.busy());
    tick(unassociated, 30);
    assert(unassociated._radio.sent.empty() && !unassociated._radio.busy());

    // Au créneau de consommation, simuler un délai expiré puis une réussite. Chaque
    // passage dans App exécute OTA, HTTP et MQTT, y compris pendant la nouvelle tentative.
    App app;
    auto &m = app._frisquetManager;
    m._connect._initialReadPending = false;
    clockMs = 300000;
    seconds = 300;
    app.loop();
    app.loop();
    app.loop();
    app.loop();
    assert(address(m._radio) == 0x7a18);
    m._radio.packets.push_back(reply(m._radio));
    app.loop();
    for (unsigned i = 0; i < 100 && m._radio.sent.size() < 2; ++i)
        app.loop();
    assert(m._radio.sent.size() == 2);
    assert(address(m._radio) == 0xa063);
    const auto initialLoops = app._ota.calls;
    for (unsigned i = 0; i < 60; ++i)
        app.loop(); // dépasse un délai de réception de 408 ms
    assert(m._radio.sent.size() == 2 && !m._radio.readReady(ID_CONNECT));
    m._radio.packets.push_back(reply(m._radio));
    app.loop();
    assert(app._ota.calls - initialLoops == 61 && app._ota.calls == app.portal.calls &&
           app._ota.calls == app._mqtt.calls);
    assert(m.boiler.annualHeat == 120);
    for (unsigned i = 0; i < 100 && m._radio.sent.size() < 3; ++i)
        app.loop();
    assert(m._radio.sent.size() == 3 && address(m._radio) == 0xa07b);
    m._radio.packets.push_back(reply(m._radio));
    app.loop();
    assert(m._connect._readKind == Kind::None && m.boiler.annualHeat == 120);
    assert(m._connect.getConsommationsAnneePrecedente().chauffage[0] == 20);
    for (unsigned i = 0; i < 50; ++i)
        app.loop();
    assert(m._radio.sent.size() == 3); // aucune répétition du même créneau

    // Aucune réponse : deux tentatives externes, cinq émissions chacune, cinq réceptions par
    // émission.
    FrisquetManager absent;
    absent._connect._initialReadPending = false;
    seconds = 180;
    tick(absent, 180000);
    for (unsigned i = 1; absent._connect._readKind != Kind::None && i < 3000; ++i)
        tick(absent, 180000 + i * 10);
    assert(absent._connect._readKind == Kind::None && absent._radio.sent.size() == 10);
    assert(absent._radio.sent[0][3] == 1 && absent._radio.sent[4][3] == 1 &&
           absent._radio.sent[5][3] == 2);
    tick(absent, clockMs + 10);
    assert(absent._radio.sent.size() == 10 && absent.boiler.publishes == 0);

    // Une suspension abandonne le résultat périmé ; la collecte initiale reprend
    // après libération, y compris si la version n'avait pas encore été décodée.
    for (unsigned phase = 0; phase < 4; ++phase) {
        FrisquetManager c;
        seconds = 0;
        tick(c, 0);
        if (phase)
            tick(c, 30);
        if (phase == 2) {
            c._radio.packets.push_back(reply(c._radio));
            c._radio.pollRead();
        }
        if (phase == 3)
            answer(c, 40);
        const auto sent = c._radio.sent.size();
        c._radio.suspend(R::Pause::HttpOta, true);
        tick(c, 10000);
        assert(c._connect._readKind == Kind::None && c._radio.sent.size() == sent);
        assert(c._connect._initialReadPending && c._radio.busy());
        c._radio.suspend(R::Pause::HttpOta, false);
        tick(c, 10010);
        tick(c, 10040);
        assert(address(c._radio) == 0x7541 && c._radio.sent.size() == sent + 1);
        assert(!c._connect._boilerFirmware.known);
        answer(c, 10050);
        assert(c._connect._boilerFirmware.directPower());
    }
    // Une lecture périodique annulée ne relance pas la collecte initiale terminée.
    {
        FrisquetManager c;
        c._connect._initialReadPending = false;
        seconds = 180;
        tick(c, 180000);
        tick(c, 180030);
        const auto sent = c._radio.sent.size();
        c._radio.cancelOperations();
        tick(c, 180040);
        assert(c._connect._readKind == Kind::None && c._radio.sent.size() == sent &&
               !c._radio.busy());
    }
    // L'API d'annulation directe invalide la collecte pendant la transaction
    // comme pendant l'intervalle libre entre deux lectures.
    for (unsigned phase = 0; phase < 4; ++phase) {
        FrisquetManager c;
        seconds = 0;
        tick(c, 0);
        if (phase) tick(c, 30);
        if (phase == 2) {
            c._radio.packets.push_back(reply(c._radio));
            clockMs = 40;
            c._radio.pollRead();
            assert(c._radio.readReady(ID_CONNECT));
        }
        if (phase == 3) answer(c, 40);
        c._radio.cancelRead();
        c._connect.pollReads();
        assert(c._connect._readKind == Kind::None && !c._radio.busy());
        assert(c._connect._initialReadPending);
        const uint32_t resume = clockMs + 10;
        tick(c, resume);
        tick(c, resume + 30);
        assert(address(c._radio) == 0x7541 && !c._connect._boilerFirmware.known);
    }
    for (unsigned change = 0; change < 5; ++change) {
        FrisquetManager c;
        seconds = 0;
        tick(c, 0);
        if (change == 0)
            c._cfg.enabled = false;
        if (change == 1)
            c._cfg.passive = true;
        if (change == 2)
            c._connect.association = 77;
        if (change == 3)
            c._connect.association = 255;
        if (change == 4)
            c._cfg.boiler = 0x84;
        clockMs = 30;
        c._connect.pollReads();
        assert(!c._radio.busy() && c._radio.sent.empty());
        c._cfg.enabled = true;
        c._cfg.passive = false;
        if (change == 3)
            c._connect.association = 55;
        tick(c, 40);
        tick(c, 70);
        assert(address(c._radio) == 0x7541);
        assert(c._radio.sent.back()[0] == c._cfg.boilerId());
    }

    // La dernière consigne ECS reste indépendante d’une réponse à une lecture antérieure.
    FrisquetManager ecs;
    ecs._connect._initialReadPending = false;
    seconds = 420;
    tick(ecs, 420000);
    tick(ecs, 420030);
    ecs._connect.handleModeEcsCommand("eco");
    ecs._connect.handleModeEcsCommand("max");
    assert(ecs.boiler.writes == 0);
    answer(ecs, 420040);
    assert(ecs.boiler.writes == 1 && ecs.boiler.lastSent == Chaudiere::MODE_ECS::MAX);
    assert(ecs.boiler.publishedModes == std::vector<Chaudiere::MODE_ECS>{Chaudiere::MODE_ECS::MAX});
    assert(ecs._connect._modeECSFrameBits == 1); // conserver les bits bruts pour l'envoi
    tick(ecs, 420050);
    assert(ecs.boiler.writes == 1);

    // Une commande peut arriver pendant une lecture. Conserver la consigne demandée
    // et affichée pour chaque zone, tout en acceptant la température ambiante
    // mesurée. Les observations suivantes s'appliquent même sans envoi réussi.
    for (unsigned zoneNumber = 0; zoneNumber < 3; ++zoneNumber) {
        for (bool retry : {false, true}) {
            FrisquetManager pending;
            pending._connect._initialReadPending = false;
            seconds = 180;
            tick(pending, 180000);
            tick(pending, 180030);
            Zone *zones[] = {&pending._zone1, &pending._zone2, &pending._zone3};
            auto &zone = *zones[zoneNumber];
            zone.changed = 180035;
            zone.setpoint = 23.5f;
            uint32_t completed = 180040;
            if (retry) {
                // Une réponse malformée provoque la deuxième tentative externe.
                // Celle-ci doit conserver le contexte antérieur à la commande.
                auto invalid = reply(pending._radio);
                invalid.data[6] = 0;
                pending._radio.packets.push_back(invalid);
                tick(pending, 180040);
                tick(pending, 180140);
                tick(pending, 180170);
                assert(pending._radio.sent.size() == 2);
                completed = 180180;
            }
            answer(pending, completed);
            assert(zone.setpoint == 23.5f && zone.ambient != -999);
            // Sans envoi réussi, une lecture ultérieure doit quand même reprendre
            // la consigne observée : programmation absente ou configuration incomplète.
            pending._connect.startReads(Kind::Information, Kind::Information);
            tick(pending, completed + 30);
            answer(pending, completed + 40);
            assert(zone.setpoint != 23.5f);
            zone.sent = completed + 45;
            auto observed = reply(pending._radio);
            assert(pending._connect.applyInformation(observed.data.data(), observed.data.size()));
            assert(zone.setpoint != 23.5f);
        }
    }

    // Valider avant application : une réponse tronquée ou de longueur incohérente
    // laisse les valeurs intactes ; une nouvelle réponse valide les met à jour.
    FrisquetManager malformed;
    malformed._connect._initialReadPending = false;
    seconds = 300;
    tick(malformed, 300000);
    tick(malformed, 300030);
    auto bad = reply(malformed._radio);
    bad.data.resize(28);
    malformed._radio.packets.push_back(bad);
    tick(malformed, 300040);
    assert(malformed.boiler.heat == -999 && malformed._connect._readAttempts == 1);
    tick(malformed, 300139);
    assert(!malformed._radio.busy());
    tick(malformed, 300140);
    tick(malformed, 300170);
    answer(malformed, 300180);
    assert(malformed.boiler.heat == 456);
    auto good = reply(boot._radio);
    for (size_t n = 0; n < 9; ++n)
        assert(!boot._connect.applyEcs(good.data.data(), n));
    good.data[6] = 1;
    assert(!boot._connect.applyEcs(good.data.data(), good.data.size()));
    auto &annual = m._radio;
    auto monthly = reply(annual);
    for (size_t n = 0; n < 55; ++n)
        assert(!m._connect.setConsommationAnnuelle(monthly.data.data(), n));
    monthly.data[6] = 47;
    assert(!m._connect.setConsommationAnnuelle(monthly.data.data(), monthly.data.size()));

    // Aucun rattrapage hors créneau ; la collecte initiale consomme le créneau en cours.
    FrisquetManager quiet;
    quiet._connect._initialReadPending = false;
    seconds = 179;
    tick(quiet, 179000);
    seconds = 191;
    tick(quiet, 191000);
    assert(quiet._radio.sent.empty());
    for (unsigned slot : {180, 360, 480, 300, 420}) {
        FrisquetManager overlap;
        seconds = slot;
        const uint32_t base = slot * 1000;
        tick(overlap, base);
        tick(overlap, base + 30);
        for (unsigned i = 0; i < 6; ++i) {
            answer(overlap, base + 40 + i * 140);
            if (i < 5) {
                tick(overlap, base + 140 + i * 140);
                tick(overlap, base + 170 + i * 140);
            }
        }
        for (unsigned i = 840; i <= 1140; i += 10)
            tick(overlap, base + i);
        assert(overlap._connect._readKind == Kind::None && !overlap._radio.busy());
        assert(overlap._radio.sent.size() == 6 && !overlap._connect._initialReadPending);
    }

    // Entre deux lectures initiales, envoyer d'abord la commande ECS différée.
    FrisquetManager priority;
    seconds = 0;
    tick(priority, 0);
    tick(priority, 30);
    priority._connect.handleModeEcsCommand("max");
    priority._radio.packets.push_back(reply(priority._radio));
    clockMs = 40;
    priority._connect.pollReads();
    assert(priority._connect._modeEcsCommandPending);
    clockMs = 140;
    priority._connect.pollReads();
    assert(!priority._radio.busy() && priority.boiler.writes == 0);
    priority._connect.loop();
    assert(priority.boiler.writes == 1);
    priority._connect.pollReads();
    assert(priority._radio.busy());

    // Traiter un paquet déjà reçu avant de réserver la prochaine lecture,
    // qu'il soit signalé par IRQ, encore matériel ou déjà dans le tampon logiciel.
    for (unsigned queued = 0; queued < 3; ++queued) {
        FrisquetManager packet;
        seconds = 0;
        tick(packet, 0);
        tick(packet, 30);
        answer(packet, 40);
        if (queued == 0)
            R::receivedFlag = true;
        if (queued == 1)
            packet._radio.packets.push_back(reply(packet._radio));
        if (queued == 2) {
            R::RadioTrameHeader other;
            other.idMessage = 99;
            assert(packet._radio.beginObservation(8, other));
            packet._radio.packets.push_back(reply(packet._radio));
            clockMs = 340;
            packet._radio.pollObservation();
            packet._radio.cancelObservation();
            assert(packet._radio.hasBufferedPacket());
        }
        clockMs = 400;
        packet._connect.pollReads();
        assert(!packet._radio.busy());
        tick(packet, 410);
        assert(packet.dispatches == 1 && !packet._radio.busy());
        tick(packet, 420);
        assert(packet._radio.busy());
    }
    puts("OK : démarrage et planification Connect, progression des services pendant les attentes, "
         "limites des tentatives, annulation, consigne ECS, bornes et absence de rattrapage");
}
