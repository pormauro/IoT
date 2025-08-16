#include "ota.h"
#include "esp_https_ota.h"
#include "esp_log.h"

static const char *TAG = "OTA";

void ota_init(const config_t *cfg) {
    (void)cfg;
}

esp_err_t ota_trigger(const char *url) {
#if CONFIG_ENABLE_OTA
    esp_http_client_config_t http_cfg = {
        .url = url,
        .cert_pem = NULL,
    };
    esp_err_t err = esp_https_ota(&http_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA OK, reiniciando...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA fallo: %s", esp_err_to_name(err));
    }
    return err;
#else
    (void)url;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
