#include "config_store.h"
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_crc.h"

static const char *TAG = "CFG";
#define CFG_NS "appcfg"
#define CFG_KEY "config"

void config_store_defaults(config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->version = 0x000200;
    cfg->mode = NET_WIFI;
    cfg->wifi.dhcp = true;
    cfg->services.https_api = true;
    cfg->services.mqtt = false;
    cfg->sec.require_admin_token = true;
    cfg->provisioned = false;
    cfg->crc32 = config_crc32(cfg, sizeof(*cfg) - sizeof(uint32_t));
}

void config_store_init(void) {
}

bool config_store_load(config_t *cfg) {
    nvs_handle_t h;
    if (nvs_open(CFG_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open RO fallo");
        config_store_defaults(cfg);
        return false;
    }
    size_t len = sizeof(*cfg);
    esp_err_t err = nvs_get_blob(h, CFG_KEY, cfg, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(*cfg)) {
        ESP_LOGW(TAG, "No hay config valida");
        config_store_defaults(cfg);
        return false;
    }
    uint32_t crc = config_crc32(cfg, sizeof(*cfg) - sizeof(uint32_t));
    if (crc != cfg->crc32) {
        ESP_LOGE(TAG, "CRC invalido");
        config_store_defaults(cfg);
        return false;
    }
    return true;
}

bool config_store_save(const config_t *cfg_in) {
    config_t tmp = *cfg_in;
    tmp.crc32 = config_crc32(&tmp, sizeof(tmp) - sizeof(uint32_t));

    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(CFG_NS, NVS_READWRITE, &h));
    esp_err_t err = nvs_set_blob(h, CFG_KEY, &tmp, sizeof(tmp));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob fallo: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

uint32_t config_crc32(const void *data, size_t len) {
    return esp_crc32_le(0, data, len);
}
