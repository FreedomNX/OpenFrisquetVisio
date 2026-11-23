#pragma once
#include <stdint.h>

class Config;

struct HistoriqueVeille {
    static constexpr int nbMois = 120;
    struct Valeurs {
        int16_t chauffage = -1;
        int16_t ecs = -1;
        bool disponible() const { return chauffage >= 0 || ecs >= 0; }
    };

    // Les numéros de mois (année * 12 + mois de 0 à 11) sont consécutifs.
    int premierMois = 0, dernierMois = 0, mois = 0;
    uint32_t revision = 0;
    unsigned nbJours = 0;
    Valeurs jours[31];

    // Sans sélection, ouvrir le mois du dernier relevé, même sans horloge.
    bool lire(Config& config, int selection = 0);
    uint32_t dernierJour() const;
};
