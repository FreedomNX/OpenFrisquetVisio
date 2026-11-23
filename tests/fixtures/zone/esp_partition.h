#pragma once
#include <cstdint>
#include <cstddef>
#include "esp_littlefs.h"
struct esp_partition_t { size_t size; };
enum { ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS };
const esp_partition_t* esp_partition_find_first(int, int, const char*);
esp_err_t esp_partition_read(const esp_partition_t*, size_t, void*, size_t);
