#pragma once
using esp_err_t = int;
enum { ESP_OK, ESP_FAIL };
struct esp_vfs_littlefs_conf_t {
    const char* base_path;
    const char* partition_label;
    bool format_if_mount_failed;
};
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t*);
