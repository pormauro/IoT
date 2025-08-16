#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "app_version.h"

#include "config_store.h"
#include "security.h"
#include "net_mgr.h"
#include "ble_prov.h"
#include "http_api.h"
#include "mqtt_client_mod.h"
#include "ota.h"

static const char *TAG = "APP";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Boot v%s", APP_VERSION_STR);

    config_t cfg = {0};
    config_store_init();
    bool has_cfg = config_store_load(&cfg);
    security_init(&cfg);

#if CONFIG_ENABLE_WIFI || CONFIG_ENABLE_ETH
    net_mgr_init(&cfg);
#endif

    if (!has_cfg) {
#if CONFIG_ENABLE_BLE
        ESP_LOGW(TAG, "Sin configuración -> modo PROVISION BLE");
        ble_prov_start(&cfg);
        ble_prov_wait_done();
        config_store_load(&cfg);
#else
        ESP_LOGE(TAG, "Sin configuración y BLE deshabilitado. Inhabilitado.");
        vTaskDelay(portMAX_DELAY);
#endif
    }

#if CONFIG_ENABLE_HTTP_API
    http_api_start(&cfg);
#endif

#if CONFIG_ENABLE_MQTT
    mqtt_client_start(&cfg);
#endif

#if CONFIG_ENABLE_OTA
    ota_init(&cfg);
#endif

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
