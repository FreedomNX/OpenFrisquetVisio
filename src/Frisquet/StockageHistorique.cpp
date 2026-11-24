#include "StockageHistorique.h"
#include <esp_littlefs.h>
#include <esp_partition.h>
#include <algorithm>

namespace {
const char* erreur = nullptr;
const char* ouvrir() {
    static constexpr char chemin[] = "/littlefs";
    esp_vfs_littlefs_conf_t config{};
    config.base_path = chemin;
    config.partition_label = "spiffs";
    esp_err_t resultat = esp_vfs_littlefs_register(&config);
    if (resultat == ESP_FAIL) {
        const auto* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_DATA_SPIFFS, config.partition_label);
        if (!partition) {
            erreur = "Partition de l’historique introuvable.";
            return nullptr;
        }
        uint8_t octets[512];
        for (size_t pos = 0; pos < partition->size; pos += sizeof(octets)) {
            const size_t n = std::min(sizeof(octets), size_t(partition->size - pos));
            if (esp_partition_read(partition, pos, octets, n) != ESP_OK) {
                erreur = "Lecture de la partition de l’historique impossible.";
                return nullptr;
            }
            if (!std::all_of(octets, octets + n, [](uint8_t b){ return b == 0xff; })) {
                erreur = "Partition non vierge illisible. Son contenu a été préservé.";
                return nullptr;
            }
        }
        config.format_if_mount_failed = true;
        resultat = esp_vfs_littlefs_register(&config);
    }
    if (resultat == ESP_OK) return chemin;
    erreur = "Échec du montage de la partition de l’historique.";
    return nullptr;
}
}

const char* monterStockageHistorique() {
    static const char* const racine = ouvrir();
    return racine;
}

const char* erreurStockageHistorique() { return erreur; }
