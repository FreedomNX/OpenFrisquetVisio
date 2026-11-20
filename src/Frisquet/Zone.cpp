#include "Zone.h"
#include "ZoneProgram.h"
#include "NumberParsing.h"
#include <TimeLib.h>
#include <stdlib.h>

constexpr uint8_t Zone::MAX_FENETRES;

namespace {
// Départs choisis autour des quatre pics mesurés sur Z1.
// Les mêmes délais après le statut propre à Z2 ou Z3 restent à confirmer.
constexpr uint32_t ouvertures[] = {120000, 264000, 408000, 552000};
constexpr uint32_t cycleStatut = 600000, toleranceStatut = 2500;
constexpr uint32_t espacement = 1000, margeEmission = 400;
constexpr uint32_t finEmissions = 2 * espacement + margeEmission;
constexpr uint32_t finConfirmation = 5000, attenteStatut = 610000;

String nomPreset(Zone::MODE_ZONE mode, byte options) {
    if(mode == Zone::MODE_ZONE::AUTO) {
        if(options & 0x02) return (options & 0x01) ? "Dérogation Confort" : "Dérogation Réduit";
        return "Auto";
    }
    if(mode == Zone::MODE_ZONE::CONFORT) return (options & 0x40) ? "Boost" : "Confort";
    if(mode == Zone::MODE_ZONE::REDUIT) return "Réduit";
    if(mode == Zone::MODE_ZONE::HORS_GEL) return "Hors Gel";
    return "None";
}
String nomRegime(Zone::MODE_ZONE mode, byte options) {
    if(mode == Zone::MODE_ZONE::HORS_GEL) return "Hors Gel";
    if(mode == Zone::MODE_ZONE::CONFORT || (mode == Zone::MODE_ZONE::AUTO && (options & 1))) return "Confort";
    if(mode == Zone::MODE_ZONE::REDUIT || mode == Zone::MODE_ZONE::AUTO) return "Réduit";
    return "None";
}
}

void Zone::loadConfig() {
    bool migrerProgramme = false;

    _preferences.begin("zoneCfg" + getNumeroZone(), false);

    setMode((Zone::MODE_ZONE)_preferences.getUChar("mode", (uint8_t)Zone::MODE_ZONE::INCONNU));
    setModeOptions(_preferences.getUChar("modeOpts"));
    setTemperatureConfort(_preferences.getFloat("tempConfort"));
    setTemperatureReduit(_preferences.getFloat("tempReduit"));
    setTemperatureHorsGel(_preferences.getFloat("tempHorsGel"));
    setTemperatureBoost(_preferences.getFloat("tempBoost", 2));
    setTemperatureAmbiante(_preferences.getFloat("tempAmbiante", NAN));
    setTemperatureConsigne(_preferences.getFloat("tempConsigne", NAN));

    Zone::Programmation programmation;
    const size_t tailleProgrammation = _preferences.getBytes("prog", &programmation, sizeof(programmation));
    _reglages.programmeConnu = false;
    setProgrammation(programmation,
        tailleProgrammation == sizeof(programmation) && _preferences.getBool("progValid", false));
    if(_source == CONNECT && _preferences.getBytesLength("demandeV1") == sizeof(_demande)
        && _preferences.getBytes("demandeV1", &_demande, sizeof(_demande)) == sizeof(_demande)
        && _demande.objet > Objet::Aucun && _demande.objet <= Objet::Programme) {
        // Après une coupure, le résultat radio est inconnu. L'utilisateur peut relancer.
        _livraison = Livraison::Echec;
    } else if(_source == CONNECT && programmationRecue() && _preferences.getBool("progPending", false)) {
        _demande.reglages = _reglages;
        _demande.objet = Objet::Programme;
        _demande.champs = ProgrammeModifie;
        _reglages.programmeConnu = false;
        demarrerCommande();
        migrerProgramme = true;
    } else {
        _demande = Demande{};
    }

    _derogationFin = _preferences.getUInt("derogFin", 0);
    _preferences.end();
    if(boostActif()) actualiserConsigne();
    if(migrerProgramme) saveConfig();
}

void Zone::saveConfig() {   
    _preferences.begin("zoneCfg" + getNumeroZone(), false);

    _preferences.putUChar("mode", getMode());
    _preferences.putUChar("modeOpts", getModeOptions());
    _preferences.putFloat("tempConfort", getTemperatureConfort());
    _preferences.putFloat("tempReduit", getTemperatureReduit());
    _preferences.putFloat("tempHorsGel", getTemperatureHorsGel());
    _preferences.putFloat("tempBoost", getTemperatureBoost());
    const size_t tailleProgrammation = _preferences.putBytes("prog", &getProgrammation(), sizeof(getProgrammation()));
    _preferences.putBool("progValid", programmationRecue() && tailleProgrammation == sizeof(getProgrammation()));
    _preferences.putFloat("tempAmbiante", getTemperatureAmbiante());
    _preferences.putFloat("tempConsigne", getTemperatureConsigne());
    _preferences.putUInt("derogFin", _derogationFin);

    if(enAttente() || nonConfirmee()) {
        if(_preferences.putBytes("demandeV1", &_demande, sizeof(_demande)) == sizeof(_demande)) {
            _preferences.putBool("progPending", false);
        }
    } else {
        _preferences.putBool("progPending", false);
        if(_preferences.isKey("demandeV1")) _preferences.remove("demandeV1");
    }
    _preferences.end();
}


