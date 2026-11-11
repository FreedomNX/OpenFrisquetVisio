// Traitement Satellite réel avec réponses radio synthétiques et simulation MQTT.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
using byte = uint8_t;
using std::isnan;
struct RadioSchedule {};
class String : public std::string {
public:
    using std::string::string;
    String(const std::string& s): std::string(s) {}
    String(unsigned n): std::string(std::to_string(n)) {}
    String(int n): std::string(std::to_string(n)) {}
};
struct Preferences {};
struct MqttTopic {String full;};
struct MqttEntity {String value; MqttTopic stateTopic;};
struct MqttManager {
    unsigned publications = 0;
    void publishState(MqttEntity& entity, const String& value) {
        entity.value = value;
        ++publications;
    }
};
struct Config {};
constexpr int ID_CHAUDIERE = 0x80;
constexpr int RADIOLIB_ERR_NONE = 0;
constexpr int RADIOLIB_SX126X_MAX_PACKET_LENGTH = 255;
void delay(unsigned) {}
uint32_t millis() { return 100; }
void info(const char*, ...) {}
void error(const char*, ...) {}
void warning(const String&) {}
struct fword {
    byte b[2];
    uint16_t toUInt16() const { return (uint16_t(b[0]) << 8) | b[1]; }
};
struct temperature16 : fword {float toFloat() const { return toUInt16() / 10.0f; }};
struct Date {Date(const byte*) {}};
struct Chaudiere {
    struct ETAT_CHAUDIERE {};
    void setEtatChaudiere(byte) {}
    ETAT_CHAUDIERE getEtatChaudiere() {return {};}
};
class FrisquetRadio {
public:
    #include "wire-types.inc"
    #include "radio-stub.inc"
};
#include "buffer.inc"
struct FrisquetDevice {
    FrisquetRadio& link;
    byte id, association = 0, message = 0;
    FrisquetDevice(FrisquetRadio& radio, Config&, MqttManager&, byte id): link(radio), id(id) {}
    FrisquetRadio& radio() {return link;}
    byte getId() {return id;}
    byte getIdMessage() {return message;}
    byte getIdAssociation() {return association;}
    void setIdMessage(byte n) {message=n;}
    void setIdAssociation(byte n) {association=n;}
    void incrementIdMessage(byte n) {message+=n;}
    void setDate(Date) {}
};
#include "classes.inc"

void Zone::saveConfig() {}
void Zone::publishMqtt() {publierMode();}
uint8_t Zone::getNumeroZone() {return _idZone == 8 ? 1 : _idZone == 9 ? 2 : 3;}
uint8_t Zone::getIdZone() {return _idZone;}
bool Zone::programmeDisponible() const {return true;}
void Zone::setPilotageActif(bool actif) {_pilotageActif=actif;}
void Zone::setTemperatureAmbiante(float value) {_temperatureAmbiante=value;}
void Zone::setTemperatureConsigne(float value) {_temperatureConsigne=value;}
float Zone::getTemperatureConsigne() {return _temperatureConsigne;}
void Satellite::saveConfig() {}
void Satellite::publishMqtt() {}
bool Satellite::envoyerConsigne() {assert(false && "le test ne doit jamais émettre de commande radio"); return false;}

#include "methods.inc"
// Paquets synthétiques : seuls les champs utilisés par le traitement sont renseignés.
std::vector<byte> makeRequest(byte mode) {
    static_assert(sizeof(FrisquetRadio::RadioTrameHeader) == 6);
    static_assert(sizeof(FrisquetRadio::RadioTrameInit) == 9);
    std::vector<byte> request(23, 0);
    FrisquetRadio::RadioTrameHeader header;
    header.idDestinataire = ID_CHAUDIERE;
    header.idExpediteur = 8;
    header.idAssociation = 1;
    header.idMessage = 2;
    header.idReception = 1;
    header.type = FrisquetRadio::INIT;
    memcpy(request.data(), &header, sizeof(header));
    // Lire A029 et écrire quatre mots dans A02F.
    request[6] = 0xA0; request[7] = 0x29; request[9] = 0x15;
    request[10] = 0xA0; request[11] = 0x2F; request[13] = 4; request[14] = 8;
    request[16] = 200; // 20 degrés mesurés
    request[18] = 210; // 21 degrés demandés
    request[20] = mode;
    return request;
}

std::vector<byte> makeReply(byte mode) {
    const auto request = makeRequest(mode);
    FrisquetRadio::RadioTrameHeader header, response;
    memcpy(&header, request.data(), sizeof(header));
    header.answer(response);
    std::vector<byte> reply(49, 0);
    memcpy(reply.data(), &response, sizeof(response));
    reply[6] = 42;
    for (unsigned zone = 0; zone < 3; ++zone) {
        reply[20 + zone * 10] = 200;
        reply[22 + zone * 10] = 210;
        reply[24 + zone * 10] = mode;
    }
    return reply;
}

