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