void Zone::begin() {
    
    loadConfig();

    // Device commun
    MqttDevice* device = mqtt().getDevice("openFrisquetVisio");

    // SELECT: Mode zone
    _mqttEntities.mode.id = "modeChauffageZ" + String(getNumeroZone());
    _mqttEntities.mode.name = "Mode Chauffage Z" + String(getNumeroZone());
    _mqttEntities.mode.component = "select";
    _mqttEntities.mode.stateTopic   = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"mode"}), 0, true);
    _mqttEntities.mode.commandTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"mode","set"}), 0, true);
    _mqttEntities.mode.set("icon", "mdi:tune-variant");
    _mqttEntities.mode.set("entity_category", "config");
    _mqttEntities.mode.setRaw("options", R"(["Auto","Dérogation Confort","Dérogation Réduit","Confort","Réduit","Hors Gel","Boost"])");
    mqtt().registerEntity(*device, _mqttEntities.mode, true);
    mqtt().onCommand(_mqttEntities.mode, [&](const String& payload){
        appliquerPreset(payload);
    });

    // SENSOR: Température ambiante
    _mqttEntities.temperatureAmbiante.id = "temperatureAmbianteZ" + String(getNumeroZone());
    _mqttEntities.temperatureAmbiante.name = "Température ambiante Z" + String(getNumeroZone());
    _mqttEntities.temperatureAmbiante.component = "sensor";
    _mqttEntities.temperatureAmbiante.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"temperatureAmbiante"}), 0, true);
    _mqttEntities.temperatureAmbiante.set("device_class", "temperature");
    _mqttEntities.temperatureAmbiante.set("state_class", "measurement");
    _mqttEntities.temperatureAmbiante.set("unit_of_measurement", "°C");
    if(getSource() == SOURCE::SATELLITE_VIRTUEL) {
        _mqttEntities.temperatureAmbiante.component = "number";
        _mqttEntities.temperatureAmbiante.set("min", "0");
        _mqttEntities.temperatureAmbiante.set("max", "50");
        _mqttEntities.temperatureAmbiante.set("mode", "box");
        _mqttEntities.temperatureAmbiante.set("step", "0.1");
        _mqttEntities.temperatureAmbiante.commandTopic = MqttTopic(MqttManager::compose({device->baseTopic, "z" + String(getNumeroZone()),"temperatureAmbiante", "set"}), 0, true);
        mqtt().onCommand(_mqttEntities.temperatureAmbiante, [&](const String& payload) {
            float temperature;
            // Mêmes bornes que l'entité découverte (0 à 50 °C) : pas de bornage plus loin.
            if(NumberParsing::finiteDecimal(payload.c_str(), temperature) && temperature >= 0 && temperature <= 50) {
                info("[ZONE Z%d] Modification de la température ambiante à %0.2f.", getNumeroZone(), temperature);
                setTemperatureAmbiante(temperature);
                mqtt().publishState(_mqttEntities.temperatureAmbiante, getTemperatureAmbiante());
                saveConfig();
            }
        });
    }
    mqtt().removeEntityDiscovery(*device, _mqttEntities.temperatureAmbiante,
        _mqttEntities.temperatureAmbiante.component == "number" ? "sensor" : "number");
    mqtt().registerEntity(*device, _mqttEntities.temperatureAmbiante, true);

    // SENSOR: Température consigne
    _mqttEntities.temperatureConsigne.id = "temperatureConsigneZ" + String(getNumeroZone());
    _mqttEntities.temperatureConsigne.name = "Consigne Z" + String(getNumeroZone());
    _mqttEntities.temperatureConsigne.component = "sensor";
    _mqttEntities.temperatureConsigne.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"temperatureConsigne"}), 0, true);
    _mqttEntities.temperatureConsigne.set("device_class", "temperature");
    _mqttEntities.temperatureConsigne.set("state_class", "measurement");
    _mqttEntities.temperatureConsigne.set("unit_of_measurement", "°C");
    _mqttEntities.temperatureConsigne.commandTopic = MqttTopic(MqttManager::compose({device->baseTopic, "z" + String(getNumeroZone()), "temperatureConsigne", "set"}), 0, false);
    mqtt().onCommand(_mqttEntities.temperatureConsigne, [this](const String& payload) {
        char* end = nullptr;
        const float temperature = strtof(payload.c_str(), &end);
        if(end != payload.c_str() && *end == '\0') appliquerConsigne(temperature);
    });
    mqtt().registerEntity(*device, _mqttEntities.temperatureConsigne, true);

    // SENSOR: Température confort
    _mqttEntities.temperatureConfort.id = "temperatureConfortZ" + String(getNumeroZone());
    _mqttEntities.temperatureConfort.name = "Température Confort Z" + String(getNumeroZone());
    _mqttEntities.temperatureConfort.component = "number";
    _mqttEntities.temperatureConfort.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"temperatureConfort"}), 0, true);
    _mqttEntities.temperatureConfort.commandTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"temperatureConfort", "set"}), 0, true);
    _mqttEntities.temperatureConfort.set("device_class", "temperature");
    _mqttEntities.temperatureConfort.set("state_class", "measurement");
    _mqttEntities.temperatureConfort.set("unit_of_measurement", "°C");
    _mqttEntities.temperatureConfort.set("min", "5");
    _mqttEntities.temperatureConfort.set("max", "30");
    _mqttEntities.temperatureConfort.set("mode", "box");
    _mqttEntities.temperatureConfort.set("step", "0.5");
    mqtt().registerEntity(*device, _mqttEntities.temperatureConfort, true);
    mqtt().onCommand(_mqttEntities.temperatureConfort, [&](const String& payload) {
        if(!pilotageDisponible()) return;
        float temperature;
        if(NumberParsing::finiteDecimal(payload.c_str(), temperature)) {
            info("[ZONE %d] Modification de la température confort à %0.2f.", getNumeroZone(), temperature);
            modifier(Objet::Confort, [&]() {
                setTemperatureConfort(temperature);
                actualiserConsigne();
                return true;
            });
        }
    });

    // SENSOR: Température réduite
    _mqttEntities.temperatureReduit.id = "temperatureReduitZ" + String(getNumeroZone());
    _mqttEntities.temperatureReduit.name = "Température Réduit Z" + String(getNumeroZone());
    _mqttEntities.temperatureReduit.component = "number";
    _mqttEntities.temperatureReduit.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"temperatureReduit"}), 0, true);
    _mqttEntities.temperatureReduit.commandTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"temperatureReduit", "set"}), 0, true);
    _mqttEntities.temperatureReduit.set("device_class", "temperature");
    _mqttEntities.temperatureReduit.set("state_class", "measurement");
    _mqttEntities.temperatureReduit.set("unit_of_measurement", "°C");
    _mqttEntities.temperatureReduit.set("min", "5");
    _mqttEntities.temperatureReduit.set("max", "30");
    _mqttEntities.temperatureReduit.set("mode", "box");
    _mqttEntities.temperatureReduit.set("step", "0.5");
    mqtt().registerEntity(*device, _mqttEntities.temperatureReduit, true);
    mqtt().onCommand(_mqttEntities.temperatureReduit, [&](const String& payload) {
        if(!pilotageDisponible()) return;
        float temperature;
        if(NumberParsing::finiteDecimal(payload.c_str(), temperature)) {
            info("[ZONE %d] Modification de la température réduit à %0.2f.", getNumeroZone(), temperature);
            modifier(Objet::Reduit, [&]() {
                setTemperatureReduit(temperature);
                actualiserConsigne();
                return true;
            });
        }
    });

    // SENSOR: Température hors-gel
    _mqttEntities.temperatureHorsGel.id = "temperatureHorsGelZ" + String(getNumeroZone());
    _mqttEntities.temperatureHorsGel.name = "Température Hors-Gel Z" + String(getNumeroZone());
    _mqttEntities.temperatureHorsGel.component = "number";
    _mqttEntities.temperatureHorsGel.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"temperatureHorsGel"}), 0, true);
    _mqttEntities.temperatureHorsGel.commandTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"temperatureHorsGel", "set"}), 0, true);
    _mqttEntities.temperatureHorsGel.set("device_class", "temperature");
    _mqttEntities.temperatureHorsGel.set("state_class", "measurement");
    _mqttEntities.temperatureHorsGel.set("unit_of_measurement", "°C");
    _mqttEntities.temperatureHorsGel.set("min", "5");
    _mqttEntities.temperatureHorsGel.set("max", "30");
    _mqttEntities.temperatureHorsGel.set("mode", "box");
    _mqttEntities.temperatureHorsGel.set("step", "0.5");
    mqtt().registerEntity(*device, _mqttEntities.temperatureHorsGel, true);
    mqtt().onCommand(_mqttEntities.temperatureHorsGel, [&](const String& payload) {
        if(!pilotageDisponible()) return;
        float temperature;
        if(NumberParsing::finiteDecimal(payload.c_str(), temperature)) {
            info("[ZONE %d] Modification de la température hors-gel à %0.2f.", getNumeroZone(), temperature);
            modifier(Objet::HorsGel, [&]() {
                setTemperatureHorsGel(temperature);
                actualiserConsigne();
                return true;
            });
        }
    });

    // SENSOR: Température départ
    _mqttEntities.temperatureDepart.id = "temperatureDepartZ" + String(getNumeroZone());
    _mqttEntities.temperatureDepart.name = "Température Départ Z" + String(getNumeroZone());
    _mqttEntities.temperatureDepart.component = "sensor";
    _mqttEntities.temperatureDepart.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"temperatureDepart"}), 0, true);
    _mqttEntities.temperatureDepart.set("device_class", "temperature");
    _mqttEntities.temperatureDepart.set("state_class", "measurement");
    _mqttEntities.temperatureDepart.set("unit_of_measurement", "°C");
    mqtt().registerEntity(*device, _mqttEntities.temperatureDepart, true);

    // SENSOR: Température consigne départ
    _mqttEntities.temperatureConsigneDepart.id = "temperatureConsigneDepartZ" + String(getNumeroZone());
    _mqttEntities.temperatureConsigneDepart.name = "Température Consigne Départ Z" + String(getNumeroZone());
    _mqttEntities.temperatureConsigneDepart.component = "sensor";
    _mqttEntities.temperatureConsigneDepart.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"temperatureConsigneDepart"}), 0, true);
    _mqttEntities.temperatureConsigneDepart.set("device_class", "temperature");
    _mqttEntities.temperatureConsigneDepart.set("state_class", "measurement");
    _mqttEntities.temperatureConsigneDepart.set("unit_of_measurement", "°C");
    mqtt().registerEntity(*device, _mqttEntities.temperatureConsigneDepart, true);

    // SENSOR: Température boost
    _mqttEntities.temperatureBoost.id = "temperatureBoostZ" + String(getNumeroZone());
    _mqttEntities.temperatureBoost.name = "Température Boost Z" + String(getNumeroZone());
    _mqttEntities.temperatureBoost.component = "number";
    _mqttEntities.temperatureBoost.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z"+ String(getNumeroZone()),"temperatureBoost"}), 0, true);
    _mqttEntities.temperatureBoost.commandTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z"+ String(getNumeroZone()),"temperatureBoost", "set"}), 0, true);
    _mqttEntities.temperatureBoost.set("device_class", "temperature");
    _mqttEntities.temperatureBoost.set("state_class", "measurement");
    _mqttEntities.temperatureBoost.set("unit_of_measurement", "°C");
    _mqttEntities.temperatureBoost.set("min", "0.5");
    _mqttEntities.temperatureBoost.set("max", "5");
    _mqttEntities.temperatureBoost.set("mode", "box");
    _mqttEntities.temperatureBoost.set("step", "0.5");
    mqtt().registerEntity(*device, _mqttEntities.temperatureBoost, true);
    mqtt().onCommand(_mqttEntities.temperatureBoost, [&](const String& payload) {
        if(!pilotageDisponible()) return;
        float temperature;
        if(NumberParsing::finiteDecimal(payload.c_str(), temperature)) {
            info("[ZONE %d] Modification de la température de boost à %0.2f.", getNumeroZone(), temperature);
            if(boostActif() || (enAttente() && (demande().options & 0x40))) {
                modifier(Objet::Boost, [&]() {
                    setTemperatureBoost(temperature);
                    actualiserConsigne();
                    return true;
                });
            } else {
                setTemperatureBoost(temperature);
                saveConfig();
                publishMqtt();
            }
        }
    });

    // SWITCH: Activation Boost
    _mqttEntities.boost.id = "boostZ" + String(getNumeroZone());
    _mqttEntities.boost.name = "Boost Z" + String(getNumeroZone());
    _mqttEntities.boost.component = "switch";
    _mqttEntities.boost.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"boost"}), 0, true);
    _mqttEntities.boost.commandTopic = MqttTopic(MqttManager::compose({device->baseTopic,"z" + String(getNumeroZone()),"boost", "set"}), 0, true);
    _mqttEntities.boost.set("icon", "mdi:tune-variant");
    mqtt().registerEntity(*device, _mqttEntities.boost, true);
    mqtt().onCommand(_mqttEntities.boost, [&](const String& payload){
        if(!pilotageDisponible()) return;
        if(payload.equalsIgnoreCase("ON")) {
            appliquerPreset("Boost");
        } else if(payload.equalsIgnoreCase("OFF")) {
            modifier(Objet::Mode, [&]() {
                desactiverBoost();
                actualiserConsigne();
                return true;
            });
        }
    });


    // THERMOSTAT
    _mqttEntities.thermostat.id = "thermostatZ" + String(getNumeroZone());
    _mqttEntities.thermostat.name = "Thermostat Z" + String(getNumeroZone());
    _mqttEntities.thermostat.component = "climate";
    _mqttEntities.thermostat.set("icon", "mdi:tune-variant");
    _mqttEntities.thermostat.setRaw("modes", R"(["auto","heat"])");
    _mqttEntities.thermostat.set("temperature_unit", "C");
    _mqttEntities.thermostat.set("precision", 0.1);
    _mqttEntities.thermostat.set("temp_step", 0.5);
    _mqttEntities.thermostat.set("min_temp", 5);
    _mqttEntities.thermostat.set("max_temp", 30);
    _mqttEntities.thermostat.stateTopic   = MqttTopic(MqttManager::compose({device->baseTopic, "z" + String(getNumeroZone()),"thermostat"}));
    _mqttEntities.thermostat.set("mode_state_topic", MqttManager::compose({device->baseTopic, "z" + String(getNumeroZone()),"thermostat"}));
    _mqttEntities.thermostat.set("preset_mode_command_topic", MqttManager::compose({device->baseTopic, "z" + String(getNumeroZone()),"mode","set"}));
    _mqttEntities.preset.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic, "z" + String(getNumeroZone()), "thermostat", "preset"}));
    _mqttEntities.thermostat.set("preset_mode_state_topic", _mqttEntities.preset.stateTopic.full);
    _mqttEntities.preset.id = "modeActifZ" + String(getNumeroZone());
    _mqttEntities.preset.name = "Mode actif Z" + String(getNumeroZone());
    _mqttEntities.preset.component = "sensor";
    mqtt().registerEntity(*device, _mqttEntities.preset, true);
    _mqttEntities.thermostat.set("current_temperature_topic", MqttManager::compose({device->baseTopic, "z" + String(getNumeroZone()),"temperatureAmbiante"}));
    _mqttEntities.thermostat.set("temperature_command_topic", MqttManager::compose({device->baseTopic, "z" + String(getNumeroZone()),"temperatureConsigne", "set"}));
    _mqttEntities.thermostat.set("temperature_state_topic", MqttManager::compose({device->baseTopic, "z" + String(getNumeroZone()),"temperatureConsigne"}));
    _mqttEntities.thermostat.setRaw("preset_modes", R"(["Auto","Dérogation Confort","Dérogation Réduit","Confort","Réduit","Hors Gel","Boost"])");
    _mqttEntities.thermostat.set("optimistic", false);
    mqtt().registerEntity(*device, _mqttEntities.thermostat, false);
    const MqttTopic hvacCommand(MqttManager::compose({device->baseTopic, "z" + String(getNumeroZone()), "thermostat", "set"}));
    mqtt().onCommand(hvacCommand, [this](const String& payload) {
        if(payload == "auto") appliquerPreset("Auto");
        else if(payload == "heat" && getMode() == MODE_ZONE::AUTO) appliquerPreset("Confort");
        else if(payload == "heat" && getMode() == MODE_ZONE::INCONNU) appliquerPreset("Confort");
    });
    _mqttEntities.regime.id = "regimeActifZ" + String(getNumeroZone());
    _mqttEntities.regime.name = "Régime actif Z" + String(getNumeroZone());
    _mqttEntities.regime.component = "sensor";
    _mqttEntities.regime.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic, "z" + String(getNumeroZone()), "regime"}));
    mqtt().registerEntity(*device, _mqttEntities.regime, true);
    _mqttPret = true;
    actualiserDecouverte();
}


