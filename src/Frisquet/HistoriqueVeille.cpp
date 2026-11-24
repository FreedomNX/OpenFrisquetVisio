#include "HistoriqueVeille.h"
#include "StockageHistorique.h"
#include "../Config.h"
#include "../Logs.h"
#include <TimeLib.h>
#include <cstring>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <unistd.h>

namespace {
using Releve = HistoriqueVeille::Releve;
using Valeurs = HistoriqueVeille::Valeurs;

// Chaque fichier offre une case par mois du calendrier. Le marqueur final
// désigne la dernière écriture acceptée, même après un recul de l’horloge.
struct Mois {
    uint32_t numero = 0;
    Valeurs jours[31];
};
struct Bloc {
    Mois mois[12];
    uint32_t revision = 0;
    uint32_t jourEcrit = 0;
};
static_assert(sizeof(Bloc) == 1544, "Format du fichier de consommation");
void cleInstallation(Config& config, char (&cle)[11]) {
    const auto& id = config.getNetworkID();
    snprintf(cle, sizeof(cle), "%02x%02x%02x%02x%02x",
             id.bytes[0], id.bytes[1], id.bytes[2], id.bytes[3], config.boilerId());
}

struct Stockage {
    const char* racine;
    char cle[11];
    explicit Stockage(Config& config) : racine(monterStockageHistorique()) {
        cleInstallation(config, cle);
    }
    void chemin(char (&nom)[96], int index) const {
        snprintf(nom, sizeof(nom), "%s/veille-%s-%d", racine, cle, index);
    }
};

uint32_t jourCivil(int numero, unsigned jour) {
    if (!jour) return 0;
    tmElements_t date{};
    date.Year = CalendarYrToTm(numero / 12);
    date.Month = numero % 12 + 1;
    date.Day = jour;
    return makeTime(date) / 86400;
}

bool calendrier(uint32_t jour, int& numero, int& quantieme) {
    if (!jour || jour >= 47482) return false; // 01/01/2100
    const time_t secondes = static_cast<time_t>(jour) * 86400;
    struct tm date{};
    if (!gmtime_r(&secondes, &date)) return false;
    numero = (date.tm_year + 1900) * 12 + date.tm_mon;
    quantieme = date.tm_mday;
    return true;
}

bool lireFichier(const char* chemin, Bloc& bloc) {
    FILE* fichier = fopen(chemin, "rb");
    if (!fichier) return errno == ENOENT;
    const size_t lus = fread(&bloc, 1, sizeof(bloc), fichier);
    const bool complet = lus == sizeof(bloc) && fgetc(fichier) == EOF && !ferror(fichier);
    if (fclose(fichier) != 0 || !complet) return false;
    for (unsigned m = 0; m < 12; ++m) {
        const unsigned n = bloc.mois[m].numero;
        if (n && (n < 1970 * 12 || n >= 2100 * 12 || n % 12 != m)) return false;
    }
    int numero, jour;
    return bloc.revision && calendrier(bloc.jourEcrit, numero, jour)
        && bloc.mois[numero % 12].numero == unsigned(numero)
        && bloc.mois[numero % 12].jours[jour - 1].disponible();
}

struct Index {
    uint16_t numeros[10][12] = {};
    int premierMois = 0;
    uint32_t revision = 0, jourActif = 0;

    bool lire(const Stockage& stockage) {
        for (int i = 0; i < 10; ++i) {
            char chemin[96];
            stockage.chemin(chemin, i);
            Bloc bloc;
            if (!lireFichier(chemin, bloc)) return false;
            if (bloc.revision > revision) {
                revision = bloc.revision;
                jourActif = bloc.jourEcrit;
            }
            for (unsigned m = 0; m < 12; ++m) {
                const int n = bloc.mois[m].numero;
                if (!n) continue;
                numeros[i][m] = n;
                if (!premierMois || n < premierMois) premierMois = n;
            }
        }
        return true;
    }

