/* Le portail répond 401 à tout appel /api/ sans session valide : session expirée
   après 12 h, boîtier redémarré (les sessions ne survivent pas au redémarrage),
   mot de passe changé ou déconnexion depuis un autre onglet. Une page déjà
   ouverte ne peut alors plus rien charger ni enregistrer : revenir à la page de
   connexion, comme le fait déjà le serveur pour les pages elles-mêmes. */
(() => {
  'use strict';
  const request = globalThis.fetch;
  let leaving = false;
  const departing = new Promise(() => {});
  globalThis.fetch = async (...args) => {
    if (leaving) return departing;
    let response;
    try { response = await request(...args); }
    catch (error) { if (leaving) return departing; throw error; }
    if (leaving) return departing;
    if (response.status !== 401) return response;
    // Un éditeur peut garder ses modifications et proposer une reconnexion
    // sur place. Dans ce cas, rendre aussi le premier 401 pour libérer les
    // contrôles et les gardes de rafraîchissement dans les blocs finally.
    if (!window.dispatchEvent(new Event('ofv-session-expired', {cancelable:true}))) return response;
    leaving = true;
    location.replace('/login');
    // La page s’en va : suspendre aussi les autres appels et leurs réponses,
    // pour ne plus modifier l’affichage en attendant la page de connexion.
    return departing;
  };
})();