void Zone::setTemperatureConfort(float temperature) {
    if(isnan(temperature)) {
        this->_reglages.confort = NAN;
        return;
    }
    temperature = round(temperature * 2) / 2.0f;
    this->_reglages.confort = std::min(30.0f, std::max(5.0f, temperature));
}
void Zone::setTemperatureReduit(float temperature) {
    if(isnan(temperature)) {
        this->_reglages.reduit = NAN;
        return;
    }
    temperature = round(temperature * 2) / 2.0f;
    this->_reglages.reduit = std::min(30.0f, std::max(5.0f, temperature));
}
void Zone::setTemperatureHorsGel(float temperature) {
    if(isnan(temperature)) {
        this->_reglages.horsGel = NAN;
        return;
    }
    temperature = round(temperature * 2) / 2.0f;
    this->_reglages.horsGel = std::min(30.0f, std::max(5.0f, temperature));
}
void Zone::setTemperatureAmbiante(float temperature) {
    if(isnan(temperature)) {
        this->_temperatureAmbiante = NAN;
        return;
    }
    this->_temperatureAmbiante = temperature;
}
void Zone::setTemperatureConsigne(float temperature) {
    if(isnan(temperature)) {
        this->_reglages.consigne = NAN;
        return;
    }
    this->_reglages.consigne = std::min(30.0f, std::max(5.0f, temperature));
}
void Zone::setTemperatureBoost(float temperature) {
    if(isnan(temperature)) {
        this->_reglages.boost = NAN;
        return;
    }
    temperature = round(temperature * 2) / 2.0f;
    //this->_reglages.boost = std::min(30.0f, std::max(5.0f, temperature));
    this->_reglages.boost = std::min(5.0f, std::max(0.5f, temperature));
}
void Zone::setTemperatureDepart(float temperature) {
    this->_temperatureDepart = temperature;
}
void Zone::setTemperatureConsigneDepart(float temperature) {
    this->_temperatureConsigneDepart = temperature;
}

