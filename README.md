# 🧠 OpenFrisquetVisio

Emulation complète du module **Frisquet Connect**, **sonde Extérieure** ou encore **satellite d'ambiance** sur base **Heltec ESP32 WiFi LoRa 32 (SX1262)** ou **TTGO / LilyGO LoRa32 V2.1 (SX1276)**.  
Ce projet permet à un ESP32 de dialoguer directement avec votre chaudière Frisquet,  
de récupérer les informations en temps réel et de les exposer à **Home Assistant via MQTT**.

---

## 🚀 Fonctionnalités principales

### 🔧 Emulation Frisquet Connect
- Récupération des informations chaudière :
  - Température **ECS (eau chaude sanitaire)**
  - Température **corps de chauffe (CDC)**
  - Température **extérieure**
  - **Pression** du circuit (bar)
  - **Consommation gaz** ECS et chauffage (veille)
  - **Affichage et séléction du mode ECS** (Max, Eco, Eco Horaires Eco+, Eco+ Horaires, Stop)
- **Mode Connect passif** : écoute des échanges chaudière sans émettre de trame (utile si une box Connect est déjà installée).
- Gestion des **zones 1, 2 et 3** :
  - Température **départ eau**
  - Température **ambiante** (si source Connect)
  - Température **consigne**
  - **Configuration des consignes** pour chaque mode :
    - Réduit
    - Hors gel
    - Confort
  - **Sélection du mode actif** (Auto, Confort, Réduit, Hors Gel)

### 🌡️ Emulation de la sonde extérieure
Deux modes possibles :
- Lecture **réelle** via une sonde **DS18B20**
- Lecture **virtuelle** via **MQTT**, permettant d’utiliser la température issue de la météo via un capteur HA.

### Gestion des satellites (utile si chaudière non-compatible Connect)
- Gestion des **zones 1, 2 et 3** :
  - Température **ambiance**
  - Température **consigne**
  - **Configuration d'un boost consigne** (écrasement de la consigne envoyé par le satellite)
  - **Affichage du mode actif** (Auto, Confort, Réduit, Hors Gel)

### 🧩 Intégration Home Assistant (MQTT Discovery)
- Découverte automatique de tous les capteurs et entités :
  - Capteurs de température, de consommation, d’état de zones
  - Sélecteurs de modes
  - Commandes de consigne et interrupteurs (Boost, associations, etc.)
- Entités disponibles immédiatement dans Home Assistant

### 🌐 Portail Web intégré
- Configuration du **WiFi** et du **MQTT**
- Visualisation des **logs**
- Informations système et réseau
- Envoi de trame radio personnalisée (debug)
- Lecture de zones mémoire (debug)

### 🔁 Mise à jour OTA
- Mise à jour du firmware directement via WiFi

---

## 🧰 Matériel nécessaire

| Composant | Description | Remarques |
|------------|-------------|-----------|
| 🧠 Heltec ESP32 WiFi LoRa 32 V3/V4 (SX1262) | Carte principale | Doit posséder le module **SX1262** |
| 🧠 *ou* TTGO / LilyGO LoRa32 V2.1 (SX1276) | Carte principale alternative | Version **868 MHz** obligatoire |
| 🌡️ DS18B20 (optionnel) | Sonde de température | GPIO **33** (Heltec) / GPIO **13** (TTGO) |
| 🔩 Résistance 4,7 kΩ | Pull-up pour DS18B20 | Entre **VCC** et **DATA** |
| ☕ Café | Indispensable | Pour le développeur 😁 |

---

## ⚙️ Configuration et installation

### 1️⃣ Préparation du firmware

Avant le flash :
- Ouvrir le fichier **`include/Board.h`**  
  et vérifier / modifier les options de votre carte selon vos besoins :

| Option | Description |
|---------|-------------|
| `DS18B20_PIN` | GPIO de la sonde DS18B20 (33 sur Heltec, 13 sur TTGO) |
| `RADIO_*` | Brochage du module radio |

---

### 2️⃣ Flash du firmware

- Connecter la carte via **USB**  
- Compiler et téléverser avec **PlatformIO** ou **Arduino IDE**
  - Heltec V3 : environnement `heltec_wifi_lora_32_V3` (V4 : `heltec_wifi_lora_32_V4`)
  - TTGO LoRa32 V2.1 : environnement `ttgo_lora32_v21`, par ex. `pio run -e ttgo_lora32_v21 -t upload`

