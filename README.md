# 🧠 OpenFrisquetVisio

Émulation du module **Frisquet Connect**, d'une **sonde extérieure** ou d'un **satellite d'ambiance** sur base **Heltec ESP32 WiFi LoRa 32 V3/V4/V4.3 (SX1262)** ou **LilyGo/TTGO LoRa32 V2.1 (SX1276, 868 MHz)**.
Ce projet permet à un ESP32 de dialoguer directement avec votre chaudière Frisquet,  
de récupérer les informations en temps réel et de les exposer à **Home Assistant via MQTT**.

Les notes intégrées au code sont décrites dans [CHANGELOG.md](CHANGELOG.md).
L'[historique des versions publiées](https://github.com/FreedomNX/OpenFrisquetVisio/releases/latest/download/CHANGELOG.md)
est généré automatiquement et joint aux releases.

Les échanges avec la chaudière utilisent le mode **FSK à 868,96 MHz** des puces
radio, et non la modulation LoRa ni un réseau LoRaWAN. Les fonctions disponibles
dépendent du mode choisi et des données effectivement reçues de l'installation.

---

## 🚀 Fonctionnalités principales

### 🔧 Emulation Frisquet Connect
- Récupération des informations chaudière :
  - Température **ECS (eau chaude sanitaire)**
  - Température **corps de chauffe (CDC)**
  - Température **extérieure**
  - **Pression** du circuit (bar)
  - **Consommation gaz** ECS et chauffage, avec le relevé de la veille et le cumul annuel. Dans le tableau de bord Énergie de Home Assistant, choisissez uniquement les cumuls annuels pour éviter de compter deux fois les mêmes kWh.
  - **Affichage et sélection du mode ECS** (Max, Eco, Eco Horaires, Eco+, Eco+ Horaires, Stop ; commande en Connect virtuel actif)
- **Mode Connect passif** : écoute des échanges chaudière/box Connect sans émettre de requête Connect (utile si une box Connect est déjà installée). Les autres modules émulés restent capables d'émettre s'ils sont activés.
- Gestion des **zones 1, 2 et 3** :
  - Température **départ eau**
  - Température **ambiante**
  - Température **consigne**
  - **Configuration des consignes** pour chaque mode :
    - Réduit
    - Hors gel
    - Confort
  - **Sélection du mode actif** (Auto, Confort, Réduit, Hors Gel)

En Connect virtuel actif, la version du firmware chaudière est lue une seule
fois au démarrage (après association), à l'adresse `0x7541` sur deux mots BCD.
Les puissances instantanées ECS et chauffage sont divisées par 10 avant
`19.02.0000`, et interprétées directement en kW à partir de cette version.
Une lecture impossible ou invalide conserve la division par 10 et produit un
message dans les journaux. Cette règle de version reste à confirmer sur matériel ;
en mode Connect passif, la même règle est appliquée dès que la lecture de
`0x7541` par la box physique est captée. En attendant, la division par 10 est
conservée. Aucune requête supplémentaire n’est émise en passif.

### 🌡️ Emulation de la sonde extérieure
Deux modes possibles :
- Lecture **réelle** via une sonde **DS18B20**
- Lecture **virtuelle** via **MQTT**, permettant d’utiliser la température issue de la météo via un capteur HA.

Dans les deux cas, l'ESP32 émule une sonde radio d'identifiant `0x20` et doit
être associé. Le choix **Sonde Frisquet existante** n'émule rien et ne décode pas
directement les trames d'une sonde physique (`0x20`, `0x25` ou `0x26`). Sa
température peut être obtenue indirectement dans les informations chaudière en
mode Connect actif ou passif.

La sonde émulée envoie sa température toutes les 10 minutes, au premier passage
dans le créneau `HH:09:49` à `HH:09:58`, puis `HH:19:49` à `HH:19:58`, etc.
Ce calendrier suit l'heure chaudière synchronisée, ou le temps depuis le démarrage
avant synchronisation. Un créneau manqué n'est pas rattrapé. La sonde dispose
d'une exception dédiée à la fenêtre de silence des lectures périodiques.

### Gestion des satellites (utile si chaudière non-compatible Connect)
- Gestion des **zones 1, 2 et 3** :
  - Température **ambiance**
  - Température **consigne**
  - **Configuration d'un boost consigne** (écrasement de la consigne envoyé par le satellite)
  - **Affichage du mode actif** (Auto, Confort, Réduit, Hors Gel)

En mode satellite physique, les réponses de neuf octets à une lecture d'un mot
(par exemple `A033` sur un satellite ancien) sont prises en charge : ambiance,
consigne et mode proviennent de la trame du satellite. La date et l'état chaudière
ne sont actualisés par cet échange que lorsqu'il contient le grand bloc zones.

Les satellites virtuels utilisent un cycle fixe de 10 minutes, sur l'heure
chaudière : Z1 de `HH:x0:30` à `HH:x0:54`, Z2 de `HH:x0:54` à `HH:x1:18`,
Z3 de `HH:x1:18` à `HH:x1:42` (borne de fin exclue). Une modification locale
de consigne ou de mode déclenche aussi un envoi direct sans attendre le créneau,
sans décaler le calendrier périodique. Après l'échec de cet échange direct,
les prochains essais attendent le créneau de la zone.
Les réessais périodiques sont autorisés dans ce créneau,
puis reportés au cycle suivant ; après un échange réussi, la zone attend aussi
le prochain cycle. Une marge de sécurité en fin de fenêtre évite de déborder
sur la zone suivante. La mise à l'heure initiale se fait également dans le
créneau ; avant synchronisation, le cycle utilise le temps depuis le démarrage.

