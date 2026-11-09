#include "FrisquetManager.h"
#include "Version.h"
#include "Buffer.h"

FrisquetManager::FrisquetManager(FrisquetRadio &radio, Config &cfg, MqttManager &mqtt)
    :   _radio(radio), _cfg(cfg), _mqtt(mqtt),
        _zone1(ID_ZONE_1, mqtt), _zone2(ID_ZONE_2, mqtt), _zone3(ID_ZONE_3, mqtt),
        _chaudiere(mqtt, cfg),
        _sondeExterieure(radio, cfg, mqtt), _connect(radio, cfg, mqtt, _zone1, _zone2, _zone3, _chaudiere),
        _satelliteZ1(radio, cfg, mqtt, _zone1, _chaudiere), _satelliteZ2(radio, cfg, mqtt, _zone2, _chaudiere), _satelliteZ3(radio, cfg, mqtt, _zone3, _chaudiere) {}

void FrisquetManager::begin()
{
    // Init radio
    _radio.init(_cfg.radioRxBandwidth(), _cfg.radioLnaBypass());
    _radio.setNetworkID(_cfg.getNetworkID());

    initMqtt();

    if (_cfg.useZone1()) {
        if(_cfg.useSatelliteVirtualZ1()) {
            _zone1.setSource(Zone::SOURCE::SATELLITE_VIRTUEL);
        } else if(_cfg.useConnect()) {
            _zone1.setSource(Zone::SOURCE::CONNECT);
        } else {
            _zone1.setSource(Zone::SOURCE::SATELLITE_PHYSIQUE);
        }
        _zone1.setPilotageActif(_cfg.useSatelliteVirtualZ1() || (_cfg.useConnect() && !_cfg.useConnectPassive()));
        _zone1.begin();
    }
    
    if (_cfg.useZone2()) {
        if(_cfg.useSatelliteVirtualZ2()) {
            _zone2.setSource(Zone::SOURCE::SATELLITE_VIRTUEL);
        } else if(_cfg.useConnect()) {
            _zone2.setSource(Zone::SOURCE::CONNECT);
        } else {
            _zone2.setSource(Zone::SOURCE::SATELLITE_PHYSIQUE);
        }
        _zone2.setPilotageActif(_cfg.useSatelliteVirtualZ2() || (_cfg.useConnect() && !_cfg.useConnectPassive()));
        _zone2.begin();
    }

    if (_cfg.useZone3()) {
        if(_cfg.useSatelliteVirtualZ3()) {
            _zone3.setSource(Zone::SOURCE::SATELLITE_VIRTUEL);
        } else if(_cfg.useConnect()) {
            _zone3.setSource(Zone::SOURCE::CONNECT);
        } else {
            _zone3.setSource(Zone::SOURCE::SATELLITE_PHYSIQUE);
        }
        _zone3.setPilotageActif(_cfg.useSatelliteVirtualZ3() || (_cfg.useConnect() && !_cfg.useConnectPassive()));
        _zone3.begin();
    }

    const bool useSatellite = _cfg.useSatelliteZ1() || _cfg.useSatelliteZ2() || _cfg.useSatelliteZ3();
    if (_cfg.useConnect() || useSatellite) {
        if (_cfg.useConnect()) {
            _chaudiere.begin([this](const String& payload) {
                _connect.handleModeEcsCommand(payload);
            });
        } else {
            _chaudiere.begin();
        }
    }

    if (_cfg.useConnect()) {
        _connect.begin();
        if (!_cfg.useConnectPassive()) {
            _connect.recupererDate();
        }
    }

    if (_cfg.useSondeExterieure()) {
        _sondeExterieure.begin();
        _sondeExterieure.recupererDate();

        if (_cfg.useDS18B20()) {
            initDS18B20();
        }
    }
    if (_cfg.useZone1() && _cfg.useSatelliteZ1()) {
        _satelliteZ1.begin(_cfg.useSatelliteVirtualZ1());
    }
    if (_cfg.useZone2() && _cfg.useSatelliteZ2()) {
        _satelliteZ2.begin(_cfg.useSatelliteVirtualZ2());
    }
    if (_cfg.useZone3() && _cfg.useSatelliteZ3()) {
        _satelliteZ3.begin(_cfg.useSatelliteVirtualZ3());
    }

    _mqtt.publishAvailability(*_mqtt.getDevice("openFrisquetVisio"), true);

    _radio.onReceive([]()
                     {
    if(!FrisquetRadio::interruptReceive) {
        FrisquetRadio::receivedFlag = true;
    } });
    _radio.startReceive();
}

