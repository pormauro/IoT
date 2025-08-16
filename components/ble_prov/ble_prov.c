#include "ble_prov.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "BLEPROV";
static config_t *g_cfg;
static bool g_done = false;

static const uint8_t SVC_UUID[16]  = {0xfb,0x34,0x9b,0x5f,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00,0x00,0xa0,0x00,0x00};
static const uint8_t CHR_INFO[16]  = {0xfb,0x34,0x9b,0x5f,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00,0x01,0xa0,0x00,0x00};
static const uint8_t CHR_NET[16]   = {0xfb,0x34,0x9b,0x5f,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00,0x02,0xa0,0x00,0x00};
static const uint8_t CHR_SCAN[16]  = {0xfb,0x34,0x9b,0x5f,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00,0x03,0xa0,0x00,0x00};
static const uint8_t CHR_CMD[16]   = {0xfb,0x34,0x9b,0x5f,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00,0x04,0xa0,0x00,0x00};

void ble_prov_start(config_t *cfg_mutable) {
#if CONFIG_ENABLE_BLE
    g_cfg = cfg_mutable;
    g_done = false;
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_LOGI(TAG, "BLE provisión (STUB) iniciado: anunciando servicio de config.");
#else
    (void)cfg_mutable;
    ESP_LOGW(TAG, "BLE deshabilitado por build flag");
#endif
}

void ble_prov_wait_done(void) {
#if CONFIG_ENABLE_BLE
    for (int i=0;i<3;i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (g_cfg) { g_cfg->provisioned = true; config_store_save(g_cfg); }
    g_done = true;
    ESP_LOGI(TAG, "Provision (STUB) marcado como completado");
#else
    (void)g_done;
#endif
}
