#pragma once

#include "Frisquet/Connect.h"

inline String programmationReadOnlyReason(Config& cfg, Connect& connect, Zone& zone, bool enabled) {
  if(!enabled) return "Cette zone est désactivée.";
  if(zone.getSource() == Zone::SOURCE::SATELLITE_VIRTUEL) return "";
  if(!cfg.useConnect()) return "Activez le Connect virtuel pour modifier la programmation.";
  if(cfg.useConnectPassive()) return "Le Connect est en écoute passive : programmation en lecture seule.";
  if(zone.getSource() != Zone::SOURCE::CONNECT) return "La modification de programmation nécessite une zone pilotée par le Connect.";
  if(!connect.estAssocie()) return "Associez le Connect avant de modifier la programmation.";
  if(zone.getMode() == Zone::MODE_ZONE::INCONNU || isnan(zone.getTemperatureConfort()) ||
     isnan(zone.getTemperatureReduit()) || isnan(zone.getTemperatureHorsGel())) {
    return "Les paramètres de la zone doivent être récupérés avant de modifier sa programmation.";
  }
  return "";
}
