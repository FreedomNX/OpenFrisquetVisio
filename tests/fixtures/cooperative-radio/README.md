# Tests natifs de l'observation radio coopérative

Les scénarios C++ utilisent une radio et une horloge simulées. Les lanceurs
Python extraient les méthodes du firmware, puis compilent et exécutent les
tests avec ASan et UBSan. Aucune copie des méthodes de production n'est
maintenue dans les fixtures.

- `receive.cpp` : garde, tentatives, sélection des réponses, annulation et suspension.
- `commands.cpp` : report de la dernière commande ECS pendant une observation.
- `services.cpp` : progression d'OTA, HTTP et MQTT lorsque la radio est réservée.
- `portal.cpp` : validation des requêtes, préemption par l'association et protections OTA/redémarrage.

Depuis la racine du dépôt, avec Python 3 et un compilateur C++17 :

```sh
python3 tests/test_cooperative_receive.py
python3 tests/test_cooperative_commands.py
python3 tests/test_cooperative_portal.py
python3 tests/test_satellite_observation.py
```

Les trames sont synthétiques ; aucun appareil ni service réseau n'est sollicité.

Les callbacks OTA HTTP/Arduino et leurs pauses sont couverts par
`../ota-auth/test.cpp`, lancé par `python3 tests/test_ota_auth.py`.
