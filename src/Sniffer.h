#pragma once

#include <Arduino.h>

// Sniffer radio : capte les trames brutes d'un réseau dont on ignore le sync word.
// On arme la réception avec un sync word court (1 octet) : le SX1262 détecte le
// préambule + 1 octet, puis livre les octets utiles, y compris les 3 octets de
// sync restants. C'est ce qui permet de retrouver un NetworkID inconnu.
namespace Sniffer {

void start(uint32_t seconds, uint8_t syncByte, uint8_t syncLen, float mhz);
void stop();
bool active();
void loop();                                     // coupe tout seul à l'échéance
void onPacket(const uint8_t* data, size_t len);  // appelé par la boucle radio
String report();                                 // JSON des paquets captés

}  // namespace Sniffer
