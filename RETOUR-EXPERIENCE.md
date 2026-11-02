# OpenFrisquetVisio — retour d'expérience sur Heltec WiFi LoRa 32 V3

Ce document consigne les verrous rencontrés lors de la mise en service d'une carte
**Heltec WiFi LoRa 32 V3** (ESP32-S3 + SX1262) face à une chaudière
**Frisquet HYDROMOTRIX COND 25 VISIO** (2023) pilotée par une commande **Visio sans fil**.
Tous ces points ont été constatés sur le matériel, pas déduits de la documentation.

## Résumé

Le firmware d'origine ne décodait aucune trame. Quatre causes se cumulaient, chacune
suffisante à elle seule pour tout bloquer. Après correction, la carte capte et publie
les trames du satellite en MQTT vers Home Assistant.

## Verrou 1 — la longueur de paquet était fixée à 68 octets

Le SX1262 était configuré en mode longueur fixe sur 68 octets. Or les trames réellement
émises font **23 octets** (trame satellite courte, toutes les 5 à 10 minutes) et parfois
**49 ou 55 octets**. En mode fixe, le modem attend 68 octets avant de rendre la main :
aucune trame courte n'est jamais délivrée.

**Correctif** : mode longueur variable, longueur maximale 255.

```cpp
_radio.variablePacketLengthMode(255);
```

**Effet mesuré** : `rxFrames` passe de 0 à 4 en quelques minutes, première trame de
49 octets décodée.

## Verrou 2 — le CRC était désactivé, donc absent du tampon

Avec le CRC matériel désactivé, le SX1262 ne valide rien et **ne retire rien**. Le
décodage logiciel cherchait un CRC qui n'était pas là, dans tous les cas.

Le format sur l'air est `[longueur][données][CRC_hi][CRC_lo]`. La trame utile de
23 octets occupe donc 26 octets sur l'air.

**Correctif** : réactiver le CRC matériel, qui vérifie et retire le CRC avant de
livrer la trame au logiciel.

```cpp
_radio.setCRC(2, 0x1D0F, 0x1021, true);  // CRC CCITT, init 0x1D0F, poly 0x1021, inversé
```

**Effet mesuré** : les trames passent la validation du modem sans rejet.

## Verrou 3 — le préambule se compte en bits, pas en octets

`SX126x.h:102` : la valeur passée à `setPreambleLength()` est en **bits**. Une valeur
de `5` (interprétée comme « 5 octets ») donne un préambule de 5 bits, ultra-court, que
le récepteur ne cale pas. Toute valeur inférieure à 8 est invalide en pratique.

## Verrou 4 — la fenêtre d'attente de la réponse chaudière

Après une trame satellite, la chaudière répond — et c'est cette réponse qui porte
l'état de la chaudière et la température extérieure. Deux écueils opposés :

- **6 secondes d'attente** bloquaient la boucle principale 22 secondes par cycle
  (boucles lentes, `maxLoopMs` à 22 264).
- **800 millisecondes** ne laissaient pas le temps à la chaudière de répondre
  (`err -6` systématique, réponse jamais captée).

**Compromis retenu** : 2 400 ms d'attente, avec le TTL du tampon de trames porté à
4 000 ms pour que les trames mises en attente n'expirent pas avant d'être rejouées.

**Effet mesuré** : `maxLoopMs` tombe de 22 264 à 15 021 — et ce 15 021 n'est plus
qu'un blocage d'initialisation au démarrage, plus aucun figement en fonctionnement.

## Correctif complémentaire — publier avant d'attendre

Le code attendait la réponse chaudière **avant** de publier les valeurs du satellite.
En cas d'absence de réponse, tout était jeté avec un `err -6`, alors que les valeurs
ambiante, consigne et mode étaient déjà décodées.

**Correctif** : publier d'abord, attendre ensuite.

## Précision sur la trame courte (23 octets)

Exemple réel :

```
80082DDC0117A0290015A02F00040800BD00BE00210280
```

- `0x80` — destinataire (chaudière)
- `0x08` — expéditeur (satellite zone 1)
- `0x17` — type de message (23)
- `0xA02F` — adresse mémoire écrite, taille `0x0004`
- `0xBD` = 189 → **18,9 °C** ambiante
- `0xBE` = 190 → **19,0 °C** consigne

Les deux températures sont des dixièmes de degré, sur un octet.

## La trame de 49 octets contient la température extérieure