### 🧩 Intégration Home Assistant (MQTT Discovery)
- Découverte automatique de tous les capteurs et entités :
  - Capteurs de température, de consommation, d’état de zones
  - Sélecteurs de modes
  - Commandes de consigne, interrupteurs Boost/écrasement et boutons de réessai des commandes Connect
- Les associations se font depuis le portail web ; aucun bouton d'association n'est publié dans Home Assistant.
- Entités découvertes après connexion au broker MQTT, si l'intégration MQTT de Home Assistant est configurée et sa découverte activée. Les mesures restent inconnues jusqu'à leur réception.
- **État chaudière** regroupe l'activité : Chauffage, Chauffage ECS ou Veille (Inconnu si les données manquent). Cet état utilise uniquement les indicateurs de fonctionnement et le mode reçus par radio. **Anomalie chaudière** reste un capteur binaire indépendant ; une anomalie ne remplace plus l'activité affichée. L'ancien capteur « Mode de fonctionnement » est retiré de la découverte MQTT. Adapter les automatisations qui utilisaient ce capteur ou l'état « Fonctionnement ».

Avec les seuls satellites, lorsque le mode détaillé chaudière manque, l'état peut
indiquer **Fonctionnement** sans distinguer chauffage et ECS.

- Le thermostat propose **Auto**, **Dérogation Confort**, **Dérogation Réduit**, **Confort**, **Réduit**, **Hors Gel** et **Boost**. Auto et ses dérogations sont affichés en mode HVAC `auto` ; les régimes permanents et Boost en `heat`. Hors Gel conserve la protection et ne correspond pas à un arrêt.
- Le capteur **Mode actif** conserve le détail du mode, y compris en écoute seule. Le capteur **Régime actif** indique Confort, Réduit ou Hors Gel. Boost applique une seule fois le supplément configuré à la consigne Confort, dans la limite de 30 °C. Modifier la consigne du thermostat ajuste le profil actif ; en Boost, la consigne affichée inclut déjà le supplément.
- Pour un satellite physique, sans Connect actif ni écrasement radio activé, le thermostat reste en lecture seule. Pour un satellite physique ou le Connect, un mode demandé est publié après retour radio ou réponse réussie ; le satellite virtuel publie son état local.
- Avec un satellite virtuel, définir le programme depuis **Programmation** (`/programming`). Auto nécessite un programme connu et l'heure chaudière synchronisée. Les dérogations locales prennent fin au prochain changement Confort/Réduit ; un programme constant ne permet pas de dérogation temporaire. Leur échéance est conservée au redémarrage. Choisir Auto ou modifier le programme local annule la dérogation. Avec le Connect, la gestion du programme et de la fin de dérogation reste confiée au système distant.
- Les topics `zN/mode/set` acceptent les commandes `Confort` et `Réduit`, ainsi que les anciens noms `Confort permanent` et `Réduit permanent` pour compatibilité. Les états publiés utilisent les libellés `Confort` et `Réduit`.

### Suivre une modification de zone dans Home Assistant

Avec un Connect actif, le thermostat affiche la dernière consigne confirmée. Le capteur **Suivi de commande Z1** indique si la modification est en attente et le bouton **Réessayer la commande Z1** permet de la relancer. Ces deux entités MQTT peuvent être affichées sur une carte Entités standard.
Les mêmes entités existent pour Z2 et Z3 lorsqu'elles sont pilotées par le Connect actif.

### Écran et bouton du Heltec V3

Le firmware Heltec V3 détecte automatiquement l’OLED 128 × 64 à l’adresse I²C
0x3C. Les cartes sans écran continuent de fonctionner normalement. Cette fonction
est réservée au V3 ; elle n’active pas l’écran des autres modèles.

Les pages présentent l’heure et l’état général, chaque zone activée (ambiance,
consigne, mode et Boost), les mesures chaudière, puis l’adresse IP du portail,
le Wi-Fi, MQTT et l’âge de la dernière trame radio valide. Une valeur inconnue
est affichée `--`. L’indication « Radio ancienne » apparaît après 15 minutes
sans trame valide ; elle concerne la réception globale, pas l’âge individuel
de chaque mesure. L’écran n’ajoute aucune requête radio.

- **PRG, écran éteint** : réveiller la dernière page, sans changer de page.
- **PRG, appui court** : page suivante (après le délai de double appui).
- **PRG, double appui** : retour à l’accueil.
- **PRG, appui long de 2 secondes** : activer/arrêter le défilement toutes les
  8 secondes. Ce choix temporaire ne provoque pas d’écriture en mémoire.
- **RST** : redémarrage matériel ; ce bouton ne sert pas à naviguer.

Dans **Paramètres → Système → Écran Heltec V3**, régler l’activation, la
luminosité, la rotation à 180°, le défilement au démarrage et l’extinction après
inactivité (60 secondes par défaut ; 0 pour toujours allumé). Enregistrer et
redémarrer pour appliquer les réglages. L’extinction met seulement l’OLED en
veille : Wi-Fi, MQTT et radio restent actifs. Aucun bouton ne modifie la chauffe.

L’image est rafraîchie au plus une fois par seconde, par transferts courts
différés pendant les transactions radio. Les boutons sont traités dans la
boucle principale : une opération synchrone existante peut retarder leur prise
en compte. Un écran débranché ou défaillant est désactivé jusqu’au redémarrage.

