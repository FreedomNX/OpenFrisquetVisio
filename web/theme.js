/* Préférence locale au navigateur, disponible aussi sur la page de connexion. */
(() => {
  'use strict';
  const key = 'ofv.portal.theme';
  const normalize = value => ['light', 'dark', 'auto'].includes(value) ? value : 'light';
  let mode = 'light';
  try { mode = normalize(localStorage.getItem(key)); } catch (_) {}

  function apply() {
    const hour = new Date().getHours();
    document.documentElement.dataset.theme = mode === 'auto'
      ? (hour >= 7 && hour < 20 ? 'light' : 'dark') : mode;
  }
  function syncControl() {
    const select = document.getElementById('portalTheme');
    if(select) select.value = mode;
  }
  // Appliquer avant le premier affichage pour éviter un flash clair la nuit.
  apply();
  setInterval(apply, 30000);
  document.addEventListener('visibilitychange', () => { if(!document.hidden) apply(); });
  window.addEventListener('pageshow', () => { apply(); syncControl(); });
  window.addEventListener('storage', event => {
    if(event.key !== key && event.key !== null) return;
    mode = normalize(event.key === null ? null : event.newValue);
    apply(); syncControl();
  });
  document.addEventListener('DOMContentLoaded', () => {
    syncControl();
    const select = document.getElementById('portalTheme');
    if(!select) return;
    select.addEventListener('change', () => {
      mode = normalize(select.value);
      let saved = true;
      try { localStorage.setItem(key, mode); } catch (_) { saved = false; }
      apply(); syncControl();
      const status = document.getElementById('portalThemeStatus');
      if(status) status.textContent = saved ? 'Style enregistré sur ce navigateur.'
        : 'Style appliqué. Ce navigateur ne permet pas de mémoriser le choix.';
    });
  });
})();