Le champ `temperatureExterieure` est présent à l'offset 7 de la structure
`donneesZones_t`, mais le firmware d'origine ne le décodait pas. Il est désormais
extrait, avec un filtre de plausibilité (−40 à +60 °C) pour écarter les sentinelles.

Cette valeur n'est disponible que si la chaudière **répond** — donc pas de façon
fiable en lecture seule.

## Ce que la lecture seule ne donne pas

La classe `Chaudiere` n'a **aucune fonction de réception**. Toutes ses valeurs
(ECS, pression hydraulique, puissances, températures de fumées et de départ,
consommations annuelles) sont remplies par `Connect.cpp`, à partir de lectures que
la box Frisquet Connect adresse à la chaudière (adresses `0x79F2`, `0x7A18`,
`0xA063`, `0xA0FC`).

Sans `useConnect`, ces valeurs restent donc indisponibles — et le Connect actif
implique que la carte **émette**, donc qu'elle s'associe à la chaudière.

## Disponibilité du pilotage

En mode satellite physique, le topic `frisquet/zN/pilotage` reste `offline` : le code
ne rend le pilotage disponible que pendant un écrasement de consigne, c'est-à-dire
quand le satellite réel autorise une dérogation temporaire. Ce n'est pas un défaut :
la carte ne pilote pas, elle écoute.

## Résultat

- 29 entités créées dans Home Assistant
- 7 informations réellement remontées : température ambiante, consigne, mode actif,
  régime actif, état chaudière, anomalie, position du thermostat
- 20 entités en attente, dont toutes les mesures techniques et les commandes
- Une trame reçue toutes les 5 à 10 minutes, aucune perte, aucun redémarrage

---

# Compléments après plusieurs jours d'exploitation

## La grille de programmation : dimanche est le jour 0

`weekday()` de TimeLib renvoie **1 pour dimanche**. Le programme occupe 42 octets,
`42 = 7 × 48` demi-heures, et l'index se calcule par `(weekday()-1)*48`. L'index 0
est donc **dimanche**, comme le rappellent `ZoneProgram.h` (« dimanche en premier »)
et `Zone.h` (« du dimanche au samedi »).

Construire la grille en plaçant lundi en position 0 décale **toute la semaine d'un
jour**, sans aucun message d'erreur : le mercredi se retrouve avec les horaires d'un
jour travaillé et le mardi chauffe en continu. Le symptôme se voit seulement en
comparant le régime observé aux heures charnières.

Le bit se lit en **LSB first** : `program[slot/8] & (1u << (slot%8))`, avec
`slot = (weekday()-1)*48 + heure*2 + minute/30`. Un décodage en MSB first produit
des horaires absurdes du type 04:00-06:00 — c'est le signe qu'on lit à l'envers.

## `pending: false` ne signifie pas « la chaudière a acquitté »

`Zone.h` pose `pending = (source != SATELLITE_VIRTUEL)`. En satellite virtuel, le
programme reste **local à la carte** : aucune trame radio n'est envoyée à la
chaudière, qui ne reçoit que la consigne recalculée. Il n'y a donc pas d'état
incohérent possible côté chaudière — mais rien ne prouve non plus que le bon jour
est programmé. La seule preuve est le régime observé aux heures charnières.

## Découverte MQTT : une config obsolète reste retenue

`Zone.cpp` déclare `temperatureAmbiante` en `sensor`, puis change son `component`
en `number` lorsque la source est `SATELLITE_VIRTUEL`. Les deux configs de
découverte sont publiées en `retain` sur des topics différents, avec **le même
`uniq_id`** :

```
homeassistant/sensor/<id>/config
homeassistant/number/<id>/config
```

Home Assistant les traite comme deux entités distinctes (domaines différents), donc
ce n'est pas un conflit bloquant. Mais l'ancienne config `sensor` n'est **jamais
effacée** : après un changement de mode, les deux entités cohabitent, et celle qui
n'est plus alimentée reste figée à sa dernière valeur. Le même schéma existe pour
`temperatureConsigne` et pour la sonde extérieure.

Correction possible : suffixer l'entité de commande (`..._set`) ou publier un payload
vide sur le topic de découverte devenu inutile.

## Sonder la carte sans se tromper d'indicateur

Une entité Home Assistant dont la valeur ne change pas conserve son horodatage :
surveiller sa fraîcheur pour détecter une panne produit de **fausses alertes**
pendant les périodes stables. La preuve de vie doit venir du **battement MQTT** de
la carte, qui émet un cycle complet chaque minute environ, que la valeur change ou
non.