float Zone::getTemperatureConfort() {
    return this->_reglages.confort;
}
float Zone::getTemperatureReduit() {
    return this->_reglages.reduit;
}
float Zone::getTemperatureHorsGel() {
    return this->_reglages.horsGel;
}
float Zone::getTemperatureConsigne() {
    return this->_reglages.consigne;
}
float Zone::getTemperatureAmbiante() {
    return this->_temperatureAmbiante;
}
float Zone::getTemperatureDepart() {
    return this->_temperatureDepart;
}
float Zone::getTemperatureConsigneDepart() {
    return this->_temperatureConsigneDepart;
}
float Zone::getTemperatureBoost() {
    return this->_reglages.boost;
}

Zone::MODE_ZONE Zone::getMode() {
    return (MODE_ZONE)this->_reglages.mode;
}

void Zone::setMode(MODE_ZONE mode, bool confort, bool derogation) {
    byte optionsMode;
    switch(mode) {
        case MODE_ZONE::AUTO:
            optionsMode = 0x04 | (confort ? 0x01 : 0x00) | (derogation ? 0x02 : 0x00);
            break;
        case MODE_ZONE::CONFORT:
            optionsMode = 0x01;
            break;
        case MODE_ZONE::REDUIT:
            optionsMode = 0x00;
            break;
        case MODE_ZONE::HORS_GEL:
            optionsMode = 0x10;
            break;
        case MODE_ZONE::INCONNU:
            _reglages.mode = mode;
            return;
        default:
            // Valeur hors plage (trame inattendue) : conserver le mode courant.
            return;
    }
    _reglages.mode = mode;

    // Même codage que Satellite::MODE pour les bits Confort, Dérogation,
    // Auto et Hors Gel. Préserver le Boost et les bits non interprétés.
    _reglages.options = (_reglages.options & ~0x17) | optionsMode;
    // Le bit Auto reçu précédemment ne doit pas survivre à un passage
    // en Confort/Réduit permanent ou en Hors Gel.
    if(mode != MODE_ZONE::AUTO) _reglages.options &= ~0x20;
}
void Zone::setMode(const String& mode, bool confort, bool derogation) {
  if (mode.equalsIgnoreCase("Auto")) {
    this->setMode(MODE_ZONE::AUTO, confort, derogation);
    this->desactiverBoost();
  } else if (mode.equalsIgnoreCase("Réduit")) {
    this->setMode(MODE_ZONE::REDUIT);
    this->desactiverBoost();
  } else if (mode.equalsIgnoreCase("Hors Gel")) {
    this->setMode(MODE_ZONE::HORS_GEL);
    this->desactiverBoost();
  } else if (mode.equalsIgnoreCase("Confort")) {
    this->setMode(MODE_ZONE::CONFORT);
    this->desactiverBoost();
  } else if (mode.equalsIgnoreCase("Boost")) {
    this->setMode(MODE_ZONE::CONFORT);
    this->activerBoost();
  }
}

