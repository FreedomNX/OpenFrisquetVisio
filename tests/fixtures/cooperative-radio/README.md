# Tests natifs des opérations radio coopératives

Les scénarios C++ utilisent une radio et une horloge simulées. Les lanceurs
Python extraient les méthodes du firmware, puis compilent et exécutent les
tests avec ASan et UBSan. Aucune copie des méthodes de production n'est
maintenue dans les fixtures.

- `connect.cpp` : plages de lectures, version chaudière retardée ou absente, limites des tentatives, progression des services, annulation et commandes pendant les nouvelles tentatives.
- `firmware.cpp` : version BCD et conversion des puissances.
- `decoding.cpp` : validation des réponses Connect et conservation des valeurs en cas de troncature.
- `read.cpp` : émission, réception coopérative, réservation du résultat et limites des tentatives.
- `receive.cpp` : garde, tentatives, sélection des réponses, annulation et suspension.
- `commands.cpp` : report de la dernière commande ECS pendant une observation.
- `services.cpp` : progression d'OTA, HTTP et MQTT lorsque la radio est réservée.
- `portal.cpp` : validation des requêtes, préemption par l'association et protections OTA/redémarrage.

Depuis la racine du dépôt, avec Python 3 et un compilateur C++17 :

```sh
python3 tests/test_connect_decoding.py
python3 tests/test_boiler_firmware.py
python3 tests/test_cooperative_receive.py
python3 tests/test_cooperative_read.py
python3 tests/test_cooperative_connect.py
python3 tests/test_cooperative_commands.py
python3 tests/test_cooperative_portal.py
python3 tests/test_satellite_observation.py
```

Les trames sont synthétiques ; aucun appareil ni service réseau n'est sollicité.

Pour l'ensemble des tests, y compris les scénarios de zone qui ont besoin des
dépendances PlatformIO, suivre la section **Développement et tests** du
[README principal](../../../README.md). La CI utilise GCC sous Linux ; les
diagnostics et en-têtes de Clang sous macOS peuvent nécessiter une adaptation locale.

Les callbacks OTA HTTP/Arduino et leurs pauses sont couverts par
`../ota-auth/test.cpp`, lancé par `python3 tests/test_ota_auth.py`.