void FrisquetManager::loop()
{
    uint32_t now = millis();

    if (FrisquetRadio::receivedFlag || _radio.hasReceivedPacket() || _radio.hasBufferedPacket()) {
        onRadioReceive();
    }

    if(_cfg.useZone1()) _zone1.loop();
    if(_cfg.useZone2()) _zone2.loop();
    if(_cfg.useZone3()) _zone3.loop();

    if (_cfg.useConnect()) {
        _connect.loop();
    }
    
    if (_cfg.useSondeExterieure()) {
        _sondeExterieure.loop();
    }
    
    if (_cfg.useSatelliteZ1()) {
        _satelliteZ1.loop();
    }
    if (_cfg.useSatelliteZ2()) {
        _satelliteZ2.loop();
    }
    if (_cfg.useSatelliteZ3()) {
        _satelliteZ3.loop();
    }
}

void FrisquetManager::initDS18B20()
{
    info("[DS18B20] Initialisation du capteur de température.");
    _ds18b20 = new DS18B20();
    _ds18b20->init(DS18B20_PIN);

    if (_ds18b20->isReady())
    {
        float temperature;
        for (uint8_t i = 0; i < 10; i++)
        { // Boucle pour éviter les valeur incohérentes
            _ds18b20->getTemperature(temperature);
            delay(100);
        }
        info("[DS18B20] Capteur prêt.");

        _sondeExterieure.setDS18B20(_ds18b20);
    }
    else
    {
        error("[DS18B20] Impossible d'initialiser le capteur.");
    }
}

void FrisquetManager::initMqtt()
{

    info("[MQTT] Initialisation du device MQTT.");

    // Device commun
    _device.deviceId = "openFrisquetVisio";
    _device.name = "OpenFrisquetVisio";
    _device.model = "OpenFrisquetVisio";
    _device.manufacturer = "FreedomNX Lab";
    _device.baseTopic = _cfg.getMQTTOptions().baseTopic;
    _device.swVersion = OPENFRISQUETVISIO_VERSION;
    _mqtt.registerDevice(_device);
}