String Zone::getNomMode() {
    return getMode() == MODE_ZONE::INCONNU ? "Inconnu" : nomPreset(_reglages.mode, _reglages.options);
}

byte Zone::getModeOptions() {
    return this->_reglages.options;
}
void Zone::setModeOptions(byte modeOptions) {
    this->_reglages.options = modeOptions;
}

uint8_t Zone::getIdZone() {
    return this->_idZone;
}

void Zone::activerBoost() {
    _reglages.options |= 0b01000000;
}
void Zone::desactiverBoost() {
    _reglages.options &= ~0b01000000;
}
bool Zone::boostActif() {
    return (_reglages.options & 0b01000000) != 0;
}
uint8_t Zone::getNumeroZone() {
    switch(_idZone) {
            case ID_ZONE_1: 
                return 1;
            case ID_ZONE_2: 
                return 2;
            case ID_ZONE_3: 
                return 3;
        }
        
        return 0;
}
bool Zone::confortActif() {
    return 
        _reglages.mode == MODE_ZONE::CONFORT ||
        (_reglages.mode == MODE_ZONE::AUTO && (_reglages.options & 0b00000001) != 0);
}

bool Zone::reduitActif() {
    return 
        _reglages.mode == MODE_ZONE::REDUIT ||
        (_reglages.mode == MODE_ZONE::AUTO && (_reglages.options & 0b00000001) == 0);
}

bool Zone::horsGelActif() {
    return _reglages.mode == MODE_ZONE::HORS_GEL;
}

bool Zone::derogationActive() {
    return 
        _reglages.mode == MODE_ZONE::AUTO && (_reglages.options & 0b00000010) != 0;
}


void Zone::publishMqtt() {
    actualiserDecouverte();
    mqtt().publishAvailability(_mqttEntities.mode, pilotageDisponible());
    publierMode();
    if(!isnan(getTemperatureAmbiante())) {
        mqtt().publishState(*mqtt().getDevice("openFrisquetVisio")->getEntity("temperatureAmbianteZ" + String(getNumeroZone())), getTemperatureAmbiante());
    }
    if(!isnan(getTemperatureConsigne())) {
        mqtt().publishState(*mqtt().getDevice("openFrisquetVisio")->getEntity("temperatureConsigneZ" + String(getNumeroZone())), getTemperatureConsigne());
    }
    if(!isnan(getTemperatureDepart())) {
        mqtt().publishState(*mqtt().getDevice("openFrisquetVisio")->getEntity("temperatureDepartZ" + String(getNumeroZone())), getTemperatureDepart());
    }
    if(!isnan(getTemperatureConsigneDepart())) {
        mqtt().publishState(*mqtt().getDevice("openFrisquetVisio")->getEntity("temperatureConsigneDepartZ" + String(getNumeroZone())), getTemperatureConsigneDepart());
    }
    if(!isnan(getTemperatureConfort())) {
        mqtt().publishState(*mqtt().getDevice("openFrisquetVisio")->getEntity("temperatureConfortZ" + String(getNumeroZone())), getTemperatureConfort());
    }
    if(!isnan(getTemperatureReduit())) {
        mqtt().publishState(*mqtt().getDevice("openFrisquetVisio")->getEntity("temperatureReduitZ" + String(getNumeroZone())), getTemperatureReduit());
    }
    if(!isnan(getTemperatureHorsGel())) {
        mqtt().publishState(*mqtt().getDevice("openFrisquetVisio")->getEntity("temperatureHorsGelZ" + String(getNumeroZone())), getTemperatureHorsGel());
    }
    if(!isnan(getTemperatureBoost())) {
        mqtt().publishState(*mqtt().getDevice("openFrisquetVisio")->getEntity("temperatureBoostZ" + String(getNumeroZone())), getTemperatureBoost());
    }
}

bool Zone::programmeDisponible() const {
    return _reglages.programmeConnu && timeStatus() != timeNotSet && year(::now()) >= 2020;
}

