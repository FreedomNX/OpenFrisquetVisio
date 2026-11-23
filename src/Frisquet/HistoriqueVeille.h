#pragma once
#include <stdint.h>

class Config;

struct HistoriqueVeille {
    static constexpr int nbMois = 120;
    struct Releve {
        uint32_t jour; // Jour civil depuis le 01/01/1970.
        int16_t chauffage;
        int16_t ecs;
    };
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

// La réception radio ne modifie que cette mémoire. Le gestionnaire effectue
// l'écriture entre deux transactions, pour l'installation qui a émis le relevé.
class CollecteVeille {
public:
    using Releve = HistoriqueVeille::Releve;
    void recevoir(Config& config, Releve releve, uint32_t reception);
    bool traiter(Config& config, uint32_t maintenant);
    bool enAttente() const { return _attente.jour != 0; }
private:
    bool enregistrer(Config& config);
    char _installation[11] = {};
    Releve _attente{}, _aConfirmer{};
    uint32_t _reception = 0, _premiereReception = 0, _derniereTentative = 0;
    bool _enEchec = false;
};
