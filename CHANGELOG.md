# Historique des changements

Chaque PR ajoute ici des notes courtes décrivant les changements pour l'utilisateur.
Ce fichier source garde les notes intégrées au code ; leur présence ne signifie pas
qu'une release est déjà disponible. Après validation de `main`, la CI les archive
automatiquement sous la version des firmwares, sans étape de préparation manuelle.

L'[historique versionné publié](https://github.com/FreedomNX/OpenFrisquetVisio/releases/latest/download/CHANGELOG.md)
est joint à chaque release. Les versions antérieures à ce système restent dans les
[releases GitHub](https://github.com/FreedomNX/OpenFrisquetVisio/releases).

## Notes intégrées à main

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

### Actions nécessaires après mise à jour