bool Zone::regimeProgramme(bool& confort, uint32_t* prochaineTransition) const {
    if(!programmeDisponible()) return false;
    const time_t current = ::now();
    const unsigned slot = (weekday(current) - 1) * 48 + hour(current) * 2 + minute(current) / 30;
    const auto* bytes = reinterpret_cast<const uint8_t*>(&_reglages.programme);
    confort = ZoneProgram::comfort(bytes, slot);
    if(prochaineTransition) {
        const uint32_t delay = ZoneProgram::nextTransition(bytes, slot, (minute(current) % 30) * 60 + second(current));
        *prochaineTransition = delay ? static_cast<uint32_t>(current) + delay : 0;
    }
    return true;
}

void Zone::setPilotageActif(bool actif) {
    _pilotageActif = actif;
    if(_mqttPret) {
        actualiserDecouverte();
        publishMqtt();
    }
}

void Zone::actualiserConsigne() {
    float temperature = horsGelActif() ? getTemperatureHorsGel()
        : confortActif() ? getTemperatureConfort() : getTemperatureReduit();
    if(confortActif() && boostActif()) temperature += getTemperatureBoost();
    if(isfinite(temperature)) setTemperatureConsigne(temperature);
}

bool Zone::appliquerConsigne(float temperature) {
    return modifier(Objet::Consigne, [&]() { return definirConsigne(temperature); });
}

bool Zone::appliquerPreset(const String& preset) {
    return modifier(Objet::Mode, [&]() { return definirPreset(preset); });
}

bool Zone::definirConsigne(float temperature) {
    if(!pilotageDisponible() || !isfinite(temperature) || temperature < 5 || temperature > 30) return false;
    if(confortActif()) {
        const float profil = temperature - (boostActif() ? getTemperatureBoost() : 0);
        if(profil < 5) return false;
        setTemperatureConfort(profil);
    } else if(reduitActif()) {
        setTemperatureReduit(temperature);
    } else if(horsGelActif()) {
        setTemperatureHorsGel(temperature);
    } else {
        return false;
    }
    actualiserConsigne();
    return true;
}

bool Zone::definirPreset(const String& preset) {
    if(!pilotageDisponible()) {
        info("[ZONE Z%d] Commande ignorée : pilotage inactif.", getNumeroZone());
        return false;
    }
    // Home Assistant ajoute « none » à son sélecteur : retirer l'option
    // temporaire en conservant le régime permanent, ou reprendre Auto.
    if(preset == "none") {
        if(getMode() == MODE_ZONE::AUTO) return definirPreset("Auto");
        if(getMode() == MODE_ZONE::CONFORT) return definirPreset("Confort");
        if(getMode() == MODE_ZONE::REDUIT) return definirPreset("Réduit");
        if(getMode() == MODE_ZONE::HORS_GEL) return definirPreset("Hors Gel");
        return false;
    }
    MODE_ZONE cible = MODE_ZONE::INCONNU;
    bool confort = confortActif();
    uint32_t fin = 0;
    const bool autoMode = preset.equalsIgnoreCase("Auto");
    const bool derogConfort = preset.equalsIgnoreCase("Dérogation Confort");
    const bool derogReduit = preset.equalsIgnoreCase("Dérogation Réduit");
    const bool boost = preset.equalsIgnoreCase("Boost");
    const bool derogation = derogConfort || derogReduit;
    if(autoMode || derogation) {
        if(programmeLocal() && (!regimeProgramme(confort, &fin) || (derogation && !fin))) {
            info("[ZONE Z%d] Auto/dérogation indisponible : horloge, programme ou prochaine transition inconnus.", getNumeroZone());
            return false;
        }
        if(!programmeLocal()) regimeProgramme(confort);
        if(derogation) confort = derogConfort;
        cible = MODE_ZONE::AUTO;
    } else if(preset.equalsIgnoreCase("Confort") || preset.equalsIgnoreCase("Confort permanent") || boost) {
        cible = MODE_ZONE::CONFORT;
    } else if(preset.equalsIgnoreCase("Réduit") || preset.equalsIgnoreCase("Réduit permanent")) {
        cible = MODE_ZONE::REDUIT;
    } else if(preset.equalsIgnoreCase("Hors Gel")) {
        cible = MODE_ZONE::HORS_GEL;
    } else {
        return false;
    }
    const float temperature = cible == MODE_ZONE::HORS_GEL ? getTemperatureHorsGel()
        : (cible == MODE_ZONE::CONFORT || (cible == MODE_ZONE::AUTO && confort)) ? getTemperatureConfort() : getTemperatureReduit();
    if(!isfinite(temperature) || (boost && !isfinite(getTemperatureBoost()))) return false;
    setMode(cible, confort, derogation);
    _derogationFin = derogation && programmeLocal() ? fin : 0;
    desactiverBoost();
    if(boost) activerBoost();
    actualiserConsigne();
    return true;
}

void Zone::loop() {
    actualiserLivraison();
    const bool connecte = mqtt().connected();
    if(connecte && !_mqttConnecte) publishMqtt();
    _mqttConnecte = connecte;
    actualiserDecouverte();
    if(!pilotageDisponible() || !programmeLocal() || getMode() != MODE_ZONE::AUTO) return;
    bool confort;
    uint32_t transition = 0;
    if(!regimeProgramme(confort, &transition)) return;
    if(derogationActive() && transition && _derogationFin && static_cast<uint32_t>(::now()) < _derogationFin) return;
    if(!derogationActive() && confort == confortActif()) return;
    // Une dérogation restaurée sans échéance ne doit pas devenir permanente.
    setMode(MODE_ZONE::AUTO, confort, false);
    _derogationFin = 0;
    actualiserConsigne();
    refreshLastChange();
    saveConfig();
    publishMqtt();
}

void Zone::confirmerMode(MODE_ZONE mode, byte options) {
    _modeObserve = mode;
    _optionsObservees = options;
    if(_mqttPret) publierMode();
}

