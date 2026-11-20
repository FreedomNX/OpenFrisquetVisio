class SuiviCommandeZone extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({mode: "open"}).innerHTML = `
      <style>
        :host{display:block;color:inherit} :host([hidden]),[hidden]{display:none!important}
        section{padding:.85rem 0 .15rem}
        p{margin:.3rem 0} progress{display:block;width:100%;margin:.6rem 0;accent-color:var(--blue)}
        small{color:var(--muted)} button{margin-top:.5rem;cursor:pointer;font:inherit}
      </style>
      <section><strong></strong><p aria-live="polite"></p>
      <progress aria-label="Temps écoulé dans cette période d’attente"></progress><small></small>
      <button type="button">Réessayer</button></section>`;
    this.titre = this.shadowRoot.querySelector("strong");
    this.message = this.shadowRoot.querySelector("p");
    this.barre = this.shadowRoot.querySelector("progress");
    this.temps = this.shadowRoot.querySelector("small");
    this.bouton = this.shadowRoot.querySelector("button");
    this.bouton.onclick = async () => {
      this.bouton.disabled = true;
      try { await this.relancer(); }
      catch { this.message.textContent = "Impossible de relancer la commande."; }
      finally { this.bouton.disabled = false; }
    };
    this.shadowRoot.querySelector("section").hidden = true;
  }

  afficher(suivi, relancer) {
    this.suivi = suivi;
    this.recu = performance.now();
    this.relancer = relancer;
    this.shadowRoot.querySelector("section").hidden = suivi.etat === "Au repos";
    this.titre.textContent = suivi.objet || "";
    const degres = n => n.toLocaleString("fr-FR") + " °C";
    const valeurs = [suivi.valeur_confirmee, suivi.valeur_demandee];
    let cible = suivi.mode_demande || "";
    if (Number.isFinite(valeurs[1])) {
      cible = suivi.etat !== "Modification confirmée" && valeurs.every(Number.isFinite) && valeurs[0] !== valeurs[1] ?
        valeurs.map(degres).join(" → ") : degres(valeurs[1]);
    }
    this.message.textContent = [cible, suivi.etat].filter(Boolean).join(" · ");
    this.bouton.hidden = suivi.etat !== "Modification non confirmée" || !relancer;
    this.barre.hidden = this.temps.hidden = suivi.etat !== "En attente de confirmation";
    this.actualiser();
    this.demarrerHorloge();
  }

  actualiser() {
    if (!this.suivi || this.barre.hidden) return;
    const reste = Math.max(0, this.suivi.restant_ms - (performance.now() - this.recu));
    this.barre.max = this.suivi.duree_ms;
    this.barre.value = Math.min(this.barre.max, Math.max(0, this.barre.max - reste));
    const s = Math.ceil(reste / 1000);
    const phase = this.suivi.synchronisation ? "En attente du statut du satellite" :
      "Fenêtre prévue " + this.suivi.tentative + " sur " + this.suivi.max_tentatives;
    this.temps.textContent = phase + " · " + Math.floor(s / 60) + " min " + String(s % 60).padStart(2, "0") + " s restantes";
  }

  demarrerHorloge() {
    clearInterval(this.horloge);
    if (this.isConnected && this.suivi && !this.barre.hidden) {
      this.horloge = setInterval(() => this.actualiser(), 500);
    }
  }
  connectedCallback() { this.demarrerHorloge(); }
  disconnectedCallback() { clearInterval(this.horloge); }
}
customElements.define("suivi-commande-zone", SuiviCommandeZone);
