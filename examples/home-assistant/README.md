# Surveiller une sonde qui alimente la régulation

Quand le satellite virtuel remplace la commande Visio physique, la chaudière ne
régule plus sur sa propre sonde : elle régule sur la valeur qu'on lui pousse. Si
cette valeur vient d'un capteur Zigbee et que le capteur meurt, **la carte
conserve la dernière valeur reçue** — le firmware n'a aucun délai de péremption.
La chaudière régule alors sur une mesure morte, sans aucun symptôme : pas
d'alarme, pas d'entité `unavailable`, rien.

Ces automatisations détectent ce cas.

## Le piège à éviter

```yaml
# NE PAS FAIRE
value_template: "{{ (now() - states.sensor.ambiance.last_reported).total_seconds() > 3600 }}"
```

Un capteur Zigbee **n'émet pas quand la valeur ne change pas**. Une pièce à
température stable produit exactement le même silence qu'un capteur mort. Cette
condition déclenche donc sur du vide, pendant les nuits calmes, et elle finit
par être ignorée — ce qui est pire que pas d'alerte du tout.

Le second piège est du même ordre :

```yaml
# NE PAS FAIRE non plus
value_template: "{{ e is none or e.state in ['unknown','unavailable'] or ... }}"
```

Une transition vers `unavailable` doit être une **alerte**, jamais un signe de
vie. Mal filtrée, elle remet l'horodateur à zéro et désarme la surveillance
précisément quand l'appareil tombe.

## Le principe

Une preuve de vie est un **événement d'un autre signal du même appareil**, pas
l'âge de la valeur surveillée.

Les capteurs de température/humidité exposent les deux grandeurs. L'humidité
varie presque toujours, la température non. On prend donc l'humidité comme
battement de cœur, et on surveille l'âge de ce battement.

Vérifier la fréquence réelle avant de fixer un seuil :

```bash
# écart entre deux messages, par entité
```

## Le code

Voir `surveillance-sonde.yaml`. Trois pièces :

1. **Battement** — chaque message de l'appareil horodate un `input_datetime`.
   Le déclencheur exclut explicitement `unavailable` et `unknown`.
2. **Muette** — déclencheur périodique plus condition sur l'âge. Un
   `trigger: state` avec `for:` serait perdu au redémarrage de Home Assistant,
   et l'alerte ne partirait jamais.
3. **Repli** — quand la mesure est trop vieille ou hors plage, on écrit une
   consigne neutre. Ne rien écrire ne protège de rien : la carte garde de toute
   façon la dernière valeur reçue.

## Ce qu'il faut adapter

- `sensor.ambiance_temperature` / `sensor.ambiance_humidity` : les entités de
  votre capteur.
- `number.chaudiere_consigne_ambiante` : l'entité de consigne de votre satellite.
- `REPLI` : la consigne neutre. 18 °C convient pour une maison ; à ajuster si
  votre régime réduit est plus bas, sinon le repli déclenchera du chauffage.
- Le service de notification.

## Deux choses à ne pas oublier

**Ne pas initialiser l'horodateur au démarrage de Home Assistant.** C'est
tentant pour éviter une fausse alerte, mais cela remet le compteur à zéro à
chaque redémarrage et **masque une sonde morte depuis des heures**. Laisser
l'horodateur persister, et l'initialiser une seule fois à la main.

**Aligner les seuils.** Si l'alerte parle d'une bascule vers la consigne de
repli, le repli doit s'appliquer au même moment. Une alerte qui annonce une
action qui n'a pas eu lieu est un mensonge de plus dans les notifications, et
elle coûte la confiance dans les suivantes.
