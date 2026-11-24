# Tests des observations satellite

Depuis la racine du dépôt, avec Python 3 et un compilateur C++17 disponible sous
le nom `c++` (voir [Développement et tests](../../../README.md)) :

`python3 tests/test_satellite_observation.py` compile les méthodes Satellite et
Zone du firmware avec des simulations radio/MQTT et les outils ASan/UBSan.
Les paquets sont construits à partir de données synthétiques.

Les tests couvrent les réponses différées, la copie des requêtes, les annulations,
les modes demandés et confirmés, les consignes en attente, les trois zones et les
réponses tronquées. Les tests radio coopératifs couvrent les délais, la sélection
et les nouvelles tentatives.
