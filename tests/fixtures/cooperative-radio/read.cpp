#include "radio-fixture.h"
void delay(uint32_t) {
    assert(false && "un appel occupé ne doit pas attendre");
}

using R = FrisquetRadio;
R::RadioTrameHeader request() {
    R::RadioTrameHeader h;
    h.idDestinataire = 0x80;
    h.idExpediteur = 0x7e;
    h.idAssociation = 55;
    h.idMessage = 42;
    h.idReception = 1;
    h.type = 3;
    return h;
}
Radio::Packet reply(int16_t error = 0) {
    R::RadioTrameHeader h;
    request().answer(h);
    Radio::Packet p{{}, error};
    p.data.resize(55);
    memcpy(p.data.data(), &h, sizeof(h));
    p.data[6] = 48;
    return p;
}
void poll(R &r, uint32_t t) {
    clockMs = t;
    r.pollRead();
    assert(clockMs == t);
}
void begin(R &r, uint8_t retry = 5) {
    assert(r.beginRead(0x7e, request(), 0xa063, 24, retry));
}
int16_t take(R &r) {
    byte out[255];
    size_t n = sizeof(out);
    return r.takeRead(0x7e, out, n);
}
int main() {
    // Chaque champ de corrélation doit distinguer une autre trame de la réponse.
    for (unsigned field : {0, 1, 3, 4, 5}) {
        R candidate;
        clockMs = 0;
        begin(candidate);
        poll(candidate, 30);
        auto unrelated = reply();
        unrelated.data[field] ^= 1;
        candidate.packets.push_back(unrelated);
        poll(candidate, 40);
        assert(!candidate.readReady(0x7e));
        candidate.packets.push_back(reply());
        poll(candidate, 50);
        assert(take(candidate) == 0);
    }

    static_assert(sizeof(R::RadioTrameHeader) == 6 && sizeof(R::RadioTrameAsk) == 4);
    R r;
    clockMs = 0;
    begin(r);
    assert(!r.beginObservation(8, request()) && !r.beginRead(9, request(), fword(), fword()));
    for (unsigned t = 0; t < 30; ++t)
        poll(r, t);
    assert(r.sent.empty() && r.reads == 0);
    poll(r, 30);
    assert(r.sent.size() == 1 && r.armed);
    assert(r.sent[0] == std::vector<byte>({0x80, 0x7e, 55, 42, 1, 3, 0xa0, 0x63, 0, 24}));
    for (unsigned t = 31; t < 438; ++t)
        poll(r, t);
    assert(r.reads == 0 && r.sent.size() == 1);
    poll(r, 438);
    assert(!r.readReady(0x7e)); // expiration de la première fenêtre de réception
    r.packets.push_back(reply());
    poll(r, 630);
    assert(r.readReady(0x7e) && r.busy() && r.armed && r.sent.size() == 1);
    byte out[255];
    size_t n = sizeof(out);
    assert(r.takeRead(8, out, n) == R::ERR_BUSY);
    assert(r.sendAsk(1, 2, 3, 4, 5, 6, 7, out, n) == R::ERR_BUSY && r.sent.size() == 1);
    r.packets.push_back(reply()); // ne doit pas écraser la réponse réservée
    assert(r.takeRead(0x7e, out, n) == 0 && n == 55 && out[3] == 42 && !r.busy());

    // Cinq émissions, chacune avec cinq délais de réception, puis libération à la consommation.
    R absent;
    clockMs = 0;
    begin(absent);
    for (unsigned tx = 1; tx <= 5; ++tx) {
        poll(absent, clockMs + 29);
        assert(absent.sent.size() == tx - 1);
        poll(absent, clockMs + 1);
        assert(absent.sent.size() == tx);
        for (unsigned rx = 1; rx <= 5; ++rx) {
            const auto rearms = absent.rearms;
            poll(absent, clockMs + 407);
            assert(absent.rearms == rearms && !absent.readReady(0x7e));
            poll(absent, clockMs + 1);
        }
    }
    assert(clockMs == 5 * (30 + 5 * 408) && absent.readReady(0x7e) && absent.reads == 0);
    assert(take(absent) == -6 && !absent.busy());

    // CRC invalide, en-tête tronqué, paquet sans rapport, puis réponse attendue tardive.
    R mixed;
    clockMs = 0;
    begin(mixed);
    poll(mixed, 30);
    mixed.packets.push_back(reply(-7));
    poll(mixed, 40);
    auto shortOne = reply();
    shortOne.data.resize(2);
    mixed.packets.push_back(shortOne);
    poll(mixed, 50);
    auto other = reply();
    other.data[3] = 99;
    mixed.packets.push_back(other);
    poll(mixed, 60);
    mixed.packets.push_back(reply());
    poll(mixed, 70);
    assert(take(mixed) == 0 && mixed.reads == 4);
    n = sizeof(out);
    float rssi = 0;
    uint32_t reception = 0;
    assert(mixed.popBufferedPacket(out, n, &rssi, &reception));
    assert(reception == 60);
    assert(out[3] == 99 && n == 55 && rssi == -55);
    R ttl;
    clockMs = 0;
    begin(ttl);
    poll(ttl, 30);
    ttl.packets.push_back(other);
    poll(ttl, 40);
    clockMs = 1041;
    assert(!ttl.hasBufferedPacket());
    ttl.cancelRead();
    const auto generation = ttl.operationGeneration();
    ttl.cancelRead(); // Une annulation entre deux lectures invalide aussi la collecte.
    assert(ttl.operationGeneration() != generation && !ttl.busy());

    R cancelled;
    clockMs = 0;
    begin(cancelled);
    const auto activeGeneration = cancelled.operationGeneration();
    cancelled.cancelRead();
    poll(cancelled, 1000);
    assert(cancelled.operationGeneration() != activeGeneration);
    assert(!cancelled.busy() && cancelled.armed && cancelled.sent.empty());

    R crc;
    clockMs = 0;
    begin(crc, 2);
    poll(crc, 30);
    for (unsigned i = 0; i < 2; ++i) {
        crc.packets.push_back(reply(-7));
        poll(crc, clockMs + 1);
    }
    poll(crc, clockMs + 30);
    assert(crc.sent.size() == 2);
    for (unsigned i = 0; i < 2; ++i) {
        crc.packets.push_back(reply(-7));
        poll(crc, clockMs + 1);
    }
    assert(take(crc) == -7);
    R address;
    clockMs = 0;
    begin(address);
    poll(address, 30);
    other.data[5] = 0x83;
    address.packets.push_back(other);
    poll(address, 31);
    assert(take(address) == -9 && address.sent.size() == 1);
    R assoc;
    clockMs = 0;
    begin(assoc);
    poll(assoc, 30);
    other = reply();
    other.data[2] = 99;
    assoc.packets.push_back(other);
    poll(assoc, 31);
    assert(take(assoc) == 0);

    R txFail;
    clockMs = 0;
    txFail.txResult = -5;
    begin(txFail);
    for (unsigned tx = 1; tx <= 5; ++tx)
        poll(txFail, tx * 30);
    assert(txFail.sent.size() == 5 && txFail.reads == 0 && take(txFail) == -5 && txFail.armed);
    for (unsigned phase = 0; phase < 3; ++phase) {
        R fail;
        clockMs = 0;
        begin(fail);
        if (phase == 0)
            fail.rearmResult = -2;
        poll(fail, 30);
        if (phase) {
            fail.rearmResult = -2;
            if (phase == 2)
                fail.packets.push_back(reply());
            poll(fail, 438);
        }
        // Une réponse déjà reçue reste exploitable si le réarmement suivant échoue.
        assert(take(fail) == (phase == 2 ? RADIOLIB_ERR_NONE : -2));
    }

    for (unsigned phase = 0; phase < 4; ++phase) {
        R c;
        clockMs = 0;
        begin(c);
        if (phase)
            poll(c, 30);
        if (phase == 2) {
            c.packets.push_back(reply());
            poll(c, 31);
        }
        if (phase == 3)
            for (unsigned i = 0; i < 5; ++i)
                poll(c, clockMs + 408);
        auto generation = c.operationGeneration();
        c.suspend(R::Pause::HttpOta, true);
        assert(c.operationGeneration() != generation && c.busy() && !c.ownsRead(0x7e));
        const auto sent = c.sent.size();
        poll(c, clockMs + 10000);
        assert(c.sent.size() == sent);
        c.suspend(R::Pause::ArduinoOta, true);
        c.suspend(R::Pause::HttpOta, false);
        assert(c.busy());
        c.suspend(R::Pause::ArduinoOta, false);
        assert(!c.busy() && c.armed);
        begin(c);
        c.setNetworkID({{1, 2, 3, 4}});
        assert(!c.busy() && take(c) == R::ERR_BUSY);
    }
    R wrap;
    clockMs = 0xfffffff0U;
    const auto start = clockMs;
    begin(wrap);
    poll(wrap, start + 29);
    assert(wrap.sent.empty());
    poll(wrap, start + 30);
    poll(wrap, start + 437);
    assert(wrap.rearms == 1);
    poll(wrap, start + 438);
    assert(wrap.rearms == 2);
    wrap.packets.push_back(reply());
    poll(wrap, start + 500);
    assert(take(wrap) == 0);
    R zero;
    assert(!zero.beginRead(1, request(), fword(), fword(), 0) && !zero.busy());
    puts("OK : lectures coopératives, délais et nouvelles tentatives, épuisement des 25 "
         "tentatives, CRC, réponses tronquées, inattendues ou tardives, expiration, erreurs TX/RX, "
         "réservation, suspension, annulation et débordement du compteur");
}
