# Consommations mensuelles

`test_monthly_consumption.py` compile le décodeur et les déclarations du cache
extraits de la production avec le scénario C++ `decoding.cpp`, ASan/UBSan et
`-Werror`. Aucun scénario C++ n'est incorporé au lanceur Python.

Les valeurs synthétiques vérifient l'ordre janvier–décembre, l'alternance
chauffage/ECS, les cumuls existants, les zéros, les mots de 16 bits, les réponses
tronquées ou de longueur déclarée invalide et la conservation de la date du
dernier relevé. Ce test ne constitue pas une validation sur chaudière.

Les deux années sont lues séparément : 24 mots à partir de `0xA063`, puis
24 mots à partir de `0xA07B`. Chaque réponse fait 55 octets, sous la limite RX
de 63 octets. Le scénario contrôle cette limite réelle, les deux identités
chaudière, les archives passives et l'indépendance des caches/cumuls MQTT.

`serialization.cpp` exécute le sérialiseur JSON du portail ; le lanceur vérifie
le résultat avec le parseur JSON Python. Le JSON date chaque cache à réception ;
l’archive porte l’année précédente et reste complète jusqu’à décembre.