### 🌐 Portail Web intégré

Le volet « Consommation quotidienne » conserve jusqu’à dix ans de relevés collectés
en Connect actif ou passif, consultables mois par mois même si Home Assistant est
hors ligne. Le téléchargement CSV couvre la même période. Les dates sont déduites
de l’heure chaudière ; les lectures sans heure connue ou pendant la première heure
après minuit ne sont pas archivées. La première date et les sauts d'horloge doivent
être corroborés par deux lectures espacées d'au moins 30 minutes. L'archive se
remplit à partir de l'installation de cette fonction : elle ne récupère pas les
jours antérieurs ou manqués. Un tiret indique une donnée manquante ; zéro est une
mesure valide. Le CSV conserve cette distinction avec des cellules vides.

Les fichiers sont conservés dans LittleFS, séparément pour chaque NetworkID et
identifiant chaudière. Aucun téléversement de système de fichiers n'est nécessaire :
le firmware initialise la partition si elle est entièrement vierge. Une partition
non vierge illisible est préservée et le portail affiche une erreur. L'archive
n'est pas réinjectée dans l'historique Home Assistant. Une horloge chaudière
durablement fausse peut dater les relevés à tort.

Le choix **Sonde Frisquet existante** est mémorisé séparément de **Désactivée**.
Il ne lance aucune émulation radio : la sonde reste gérée par la chaudière
ou son module H. Après une mise à jour depuis une version qui perdait ce choix,
sélectionnez-le et enregistrez-le à nouveau ; l'ancienne configuration ne permet
pas de retrouver cette sélection automatiquement.

Avec le **Connect virtuel actif**, le statut chaudière est lu directement en
`A02E` au démarrage et après chaque collecte de températures (minutes :03, :06
et :08 de chaque cycle de dix minutes). L'anomalie est donc actualisée sans
dépendre de l'écoute d'un satellite physique. Une réponse manquante ou mal
formée conserve l'état précédent ; tant qu'aucun état valide n'a été reçu,
l'anomalie reste inconnue. En Connect passif, aucune lecture n'est émise : une
lecture `A02E` observée peut fournir le même statut.

- Configuration du **WiFi** et du **MQTT**
- **Apparence du portail** : choix Clair, Sombre ou Auto dans **Paramètres → Système**. Auto utilise l’heure locale du navigateur : clair de 7 h à 20 h, sombre le reste du temps. Le choix est appliqué immédiatement sur toutes les pages, connexion comprise, et mémorisé par navigateur sans redémarrage de l’appareil.
- **Programmation hebdomadaire** : créneaux Confort/Réduit de 30 minutes, copie de journées et sauvegarde persistante pour les zones pilotées par un Connect actif ou un satellite virtuel. Accessible depuis le tableau de bord ou `/programming`.
- Visualisation des **logs**
- Informations système et réseau
- Envoi de trame radio personnalisée (debug)
- Lecture de zones mémoire (debug)

Les pages du portail se trouvent dans le dossier **`web/`**. Lors d'une compilation
PlatformIO, le script `tools/generate_web_assets.py` les transforme en ressources
C++ stockées dans le firmware : il y remplace le marqueur
`{{OPENFRISQUETVISIO_VERSION}}` par la version du firmware, c’est-à-dire la date
et l’heure de compilation au format `AA.MM.JJ.HHmm`, puis les compresse en gzip.
Cette version s’affiche en bas des pages.

Avec un Connect actif, la programmation est envoyée avec le bloc complet de
configuration de la zone, dans une fenêtre d'envoi disponible après observation
du satellite physique. Les paramètres de la zone doivent avoir été reçus et le
Connect doit être associé. Avec un satellite virtuel, la programmation est
modifiable et enregistrée localement. Les zones en Connect passif ou en satellite
physique sans Connect actif restent en lecture seule. Le programme règle les
périodes Confort/Réduit en mode Auto ; enregistrer la semaine ne change pas à lui
seul le mode de chauffage.

Les commandes de vérification sont décrites dans la section **Développement et tests**.

### 🔁 Mise à jour OTA
- Portail : **Paramètres → Système → Mise à jour du firmware**.
- Choisir le fichier `firmware-<environnement>.bin` de la release correspondant exactement à la carte, ou le `firmware.bin` de sa compilation locale.
- ArduinoOTA (port 3232) utilise le mot de passe du portail. Changez `admin`
  pour protéger les deux accès.

L'OTA met à jour uniquement l'application. Elle suppose qu'un premier flash USB
a installé le bootloader et une table de partitions compatible avec l'OTA.
Un changement de table de partitions nécessite un nouveau téléversement USB.