void Zone::publierMode() {
    // Le virtuel possède son état. Pour le physique/Connect, attendre un retour
    // radio ou une réponse réussie avant d'annoncer le changement demandé.
    MODE_ZONE mode = _source == SOURCE::SATELLITE_VIRTUEL ? _reglages.mode : _modeObserve;
    if(_source == SOURCE::SATELLITE_VIRTUEL && mode == MODE_ZONE::AUTO && !programmeDisponible()) mode = MODE_ZONE::INCONNU;
    const byte options = _source == SOURCE::SATELLITE_VIRTUEL ? _reglages.options : _optionsObservees;
    mqtt().publishState(_mqttEntities.thermostat, mode == MODE_ZONE::INCONNU ? "None" : mode == MODE_ZONE::AUTO ? "auto" : "heat");
    mqtt().publishState(_mqttEntities.preset, nomPreset(mode, options));
    if(mode != MODE_ZONE::INCONNU) mqtt().publishState(_mqttEntities.mode, nomPreset(mode, options));
    mqtt().publishState(_mqttEntities.regime, nomRegime(mode, options));
    mqtt().publishState(_mqttEntities.boost, (options & 0x40) ? "ON" : "OFF");
}

void Zone::actualiserDecouverte() {
    if(!_mqttPret) return;
    bool confort;
    uint32_t transition = 0;
    const bool programme = regimeProgramme(confort, &transition);
    const uint8_t capacites = (pilotageDisponible() ? 1 : 0) | ((!programmeLocal() || programme) ? 2 : 0)
        | ((!programmeLocal() || (programme && transition)) ? 4 : 0);
    if(capacites == _capacitesPubliees) return;
    _capacitesPubliees = capacites;
    String presets = "[";
    // En lecture seule, conserver tous les états observables.
    if(!pilotageDisponible() || (capacites & 2)) presets += "\"Auto\",";
    if(!pilotageDisponible() || (capacites & 4)) presets += "\"Dérogation Confort\",\"Dérogation Réduit\",";
    presets += "\"Confort\",\"Réduit\",\"Hors Gel\",\"Boost\"]";
    _mqttEntities.mode.setRaw("options", presets);

    auto* device = mqtt().getDevice("openFrisquetVisio");
    const String root = MqttManager::compose({device->baseTopic, "z" + String(getNumeroZone())});
    _mqttEntities.thermostat.extraFields.erase("preset_mode_command_topic");
    _mqttEntities.thermostat.extraFields.erase("preset_mode_state_topic");
    _mqttEntities.thermostat.extraFields.erase("preset_modes");
    _mqttEntities.thermostat.extraFields.erase("temperature_command_topic");
    _mqttEntities.thermostat.extraFields.erase("mode_command_topic");
    if(pilotageDisponible()) {
        // Home Assistant exige preset_modes et son command_topic ensemble.
        // En écoute, le capteur Mode actif conserve l'affichage détaillé.
        _mqttEntities.thermostat.setRaw("preset_modes", presets);
        _mqttEntities.thermostat.set("preset_mode_state_topic", _mqttEntities.preset.stateTopic.full);
        _mqttEntities.thermostat.set("preset_mode_command_topic", root + "/mode/set");
        _mqttEntities.thermostat.set("temperature_command_topic", root + "/temperatureConsigne/set");
        _mqttEntities.thermostat.set("mode_command_topic", root + "/thermostat/set");
        _mqttEntities.thermostat.setRaw("modes", (capacites & 2) ? R"(["auto","heat"])" : R"(["heat"])" );
    } else {
        _mqttEntities.thermostat.setRaw("modes", R"(["auto","heat"])" );
    }
    // Le select historique et les réglages ne sont disponibles qu'en pilotage.
    const MqttTopic availability(root + "/pilotage", 0, true);
    for(auto* entity : {&_mqttEntities.mode, &_mqttEntities.boost, &_mqttEntities.temperatureConfort,
                       &_mqttEntities.temperatureReduit, &_mqttEntities.temperatureHorsGel, &_mqttEntities.temperatureBoost}) {
        entity->availabilityTopic = availability;
        mqtt().registerEntity(*device, *entity, true);
    }
    mqtt().registerEntity(*device, _mqttEntities.thermostat, true);
    mqtt().publishAvailability(_mqttEntities.mode, pilotageDisponible());
}

void Zone::observerStatut(uint32_t reception, byte association) {
    // Une sauvegarde peut provoquer un statut supplémentaire. Les réémissions
    // et ces statuts hors cycle ne doivent pas déplacer un repère récent.
    if(_statutConnu) {
        if(_associationSatellite != association) oublierStatut();
        else if(static_cast<int32_t>(reception - _statut) < static_cast<int32_t>(cycleStatut - toleranceStatut)) return;
    }
    _statut = reception;
    _statutConnu = true;
    _associationSatellite = association;
    if(enAttente() && _fenetre == Fenetre::CycleSuivant) {
        _ouverture = reception + ouvertures[0];
        _fenetre = Fenetre::CycleEnCours;
        publishMqtt();
    }
    actualiserLivraison();
}

void Zone::oublierStatut() {
    _statutConnu = false;
    if(_fenetre != Fenetre::SansStatut && enAttente()) {
        if(_fenetre == Fenetre::CycleSuivant || static_cast<int32_t>(millis() - _ouverture) < 0) --_tentative;
        _fenetre = Fenetre::SansStatut;
        _debutPeriode = millis();
        publishMqtt();
    }
}

bool Zone::planifierFenetre(uint32_t maintenant) {
    if(!_statutConnu || _tentative >= MAX_FENETRES) return false;
    const uint32_t age = maintenant - _statut;
    if(age > cycleStatut + toleranceStatut) return false;
    // Après le dernier pic, la carte peut déjà estimer le premier du cycle
    // suivant. Seul le prochain statut de cette zone autorisera son émission.
    _fenetre = Fenetre::CycleSuivant;
    _ouverture = _statut + cycleStatut + ouvertures[0];
    for(uint32_t ouverture : ouvertures) {
        if(age < ouverture + finEmissions) {
            _ouverture = _statut + ouverture;
            _fenetre = Fenetre::CycleEnCours;
            break;
        }
    }
    ++_tentative;
    _debutPeriode = maintenant;
    return true;
}

int Zone::rangEmission(uint32_t maintenant) const {
    if(!enAttente() || _fenetre != Fenetre::CycleEnCours) return -1;
    const uint32_t ecoule = maintenant - _ouverture;
    if(ecoule >= finEmissions || ecoule % espacement >= margeEmission) return -1;
    return ecoule / espacement;
}