> ℹ️ **TTGO / SX1276** : la FIFO FSK du SX1276 est limitée à 64 octets ; les plus grandes trames Frisquet (63 octets) passent sans marge.
> L'ESP32 classique ayant moins de RAM statique, le journal en mémoire est limité à 120 lignes (300 sur Heltec).
- Au premier démarrage, le module crée un **point d’accès WiFi**

---

### 3️⃣ Configuration via le portail web

1. Se connecter au WiFi créé : `OpenFrisquetVisio-Setup`, mot de passe `frisquetconfig`
2. Ouvrir un navigateur sur `192.168.4.1`
3. Renseigner :
   - Vos **informations WiFi**
   - Vos **informations MQTT**
   - Vos **Les modules à utiliser**
   - **Mode passif Connect** si une box Connect est déjà présente
4. Sauvegarder → le module redémarre automatiquement

---

### 4️⃣ Association avec la chaudière

#### 🔹 Module Connect
1. Sur la chaudière : **lancer l’association Connect**  
  a. Accédez au menu de configuration
  b. aller dans "partenaire" puis sur "Ajouter"
  c. Lancez l'association Frisquet Connect. Appuyez sur OK jusqu’à ce que l’écran demande d’associer la Frisquet Connect.
2. Sur le portail ou Home Assistant : activer le bouton **“Associer Connect”**
3. Une fois reconnu, la chaudière commencera à envoyer les données vers le module

#### 🔹 Mode passif Connect (box Connect déjà en place)
1. Activer **“Mode passif Connect”** dans le portail.
2. L’ESP32 **n’émet pas** de trames et **n’associe pas**.
3. Il récupère les informations en **écoutant les réponses chaudière** aux requêtes de la box Connect.

#### 🔹 Sonde extérieure
1. Sur la chaudière : **lancer l’association Sonde Extérieure**
2. Sur le portail ou Home Assistant : activer le bouton **“Associer Sonde Extérieure”**

Si une **DS18B20** est branchée, la température sera lue localement.
Sinon, envoyez la température via MQTT (ex. depuis un capteur météo HA), soit en modifiant l'entité sur HA, soit en publiant sur le topic **“"frisquet/sondeExterieure/temperatureExterieure/set"”**. (si Base Topic est toujours par défault à "frisquet" )

#### 🔹 Récupération du NetworkID (sans association)
1. Sur la chaudière, lancez une **association** :
   - soit une association Connect (ou remplacement)
   - soit **Remplacer Satellite Z1**
2. Sur le portail, cliquez sur **“Récupérer”** à côté du champ NetworkID.
3. Une fois le NetworkID affiché, **annulez l’action** côté chaudière.

---

## 🔄 Fonctionnement général

1. Le module se connecte au **WiFi** et au **broker MQTT**
2. Il **écoute** les trames radio LoRa de la chaudière (mode SX1262)
3. Il **publie** les mesures et états via MQTT
4. Home Assistant les découvre automatiquement via **MQTT Discovery**
5. Les commandes (modes, consignes, associations) envoyées depuis HA  
   sont traduites en trames radio vers la chaudière
6. Les infos satellites hors Connect (consigne boost) envoyées depuis HA  
   sont traduites en trames radio vers la chaudière par écrasement (non visible sur Satellites originaux)

---

## 🔧 Dépannage

| Problème | Cause possible | Solution |
|-----------|----------------|----------|
| Pas de données reçues sur Connect | Changer de consigne sur chacun des satellites de zone pour initialiser le Connect une première fois.

---

## 🧑‍💻 Crédits

Projet développé pour émuler le **Frisquet Connect** avec compatibilité Home Assistant.  
- Firmware basé sur **Arduino / PlatformIO**
- Utilise **RadioLib** (SX1262)
- Intégration **MQTT + Auto-discovery HA**

---

## ☕ Licence

Projet open-source à but expérimental.  
Utilisation à vos risques et périls — aucune affiliation avec Frisquet.  
Mais bon, si ça marche, vous pouvez toujours m’offrir un café ☕ 😉
[!["Buy Me A Coffee"](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/freedomnx)

---

**Auteur :** FreedomNX  
**Année :** 2025  
**Plateforme :** ESP32 (Heltec WiFi LoRa 32, SX1262)  
**Compatibilité :** Home Assistant, MQTT, Frisquet Chaudière série Eco Radio Visio  
**Version :** 26.08.14.1757
