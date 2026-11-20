#include "materiel.h"
#include "ProgrammationPortail.h"
#include "Frisquet/ZoneProgram.h"
#include "Frisquet/Satellite.h"
#include <cassert>

namespace {
const char* const decouverte = "homeassistant/climate/openFrisquetVisio/thermostatZ1/config";

JsonDocument lireJson(const String& message) {
    JsonDocument doc;
    assert(deserializeJson(doc, message) == DeserializationError::Ok);
    return doc;
}

struct ConnectDeTest : Connect {
    using Connect::Connect;
    using Connect::setIdMessage;
};

struct Banc {
    Client reseau;
    MqttManager mqtt{reseau};
    MqttDevice appareil;
    Config config;
    FrisquetRadio radio;
    Chaudiere chaudiere{mqtt, config};
    Zone z1{ID_ZONE_1, mqtt}, z2{ID_ZONE_2, mqtt}, z3{ID_ZONE_3, mqtt};
    ConnectDeTest connect{radio, config, mqtt, z1, z2, z3, chaudiere};

    Banc() {
        Preferences::effacer();
        publications.clear();
        nombrePublications.clear();
        emissions.clear();
        reception.clear();
        journal.clear();
        tempsMs = 0;
        heureConnue = false;
        heureChaudiere = 0;
        appareil.deviceId = "openFrisquetVisio";
        appareil.baseTopic = "frisquet";
        mqtt.registerDevice(appareil);
    }
};

void initialiser(Zone& zone, Zone::SOURCE source) {
    zone.setSource(source);
    zone.setPilotageActif(true);
    zone.begin();
    zone.setTemperatureConfort(20);
    zone.setTemperatureReduit(17);
    zone.setTemperatureHorsGel(8);
    zone.setTemperatureBoost(2);
}

Zone::Programmation programmeReduit() {
    Zone::Programmation programme;
    std::memset(&programme, 0, sizeof(programme));
    return programme;
}

void verifierCreneaux() {
    byte semaine[42] = {};
    semaine[0] = 1;
    semaine[41] = 128;
    assert(ZoneProgram::comfort(semaine, 0));
    assert(!ZoneProgram::comfort(semaine, 1));
    assert(ZoneProgram::comfort(semaine, 335));
    assert(ZoneProgram::comfort(semaine, 336));
    assert(ZoneProgram::nextTransition(semaine, 335, 120) == 3480);
    std::memset(semaine, 0, sizeof(semaine));
    assert(ZoneProgram::nextTransition(semaine, 0, 0) == 0);
    std::memset(semaine, 255, sizeof(semaine));
    assert(ZoneProgram::nextTransition(semaine, 335, 1799) == 0);
    for (unsigned i = 0; i < 336; ++i) {
        std::memset(semaine, 0, sizeof(semaine));
        semaine[i / 8] = 1U << (i % 8);
        assert(ZoneProgram::comfort(semaine, i));
        assert(ZoneProgram::nextTransition(semaine, i, 1799) == 1);
    }
}

void verifierOptions() {
    Banc banc;
    Zone& z = banc.z1;
    for (byte options : {0x20, 0x21, 0x24, 0x25, 0x27}) {
        z.setModeOptions(options);
        z.setMode(Zone::CONFORT);
        assert(z.getModeOptions() == 0x01);
        z.setModeOptions(options);
        z.setMode(Zone::REDUIT);
        assert(z.getModeOptions() == 0x00);
        z.setModeOptions(options);
        z.setMode(Zone::HORS_GEL);
        assert(z.getModeOptions() == 0x10);
    }
    z.setModeOptions(0xED);
    z.setMode(Zone::CONFORT);
    assert(z.getModeOptions() == 0xC9);
    z.setModeOptions(0x25);
    z.setMode(Zone::AUTO, true);
    assert(z.getModeOptions() == 0x25);

    z.setMode(Zone::CONFORT);
    z.activerBoost();
    const byte options = z.getModeOptions();
    for (byte invalide : {0x09, 0x00}) {
        z.setMode(static_cast<Zone::MODE_ZONE>(invalide));
        assert(z.getMode() == Zone::CONFORT && z.getModeOptions() == options);
        assert(z.boostActif());
    }
    z.setMode(Zone::REDUIT);
    assert(z.getMode() == Zone::REDUIT && z.boostActif());
    z.setMode(Zone::INCONNU);
    assert(z.getMode() == Zone::INCONNU);
}

void verifierModesLocaux() {
    Banc banc;
    Zone& z = banc.z1;
    z.setSource(Zone::SATELLITE_VIRTUEL);
    z.setPilotageActif(true);
    assert(!z.appliquerPreset("Confort permanent") && z.getMode() == Zone::INCONNU);
    initialiser(z, Zone::SATELLITE_VIRTUEL);
    auto climat = lireJson(publications.at(decouverte));
    assert(climat["preset_modes"].is<JsonArray>());
    for (auto mode : climat["preset_modes"].as<JsonArray>())
        assert(!strstr(mode.as<const char*>(), "Dérogation"));
    assert(climat["mode_command_topic"] == "frisquet/z1/thermostat/set");
    const auto nombre = nombrePublications.at(decouverte);
    z.loop();
    assert(nombrePublications.at(decouverte) == nombre);
    assert(!z.appliquerPreset("Auto") && !z.appliquerPreset("Dérogation Confort"));
    assert(z.appliquerPreset("Confort permanent") && z.getMode() == Zone::CONFORT);
    assert(z.getNomMode() == "Confort" && z.getTemperatureConsigne() == 20);
    assert(publications.at("frisquet/z1/thermostat") == "heat");
    assert(z.appliquerPreset("Réduit") && z.getNomMode() == "Réduit");
    assert(z.getTemperatureConsigne() == 17 && publications.at("frisquet/z1/regime") == "Réduit");
    assert(z.appliquerPreset("Hors Gel") && z.getTemperatureConsigne() == 8);
    assert(z.appliquerPreset("Boost") && z.getNomMode() == "Boost");
    assert(z.getTemperatureConsigne() == 22 && publications.at("frisquet/z1/mode") == "Boost");
    for (byte confort : {30, 28, 30}) {
        z.setTemperatureConfort(confort);
        assert(z.appliquerPreset("Boost") && z.getTemperatureConsigne() == 30);
        z.setTemperatureBoost(5);
    }
    z.setTemperatureConfort(20);
    z.setTemperatureBoost(2);
    assert(z.appliquerConsigne(23) && z.getTemperatureConsigne() == 23 && z.getTemperatureConfort() == 21);
    assert(!z.appliquerConsigne(NAN) && !z.appliquerConsigne(31) && !z.appliquerConsigne(5));
    assert(z.appliquerPreset("none") && !z.boostActif() && z.getMode() == Zone::CONFORT);
    assert(z.appliquerPreset("Boost"));
    const auto ecritures = Preferences::ecritures;
    assert(!z.appliquerPreset("invalide") && Preferences::ecritures == ecritures);
    z.setPilotageActif(false);
    assert(!z.appliquerPreset("Réduit") && z.boostActif() && !z.appliquerConsigne(19));
    climat = lireJson(publications.at(decouverte));
    for (const char* cle : {"mode_command_topic", "temperature_command_topic", "preset_mode_command_topic",
                            "preset_modes", "preset_mode_state_topic"})
        assert(climat[cle].isNull());
}

void verifierDerogationApresRedemarrage() {
    Banc banc;
    Zone& z = banc.z1;
    initialiser(z, Zone::SATELLITE_VIRTUEL);
    auto programme = programmeReduit();
    programme.dimanche[0] = 0x0C; // Confort de 01 h à 02 h.
    z.modifierProgrammation(programme);
    assert(!z.programmationEnvoiEnAttente());
    setTime(0, 15, 0, 25, 10, 26); // Dimanche 25 octobre 2026
    assert(z.appliquerPreset("Auto") && !z.confortActif() && !z.boostActif());
    assert(publications.at("frisquet/z1/thermostat") == "auto");
    assert(z.appliquerPreset("Dérogation Confort") && z.derogationActive() && z.confortActif());
    assert(publications.at("frisquet/z1/mode") == "Dérogation Confort");
    const auto climat = lireJson(publications.at(decouverte));
    bool proposee = false;
    for (auto mode : climat["preset_modes"].as<JsonArrayConst>())
        if (mode == "Dérogation Confort") proposee = true;
    assert(proposee);
    heureChaudiere += 15 * 60;
    z.loop();
    assert(z.derogationActive());

    Zone restauree(ID_ZONE_1, banc.mqtt);
    restauree.setSource(Zone::SATELLITE_VIRTUEL);
    restauree.begin();
    restauree.setPilotageActif(true);
    heureChaudiere += 30 * 60 - 1;
    restauree.loop();
    assert(restauree.derogationActive());
    ++heureChaudiere;
    restauree.loop();
    assert(!restauree.derogationActive() && restauree.confortActif());
    assert(restauree.appliquerPreset("Dérogation Réduit") && !restauree.confortActif());
    assert(restauree.getTemperatureConsigne() == 17);
    assert(restauree.appliquerPreset("Auto") && !restauree.derogationActive() && restauree.confortActif());
    heureChaudiere += 3600;
    restauree.loop();
    assert(restauree.reduitActif() && restauree.getTemperatureConsigne() == 17);
    assert(restauree.appliquerPreset("Dérogation Confort"));
    heureConnue = false;
    heureChaudiere += 7200;
    restauree.loop();
    assert(restauree.derogationActive() && !restauree.appliquerPreset("Auto"));
    heureConnue = true;
    restauree.loop();
    restauree.setProgrammation(programmeReduit(), true);
    assert(restauree.appliquerPreset("Auto") && !restauree.appliquerPreset("Dérogation Confort"));
}

void verifierPilotagePhysiqueEtPortail() {
    Banc banc;
    Zone& physique = banc.z1;
    initialiser(physique, Zone::SATELLITE_PHYSIQUE);
    physique.setPilotageActif(false);
    assert(!physique.appliquerPreset("Boost"));
    physique.confirmerMode(Zone::AUTO, 6);
    assert(publications.at("frisquet/z1/mode") == "Dérogation Réduit");
    physique.setPilotageActif(true);
    assert(!physique.appliquerPreset("Auto"));
    assert(physique.appliquerPreset("Boost"));
    assert(publications.at("frisquet/z1/mode") == "Dérogation Réduit");
    physique.confirmerMode();
    assert(publications.at("frisquet/z1/mode") == "Boost");

    auto raison = [&](Zone& zone, bool active) {
        return programmationReadOnlyReason(banc.config, banc.connect, zone, active);
    };
    banc.z2.setSource(Zone::SATELLITE_VIRTUEL);
    assert(raison(banc.z2, true).isEmpty() && !raison(banc.z2, false).isEmpty());
    assert(!raison(physique, true).isEmpty());
    initialiser(banc.z3, Zone::CONNECT);
    banc.config.useConnect(true);
    banc.config.useConnectPassive(true);
    assert(!raison(banc.z3, true).isEmpty());
    banc.config.useConnectPassive(false);
    assert(!raison(banc.z3, true).isEmpty());
    banc.connect.setIdAssociation(0xac);
    assert(!raison(physique, true).isEmpty() && !raison(banc.z3, true).isEmpty());
    banc.z3.setMode(Zone::CONFORT);
    assert(raison(banc.z3, true).isEmpty());
}
} // namespace

int main() {
    verifierCreneaux();
    verifierOptions();
    verifierModesLocaux();
    verifierDerogationApresRedemarrage();
    verifierPilotagePhysiqueEtPortail();
}
