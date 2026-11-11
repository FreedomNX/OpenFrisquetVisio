# Tests natifs de l’observation radio coopérative

`receive.cpp` exécute les méthodes du firmware avec une radio et une horloge
simulées : garde, tentatives, sélection, réservation, annulation et pauses.
Le lanceur extrait les méthodes de production et compile avec ASan/UBSan.

```sh
python3 tests/test_cooperative_receive.py
```

Les trames sont synthétiques ; aucun appareil ni service réseau n’est sollicité.
