#include "radio-fixture.h"
void delay(uint32_t) {
    assert(false && "un appel occupé ne doit pas attendre");
}

using R = FrisquetRadio;
R::RadioTrameHeader expected() {
    R::RadioTrameHeader h;
    h.idDestinataire = 8;
    h.idExpediteur = 0x80;
    h.idAssociation = 55;
    h.idMessage = 42;
    h.idReception = 0x81;
    h.type = 0x17;
    return h;
}
Radio::Packet packet(R::RadioTrameHeader h = expected(), int16_t result = 0) {
    Radio::Packet p{{}, result};
    p.data.resize(49);
    memcpy(p.data.data(), &h, sizeof(h));
    return p;
}
void poll(R &r, uint32_t when) {
    clockMs = when;
    r.pollObservation();
    assert(clockMs == when);
}
int16_t take(R &r) {
    byte out[255];
    size_t n = sizeof(out);
    return r.takeObservation(8, out, n);
}
int main() {
    // Aucune lecture matérielle ni avance du temps pendant la garde ; le résultat
    // conserve son paquet même si un autre arrive avant sa consommation.
    R success;
    clockMs = 0;
    auto wanted = expected();
    assert(success.beginObservation(8, wanted));
    wanted.idMessage = 99;
    success.packets.push_back(packet());
    for (uint32_t t = 0; t < 300; ++t)
        poll(success, t);
    assert(success.reads == 0 && success.busy() && !success.beginObservation(9, expected()));
    poll(success, 300);
    assert(success.observationReady(8) && success.armed);
    assert(!success.observationReady(9));
    byte out[255];
    size_t n = sizeof(out);
    assert(success.takeObservation(9, out, n) == R::ERR_BUSY && success.busy());
    success.packets.push_back(packet());
    assert(success.takeObservation(8, out, n) == 0 && n == 49 && out[3] == 42 && !success.busy());
    assert(take(success) == R::ERR_BUSY);

    // La première lecture est immédiate, puis viennent exactement 14 fenêtres de réception.
    R missing;
    clockMs = 0;
    assert(missing.beginObservation(8, expected()));
    poll(missing, 300);
    assert(missing.reads == 1 && !missing.observationReady(8));
    for (unsigned attempt = 1; attempt <= 14; ++attempt) {
        poll(missing, 300 + attempt * 408 - 1);
        assert(!missing.observationReady(8));
        poll(missing, 300 + attempt * 408);
    }
    assert(missing.observationReady(8) && take(missing) == -6 && missing.reads == 1 &&
           missing.armed);

    // Chaque paquet tronqué, sans correspondance ou au CRC invalide consomme une tentative.
    R mixed;
    clockMs = 0;
    assert(mixed.beginObservation(8, expected()));
    auto shortPacket = packet();
    shortPacket.data.resize(2);
    mixed.packets.push_back(shortPacket);
    poll(mixed, 300);
    auto other = expected();
    other.idMessage = 9;
    mixed.packets.push_back(packet(other));
    poll(mixed, 310);
    mixed.packets.push_back(packet(expected(), -7));
    poll(mixed, 320);
    mixed.packets.push_back(packet());
    poll(mixed, 330);
    assert(take(mixed) == 0);
    n = sizeof(out);
    float rssi = 0;
    assert(mixed.popBufferedPacket(out, n, &rssi));
    assert(n == 49 && out[3] == 9 && rssi == -55 && !mixed.hasBufferedPacket());

    R exhausted;
    clockMs = 0;
    assert(exhausted.beginObservation(8, expected()));
    for (unsigned i = 0; i < 15; ++i) {
        exhausted.packets.push_back(packet(expected(), -7));
        poll(exhausted, 300 + i);
    }
    assert(exhausted.reads == 15 && take(exhausted) == -7);

    // Conserver le traitement des adresses absentes et la règle de sélection des associations.
    R address;
    clockMs = 0;
    assert(address.beginObservation(8, expected()));
    other.type = 0x83;
    address.packets.push_back(packet(other));
    poll(address, 300);
    assert(take(address) == -9);
    R association;
    clockMs = 0;
    assert(association.beginObservation(8, expected()));
    other = expected();
    other.idAssociation = 77;
    association.packets.push_back(packet(other));
    poll(association, 300);
    assert(take(association) == 0);

    // Annuler dans chaque phase active : aucun résultat périmé, réservation réutilisable.
    for (unsigned phase = 0; phase < 3; ++phase) {
        R cancel;
        clockMs = 0;
        assert(cancel.beginObservation(8, expected()));
        if (phase == 2)
            cancel.packets.push_back(packet());
        if (phase)
            poll(cancel, 300);
        cancel.cancelObservation();
        assert(!cancel.busy() && !cancel.ownsObservation(8));
        poll(cancel, 10000);
        assert(take(cancel) == R::ERR_BUSY);
        assert(cancel.beginObservation(9, expected()));
        cancel.suspend(R::Pause::ArduinoOta, true);
        assert(cancel.busy() && !cancel.beginObservation(8, expected()));
        cancel.suspend(R::Pause::ArduinoOta, false);
        assert(!cancel.busy() && cancel.armed);
    }
    R config;
    clockMs = 0;
    assert(config.beginObservation(8, expected()));
    config.setNetworkID({{1, 2, 3, 4}});
    assert(!config.busy() && config.syncChanges == 1);

    R gate;
    clockMs = 0;
    assert(gate.beginObservation(8, expected()));
    n = sizeof(out);
    assert(gate.sendAsk(1, 2, 3, 4, 5, 6, 7, out, n) == R::ERR_BUSY);
    assert(gate.sendInit(1, 2, 3, 4, 5, 6, 7, 8, 9, out, 1, out, n) == R::ERR_BUSY);
    assert(gate.sendAnswer(1, 2, 3, 4, 5, 6, out, 1) == R::ERR_BUSY);
    assert(gate.receiveExpected(1, 2, 3, 4, 5, 6, out, n) == R::ERR_BUSY);
    assert(!gate.reads && !gate.rearms);
    gate.suspend(R::Pause::HttpOta, true);
    gate.suspend(R::Pause::ArduinoOta, true);
    gate.suspend(R::Pause::ArduinoOta, false);
    assert(gate.busy());
    gate.suspend(R::Pause::HttpOta, false);
    assert(!gate.busy());
    gate.suspend(R::Pause::Reboot, true);
    gate.suspend(R::Pause::HttpOta, false);
    assert(gate.busy());

    R restart;
    clockMs = 0;
    assert(restart.beginObservation(8, expected()));
    restart.rearmResult = -2;
    poll(restart, 300);
    assert(take(restart) == -2);
    R finalRestart;
    clockMs = 0;
    assert(finalRestart.beginObservation(8, expected()));
    finalRestart.packets.push_back(packet());
    finalRestart.rearmResult = -2;
    poll(finalRestart, 300);
    assert(take(finalRestart) == RADIOLIB_ERR_NONE);

    R wrap;
    clockMs = 0xffffff00U;
    const auto start = clockMs;
    assert(wrap.beginObservation(8, expected()));
    poll(wrap, start + 299);
    assert(wrap.reads == 0);
    poll(wrap, start + 300);
    wrap.packets.push_back(packet());
    poll(wrap, start + 350);
    assert(take(wrap) == 0);
    puts("OK : réception coopérative, garde, tentatives, sélection, CRC, tampon, réservation, "
         "annulation, suspension, échec de réarmement et débordement du compteur");
}
