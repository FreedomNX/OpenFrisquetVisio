# Consommations mensuelles

Depuis la racine du dépôt, avec Python 3, Node.js et un compilateur C++17
disponible sous le nom `c++` (voir [Développement et tests](../../../README.md)) :

```sh
python3 tests/test_monthly_consumption.py
node --test tests/monthly-consumption.test.mjs
```

`tests/test_monthly_consumption.py` compile le décodeur et les déclarations du cache
extraits de la production avec le scénario C++ `decoding.cpp`, ASan/UBSan et
`-Werror`. Aucun scénario C++ n'est incorporé au lanceur Python.

Les valeurs synthétiques vérifient l'ordre janvier–décembre, l'alternance
chauffage/ECS, les cumuls existants, les zéros, les mots de 16 bits, les réponses
tronquées ou de longueur déclarée invalide et la conservation de la date du
dernier relevé. Ce test ne constitue pas une validation sur chaudière.

`serialization.cpp` exécute le sérialiseur JSON du portail ; le lanceur vérifie
le résultat avec le parseur JSON Python. `monthly-consumption.test.mjs` teste
le rendu réel du tableau de bord, notamment l'attente, les zéros et la date
conservée lorsque l'année courante du navigateur diffère de celle du relevé.
Le mois du relevé limite les lignes affichées : les mois suivants sont omis,
le dernier est partiel et les zéros des mois précédents restent visibles.
Sans date connue, la période reste non confirmée et toutes les lignes sont
neutres ; le navigateur ne devine ni l'année ni le mois.

Les deux années sont lues séparément : 24 mots à partir de `0xA063`, puis
24 mots à partir de `0xA07B`. Chaque réponse fait 55 octets, sous la limite RX
de 63 octets. Le scénario contrôle cette limite réelle et l'indépendance des
caches/cumuls MQTT. Les requêtes des deux années, pour les deux identités
chaudière, sont vérifiées par `tests/fixtures/cooperative-radio/connect.cpp`.
Le JSON date chaque cache à réception ; l’archive porte l’année précédente et
reste complète jusqu’à décembre. Les tests de rendu distinguent les deux
années, leurs données absentes et leurs mois complets/partiels.
