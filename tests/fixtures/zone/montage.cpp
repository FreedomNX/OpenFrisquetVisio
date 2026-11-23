#include <array>
#include <cassert>
#include <cstring>
#include "esp_littlefs.h"
#include "esp_partition.h"
#include "Frisquet/StockageHistorique.h"

static std::array<uint8_t, 1025> contenu;
static bool panneLecture = false, formate = false;
static unsigned tentatives = 0;
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t* config) {
    ++tentatives;
    assert(!strcmp(config->partition_label, "spiffs"));
    if (!config->format_if_mount_failed) return ESP_FAIL;
    formate = true;
    return ESP_OK;
}
const esp_partition_t* esp_partition_find_first(int, int, const char*) {
    static const esp_partition_t part{contenu.size()};
    return &part;
}
esp_err_t esp_partition_read(const esp_partition_t*, size_t pos, void* tampon, size_t taille) {
    if (panneLecture) return ESP_FAIL;
    assert(pos + taille <= contenu.size());
    memcpy(tampon, contenu.data() + pos, taille);
    return ESP_OK;
}
int main(int argc, char** argv) {
    assert(argc == 2);
    contenu.fill(0xff);
    const bool vierge = !strcmp(argv[1], "vierge");
    if (!strcmp(argv[1], "occupee")) contenu.back() = 0x30;
    panneLecture = !strcmp(argv[1], "panne");
    assert((monterStockageHistorique() != nullptr) == vierge && formate == vierge);
    const auto avant = tentatives;
    assert((monterStockageHistorique() != nullptr) == vierge && tentatives == avant);
    if (!vierge) assert(strstr(erreurStockageHistorique(), panneLecture ? "Lecture" : "préservé"));
}