    int emplacement(int numero) const {
        const int mois = numero % 12;
        int libreOuAncien = numero / 12 % 10;
        for (int i = 0; i < 10; ++i) {
            if (numeros[i][mois] == numero) return i;
            if (numeros[i][mois] < numeros[libreOuAncien][mois]) libreOuAncien = i;
        }
        return libreOuAncien;
    }
};

bool sauverFichier(const char* chemin, const Bloc& bloc) {
    char temporaire[104];
    snprintf(temporaire, sizeof(temporaire), "%s.tmp", chemin);
    FILE* fichier = fopen(temporaire, "wb");
    if (!fichier) return false;
    const bool complet = fwrite(&bloc, 1, sizeof(bloc), fichier) == sizeof(bloc)
                      && fflush(fichier) == 0 && fsync(fileno(fichier)) == 0;
    const bool ferme = fclose(fichier) == 0;
    if (complet && ferme && rename(temporaire, chemin) == 0) return true;
    remove(temporaire);
    return false;
}

bool ajouter(const Stockage& stockage, Index& index, const Releve& releve) {
    int numero, jour;
    if (!calendrier(releve.jour, numero, jour)) return false;
    const int position = index.emplacement(numero);
    char chemin[96];
    stockage.chemin(chemin, position);
    Bloc bloc;
    if (!lireFichier(chemin, bloc)) return false;
    Mois& mois = bloc.mois[numero % 12];
    if (mois.numero != numero) {
        mois = Mois{};
        mois.numero = numero;
    }
    Valeurs& valeurs = mois.jours[jour - 1];
    const Valeurs avant = valeurs;
    if (releve.chauffage >= 0) valeurs.chauffage = releve.chauffage;
    if (releve.ecs >= 0) valeurs.ecs = releve.ecs;
    if (index.jourActif == releve.jour && valeurs.chauffage == avant.chauffage && valeurs.ecs == avant.ecs) return true;
    bloc.revision = index.revision + 1;
    bloc.jourEcrit = releve.jour;
    const uint32_t debut = micros();
    const bool sauve = sauverFichier(chemin, bloc);
    debug("[HISTORIQUE] Écriture de %u octets en %lu µs (%s).",
          unsigned(sizeof(bloc)), (unsigned long)(uint32_t(micros()) - debut), sauve ? "OK" : "échec");
    if (sauve) {
        index.revision = bloc.revision;
        index.jourActif = releve.jour;
        index.numeros[position][numero % 12] = numero;
    }
    return sauve;
}

unsigned longueurMois(int numero) {
    const unsigned longueurs[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const int annee = numero / 12;
    const bool bissextile = annee % 4 == 0 && (annee % 100 != 0 || annee % 400 == 0);
    return longueurs[numero % 12] + (numero % 12 == 1 && bissextile);
}
}

bool HistoriqueVeille::lire(Config& config, int selection) {
    *this = HistoriqueVeille{};
    Stockage stockage(config);
    if (!stockage.racine) return false;
    Index index;
    if (!index.lire(stockage)) return false;
    if (!index.jourActif) return true;
    int jour;
    if (!calendrier(index.jourActif, dernierMois, jour)) return false;
    revision = index.revision;
    premierMois = std::max(index.premierMois, dernierMois - nbMois + 1);
    if (selection && (selection < premierMois || selection > dernierMois)) return true;
    mois = selection ? selection : dernierMois;
    nbJours = mois == dernierMois ? unsigned(jour) : longueurMois(mois);
    const int position = index.emplacement(mois);
    if (index.numeros[position][mois % 12] == mois) {
        char chemin[96];
        stockage.chemin(chemin, position);
        Bloc bloc;
        if (!lireFichier(chemin, bloc)) return false;
        memcpy(jours, bloc.mois[mois % 12].jours, sizeof(jours));
    }
    return true;
}

uint32_t HistoriqueVeille::dernierJour() const { return jourCivil(mois, nbJours); }

void CollecteVeille::recevoir(Config& config, Releve releve, uint32_t reception) {
    if (!releve.jour || (releve.chauffage < 0 && releve.ecs < 0)) return;
    char cle[11];
    cleInstallation(config, cle);
    if (strcmp(cle, _installation) != 0) {
        *this = CollecteVeille{};
        memcpy(_installation, cle, sizeof(cle));
    }
    if (_attente.jour == releve.jour) {
        if (releve.chauffage < 0) releve.chauffage = _attente.chauffage;
        if (releve.ecs < 0) releve.ecs = _attente.ecs;
    }
    _attente = releve;
    _reception = reception;
}

bool CollecteVeille::traiter(Config& config, uint32_t maintenant) {
    if (!enAttente() || (_enEchec && maintenant - _derniereTentative < 60000)) return true;
    Stockage stockage(config);
    if (!stockage.racine || strcmp(stockage.cle, _installation) != 0) {
        *this = CollecteVeille{};
        return true;
    }
    _derniereTentative = maintenant;
    _enEchec = !enregistrer(config);
    if (!_enEchec) _attente = {};
    return !_enEchec;
}

bool CollecteVeille::enregistrer(Config& config) {
    Stockage stockage(config);
    Index index;
    if (!index.lire(stockage)) return false;
    if (!index.jourActif || _attente.jour < index.jourActif || _attente.jour > index.jourActif + 1) {
        // Première date ou saut dans un sens ou l’autre. Seule une nouvelle
        // observation radio peut confirmer la date, jamais une relance disque.
        const bool memeJour = _aConfirmer.jour == _attente.jour;
        if (!_aConfirmer.jour || (!memeJour && _attente.jour != _aConfirmer.jour + 1)) {
            _aConfirmer = _attente;
            _premiereReception = _reception;
            return true;
        }
        if (memeJour) {
            if (_attente.chauffage >= 0) _aConfirmer.chauffage = _attente.chauffage;
            if (_attente.ecs >= 0) _aConfirmer.ecs = _attente.ecs;
        }
        if (_reception - _premiereReception < 1800000) return true;
        if (!ajouter(stockage, index, _aConfirmer)) return false;
        _aConfirmer = {};
        if (memeJour) return true;
    }
    _aConfirmer = {};
    return ajouter(stockage, index, _attente);
}