Téléchargez [`espota.py`](https://github.com/espressif/arduino-esp32/blob/master/tools/espota.py)
avec le bouton **Download raw file**, puis lancez-le depuis un terminal.
Le script demande le mot de passe du portail :

```bash
python3 /chemin/vers/espota.py -i 192.168.1.184 -f .pio/build/heltec_wifi_lora_32_V3/firmware.bin
```

Adaptez le chemin, l’adresse et l’environnement de compilation.
`pio run -t upload` utilise le script fourni par PlatformIO ; les anciennes
versions ne proposent pas cette saisie du mot de passe.

---

## 🧰 Matériel nécessaire

| Composant | Description | Remarques |
|------------|-------------|-----------|
| 🧠 Heltec ESP32 WiFi LoRa 32 V3/V4/V4.3 (SX1262) | Carte principale | Choisir l'environnement correspondant à la révision |
| 🧠 LilyGo/TTGO LoRa32 V2.1 (SX1276) | Alternative | Version **868 MHz** |
| 🌡️ DS18B20 (optionnel) | Sonde de température | DATA sur GPIO **33** sur Heltec, GPIO **13** sur LilyGo ; VDD sur 3,3 V et GND commun |
| 🔩 Résistance 4,7 kΩ | Pull-up pour DS18B20 | Entre **3,3 V** et **DATA** |
| ☕ Café | Indispensable | Pour le développeur 😁 |

---

## ⚙️ Configuration et installation

### 1️⃣ Préparation du firmware

Installer [PlatformIO IDE pour VS Code](https://docs.platformio.org/en/stable/integration/ide/vscode.html)
ou PlatformIO Core, puis ouvrir le dossier contenant [platformio.ini](platformio.ini).
Les commandes `pio` ci-dessous s'exécutent dans le terminal PlatformIO, à la racine
du dépôt. PlatformIO installe les dépendances lors de la première compilation.

Choisir l'environnement adapté à la carte :

| Carte | Environnement PlatformIO |
|---|---|
| Heltec V3 | `heltec_wifi_lora_32_V3` (par défaut) |
| Heltec V4 | `heltec_wifi_lora_32_V4` |
| Heltec V4.3, amplificateur KCT8103L | `heltec_wifi_lora_32_V4_3` |
| LilyGo/TTGO LoRa32 V2.1, SX1276 868 MHz | `ttgo_lora32_v21` |

La V4.3 réutilise la définition de carte V4 et active le KCT8103L par ses options
de compilation. Les modèles LilyGo autres que celui indiqué ne sont pas
interchangeables. Les brochages sont définis dans [include/Board.h](include/Board.h).
Il n'est normalement pas nécessaire de modifier ce fichier ; seule une adaptation
de câblage, par exemple le GPIO de la DS18B20, le justifie.

| Option | Description |
|---------|-------------|
| `DS18B20_PIN` | GPIO utilisé (33 sur Heltec, 13 sur LilyGo) |

---

### 2️⃣ Flash du firmware

**Premier flash avec PlatformIO par USB — méthode recommandée**

1. Connecter la carte avec un câble USB permettant le transfert de données.
2. Repérer le port avec `pio device list` : par exemple `COM3` sous Windows,
   `/dev/ttyUSB0` sous Linux ou `/dev/cu.usbserial-…` sous macOS.
3. Pour les Heltec, le fichier fourni est configuré pour l'OTA. Dans la section
   de l'environnement choisi, ajouter les trois options ci-dessous, en conservant
   sa ligne `board` et ses éventuelles options de partitions/compilation :

   ```ini
   upload_protocol = esptool
   upload_port = COM3
   upload_flags =
   ```

   Remplacer `COM3` par le port réel. La ligne `upload_flags =` vide supprime les
   options OTA héritées de `[env]`. L'environnement LilyGo utilise déjà `esptool` ;
   renseigner simplement son port USB si la détection automatique ne suffit pas.
4. Pour une Heltec V3, lancer :

   ```sh
   pio run -e heltec_wifi_lora_32_V3 -t upload
   ```

   Adapter le nom de l'environnement pour une V4, V4.3 ou LilyGo. Dans VS Code,
   la tâche **Upload** du même environnement réalise cette opération.

**PlatformIO écrit automatiquement tous les éléments nécessaires** : bootloader,
table de partitions, initialisation OTA (`boot_app0.bin`) et application. Il n'y a
aucune partition à créer ou à flasher séparément, ni de tâche `uploadfs` à lancer.
Le portail web est intégré dans l'application ; LittleFS sert à l'historique quotidien.

Les V4 et V4.3 utilisent explicitement `default_16MB.csv`, avec deux partitions
applicatives pour l'OTA. Les autres environnements utilisent la table prévue pour
leur carte. Conserver cette table lors des mises à jour OTA. Un effacement complet
n'est pas nécessaire pour une carte vierge ; s'il est effectué sur une carte déjà
configurée, il supprime aussi les paramètres, associations et archives locales.

Au premier démarrage sans Wi-Fi configuré, le module crée un point d'accès de
configuration. Après configuration, l'accès se fait à l'adresse IP attribuée par
le réseau local ; le point d'accès n'est pas maintenu en permanence.

**Flash avec un outil externe après compilation**

Le `firmware.bin` seul n'est pas une image complète pour une carte vierge. Avec
la configuration Arduino actuelle, les éléments sont :

| Élément | Fichier | Adresse Heltec V3/V4/V4.3 (ESP32-S3) | Adresse LilyGo (ESP32) |
|---|---|---|---|
| Bootloader | `.pio/build/<environnement>/bootloader.bin` | `0x0000` | `0x1000` |
| Table de partitions | `.pio/build/<environnement>/partitions.bin` | `0x8000` | `0x8000` |
| Initialisation OTA | `boot_app0.bin` du framework Arduino | `0xE000` | `0xE000` |
| Application | `.pio/build/<environnement>/firmware.bin` | `0x10000` | `0x10000` |

`boot_app0.bin` se trouve habituellement dans
`~/.platformio/packages/framework-arduinoespressif32/tools/partitions/`, et non
dans le dossier de compilation du projet. Utiliser les fichiers du même
environnement et reprendre les chemins et adresses de la commande affichée par
`pio run -e <environnement> -t upload -v` : cette commande effectue réellement le
téléversement. Le journal détaillé fait référence si le framework ou les
partitions changent. Voir la [procédure Espressif pour PlatformIO](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/flashing-firmware.html#platformio).

La CI compile les quatre variantes sur les PR et les pushes vers `main` et
`develop`. Les compilations Heltec V3, V4, V4.3 et LilyGo LoRa32 V2.1 ont chacune
leur propre contrôle GitHub, dans une même matrice. Les tests Python et Node
utilisent un contrôle séparé.

Après un push sur `main`, la CI publie automatiquement une Release si les tests
et les quatre compilations réussissent. Les PR et les pushes sur `develop` ne
publient pas de Release. Le titre, le tag et les quatre firmwares utilisent une
version commune au format `AA.MM.JJ.HHmm`, calculée depuis la date de création du
workflow à l'heure de Paris. Une relance réutilise la même version ; en cas de
collision avec un tag existant, la minute est avancée pour ne rien écraser.
Le tag pointe vers le commit exact compilé.

**Changelog automatique**

Chaque PR ajoute des puces dans **À venir**, avant la première version de
[CHANGELOG.md](CHANGELOG.md), avec un lien vers la PR ou l'issue. Les catégories
sont **Ajouts**, **Corrections**, **Changements** et **Actions nécessaires après
mise à jour**. Aucune commande de préparation de release n'est nécessaire.

Après validation, la CI sélectionne les nouvelles notes par rapport à la dernière
release de cette branche. Elle les place dans les notes de release et ajoute
l'entrée versionnée à l'historique précédent. Le `CHANGELOG.md` généré est joint
aux binaires : [télécharger l'historique publié](https://github.com/FreedomNX/OpenFrisquetVisio/releases/latest/download/CHANGELOG.md).
Le fichier `changelog-source.md`, également joint, conserve la source utilisée pour
éviter de republier les mêmes notes. Une version sans nouvelle note est tout de
même publiée avec une indication explicite.

Le fichier du dépôt présente les versions publiées de la plus récente à la plus
ancienne. Pour le mettre à jour avec la dernière release, sans perdre les notes
locales encore inédites :

```sh
python3 tools/changelog.py sync
```

Cette commande télécharge l'historique et sa source depuis GitHub, remplace les
notes déjà publiées par leur entrée versionnée et conserve **À venir** uniquement
s'il reste des nouveautés. Les explications sont dans ce README ; le changelog
contient uniquement les changements et leurs versions. Relire puis committer sa
mise à jour avec les prochains changements du projet. Une version locale modifiée
différemment de l'archive publiée est signalée et préservée.

La CI continue à joindre l'historique actualisé aux releases sans écrire sur
`main`, qui reste protégée. Le fichier local ne s'actualise donc pas à distance
sans cette synchronisation. Les catégories vides sont omises de l'historique.
Les anciennes releases ne sont pas reconstruites rétrospectivement.

Les publications sur `main` sont sérialisées pour reprendre le dernier historique
publié. Si plusieurs pushes arrivent pendant une compilation, GitHub peut
regrouper les exécutions en attente ; la prochaine release inclut les notes des
changements accumulés. Les sept fichiers (quatre firmwares, `SHA256SUMS`,
`CHANGELOG.md` et `changelog-source.md`) sont joints à un brouillon avant de le
rendre public. En cas d'échec, le brouillon peut être repris par une relance sur
le même commit. Une release déjà publiée n'est pas écrasée.

Pour vérifier les notes ou prévisualiser un historique sans publication :

```sh
python3 tools/changelog.py check
python3 tools/changelog.py archive 26.11.24.0955 --output /tmp/CHANGELOG-preview.md
```

La prévisualisation locale ne modifie pas la source et n'écrit rien sur GitHub.
Les firmwares de release et leur historique ne sont publiés que par la CI, après
validation des tests et des quatre cartes.

Les fichiers sont accessibles dans [Releases](https://github.com/FreedomNX/OpenFrisquetVisio/releases) :

| Carte | Binaire |
|---|---|
| Heltec V3 | `firmware-heltec_wifi_lora_32_V3.bin` |
| Heltec V4 | `firmware-heltec_wifi_lora_32_V4.bin` |
| Heltec V4.3 | `firmware-heltec_wifi_lora_32_V4_3.bin` |
| LilyGo LoRa32 V2.1 | `firmware-ttgo_lora32_v21.bin` |

Le fichier `SHA256SUMS` accompagne les binaires. Ils sont destinés aux mises à
jour applicatives (OTA) ; ils ne contiennent ni le bootloader ni la table de
partitions ni `boot_app0.bin` nécessaires pour un premier flash USB. Pour une
carte vierge, utiliser la procédure PlatformIO ci-dessus.

---

### Variante LilyGo/TTGO LoRa32 V2.1 (SX1276)

Le support reprend le brochage de la [PR #20 de Julien Hammer](https://github.com/FreedomNX/OpenFrisquetVisio/pull/20), adapté à RadioLib 7.8.1 et à la gestion actuelle de réception. Il cible la LoRa32 V2.1 avec **SX1276 en 868 MHz** ; les autres cartes LilyGo peuvent avoir un brochage différent.

L'environnement `ttgo_lora32_v21` est défini dans [platformio.ini](platformio.ini).
Il partage les réglages communs et remplace la carte, les dépendances Heltec et
le mode de téléversement. Il utilise un partitionnement avec OTA et un premier
flash USB. La Heltec V3 reste l'environnement par défaut :

```sh
pio run -e ttgo_lora32_v21
pio run -e ttgo_lora32_v21 -t upload
```

| Signal | GPIO LilyGo |
|---|---|
| SPI SCK / MISO / MOSI | 5 / 19 / 27 |
| Radio NSS / RESET | 18 / 23 |
| Radio DIO0 / DIO1 | 26 / 33 |
| DS18B20 | 13 (microSD inutilisée) |

La bande RX par défaut est **250 kHz** sur SX1276 ; les paramètres du portail proposent les bandes propres à cette puce. Le journal contient au maximum **80 lignes**, et le tampon d'affichage est limité à cette même capacité pour conserver une marge de RAM statique sur l'ESP32 classique.

La FIFO FSK du SX1276 ne contient que **64 octets**. La réception utilise donc le format variable : un octet de longueur et jusqu'à **63 octets de données**, avec validation matérielle du CRC CCITT (init `0x1D0F`, polynôme `0x1021`, complémenté). Les trames CRC invalides sont journalisées et rejetées. Le CRC reçu est retiré par la puce : la capture brute de 68 octets et la réparation logicielle du SX1262 ne sont pas disponibles sur cette variante. Voir la [documentation Semtech, sections FIFO et CRC](https://cdn.sparkfun.com/assets/7/7/3/2/2/SX1276_Datasheet.pdf).

Compilation et tests du traitement FIFO validés ; association et échanges radio à confirmer sur une carte réelle. Les configurations Heltec conservent la réception brute et la réparation CRC.

---

### 3️⃣ Configuration via le portail web

1. Se connecter au WiFi créé : `OpenFrisquetVisio-Setup`, mot de passe `frisquetconfig`
2. Ouvrir un navigateur sur `http://192.168.4.1`
3. Saisir le mot de passe du portail : `admin` par défaut, sans identifiant
4. Renseigner :
   - Vos **informations WiFi**
   - Vos **informations MQTT**
   - Les **modules et zones à utiliser**, dans **Paramètres → Chaudière**
   - **Connect physique — écoute passive** si une box Connect est déjà présente
5. Sauvegarder → le module redémarre automatiquement

Les boutons d'association ne fonctionnent que pour les modes déjà enregistrés.
Choisir d'abord les modes, cliquer sur **Enregistrer et redémarrer**, puis revenir
sur le portail à son adresse IP sur le réseau local. Le moniteur série
(`pio device monitor --port COM3 -b 115200`, avec le port USB réel), la liste des clients du routeur et l'écran du
Heltec V3 permettent de retrouver cette adresse.

L’enregistrement est refusé, avec un message indiquant le champ à corriger, si :
le hostname ne fait pas 1 à 31 caractères (lettres, chiffres et tirets, sans tiret
au début ni à la fin), le SSID dépasse 32 octets, le mot de passe Wi-Fi ne fait pas
8 à 63 caractères (ou 64 chiffres hexadécimaux), ou le préfixe MQTT contient `+`
ou `#`.

Le mot de passe du portail se modifie dans **Paramètres → Système → Accès au portail**.
Renseigner le nouveau mot de passe et sa confirmation, puis enregistrer.
Laisser les deux champs vides conserve le mot de passe actuel. Il est sauvegardé
sur l’appareil et conservé après redémarrage. Les pages et les API du portail
nécessitent une connexion ; les sessions expirent après 12 heures ou au
redémarrage, et un changement de mot de passe déconnecte toutes les sessions.
Une page restée ouverte revient alors d’elle-même à la page de connexion.
Les pages de programmation et de paramètres restent ouvertes si elles contiennent
des modifications non enregistrées : elles proposent de se reconnecter dans un
nouvel onglet, puis de revenir les enregistrer.
Le bouton **Se déconnecter** se trouve dans la même section.

Les mots de passe WiFi et MQTT ne sont jamais réaffichés : le portail indique
seulement qu’un mot de passe est enregistré. Laisser le champ vide le conserve ;
cocher **Effacer le mot de passe enregistré** le supprime (réseau ouvert, broker
sans mot de passe).

---

### 4️⃣ Association avec la chaudière

Toutes les associations se lancent dans **Paramètres → Chaudière** du portail.
Il n'y a plus de boutons d'association dans Home Assistant. Les boutons du portail
apparaissent selon le mode choisi ; enregistrer ce mode et attendre le redémarrage
avant de les utiliser.

| Choix dans le portail | Association de l'ESP32 | Comportement |
|---|---|---|
| **Connect virtuel** | **Associer le Connect** | Émule la box Connect ; ne pas l'utiliser simultanément avec une box physique active |
| **Connect physique — écoute passive** | Aucune ; renseigner ou récupérer le NetworkID | Observe les échanges de la box déjà associée |
| Zone **Satellite physique** | Aucune nouvelle association du satellite existant ; renseigner ou récupérer le NetworkID | Observe le satellite de la zone ; avec Connect actif, les mesures/commandes de la zone passent par le Connect |
| Zone **Satellite virtuel** | **Associer Z1**, **Associer Z2** ou **Associer Z3** | Remplace le satellite de cette zone |
| **Sonde virtuelle** ou **Capteur DS18B20** | **Associer la sonde** | Émet la température extérieure à la place d'une sonde Frisquet |
| **Sonde Frisquet existante** | Aucune | Laisse la sonde existante fonctionner ; pas d'émulation ni d'écoute directe de ses trames |

L'identifiant radio chaudière (`0x80` ou `0x84`) est détecté automatiquement et
mémorisé avec la configuration : aucune constante à modifier ni recompilation
spécifique n'est nécessaire. L'annonce d'association fournit directement cet
identifiant. Sur une installation déjà associée, le Connect actif et les
satellites virtuels lisent l'heure (`0xA02B`) à l'initialisation (dans leur premier
créneau pour les satellites) : ils essaient d'abord
l'identifiant mémorisé (`0x80` par défaut), puis l'autre si nécessaire. Seule une
réponse correspondant à la requête et contenant une date valide est retenue.
Sans réponse valide, la valeur précédente est conservée.

En écoute passive, l'identifiant est appris à partir d'une réponse correspondant
à une requête observée du Connect physique ou d'un satellite physique, sans
émettre de requête de détection. La valeur détectée est partagée entre les
modules et ajuste également le décalage mémoire `0xC8` du Connect pour `0x84`.

#### 🔹 Connect virtuel actif
1. Sur la chaudière : **lancer l’association Connect**  
  a. Accédez au menu de configuration
  b. aller dans "partenaire" puis sur "Ajouter"
  c. Lancez l'association Frisquet Connect. Appuyez sur OK jusqu’à ce que l’écran demande d’associer la Frisquet Connect.
2. Sur le portail, dans **Paramètres → Chaudière**, cliquer sur **Associer le Connect**.
3. Attendre le résultat de l'association. Le Connect virtuel commence ensuite ses lectures de la chaudière. Les réglages des zones sont complétés par les échanges avec les satellites physiques.

Les intitulés du menu chaudière peuvent varier selon le modèle. Si les réglages
d'une zone restent inconnus, changer une consigne sur son satellite physique peut
provoquer l'échange de configuration attendu.

#### 🔹 Mode passif Connect (box Connect déjà en place)
1. Dans le portail, choisir **Connect physique — écoute passive**, puis enregistrer.
2. Renseigner le NetworkID de l'installation ou utiliser **Récupérer** comme décrit ci-dessous. Ne pas cliquer sur une association Connect et ne pas remplacer la box existante.
3. L'émulation Connect n'émet aucune requête. Elle récupère les informations en écoutant les échanges de la box physique ; chaque mesure dépend des lectures captées.

Ce choix ne rend pas tous les modules silencieux : une sonde virtuelle, un
satellite virtuel ou l'écrasement d'un satellite physique peuvent émettre.
Pour une installation en écoute seule, laisser ces fonctions désactivées.

#### 🔹 Satellites des zones 1, 2 et 3

Pour conserver un satellite existant, choisir **Satellite physique** pour sa zone,
enregistrer et renseigner le NetworkID. Il n'est pas nécessaire de réassocier ce
satellite. Sans Connect actif, son ambiance, sa consigne et son mode sont appris
en observant les échanges radio ; un changement de consigne aide à les provoquer.

Pour remplacer un satellite par l'ESP32 :

1. Choisir **Satellite virtuel** pour la zone concernée, puis enregistrer et attendre le redémarrage.
2. Lancer sur la chaudière l'association ou le remplacement du satellite de cette même zone.
3. Dans le portail, cliquer sur **Associer Z1**, **Associer Z2** ou **Associer Z3** selon la zone.
4. Fournir la température ambiante via l'entité numérique **Température ambiante ZN** de Home Assistant ou via MQTT : `frisquet/zN/temperatureAmbiante/set`, avec une valeur comme `20.5` (0 à 50 °C). Remplacer `N` par `1`, `2` ou `3` et adapter le topic de base.

Définir aussi une consigne. Le mode Auto nécessite un programme hebdomadaire connu
et l'heure chaudière synchronisée. Un satellite virtuel ne mesure pas lui-même
la température ambiante.

#### 🔹 Sonde extérieure

1. Dans le portail, choisir **Sonde virtuelle** (MQTT) ou **Capteur DS18B20**, puis enregistrer et attendre le redémarrage.
2. Sur la chaudière : lancer l'association de la sonde extérieure.
3. Dans le portail, cliquer sur **Associer la sonde**.

Le mode **Capteur DS18B20** doit être sélectionné pour utiliser une sonde branchée :
sa présence seule ne l'active pas. En **Sonde virtuelle**, renseigner la température
dans l'entité numérique **Température extérieure** de Home Assistant ou publier
une valeur décimale comme `7.7` sur
`frisquet/sondeExterieure/temperatureExterieure/set`. Adapter `frisquet` si le topic
de base MQTT a été changé. Sans température disponible, aucun envoi de température
n'est effectué. L'association est réservée à l'émulation actuelle en `0x20` ;
le choix **Sonde Frisquet existante** ne doit pas être associé par cette procédure.

#### 🔹 Récupération du NetworkID (sans association)
1. Sur la chaudière, lancez une **association** :
   - soit une association Connect (ou remplacement)
   - soit **Remplacer Satellite Z1**
2. Sur le portail, cliquez sur **“Récupérer”** à côté du champ NetworkID.
3. Une fois le NetworkID affiché, **annulez l’action** côté chaudière.

Cette opération écoute l'annonce d'association sans la confirmer : elle ne crée
pas d'association pour l'ESP32. Ne pas laisser la chaudière terminer un
remplacement d'appareil existant. Le bouton **Récupérer** n'est pas un bouton
d'association.

---

## 🔄 Fonctionnement général

1. Le module se connecte au **WiFi** et au **broker MQTT**
2. Il échange ou observe les trames **FSK** de l'installation via le SX1262 ou le SX1276, selon les modes activés
3. Il **publie** les mesures et états via MQTT
4. Home Assistant les découvre automatiquement via **MQTT Discovery**
5. Les commandes de modes et de consignes envoyées depuis HA sont traduites en
   trames radio lorsque le mode choisi autorise le pilotage. Les associations
   se font exclusivement depuis le portail web.
6. Les infos satellites hors Connect (consigne boost) envoyées depuis HA  
   sont traduites en trames radio vers la chaudière par écrasement (non visible sur Satellites originaux)

---

## 🔧 Dépannage

| Problème | Cause possible | Solution |
|-----------|----------------|----------|
| Premier flash USB impossible | Configuration OTA encore active, mauvais port ou câble sans données | Choisir le bon environnement, `upload_protocol = esptool`, le port USB réel et `upload_flags =` vide. Vérifier `pio device list`. |
| Carte vierge ne démarrant pas après flash d'un `.bin` de release | Seule l'application a été écrite | Effectuer le téléversement USB PlatformIO complet, qui installe aussi le bootloader et les partitions. |
| Aucun bouton d'association dans Home Assistant | Les associations ne sont plus exposées en MQTT | Utiliser **Paramètres → Chaudière** du portail après avoir enregistré le mode souhaité. |
| Paramètres de zone inconnus avec un Connect virtuel | Configuration du satellite pas encore reçue | Vérifier l'association Connect et le NetworkID, puis changer une consigne sur chaque satellite physique concerné. |
| Pas de données en Connect passif ou satellite physique | NetworkID absent/incorrect ou échanges non captés | Récupérer le NetworkID sans réassocier l'appareil existant, vérifier les zones activées et consulter les journaux radio. |
| Pas de point d'accès de configuration | Un réseau Wi-Fi est déjà configuré | Chercher l'IP dans le routeur, le moniteur série ou l'écran V3. Après 5 minutes sans Wi-Fi, le point d'accès de secours s'ouvre ; après 10 minutes, la carte redémarre. |
| Session expirée | Redémarrage, expiration ou changement de mot de passe | Se reconnecter au portail. Si un brouillon est conservé, suivre le lien de reconnexion dans un nouvel onglet. |
| Historique quotidien vide | Heure non confirmée, premières lectures ou journée manquée | Attendre au moins deux lectures espacées de 30 minutes pour confirmer la première date, hors de la première heure après minuit. Les jours manqués ne sont pas reconstitués. |
| Erreur de partition de l'historique | Partition non vierge illisible ou montage impossible | Consulter le motif affiché. Le firmware préserve le contenu et ne reformate pas automatiquement une partition non vierge. |

Les journaux sont accessibles dans **Paramètres → Système → Diagnostic**.
Activer et enregistrer le **Mode développeur** pour accéder aux trames radio et à
la mémoire chaudière. La lecture mémoire est un outil actif : elle peut émettre
des requêtes, même si le Connect est configuré en écoute passive.

---

## Développement et tests

Depuis la racine du dépôt, avec Python 3, Node.js et un compilateur C++17 disponible
sous le nom `c++` (GCC sous Linux, comme dans la CI) :

```sh
pio pkg install -e heltec_wifi_lora_32_V3
node --test tests/*.test.mjs
for test in tests/test_*.py; do python3 "$test" || exit 1; done
```

La boucle Python ci-dessus utilise un shell POSIX (Linux, macOS ou Git Bash/WSL).
Les scénarios natifs s'exécutent sur l'ordinateur avec ASan/UBSan ; ils ne flashent
aucune carte. Les dépendances PlatformIO sont nécessaires à certains tests, dont
ceux qui compilent les classes de zone. Sous macOS, les en-têtes du SDK et les
diagnostics Clang peuvent nécessiter une adaptation locale ; la référence CI est
Ubuntu avec GCC.

Pour vérifier la programmation et les modes de zone en particulier :

```sh
python3 tests/test_zone_modes.py
python3 tests/test_satellite_observation.py
```

Les scénarios de consommation et de radio sont décrits dans leurs dossiers :
[consommations mensuelles](tests/fixtures/monthly-consumption/README.md),
[radio coopérative](tests/fixtures/cooperative-radio/README.md) et
[observations satellite](tests/fixtures/satellite-observation/README.md).
Une compilation locale met à jour `src/Version.h` et la ligne de version du
README ; le portail affiche cette version de compilation, pas une version fixe
à recopier manuellement.

---

## 🧑‍💻 Crédits

Projet développé pour émuler le **Frisquet Connect** avec compatibilité Home Assistant.  
- Firmware basé sur **Arduino / PlatformIO**
- Utilise **RadioLib** (SX1262 et SX1276 en FSK)
- Intégration **MQTT + Auto-discovery HA**

---

## ☕ Licence

Projet open-source à but expérimental, sous [licence GPL v3](LICENSE).
Utilisation à vos risques et périls — aucune affiliation avec Frisquet.  
Mais bon, si ça marche, vous pouvez toujours m’offrir un café ☕ 😉
[!["Buy Me A Coffee"](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/freedomnx)

---

**Auteur :** FreedomNX  
**Année :** 2025  
**Plateforme :** ESP32 (Heltec V3/V4/V4.3, SX1262 ; LilyGo LoRa32 V2.1, SX1276)\
**Compatibilité :** Home Assistant, MQTT, Frisquet Chaudière série Eco Radio Visio  
**Version :** 26.11.24.0955