void Zone::demarrerCommande() {
    _livraison = Livraison::Attente;
    _fenetre = Fenetre::SansStatut;
    _tentative = 0;
    _debutPeriode = millis();
    planifierFenetre(_debutPeriode);
    ++_revisionCommande;
}

bool Zone::reessayer() {
    if(!nonConfirmee() || !pilotageDisponible() || _source != CONNECT) return false;
    demarrerCommande();
    publishMqtt();
    return true;
}

void Zone::actualiserLivraison() {
    const uint32_t maintenant = millis();
    if(_statutConnu && maintenant - _statut > cycleStatut + toleranceStatut) {
        if(_fenetre == Fenetre::CycleSuivant) oublierStatut();
        else _statutConnu = false;
    }
    if(!enAttente()) return;
    if(_fenetre != Fenetre::SansStatut) {
        if(static_cast<int32_t>(maintenant - (_ouverture + finConfirmation)) < 0) return;
        _fenetre = Fenetre::SansStatut;
        _debutPeriode = maintenant;
        if(_tentative == MAX_FENETRES) {
            _livraison = Livraison::Echec;
            info("[ZONE %d] Modification non confirmée après cinq fenêtres.", getNumeroZone());
        } else {
            planifierFenetre(maintenant);
        }
    } else if(!planifierFenetre(maintenant)) {
        if(maintenant - _debutPeriode < attenteStatut) return;
        _livraison = Livraison::Echec;
        info("[ZONE %d] Aucun statut périodique récent pour planifier la commande.", getNumeroZone());
    }
    publishMqtt();
}

void Zone::confirmerCommande() {
    _reglages = demande();
    confirmerMode();
    _livraison = Livraison::Confirmee;
    refreshLastChange();
    refreshLastEnvoi();
    saveConfig();
    publishMqtt();
}

uint8_t Zone::champModifie(Objet objet) {
    switch(objet) {
        case Objet::Consigne: return confortActif() ? ConfortModifie : reduitActif() ? ReduitModifie : HorsGelModifie;
        case Objet::Confort: return ConfortModifie;
        case Objet::Reduit: return ReduitModifie;
        case Objet::HorsGel: return HorsGelModifie;
        case Objet::Boost: return BoostModifie;
        case Objet::Mode: return ModeModifie;
        case Objet::Programme: return ProgrammeModifie;
        default: return 0;
    }
}

Zone::Reglages Zone::demande() const {
    // Les modifications du satellite entre deux envois restent acquises pour
    // tous les champs que l'utilisateur n'a pas modifiés.
    Reglages resultat = _reglages;
    const auto& saisie = _demande.reglages;
    const uint8_t champs = _demande.champs;
    if(champs & ConfortModifie) resultat.confort = saisie.confort;
    if(champs & ReduitModifie) resultat.reduit = saisie.reduit;
    if(champs & HorsGelModifie) resultat.horsGel = saisie.horsGel;
    if(champs & BoostModifie) resultat.boost = saisie.boost;
    if(champs & ModeModifie) {
        resultat.mode = saisie.mode;
        resultat.options = saisie.options;
    }
    if(champs & ProgrammeModifie) {
        resultat.programme = saisie.programme;
        resultat.programmeConnu = true;
    }
    if(champs & ~ProgrammeModifie) {
        const bool confort = resultat.mode == CONFORT || (resultat.mode == AUTO && (resultat.options & 1));
        resultat.consigne = resultat.mode == HORS_GEL ? resultat.horsGel : confort ? resultat.confort : resultat.reduit;
        if(confort && (resultat.options & 0x40)) resultat.consigne = std::min(30.0f, resultat.consigne + resultat.boost);
    }
    return resultat;
}

Zone::BlocZone Zone::blocDemande() const {
    const auto& d = demande();
    BlocZone bloc;
    bloc.confort = (d.options & 0x40) ? std::min(30.0f, d.confort + d.boost) : d.confort;
    bloc.reduit = d.reduit;
    bloc.horsGel = d.horsGel;
    bloc.mode = d.mode;
    bloc.options = d.options;
    bloc.programme = d.programme;
    return bloc;
}

bool Zone::reconnaitreReglages(const byte* donnees, size_t taille) {
    if((!enAttente() && !nonConfirmee()) || taille != sizeof(BlocZone)) return false;
    const BlocZone attendu = blocDemande();
    if(memcmp(donnees, &attendu, sizeof(attendu)) != 0) return false;
    confirmerCommande();
    return true;
}

float Zone::valeurObjet(const Reglages& reglages) const {
    switch(_demande.objet) {
        case Objet::Consigne: return reglages.consigne;
        case Objet::Confort: return reglages.confort;
        case Objet::Reduit: return reglages.reduit;
        case Objet::HorsGel: return reglages.horsGel;
        case Objet::Boost: return reglages.boost;
        default: return NAN;
    }
}

String Zone::suiviJson() const {
    static const char* const etats[] = {"Au repos", "En attente de confirmation", "Modification confirmée", "Modification non confirmée"};
    static const char* const objets[] = {"", "Consigne", "Confort", "Réduit", "Hors Gel", "Supplément Boost", "Mode", "Programmation"};
    JsonDocument doc;
    doc["etat"] = etats[static_cast<unsigned>(_livraison)];
    doc["objet"] = objets[static_cast<unsigned>(_demande.objet)];
    doc["tentative"] = _tentative;
    if(_demande.objet == Objet::Mode) {
        doc["mode_demande"] = nomPreset(demande().mode, demande().options);
    }
    const float confirmee = valeurObjet(_reglages), demandee = valeurObjet(_demande.reglages);
    if(isfinite(confirmee)) doc["valeur_confirmee"] = confirmee;
    if(isfinite(demandee)) doc["valeur_demandee"] = demandee;
    if(enAttente()) {
        doc["synchronisation"] = _fenetre == Fenetre::SansStatut;
        doc["max_tentatives"] = MAX_FENETRES;
        const uint32_t duree = _fenetre != Fenetre::SansStatut ? _ouverture + finConfirmation - _debutPeriode : attenteStatut;
        const uint32_t ecoule = millis() - _debutPeriode;
        doc["duree_ms"] = duree;
        doc["restant_ms"] = duree > ecoule ? duree - ecoule : 0;
    }
    String json;
    serializeJson(doc, json);
    return json;
}