void FrisquetManager::onRadioReceive()
{
    //FrisquetRadio::interruptReceive = true;
    byte buff[RADIOLIB_SX126X_MAX_PACKET_LENGTH];
    size_t length = 0;
    float rssi = NAN;
    int16_t err = RADIOLIB_ERR_NONE;

    // Priorité au paquet matériel : traiter le tampon logiciel en premier
    // pouvait effacer le drapeau d'une autre trame encore présente dans le SX1262.
    if (_radio.hasReceivedPacket()) {
        FrisquetRadio::receivedFlag = false;
        err = _radio.readData(buff, 0);

        if (err != RADIOLIB_ERR_NONE)
        {
            _radio.startReceive();
            FrisquetRadio::interruptReceive = false;
            return;
        }

        length = _radio.getPacketLength();
        rssi = _radio.getLastPacketRSSI();
        _radio.startReceive();
    } else {
        // Drapeau périmé : ne pas relire le matériel ni interrompre une réception
        // éventuellement en cours. Le registre IRQ est aussi vérifié par loop().
        FrisquetRadio::receivedFlag = false;
        if (!_radio.popBufferedPacket(buff, length, &rssi)) return;
        info("[RADIO] Réutilisation paquet bufferisé : %d bytes", length);
    }

    info("[RADIO] Réception données radio : %d bytes", length);

    logRadio(true, buff, length, rssi);

    if (length < sizeof(FrisquetRadio::RadioTrameHeader))
    {
        FrisquetRadio::interruptReceive = false;
        return;
    }

    FrisquetRadio::RadioTrameHeader *header = (FrisquetRadio::RadioTrameHeader *)buff;
    if (_cfg.useConnect() &&
        (header->idDestinataire == _connect.getId() ||
         (_cfg.useConnectPassive() && header->idExpediteur == _connect.getId() && header->idDestinataire == ID_CHAUDIERE))) {
        info("[RADIO] Traitement données Connect");
        _connect.onReceive(buff, length);
    } else if (header->idDestinataire == _satelliteZ1.getId() && _cfg.useSatelliteZ1() && _cfg.useSatelliteVirtualZ1()) {
        info("[RADIO] Traitement données Satellite Z1");
        _satelliteZ1.onReceive(buff, length);
    } else if (header->idDestinataire == _satelliteZ2.getId() && _cfg.useSatelliteZ2() && _cfg.useSatelliteVirtualZ2()) {
        info("[RADIO] Traitement données Satellite Z2");
        _satelliteZ2.onReceive(buff, length);
    } else if (header->idDestinataire == _satelliteZ3.getId() && _cfg.useSatelliteZ3() && _cfg.useSatelliteVirtualZ3()) {
        info("[RADIO] Traitement données Satellite Z2");
        _satelliteZ3.onReceive(buff, length);
    } else if (header->idExpediteur == _satelliteZ1.getId() && _cfg.useSatelliteZ1() && !_cfg.useSatelliteVirtualZ1()) {
        info("[RADIO] Traitement données envoi Satellite Z1");
        _satelliteZ1.onReceive(buff, length);
    } else if (header->idExpediteur == _satelliteZ2.getId() && _cfg.useSatelliteZ2() && !_cfg.useSatelliteVirtualZ2()) {
        info("[RADIO] Traitement données envoi Satellite Z2");
        _satelliteZ2.onReceive(buff, length);
    } else if (header->idExpediteur == _satelliteZ3.getId() && _cfg.useSatelliteZ3() && !_cfg.useSatelliteVirtualZ3()) {
        info("[RADIO] Traitement données envoi Satellite Z3");
        _satelliteZ3.onReceive(buff, length);
    }

    FrisquetRadio::interruptReceive = false;
}


bool FrisquetManager::recupererNetworkID() {
    byte buff[RADIOLIB_SX126X_MAX_PACKET_LENGTH];
    size_t buffLength = 0;
    int16_t err;
    
    radio().setNetworkID({0xFF, 0xFF, 0xFF, 0xFF}); // Broadcast

    uint8_t retry = 0;
    unsigned long timeout = millis() + 30000;
    do {
        err = radio().receive(buff, 0);
        if(err != RADIOLIB_ERR_NONE) {
            continue;
        }

        buffLength = radio().getPacketLength(); 
        if (buffLength != 11) {
            continue;
        }

        struct {
            FrisquetRadio::RadioTrameHeader header;
            uint8_t length;
            NetworkID networkID;
        } donnees;

        logRadio(true, (byte*)buff, buffLength, radio().getLastPacketRSSI());

        ReadBuffer readBuffer = ReadBuffer(buff, buffLength);
        readBuffer.getBytes((byte*)&donnees, sizeof(donnees));

        if(donnees.header.idExpediteur == ID_CHAUDIERE && donnees.header.type == FrisquetRadio::MessageType::ASSOCIATION) {
            info("[DEVICE] Réception trame d'association");
            info("[DEVICE] Récupération du NetworkID : %s.", byteArrayToHexString((byte*)&donnees.networkID, sizeof(NetworkID)).c_str());
            
            config().setNetworkID(donnees.networkID);
            radio().setNetworkID(donnees.networkID);
            config().save();
            radio().startReceive();
            return true;
        }
    } while(millis() < timeout);

    // Reprendre l'écoute du réseau configuré après une tentative sans résultat.
    radio().setNetworkID(config().getNetworkID());
    radio().startReceive();
    return false;
}
