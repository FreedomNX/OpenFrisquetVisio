#include "materiel.h"
#include "ProgrammationPortail.h"
#include "Frisquet/ZoneProgram.h"
#include "Frisquet/Satellite.h"
#include <cassert>
#include <fstream>
#include <sstream>

namespace {
const char* const decouverte = "homeassistant/climate/openFrisquetVisio/thermostatZ1/config";
using Trame = std::vector<byte>;

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

    uint32_t origine = 0;

    Banc() {
        Preferences::effacer();
        publications.clear();
        nombrePublications.clear();
        courtierDisponible = true;
        sujetRefuse.clear();
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

void verifierStatutChaudiereConnect() {
    for (byte boiler : {0x80, 0x84}) {
        Banc banc;
        banc.config.useConnect(true);
        banc.config.boilerId(boiler);
        banc.connect.setIdAssociation(0xAC);
        initialiser(banc.z1, Zone::CONNECT);
        initialiser(banc.z2, Zone::CONNECT);
        initialiser(banc.z3, Zone::CONNECT);
        banc.chaudiere.begin();
        banc.chaudiere.publishMqtt();
        const char* topic = "frisquet/chaudiere/anomalieChaudiere";
        assert(publications.at(topic) == "None");
        auto collecter = [&](unsigned objectif, byte etat) {
            size_t traite = emissions.size();
            for (unsigned tour = 0; tour < 2000; ++tour) {
                banc.connect.pollReads();
                banc.connect.loop();
                if (emissions.size() > traite) {
                    const auto& tx = emissions.back();
                    assert(tx.size() == 10 && tx[0] == boiler && tx[1] == 0x7E && tx[5] == 3);
                    const uint16_t adresse = (tx[6] << 8) | tx[7];
                    const unsigned octets = 2 * ((tx[8] << 8) | tx[9]);
                    reception.assign(7 + octets, 0);
                    reception[0] = tx[1]; reception[1] = tx[0]; reception[2] = tx[2];
                    reception[3] = tx[3]; reception[4] = 0x81; reception[5] = 3; reception[6] = octets;
                    if (adresse == 0x7541) {reception[7] = 0x19; reception[8] = 2; reception[10] = 0x11;}
                    if (adresse == 0xA02E) {assert(octets == 2); reception[7] = etat; reception[8] = 6;}
                    if (adresse == 0xA0FC) reception[8] = 8;
                    traite = emissions.size();
                }
                if (emissions.size() == objectif && reception.empty() && !banc.radio.busy()) return;
                tempsMs += 10;
            }
            assert(false && "collecte Connect non terminée");
        };
        collecter(7, 0x29);
        assert(banc.chaudiere.etatChaudiereConnu() && publications.at(topic) == "ON");
        setTime(0, 3, 0, 21, 11, 26);
        collecter(9, 0x28);
        assert(publications.at(topic) == "OFF");

        Banc passif;
        passif.config.useConnect(true); passif.config.useConnectPassive(true);
        passif.config.boilerId(boiler); passif.connect.setIdAssociation(0xAC);
        passif.chaudiere.begin();
        passif.connect.loop(); passif.connect.pollReads();
        assert(emissions.empty() && !passif.radio.busy());
        Trame requete = {boiler, 0x7E, 0xAC, 0x51, 1, 3, 0xA0, 0x2E, 0, 1};
        reception = {0x7E, boiler, 0xAC, 0x51, 0x81, 3, 2, 0x29, 6};
        assert(passif.connect.onReceive(requete.data(), requete.size()));
        assert(publications.at(topic) == "ON" && emissions.empty());
    }
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

Trame statut(byte zone = ID_ZONE_1) {
    // Statut A029 du 20 novembre. L’adresse d’écriture dépend de la zone.
    return {0x80,zone,0x12,0x14,0x01,0x17,0xA0,0x29,0x00,0x15,0xA0,
            static_cast<byte>(0x2F + 5 * (zone - ID_ZONE_1)),0x00,0x04,0x08,0x00,0xC9,0x00,0xBE,0x00,0x21,0x02,0x28};
}

void observer(Banc& banc, const Trame& trame) {
    banc.connect.observerSatellite(trame.data(), trame.size(), tempsMs);
}

void avancer(Banc& banc, uint32_t depuisStatut) {
    tempsMs = banc.origine + depuisStatut;
    banc.z1.actualiserLivraison();
    banc.z2.actualiserLivraison();
    banc.z3.actualiserLivraison();
    banc.connect.envoiZones();
}

void preparerConnect(Banc& banc) {
    banc.config.useConnect(true);
    banc.config.useSatelliteZ1(true);
    banc.connect.setIdAssociation(0xac);
    initialiser(banc.z1, Zone::CONNECT);
    banc.z1.setMode(Zone::CONFORT);
    banc.z1.confirmerMode();
    banc.z1.setTemperatureConsigne(20);
    banc.z1.setProgrammation(programmeReduit(), true);
    banc.origine = tempsMs;
    observer(banc, statut());
}

Trame envoyerEtPreparerAccuse(Banc& banc, byte sequenceCapture = 0) {
    const size_t avant = emissions.size();
    for(unsigned i = 0; i < 1800 && emissions.size() == avant; ++i) {
        const uint32_t phase = tempsMs - banc.origine;
        if(phase % 600000 < 100) observer(banc, statut());
        avancer(banc, phase + 100);
    }
    assert(emissions.size() == avant + 1);
    const Trame& tx = emissions.back();
    const byte sequence = sequenceCapture ? sequenceCapture : static_cast<byte>(avant + 1);
    const Trame entete = {0x80, 0x7e, banc.connect.getIdAssociation(), sequence, 8, 0x17};
    assert(tx.size() == 63 && std::equal(entete.begin(), entete.end(), tx.begin()));
    Trame accuse = {0x7e, 0x80, banc.connect.getIdAssociation(), sequence, 0x88, 0x17};
    accuse.insert(accuse.end(), tx.begin() + 14, tx.end());
    return accuse;
}

void verifierAccuses() {
    Banc banc;
    preparerConnect(banc);
    Zone& z = banc.z1;
    for (const char* preset : {"Réduit", "Auto", "Dérogation Confort", "Boost", "Hors Gel", "Confort"}) {
        const auto precedent = z.getMode();
        assert(z.appliquerPreset(preset) && z.enAttente() && z.getMode() == precedent);
        auto accuse = envoyerEtPreparerAccuse(banc);
        assert(banc.connect.onReceive(accuse.data(), accuse.size()));
        assert(!z.enAttente() && publications.at("frisquet/z1/mode") == preset);
    }
    assert(z.appliquerConsigne(21) && z.getTemperatureConsigne() == 20);
    auto accuse = envoyerEtPreparerAccuse(banc);
    auto relais = accuse;
    relais[0] = 8;
    relais[4] = 0x7e;
    assert(!banc.connect.onReceive(relais.data(), relais.size()));
    assert(!banc.connect.onReceive(accuse.data(), accuse.size() - 1));
    auto errone = accuse;
    ++errone[7]; // Température confort
    assert(!banc.connect.onReceive(errone.data(), errone.size()));
    errone = accuse;
    ++errone[3]; // Identifiant d'une commande jamais envoyée
    assert(!banc.connect.onReceive(errone.data(), errone.size()));

    assert(z.appliquerConsigne(22));
    assert(!banc.connect.onReceive(accuse.data(), accuse.size()));
    assert(z.getTemperatureConfort() == 20 && z.demande().confort == 22);
    auto premierAccuse = envoyerEtPreparerAccuse(banc);
    envoyerEtPreparerAccuse(banc);
    assert(banc.connect.onReceive(premierAccuse.data(), premierAccuse.size()));
    assert(z.getTemperatureConfort() == 22 && z.getTemperatureConsigne() == 22);
}

Trame lireTrame(std::istream& capture) {
    std::string ligne;
    assert(std::getline(capture, ligne));
    std::istringstream octets(ligne);
    Trame trame;
    unsigned valeur;
    while (octets >> std::hex >> valeur) {
        assert(valeur <= 255);
        trame.push_back(static_cast<byte>(valeur));
    }
    assert(octets.eof());
    return trame;
}

void verifierCaptures(const char* chemin) {
    // Passerelle Z1, le 18 novembre 2026 ; accusés à 10:57:01 et 11:07:00
    // sur l’horloge chaudière. Le premier thermostat était déjà en Auto.
    std::ifstream capture(chemin);
    for (bool automatique : {true, false}) {
        const auto tx = lireTrame(capture);
        auto accuse = lireTrame(capture);
        assert(tx.size() == 63 && accuse.size() == 55);
        Banc banc;
        preparerConnect(banc);
        banc.connect.setIdAssociation(0x5f);
        auto& z = banc.z1;
        Zone::Programmation programme;
        std::memcpy(&programme, tx.data() + 21, sizeof(programme));
        z.setProgrammation(programme, true);
        z.setTemperatureReduit(15);
        z.setTemperatureHorsGel(12);
        z.setMode(automatique ? Zone::AUTO : Zone::CONFORT);
        z.setModeOptions(automatique ? 0x25 : 0x01);
        assert(z.appliquerConsigne(automatique ? 19.5f : 19.0f));
        banc.connect.setIdMessage(tx[3] - 1);
        envoyerEtPreparerAccuse(banc, tx[3]);
        assert(!emissions.empty() && emissions.back() == tx);
        assert(banc.connect.onReceive(accuse.data(), accuse.size()));
        assert(!z.enAttente() && z.getTemperatureConsigne() == (automatique ? 19.5f : 19.0f));
    }
}

void verifierRelectures() {
    for (bool physique : {false, true}) {
        Banc banc;
        preparerConnect(banc);
        Satellite satellite(banc.radio, banc.config, banc.mqtt, banc.z1, banc.chaudiere);
        satellite.setIdAssociation(0x62);
        auto& z = banc.z1;
        assert(z.appliquerConsigne(21));
        envoyerEtPreparerAccuse(banc);
        auto ecriture = emissions.back();
        ecriture[0] = physique ? 0x80 : 0x7e;
        ecriture[1] = physique ? 8 : 0x80;
        ecriture[2] = physique ? 0x62 : 0xac;
        ecriture[4] = physique ? 1 : 8;
        auto recevoir = [&]() {
            return physique ? satellite.onReceive(ecriture.data(), ecriture.size())
                            : banc.connect.onReceive(ecriture.data(), ecriture.size());
        };
        ecriture[2] ^= 1;
        recevoir();
        assert(z.enAttente());
        ecriture[2] ^= 1;
        --ecriture[15];
        recevoir();
        assert(z.enAttente());
        ++ecriture[15];
        assert(recevoir() && !z.enAttente() && z.getTemperatureConsigne() == 21);
    }
}

void verifierConsignePendantObservation() {
    Banc banc;
    preparerConnect(banc);
    Satellite satellite(banc.radio, banc.config, banc.mqtt, banc.z1, banc.chaudiere);
    satellite.begin(false);
    Trame demande = {0x80,0x08,0xB0,0x10,0x01,0x17,0xA0,0x33,0x00,0x01,
                     0xA0,0x2F,0x00,0x04,0x08,0x00,0xE1,0x00,0xD7,0x00,0x01,0x00,0xD2};
    assert(satellite.onReceive(demande.data(), demande.size()));
    assert(banc.z1.appliquerConsigne(23.5f));
    reception = {0x08,0x80,0xB0,0x10,0x81,0x17,0x02,0x00,0xC6};
    tempsMs += 300;
    banc.radio.pollObservation();
    satellite.pollObservation();
    assert(banc.z1.getTemperatureConsigne() == 21.5f);
    assert(banc.z1.enAttente() && banc.z1.demande().consigne == 23.5f);
}

void verifierAttenteProgramme() {
    Banc banc;
    initialiser(banc.z1, Zone::CONNECT);
    banc.connect.setIdAssociation(0xac);
    banc.config.useSatelliteZ1(true);
    banc.z1.setMode(Zone::CONFORT);
    assert(banc.z1.appliquerConsigne(21));
    const String motif = "[CONNECT] Zone 1 en attente d'une programmation connue (radio ou mémoire).";
    banc.origine = tempsMs;
    observer(banc, statut());
    for (unsigned i = 0; i < 5; ++i) avancer(banc, 120000 + i);
    assert(emissions.empty() && std::count(journal.begin(), journal.end(), motif) == 1);
    banc.z1.setProgrammation(programmeReduit(), true);
    banc.connect.envoiZones();
    assert(emissions.size() == 1);
}

void verifierRepriseConnect() {
    Banc banc;
    banc.mqtt.begin(MqttManager::Options{});
    preparerConnect(banc);
    assert(banc.z1.appliquerConsigne(21));
    Zone restauree(ID_ZONE_1, banc.mqtt);
    restauree.setSource(Zone::CONNECT);
    restauree.setPilotageActif(true);
    restauree.begin();
    assert(restauree.nonConfirmee() && !restauree.enAttente());
    assert(restauree.getTemperatureConsigne() == 20 && restauree.demande().consigne == 21);
    const auto bouton = lireJson(publications.at("homeassistant/button/openFrisquetVisio/reessayerCommandeZ1/config"));
    assert(bouton["availability_mode"] == "all");
    assert(bouton["availability"][0]["topic"] == banc.appareil.availabilityTopic.full);
    assert(bouton["availability"][1]["topic"] == "frisquet/z1/commande");
    assert(strstr(bouton["availability"][1]["value_template"].as<const char*>(), "Modification non confirmée"));

    const auto capteur = lireJson(publications.at("homeassistant/sensor/openFrisquetVisio/suiviCommandeZ1/config"));
    assert(capteur["state_topic"] == "frisquet/z1/commande");
    assert(capteur["json_attributes_topic"] == "frisquet/z1/commande");
    restauree.publishMqtt();
    auto suivi = lireJson(publications.at("frisquet/z1/commande"));
    assert(suivi["etat"] == "Modification non confirmée");
    assert(suivi["valeur_confirmee"] == 20 && suivi["valeur_demandee"] == 21);

    char sujet[] = "frisquet/z1/commande/reessayer";
    byte pression[] = {'P', 'R', 'E', 'S', 'S'};
    assert(bouton["command_topic"] == sujet);
    receptionMqtt(sujet, pression, sizeof(pression));
    suivi = lireJson(publications.at("frisquet/z1/commande"));
    assert(restauree.enAttente() && suivi["etat"] == "En attente de confirmation");
    assert(restauree.getTemperatureConsigne() == 20);

    // Ancien format de la zone 1 : « oneCfg » est le nom historique en NVS.
    Preferences ancien;
    ancien.begin("oneCfg", false);
    ancien.remove("demandeV1");
    ancien.putBool("progPending", true);
    ancien.end();
    Zone migree(ID_ZONE_1, banc.mqtt);
    migree.setSource(Zone::CONNECT);
    migree.loadConfig();
    assert(migree.programmationEnvoiEnAttente() && !migree.programmationRecue());
    Zone apresMigration(ID_ZONE_1, banc.mqtt);
    apresMigration.setSource(Zone::CONNECT);
    apresMigration.loadConfig();
    assert(apresMigration.nonConfirmee() && !apresMigration.enAttente());
    assert(apresMigration.demande().programmeConnu);
    auto programme = programmeReduit();
    programme.dimanche[0] = 0x55;
    banc.z1.modifierProgrammation(programme);
    avancer(banc, 603000);
    avancer(banc, 1213000);
    assert(banc.z1.programmationNonConfirmee() && banc.z1.programmeEditeConnu());
    assert(banc.z1.programmeEdite().dimanche[0] == 0x55 && banc.z1.getProgrammation().dimanche[0] != 0x55);
    assert(std::memcmp(&migree.programmeEdite(), &banc.z1.getProgrammation(), sizeof(Zone::Programmation)) == 0);
}

void verifierConsigneApresEchecProgramme() {
    for(bool redemarrage : {false, true}) {
        Banc banc;
        preparerConnect(banc);
        auto semaine = programmeReduit();
        semaine.dimanche[0] = 0x55;
        semaine.samedi[5] = 0xA7;
        banc.z1.modifierProgrammation(semaine);
        assert(banc.z1.appliquerConsigne(24));
        avancer(banc, 603000);
        avancer(banc, 1213000);
        assert(banc.z1.programmationNonConfirmee());

        Zone restauree(ID_ZONE_1, banc.mqtt);
        if(redemarrage) {
            restauree.setSource(Zone::CONNECT);
            restauree.begin();
            restauree.setPilotageActif(true);
        }
        Zone& zone = redemarrage ? restauree : banc.z1;
        assert(zone.programmationNonConfirmee() && zone.appliquerConsigne(21));
        assert(zone.programmationEnvoiEnAttente());
        assert(std::memcmp(&zone.programmeEdite(), &semaine, sizeof(semaine)) == 0);
        assert(zone.getProgrammation().dimanche[0] == 0 && zone.getTemperatureConfort() == 20);

        // Le bloc A154 du réessai associe la dernière consigne à toute la semaine saisie.
        tempsMs += 611000;
        zone.actualiserLivraison();
        assert(zone.programmationNonConfirmee() && zone.reessayer());
        const auto bloc = zone.blocDemande();
        assert(bloc.confort.toFloat() == 21 && std::memcmp(&bloc.programme, &semaine, sizeof(semaine)) == 0);
        assert(zone.reconnaitreReglages(reinterpret_cast<const byte*>(&bloc), sizeof(bloc)));
        assert(zone.getTemperatureConfort() == 21 && !zone.programmationNonConfirmee());
        assert(std::memcmp(&zone.getProgrammation(), &semaine, sizeof(semaine)) == 0);
    }
}

void verifierPartageRadio() {
    Banc banc;
    preparerConnect(banc);
    banc.config.useZone2(true);
    initialiser(banc.z2, Zone::CONNECT);
    banc.z2.setMode(Zone::CONFORT);
    banc.z2.setProgrammation(programmeReduit(), true);
    assert(banc.z1.appliquerConsigne(21) && banc.z2.appliquerConsigne(22));
    // Deux repères espacés de 24,3 s, même si l’horloge chaudière est corrigée.
    avancer(banc, 24300);
    observer(banc, statut(ID_ZONE_2));
    setTime(0, 0, 30, 25, 10, 26);
    avancer(banc, 119999);
    assert(emissions.empty());
    avancer(banc, 120000);
    avancer(banc, 121000);
    avancer(banc, 122000);
    assert(emissions.size() == 3 && emissions.back()[4] == ID_ZONE_1);
    avancer(banc, 144299);
    assert(emissions.size() == 3);
    avancer(banc, 144300);
    assert(emissions.size() == 4 && emissions.back()[4] == ID_ZONE_2);
    banc.radio.suspend(FrisquetRadio::Pause::HttpOta, true);
    avancer(banc, 145300);
    banc.radio.suspend(FrisquetRadio::Pause::HttpOta, false);
    avancer(banc, 146300);
    assert(emissions.size() == 4);
    banc.z1.setTemperatureReduit(18);
    assert(banc.z1.demande().reduit == 18);
}

void verifierCinqFenetres() {
    Banc banc;
    // Le compteur monotone reboucle entre le statut initial et la première fenêtre.
    tempsMs = 0xffff0000;
    preparerConnect(banc);
    assert(banc.z1.appliquerConsigne(21));
    avancer(banc, 4000);
    observer(banc, statut()); // Répétition après une absence de réponse.
    avancer(banc, 50000);
    observer(banc, statut()); // Statut hors cycle.
    for(uint32_t debut : {120000U, 264000U, 408000U, 552000U, 720200U}) {
        if(debut == 720200) {
            avancer(banc, 600200);
            observer(banc, statut());
        }
        const size_t avant = emissions.size();
        avancer(banc, debut - 1);
        assert(emissions.size() == avant);
        for(uint32_t delta : {0U, 100U, 999U, 1000U, 2000U, 3000U, 4999U})
            avancer(banc, debut + delta);
        assert(emissions.size() == avant + 3 && banc.z1.enAttente());
        avancer(banc, debut + 5000);
    }
    assert(emissions.size() == 15 && banc.z1.nonConfirmee());
    assert(banc.z1.reessayer() && banc.z1.enAttente());
}

void verifierStatutManquantAuChangementDeCycle() {
    Banc banc;
    preparerConnect(banc);
    avancer(banc, 560000);
    assert(banc.z1.appliquerConsigne(21));
    auto suivi = lireJson(banc.z1.suiviJson());
    assert(suivi["tentative"] == 1 && suivi["restant_ms"] == 165000);
    avancer(banc, 603000); // Le statut attendu n’est pas arrivé.
    assert(lireJson(banc.z1.suiviJson())["synchronisation"] == true);
    avancer(banc, 720000);
    assert(emissions.empty());
    avancer(banc, 1200000);
    observer(banc, statut());
    assert(banc.z1.tentative() == 1);
    avancer(banc, 1320000);
    assert(emissions.size() == 1);
}

void verifierFenetresSimultanees() {
    Banc banc;
    preparerConnect(banc);
    banc.config.useZone2(true);
    initialiser(banc.z2, Zone::CONNECT);
    banc.z2.setMode(Zone::CONFORT);
    banc.z2.setProgrammation(programmeReduit(), true);
    banc.connect.observerSatellite(statut(ID_ZONE_2).data(), 23, banc.origine);
    assert(banc.z1.appliquerConsigne(21) && banc.z2.appliquerConsigne(22));
    for(uint32_t delta : {0U, 200U, 1000U, 1100U, 2000U, 2300U})
        avancer(banc, 120000 + delta);
    assert(emissions.size() == 3);
    assert(emissions[0][4] == ID_ZONE_1 && emissions[1][4] == ID_ZONE_2 && emissions[2][4] == ID_ZONE_1);
}

void verifierAccuseIntermediaire() {
    Banc banc;
    preparerConnect(banc);
    assert(banc.z1.appliquerConsigne(21));
    avancer(banc, 120000);
    auto& tx = emissions.at(0);
    Trame accuse = {0x80, 8, 0x12, tx[3], 0xFE, 0x17};
    accuse.insert(accuse.end(), tx.begin() + 14, tx.end());
    auto incorrect = accuse;
    ++incorrect[3];
    observer(banc, incorrect);
    incorrect = accuse;
    ++incorrect[7];
    observer(banc, incorrect);
    incorrect = accuse;
    ++incorrect[2];
    observer(banc, incorrect);
    avancer(banc, 121000);
    assert(emissions.size() == 2);
    observer(banc, accuse);
    avancer(banc, 122000);
    assert(emissions.size() == 2 && banc.z1.enAttente() && banc.z1.getTemperatureConfort() == 20);
    avancer(banc, 125000);
    avancer(banc, 264000);
    observer(banc, accuse); // Ancien message, ne coupe pas la nouvelle fenêtre.
    avancer(banc, 265000);
    assert(emissions.size() == 4);
    accuse[0] = 0x7e;
    accuse[1] = 0x80;
    accuse[2] = 0xac;
    accuse[4] = 0x88;
    assert(banc.connect.onReceive(accuse.data(), accuse.size()) && !banc.z1.enAttente());
    avancer(banc, 266000);
    assert(emissions.size() == 4 && banc.z1.getTemperatureConfort() == 21);
}

void verifierSynchronisation() {
    Banc banc;
    preparerConnect(banc);
    banc.config.useZone3(true);
    initialiser(banc.z3, Zone::CONNECT);
    banc.z3.setMode(Zone::CONFORT);
    banc.z3.setProgrammation(programmeReduit(), true);
    assert(banc.z3.appliquerConsigne(21));
    auto repere = statut(ID_ZONE_3);
    for(unsigned octet : {0U, 1U, 4U, 5U, 7U, 9U, 11U, 13U, 14U}) {
        auto incorrect = repere;
        ++incorrect[octet];
        observer(banc, incorrect);
    }
    banc.connect.observerSatellite(repere.data(), repere.size()-1, tempsMs);
    avancer(banc, 120000);
    assert(emissions.empty() && lireJson(banc.z3.suiviJson())["synchronisation"] == true);
    avancer(banc, 610000);
    assert(banc.z3.nonConfirmee() && banc.z3.reessayer());
    repere[7] = repere[11] + 4;
    repere[9] = 1;
    observer(banc, repere);
    avancer(banc, 730500); // Premier départ manqué, aucun rattrapage.
    assert(emissions.empty());
    avancer(banc, 731000);
    assert(emissions.size() == 1 && emissions.back()[4] == ID_ZONE_3);
}

void verifierRepriseConsommation() {
    Banc banc;
    auto& chaudiere = banc.chaudiere;
    chaudiere.begin();
    const String chauffage = "frisquet/chaudiere/consommationChauffage";
    const String ecs = "frisquet/chaudiere/consommationECS";
    // Conserver les nombres bruts et les cumuls annuels, même sans date.
    const String annuelChauffage = "frisquet/chaudiere/consommationAnnuelleChauffage";
    const String annuelEcs = "frisquet/chaudiere/consommationAnnuelleECS";
    courtierDisponible = false;
    chaudiere.setConsommationAnnuelle(40000, 7);
    chaudiere.setConsommationVeille(17, 0, 0);
    chaudiere.publishMqtt();
    assert(!publications.count(chauffage) && !publications.count(annuelChauffage));
    courtierDisponible = true;
    chaudiere.publishMqtt();
    chaudiere.publishMqtt();
    assert(publications.at(chauffage) == "17.00" && publications.at(ecs) == "0.00");
    assert(publications.at(annuelChauffage) == "40000" && publications.at(annuelEcs) == "7");
    for (const auto& sujet : {chauffage, ecs, annuelChauffage, annuelEcs})
        assert(nombrePublications.at(sujet) == 1);

    courtierDisponible = false;
    chaudiere.setConsommationVeille(18, 0, 0);
    chaudiere.publishMqtt();
    courtierDisponible = true;
    chaudiere.publishMqtt();
    chaudiere.publishMqtt();
    assert(publications.at(chauffage) == "18.00" && nombrePublications.at(chauffage) == 2);
    assert(nombrePublications.at(ecs) == 1);
}

void verifierStatistiquesConsommation() {
    Banc banc;
    auto& chaudiere = banc.chaudiere;
    chaudiere.begin();
    const String chauffage = "frisquet/chaudiere/consommationChauffage";
    const String ecs = "frisquet/chaudiere/consommationECS";
    const String releve = chauffage + "/releve", releveEcs = ecs + "/releve";
    for (const char* id : {"consommationChauffage", "consommationECS"}) {
        const auto config = lireJson(publications.at(String("homeassistant/sensor/openFrisquetVisio/") + id + "/config"));
        const String sujet = String("frisquet/chaudiere/") + id + "/releve";
        assert(config["state_topic"] == sujet.c_str() && config["json_attributes_topic"] == sujet.c_str());
        assert(config["state_class"] == "total" && config["unit_of_measurement"] == "kWh");
        assert(config["value_template"] == "{{ value_json.kwh }}");
        assert(config["last_reset_value_template"] ==
            "{{ as_datetime(value_json.date_consommation_estimee).replace(tzinfo=now().tzinfo).isoformat() }}");
        assert(config["json_attributes_template"] ==
            "{{ {'date_consommation_estimee': value_json.date_consommation_estimee} | tojson }}");
    }
    auto lire = [&](int16_t ch, int16_t eau, uint32_t jour) {
        chaudiere.setConsommationVeille(ch, eau, jour);
        chaudiere.publishMqtt();
    };
    auto verifier = [&](const String& sujet, const char* date, int valeur, unsigned nombre) {
        const auto doc = lireJson(publications.at(sujet));
        assert(doc["date_consommation_estimee"] == date && doc["kwh"].is<int>() && doc["kwh"] == valeur);
        assert(nombrePublications.at(sujet) == nombre);
    };
    lire(17, 0, 0);
    assert(!publications.count(releve) && !publications.count(releveEcs));

    lire(17, 0, 20818);
    verifier(releve, "2026-12-31", 17, 1);
    sujetRefuse = releve;
    lire(17, 0, 20819);
    verifier(releve, "2026-12-31", 17, 1);
    verifier(releveEcs, "2027-01-01", 0, 2);
    sujetRefuse.clear();
    chaudiere.publishMqtt();
    verifier(releve, "2027-01-01", 17, 2);
    assert(nombrePublications.at(chauffage) == 1);
    assert(nombrePublications.at(ecs) == 1);

    // Une correction dans le même cycle et deux publications indépendantes.
    sujetRefuse = chauffage;
    lire(18, 0, 20819);
    verifier(releve, "2027-01-01", 18, 3);
    assert(publications.at(chauffage) == "17.00");
    sujetRefuse.clear();
    chaudiere.publishMqtt();
    assert(publications.at(chauffage) == "18.00");
    lire(19, -1, 0);
    lire(20, 0, 20818);
    lire(-1, 0, 20820);
    verifier(releve, "2027-01-01", 18, 3);
    verifier(releveEcs, "2027-01-02", 0, 3);

    // Une reconnexion publie le dernier relevé valide encore en attente.
    courtierDisponible = false;
    lire(22, 3, 20821);
    lire(-1, 77, 0);
    courtierDisponible = true;
    chaudiere.publishMqtt();
    chaudiere.publishMqtt();
    verifier(releve, "2027-01-03", 22, 4);
    verifier(releveEcs, "2027-01-03", 3, 4);
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

int main(int argc, char** argv) {
    verifierStatutChaudiereConnect();
    assert(argc == 2);
    verifierCaptures(argv[1]);
    verifierRelectures();
    verifierConsignePendantObservation();
    verifierAttenteProgramme();
    verifierRepriseConnect();
    verifierConsigneApresEchecProgramme();
    verifierCreneaux();
    verifierOptions();
    verifierModesLocaux();
    verifierDerogationApresRedemarrage();
    verifierAccuses();
    verifierPartageRadio();
    verifierCinqFenetres();
    verifierAccuseIntermediaire();
    verifierStatutManquantAuChangementDeCycle();
    verifierFenetresSimultanees();
    verifierSynchronisation();
    verifierRepriseConsommation();
    verifierStatistiquesConsommation();
    verifierPilotagePhysiqueEtPortail();
}
