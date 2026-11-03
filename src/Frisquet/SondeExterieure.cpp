#include "SondeExterieure.h"
#include <math.h>

void SondeExterieure::loadConfig() {
    bool checkMigration = false;
    getPreferences().begin("sondeExtCfg", false);
    
    if(! getPreferences().isKey("idAssociation")) {
        checkMigration = true;
    }

    setIdAssociation(getPreferences().getUChar("idAssociation", 0xFF));
    getPreferences().end();
    delay(100);

    // Migration
    if(checkMigration && getPreferences().begin("net-conf", false)) {
        if(getPreferences().isKey("son_id")) {
            setIdAssociation(getPreferences().getUChar("son_id", 0xFF));
        }
        getPreferences().end();
        checkMigration = false;
    }
}

void SondeExterieure::saveConfig() {   
    getPreferences().begin("sondeExtCfg", false);
    getPreferences().putUChar("idAssociation", getIdAssociation());
    getPreferences().end();
}

void SondeExterieure::setTemperatureExterieure(float temperatureExterieure) {
    _temperatureExterieure = std::min(std::max(-30.0f, (round(temperatureExterieure * 10.0f)/10.0f)), 80.0f);
}

float SondeExterieure::getTemperatureExterieure() {
    return _temperatureExterieure;
}

bool SondeExterieure::envoyerTemperatureExterieure() {
    if(! estAssocie() || isnan(_temperatureExterieure)) {
        return false;
    }

    struct {
        FrisquetRadio::RadioTrameHeader header;
        uint8_t longueur;
        byte date[6] = {0};
        uint8_t i1;
        uint8_t jour;
    } donnees;

    size_t length;
    int16_t err;
    
    uint8_t retry = 0;
    do {
        length = sizeof(donnees);
        err = this->radio().sendInit(
            this->getId(), 
            ID_CHAUDIERE, 
            this->getIdAssociation(),
            this->incrementIdMessage(),
            0x01, 
            0x9c54,
            0x0004,
            0xa029,
            0x0001,
            temperature16(_temperatureExterieure).bytes,
            sizeof(temperature16),
            (byte*)&donnees,
            length
        );

        if(err != RADIOLIB_ERR_NONE || length < sizeof(donnees)) {
            delay(100);
            continue;
        }

        Date date = Date(donnees.date);
        setDate(date);
        
        return true;
    } while(retry++ < 1);

    return false;
}


void SondeExterieure::begin() {

    loadConfig();

    // Initialisation MQTT
  info("[SONDE EXTERIEURE][MQTT] Initialisation des entités.");

    // Device commun
    MqttDevice* device = mqtt().getDevice("openFrisquetVisio");

    // Entités

    // SENSOR: Température extérieure
    _mqttEntities.tempExterieure.id = "temperatureExterieure";
    _mqttEntities.tempExterieure.name = "Température extérieure";
    _mqttEntities.tempExterieure.component = "sensor";
    if(! getConfig().useDS18B20()) {
        _mqttEntities.tempExterieure.component = "number";
    } 
    _mqttEntities.tempExterieure.stateTopic = MqttTopic(MqttManager::compose({device->baseTopic, "sondeExterieure", "temperatureExterieure"}), 0, true);
    _mqttEntities.tempExterieure.commandTopic = MqttTopic(MqttManager::compose({device->baseTopic,"sondeExterieure","temperatureExterieure","set"}), 0, true);
    _mqttEntities.tempExterieure.set("device_class", "temperature");
    _mqttEntities.tempExterieure.set("state_class", "measurement");
    _mqttEntities.tempExterieure.set("unit_of_measurement", "°C");
    _mqttEntities.tempExterieure.set("min", "-30");
    _mqttEntities.tempExterieure.set("max", "80");
    _mqttEntities.tempExterieure.set("mode", "box");
    _mqttEntities.tempExterieure.set("step", "0.1");
    mqtt().onCommand(_mqttEntities.tempExterieure, [&](const String& payload) {
            float temperature = payload.toFloat();
            if(!isnan(temperature)) {
                info("[SONDE EXTERIEURE] Modification manuelle de la température extérieure à %0.2f.", temperature);
                setTemperatureExterieure(payload.toFloat());
                mqtt().publishState(_mqttEntities.tempExterieure, getTemperatureExterieure());
            }
        });
    mqtt().removeEntityDiscovery(*device, _mqttEntities.tempExterieure,
        _mqttEntities.tempExterieure.component == "number" ? "sensor" : "number");
    mqtt().registerEntity(*device, _mqttEntities.tempExterieure, true);
}

void SondeExterieure::loop() {

    if(estAssocie()) {
        if (_temperatureSchedule.due(600, 240)) { // :04, :14, :24, :34, :44, :54
            info("[SONDE EXTERIEURE] Envoi de la température extérieure.");
            // Récupération de la température si DS18B20 activé.
            if(_ds18b20 != nullptr && _ds18b20->isReady()) {
                float temperature = NAN;
                if(_ds18b20->getTemperature(temperature)) {
                    info("[DS18B20] Température : %.2f", temperature);
                    setTemperatureExterieure(temperature);
                }
            }
            
            if(!isnan(getTemperatureExterieure())) {
                if(envoyerTemperatureExterieure()) {
                    publishMqtt();
                } else {
                    error("[SONDE EXTERIEURE] Echec de l'envoi de la température extérieure.");
                }
            } else {
                warning("[SONDE EXTERIEURE] Aucune température disponible.");
            }
        }
    }
}

void SondeExterieure::publishMqtt() {
    if(!isnan(getTemperatureExterieure())) {
        mqtt().publishState(_mqttEntities.tempExterieure, getTemperatureExterieure());
    }
}
