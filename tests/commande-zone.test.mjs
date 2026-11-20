import test from "node:test";
import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import {runInNewContext} from "node:vm";

const source = readFileSync(new URL("../web/commande-zone.js", import.meta.url), "utf8");

test("le portail suit la demande et propose le réessai après un échec", async () => {
  let Carte, maintenant = 0, horloge;
  const elements = new Map();
  class Element {
    isConnected = true;
    attachShadow() {
      this.shadowRoot = {querySelector(selecteur) {
        if (!elements.has(selecteur)) elements.set(selecteur, {});
        return elements.get(selecteur);
      }};
      return this.shadowRoot;
    }
  }
  runInNewContext(source, {
    HTMLElement: Element, customElements: {define(nom, classe) {Carte = classe;}},
    performance: {now: () => maintenant}, clearInterval() {horloge = null;},
    setInterval(fn) {horloge = fn; return 1;}
  });
  const carte = new Carte();
  let reessais = 0;
  function publier(etat, autres = {}) {
    carte.afficher({etat, objet:"Confort", valeur_confirmee:19, valeur_demandee:21, ...autres}, async () => { ++reessais; });
  }
  publier("Au repos");
  assert.equal(elements.get("section").hidden, true);
  publier("En attente de confirmation", {synchronisation:true, tentative:0, restant_ms:610000, duree_ms:610000});
  assert.match(elements.get("small").textContent, /En attente du statut du satellite/);
  publier("En attente de confirmation", {max_tentatives:5, tentative:1, restant_ms:125000, duree_ms:125000});
  assert.match(elements.get("p").textContent, /19 °C → 21 °C · En attente/);
  assert.equal(elements.get("progress").max, 125000);
  maintenant += 75000;
  horloge();
  assert.equal(elements.get("progress").value, 75000);
  // Même à zéro, seul un nouvel état reçu peut annoncer un échec.
  maintenant += 100000;
  horloge();
  assert.equal(elements.get("progress").value, 125000);
  assert.equal(elements.get("button").hidden, true);
  publier("En attente de confirmation", {max_tentatives:5, tentative:2, restant_ms:144000, duree_ms:144000});
  assert.match(elements.get("small").textContent, /Fenêtre prévue 2 sur 5/);
  publier("Modification non confirmée");
  assert.equal(elements.get("button").hidden, false);
  await elements.get("button").onclick();
  assert.equal(reessais, 1);
  publier("Modification confirmée", {valeur_confirmee:21});
  assert.equal(elements.get("progress").hidden, true);
  assert.equal(elements.get("button").hidden, true);
  assert.equal(horloge, null);
  assert.match(elements.get("p").textContent, /^21 °C · Modification confirmée$/);
});
