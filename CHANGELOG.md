# Historique des changements

## À venir

### Corrections

- Les entités Home Assistant deviennent indisponibles lorsque la passerelle perd
  sa connexion MQTT, puis redeviennent disponibles à sa reconnexion
  ([#88](https://github.com/FreedomNX/OpenFrisquetVisio/pull/88)).

## 26.11.24.1425 — 2026-11-24

### Corrections

- Les outils mémoire n'utilisent plus l'identité de la box Connect passive ni celle
  d'un satellite Z1 physique ; l'émission brute au nom du Connect passif est refusée
  ([#77](https://github.com/FreedomNX/OpenFrisquetVisio/pull/77)).

### Changements

- Historique des versions consultable directement dans le changelog du dépôt,
  sans paragraphe d'explication en tête de fichier.

## 26.11.24.1222 — 2026-11-24

### Ajouts

- Changelog rédigé dans le dépôt et repris automatiquement dans les releases GitHub.

### Corrections

- Un refus radio `0x83` destiné à un autre échange n'interrompt plus la réception
  attendue ([#87](https://github.com/FreedomNX/OpenFrisquetVisio/pull/87)).
- Documentation du premier flash USB, des associations depuis le portail et des
  modes actif/passif ([#81](https://github.com/FreedomNX/OpenFrisquetVisio/issues/81)).
- Clarification de la programmation des satellites virtuels et mise à jour des
  commandes de test et du dépannage.

### Changements

- Publication automatique après validation de `main` : l'historique versionné est
  joint à la release, sans préparation manuelle ni écriture du robot sur `main`.
- Détection commune des paquets radio en attente, pour préserver les mêmes gardes
  avant les émissions, l'affichage et les écritures d'historique
  ([#86](https://github.com/FreedomNX/OpenFrisquetVisio/pull/86)).