int main() {
    for (const byte raw : {0x25, 0x20, 0x05, 0x45, 0x01}) {
        for (const auto source : {Zone::CONNECT, Zone::SATELLITE_PHYSIQUE}) {
            MqttManager mqtt; Config config; FrisquetRadio radio; Chaudiere boiler;
            Zone zone(8, mqtt); zone.setSource(source); zone._mqttPret=true;
            // Représenter la consigne mémorisée ; l’état observé est initialement inconnu.
            zone.setMode(Zone::AUTO, true);
            Satellite satellite(radio, config, mqtt, zone, boiler);
            auto request=makeRequest(raw);
            radio.reply=makeReply(raw);
            assert(request.size()==23 && radio.reply.size()==49);
            assert(satellite.onReceive(request.data(), request.size()));
            assert(zone._modeObserve==Zone::INCONNU);
            memset(request.data(), 0xDD, request.size()); // la requête conservée doit être une copie indépendante
            satellite.pollObservation();
            printf("octet=0x%02X source=%s demande=%u observe=%u mode=%s regime=%s boost=%s\n",
                raw, source==Zone::CONNECT ? "Connect" : "satellite", zone._mode,
                zone._modeObserve, zone._mqttEntities.preset.value.c_str(),
                zone._mqttEntities.regime.value.c_str(), zone._mqttEntities.boost.value.c_str());

            const auto mode=(raw==0x25 || raw==0x20) ? Zone::AUTO : Zone::CONFORT;
            const char* preset=raw==0x45 ? "Boost" : mode==Zone::AUTO ? "Auto" : "Confort";
            assert(zone._modeObserve==mode);
            assert(zone._mqttEntities.preset.value==preset);
            assert(zone._mqttEntities.regime.value==(raw==0x20 ? "Réduit" : "Confort"));
            assert(zone._mqttEntities.boost.value==(raw==0x45 ? "ON" : "OFF"));

        }
    }
    for (bool overrideMode : {false,true}) {
        MqttManager mqtt; Config config; FrisquetRadio radio; Chaudiere boiler;
        Zone zone(8,mqtt); zone.setSource(Zone::CONNECT); zone._mqttPret=true;
        Satellite satellite(radio,config,mqtt,zone,boiler);
        auto request=makeRequest(0x25);radio.reply=makeReply(0x25);
        assert(satellite.onReceive(request.data(),request.size()));
        if (overrideMode) satellite.setEcrasement(true);
        else radio.cancelObservation();
        satellite.pollObservation();
        assert(!satellite._observationPending && zone._modeObserve==Zone::INCONNU);
        assert(!mqtt.publications);
    }
    for (unsigned raw : {0x21,0x24,0x22,0x23,0x26,0x27,0x00,0x04,0x10,0x41,0x08,0x40,0x60,0x80,0xff}) {
        for (bool pending : {false,true}) {
            for (unsigned zoneNumber=1;zoneNumber<=3;++zoneNumber) {
                MqttManager mqtt; Config config; FrisquetRadio radio; Chaudiere boiler;
                const byte id=zoneNumber+7;
                Zone zone(id,mqtt); zone.setSource(Zone::CONNECT); zone._mqttPret=true;
                zone.setMode(Zone::CONFORT); zone.setModeOptions(0x41);
                zone._lastChange=0; zone._lastEnvoi=0;
                Satellite satellite(radio,config,mqtt,zone,boiler);
                auto request=makeRequest(0x25); request[1]=id;
                radio.reply=makeReply(0x25); radio.reply[0]=id;
                // Contredire volontairement la requête pour vérifier la source utilisée.
                radio.reply[24+(zoneNumber-1)*10]=raw;
                assert(satellite.onReceive(request.data(),request.size()));
                // Une commande reçue pendant l’attente reste demandée ; la réponse
                // chaudière antérieure fournit seulement l’état observé.
                zone._lastChange=pending ? 2 : 0;
                zone.setTemperatureConsigne(23.5f);
                satellite.pollObservation();
                if (pending) assert(zone.getTemperatureConsigne()==23.5f);
                const bool invalid=raw==0x08 || raw==0x40 || raw==0x60 || raw==0x80 || raw==0xff;
                if (invalid) assert(zone._modeObserve==Zone::INCONNU);
                else {
                    const auto expected=raw==0x10 ? Zone::HORS_GEL : (raw&0x22) ? Zone::AUTO : (raw&1) ? Zone::CONFORT : Zone::REDUIT;
                    assert(zone._modeObserve==expected);
                    if (raw&2) assert(zone._mqttEntities.preset.value==((raw&1) ? "Dérogation Confort" : "Dérogation Réduit"));
                    assert(bool(zone._optionsObservees&0x40)==bool(raw&0x40));
                }
                if (pending || invalid) {
                    assert(zone._mode==Zone::CONFORT && zone._modeOptions==0x41);
                }
                // Une commande toujours en attente ne fige pas les observations
                // suivantes si aucune nouvelle commande n'arrive pendant l'échange.
                request[3]++;
                radio.reply[3]++;
                assert(satellite.onReceive(request.data(),request.size()));
                satellite.pollObservation();
                assert(zone.getTemperatureConsigne()!=23.5f);
            }
        }
    }
    // Toute réponse chaudière trop courte est rejetée avant mise à jour de l’état observé.
    for (size_t size=0;size<49;++size) {
        MqttManager mqtt; Config config; FrisquetRadio radio; Chaudiere boiler;
        Zone zone(8,mqtt); zone.setSource(Zone::CONNECT); zone._mqttPret=true;
        Satellite satellite(radio,config,mqtt,zone,boiler);
        auto request=makeRequest(0x25);
        radio.reply=makeReply(0x25);radio.reply.resize(size);
        assert(satellite.onReceive(request.data(),request.size()));
        satellite.pollObservation();
        assert(zone._modeObserve==Zone::INCONNU && mqtt.publications==0);
    }

    puts("OK : modes synthétiques, commandes en attente, annulation, sélection de zone, modes invalides et réponses tronquées.");
}
